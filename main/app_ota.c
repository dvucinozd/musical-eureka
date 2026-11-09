#include "app_ota.h"
#include "ota_transaction.h"

#include <string.h>

#include "audio_storage.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

const char nektar_ota_profile[] __attribute__((section(".rodata_custom_desc"), aligned(4))) =
    OTA_PROFILE_MARKER;

_Static_assert(ESP_CHIP_ID_ESP32S31 == 0x20 && offsetof(esp_image_header_t, chip_id) == 12,
               "OTA validator chip layout changed");
_Static_assert(sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) == OTA_FIRST_SEGMENT &&
               offsetof(esp_app_desc_t, project_name) == 48 && sizeof(esp_app_desc_t) == 256,
               "OTA validator descriptor layout changed");
_Static_assert(ESP_IMAGE_MAX_SEGMENTS == 16 && ESP_APP_DESC_MAGIC_WORD == 0xabcd5432,
               "OTA validator image format changed");

static const char *TAG = "S31_OTA";

bool app_ota_enabled(void)
{
    const char *password = CONFIG_NEKTAR_AP_PASSWORD;
    return strlen(password) >= 12 && strcmp(password, "nektarina-s31") != 0;
}

void *app_ota_buffer(size_t size)
{
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t reserve = 4 * 1024 * 1024;
    if (size < OTA_FIRST_SEGMENT || size > OTA_IMAGE_LIMIT ||
        heap_caps_get_free_size(caps) < size + reserve ||
        heap_caps_get_largest_free_block(caps) < size) return NULL;
    void *ptr = heap_caps_malloc(size, caps);
    if (ptr && heap_caps_get_free_size(caps) < reserve) {
        heap_caps_free(ptr);
        return NULL;
    }
    return ptr;
}

static bool digest(const void *data, size_t size, uint8_t *out)
{
    size_t n = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, size, out, OTA_DIGEST_BYTES, &n) == PSA_SUCCESS &&
           n == OTA_DIGEST_BYTES;
}

typedef struct {
    const esp_partition_t *target;
    esp_ota_handle_t handle;
    esp_err_t error;
} native_ota_t;

static bool begin(void *context, size_t size)
{
    native_ota_t *ota = context;
    ota->error = esp_ota_begin(ota->target, size, &ota->handle);
    return ota->error == ESP_OK;
}

static bool write_chunk(void *context, const uint8_t *data, size_t size)
{
    native_ota_t *ota = context;
    uint8_t bounce[1024];
    if (size > sizeof(bounce)) return false;
    memcpy(bounce, data, size);
    ota->error = esp_ota_write(ota->handle, bounce, size);
    return ota->error == ESP_OK;
}

static void abort_write(void *context)
{
    native_ota_t *ota = context;
    (void)esp_ota_abort(ota->handle);
}

static bool finish(void *context)
{
    native_ota_t *ota = context;
    ota->error = esp_ota_end(ota->handle);
    return ota->error == ESP_OK;
}

static bool select_boot(void *context)
{
    native_ota_t *ota = context;
    ota->error = esp_ota_set_boot_partition(ota->target);
    return ota->error == ESP_OK;
}

static bool ota_slot(const esp_partition_t *part)
{
    return part && part->type == ESP_PARTITION_TYPE_APP &&
           (part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
            part->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) && part->size == OTA_IMAGE_LIMIT;
}

esp_err_t app_ota_install(const uint8_t *data, size_t size, const uint8_t expected[OTA_DIGEST_BYTES])
{
    if (!app_ota_enabled()) return ESP_ERR_NOT_SUPPORTED;
    if (psa_crypto_init() != PSA_SUCCESS) return ESP_FAIL;
    const ota_image_result_t checked = ota_image_check(data, size, expected, digest);
    if (checked != OTA_IMAGE_OK) {
        ESP_LOGW(TAG, "Preflight rejected (%d)", checked);
        return checked == OTA_IMAGE_TARGET || checked == OTA_IMAGE_PROFILE
            ? ESP_ERR_INVALID_VERSION : ESP_ERR_INVALID_CRC;
    }
    esp_image_header_t header;
    esp_app_desc_t description;
    memcpy(&header, data, sizeof(header));
    memcpy(&description, data + OTA_FIRST_SEGMENT, sizeof(description));
    if (esp_ota_check_image_validity(ESP_PARTITION_TYPE_APP, &header, &description) != ESP_OK)
        return ESP_ERR_INVALID_VERSION;

    esp_err_t err = audio_storage_begin(false);
    if (err != ESP_OK) return err;

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(running);
    esp_ota_img_states_t state;
    const bool valid = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_VALID;
    if (!ota_slot(running) || !boot || boot->address != running->address || !ota_slot(target) ||
        target->address == running->address || !valid) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        native_ota_t native = {.target = target};
        const ota_transaction_t tx = {&native, begin, write_chunk, abort_write, finish, select_boot};
        if (ota_transaction_install(&tx, data, size)) {
            ESP_LOGI(TAG, "Selected %s (%zu B). Audio remains parked until restart.", target->label, size);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Install failed for %s: %s", target->label, esp_err_to_name(native.error));
        err = ESP_FAIL;
    }
    const esp_err_t resumed = audio_storage_end(NULL);
    return resumed == ESP_OK ? err : resumed;
}

void app_ota_check_boot(bool startup_ok)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    ESP_LOGI(TAG, "Running slot=%s state=%d", running->label, (int)state);
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    const audio_status_t before = audio_get_status();
    if (startup_ok && esp_psram_get_size() >= 16 * 1024 * 1024 && before.state == AUDIO_MUTED) {
        const int64_t deadline = esp_timer_get_time() + 1000000;
        do {
            vTaskDelay(pdMS_TO_TICKS(50));
        } while (esp_timer_get_time() < deadline && audio_get_status().state == AUDIO_MUTED);
        const audio_status_t after = audio_get_status();
        if (after.state == AUDIO_MUTED && after.fault == ESP_OK &&
            after.blocks - before.blocks >= 200 && after.dma_overflows == before.dma_overflows &&
            after.prepare_deadlines == before.prepare_deadlines && audio_storage_begin(false) == ESP_OK) {
            const esp_err_t accepted = esp_ota_mark_app_valid_cancel_rollback();
            if (accepted == ESP_OK) {
                const esp_err_t resumed = audio_storage_end(NULL);
                ESP_LOGI(TAG, "First boot accepted, audio resume=%s", esp_err_to_name(resumed));
                return;
            }
            ESP_LOGE(TAG, "Cannot confirm first boot: %s", esp_err_to_name(accepted));
        }
    }
    /* Keep PENDING_VERIFY. The rollback-enabled bootloader owns the fallback. */
    ESP_LOGE(TAG, "First boot unconfirmed, restarting for rollback");
    esp_restart();
}
