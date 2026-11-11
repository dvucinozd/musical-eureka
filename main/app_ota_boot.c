#include "app_ota.h"

#include "audio_storage.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "S31_OTA";

void app_ota_check_boot(bool startup_ok, esp_err_t usb, esp_err_t web)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    ESP_LOGI(TAG, "Running slot=%s state=%d", running->label, (int)state);
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    const audio_status_t before = audio_get_status();
    if (startup_ok && usb == ESP_OK && web == ESP_OK &&
        esp_psram_get_size() >= 16 * 1024 * 1024 && before.state == AUDIO_MUTED) {
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
