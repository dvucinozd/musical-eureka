#include "app_storage.h"

#include <inttypes.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

enum { PSRAM_RESERVE = 4 * 1024 * 1024, PSRAM_CAP = 10 * 1024 * 1024, CHUNK = 1024 };
static const uint32_t PSRAM = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
static const char *TAG = "S31_STORAGE";
static const char *NVS_NAMESPACE = "nektarina", *NVS_PRESET = "startup";

/* Boot owns storage until it returns. Then the single HTTP server task
 * serializes all upload, catalog, preset and lifecycle operations. */
static const esp_partition_t *s_parts[BANK_SLOT_COUNT];
static size_t s_allocated;
static bool s_allocation_failed;
static unsigned s_slot = BANK_SLOT_NONE;
static bool s_available;
static bank_meta_t s_saved;
static sf_bank *s_catalog, *s_pending;
static bank_meta_t s_catalog_meta, s_pending_meta;
static uint64_t s_catalog_generation;

static void *allocate(size_t size)
{
    if (!size || size > PSRAM_CAP - s_allocated ||
        heap_caps_get_free_size(PSRAM) < size + PSRAM_RESERVE ||
        heap_caps_get_largest_free_block(PSRAM) < size) return NULL;
    void *ptr = heap_caps_malloc(size, PSRAM);
    if (ptr) {
        size_t actual = heap_caps_get_allocated_size(ptr);
        if (actual > PSRAM_CAP - s_allocated || heap_caps_get_free_size(PSRAM) < PSRAM_RESERVE) {
            heap_caps_free(ptr);
            return NULL;
        }
        s_allocated += actual;
    }
    return ptr;
}

void app_storage_free_buffer(void *data)
{
    if (!data) return;
    s_allocated -= heap_caps_get_allocated_size(data);
    heap_caps_free(data);
}

void *app_storage_upload_buffer(size_t size)
{
    return size >= 12 && size <= BANK_PAYLOAD_MAX && s_available ? allocate(size) : NULL;
}

static void *sf_allocate(void *ctx, sf_mem_kind kind, size_t size)
{
    (void)ctx;
    (void)kind;
    void *data = allocate(size);
    if (!data) s_allocation_failed = true;
    return data;
}

static void sf_free(void *ctx, sf_mem_kind kind, void *data)
{
    (void)ctx;
    (void)kind;
    app_storage_free_buffer(data);
}

static bool flash_read(void *ctx, unsigned slot, uint32_t offset, void *data, size_t size)
{
    (void)ctx;
    uint8_t bounce[CHUNK];
    uint8_t *dest = data;
    if (!audio_storage_held()) return false;
    while (size) {
        size_t count = size < sizeof(bounce) ? size : sizeof(bounce);
        if (esp_partition_read(s_parts[slot], offset, bounce, count) != ESP_OK) return false;
        memcpy(dest, bounce, count);
        offset += count;
        dest += count;
        size -= count;
    }
    return true;
}

static bool flash_erase(void *ctx, unsigned slot, uint32_t offset, size_t size)
{
    (void)ctx;
    return audio_storage_held() && esp_partition_erase_range(s_parts[slot], offset, size) == ESP_OK;
}

static bool flash_write(void *ctx, unsigned slot, uint32_t offset, const void *data, size_t size)
{
    (void)ctx;
    uint8_t bounce[CHUNK];
    if (!audio_storage_held() || size > sizeof(bounce)) return false;
    memcpy(bounce, data, size);
    return esp_partition_write(s_parts[slot], offset, bounce, size) == ESP_OK;
}

static const bank_io_t flash = {.read = flash_read, .erase = flash_erase, .write = flash_write};

static bool digest(const void *data, size_t size, uint8_t sha[BANK_SHA_BYTES])
{
    size_t bytes = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, size, sha, BANK_SHA_BYTES, &bytes) == PSA_SUCCESS &&
           bytes == BANK_SHA_BYTES;
}

static esp_err_t prepare(const void *data, size_t size, sf_bank **candidate)
{
    sf2_info_t info;
    if (sf2_validate(data, size, false, &info) != SF2_OK) {
        ESP_LOGW(TAG, "SF2 preflight: %s", info.reason);
        return ESP_ERR_INVALID_ARG;
    }
    size_t estimate = (size_t)info.decoded_bytes + info.metadata_peak_bytes;
    if (estimate > PSRAM_CAP - s_allocated ||
        heap_caps_get_free_size(PSRAM) < estimate + PSRAM_RESERVE) return ESP_ERR_NO_MEM;
    const sf_allocator allocator = {.alloc = sf_allocate, .free = sf_free};
    s_allocation_failed = false;
    *candidate = sf_bank_prepare(data, size, false, &allocator, &info);
    if (*candidate) return ESP_OK;
    ESP_LOGW(TAG, "SF2 prepare: %s", info.reason);
    return s_allocation_failed ? ESP_ERR_NO_MEM : ESP_ERR_NOT_SUPPORTED;
}

static sf_bank *s_recovered;

static bool recover_payload(const bank_io_t *io, unsigned slot, const bank_meta_t *meta)
{
    void *data = allocate(meta->size);
    if (!data) return false;
    uint8_t sha[BANK_SHA_BYTES];
    bool valid = bank_store_read(io, slot, meta, data, meta->size) == BANK_OK &&
                 digest(data, meta->size, sha) && !memcmp(sha, meta->sha256, sizeof(sha)) &&
                 prepare(data, meta->size, &s_recovered) == ESP_OK;
    app_storage_free_buffer(data);
    if (!valid) ESP_LOGW(TAG, "Slot %u rejected (read, digest, SF2 or memory)", slot);
    return valid;
}

static void collect_bank_ack(void)
{
    sf_lc_ack ack;
    if (!audio_storage_bank_ack(&ack)) return;
    if (ack.result == SF_LC_OK) {
        s_catalog = s_pending;
        s_catalog_meta = s_pending_meta;
        s_catalog_generation = ack.generation;
#ifdef CONFIG_NEKTAR_P06_TELEMETRY
        if (s_catalog) {
            static const char hex[] = "0123456789abcdef";
            char sha[BANK_SHA_BYTES * 2 + 1];
            for (unsigned i = 0; i < BANK_SHA_BYTES; ++i) {
                const unsigned byte = s_catalog_meta.sha256[i];
                sha[2 * i] = hex[byte >> 4];
                sha[2 * i + 1] = hex[byte & 15];
            }
            sha[sizeof(sha) - 1] = '\0';
            ESP_LOGI("S31_P06", "generation=%" PRIu64 " sf2_bytes=%" PRIu32 " sf2_sha256=%s",
                     s_catalog_generation, s_catalog_meta.size, sha);
        }
#endif
    } else {
        ESP_LOGE(TAG, "Bank activation rejected (%d)", ack.result);
    }
    s_pending = NULL;
    sf_bank_destroy(ack.retired);
}

static bool first_sound(sf_bank *bank, audio_restore_t *out)
{
    unsigned id, program;
    char name[21];
    if (!sf_bank_preset(bank, 0, &id, &program, name)) return false;
    audio_control_status_t status;
    out->sound = (sound_ref_t){.engine = SOUND_SF2, .bank = id, .program = program,
        .synth_preset = audio_get_controls(&status) ? status.preset : SYNTH_PRESET_ANALOG_BRASS};
    return true;
}

static bool program_exists(sf_bank *bank, unsigned id, unsigned program)
{
    for (unsigned i = 0; i < sf_bank_preset_count(bank); ++i) {
        unsigned b, p;
        char name[21];
        if (sf_bank_preset(bank, i, &b, &p, name) && b == id && p == program) return true;
    }
    return false;
}

static audio_restore_t resolve_preset(const startup_preset_t *preset, sf_bank *bank,
                                     const bank_meta_t *meta)
{
    audio_restore_t restore = preset->restore;
    if (restore.sound.engine == SOUND_SF2 &&
        (!bank || memcmp(preset->bank_sha, meta->sha256, BANK_SHA_BYTES) ||
         !program_exists(bank, restore.sound.bank, restore.sound.program))) {
        restore.sound = (sound_ref_t){.synth_preset = restore.sound.synth_preset};
    }
    return restore;
}

static esp_err_t load_preset(startup_preset_t *preset)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t data[PRESET_BYTES];
    size_t size = sizeof(data);
    err = nvs_get_blob(handle, NVS_PRESET, data, &size);
    nvs_close(handle);
    if (err == ESP_OK && !preset_decode(data, size, preset)) return ESP_ERR_INVALID_ARG;
    return err;
}

static esp_err_t switch_bank(sf_bank *bank, const bank_meta_t *meta, audio_restore_t *restore)
{
    uint64_t generation = audio_storage_stage_bank(bank);
    if (!generation) {
        sf_bank_destroy(bank);
        (void)audio_storage_end(NULL);
        return ESP_ERR_INVALID_STATE;
    }
    s_pending = bank;
    s_pending_meta = *meta;
    if (restore && restore->sound.engine == SOUND_SF2) restore->sound.bank_generation = generation;
    esp_err_t err = audio_storage_end(restore);
    collect_bank_ack();
    if (err == ESP_OK && s_catalog_generation != (bank ? generation : 0)) return ESP_FAIL;
    return err;
}

static esp_err_t begin(bool replacing)
{
    collect_bank_ack();
    esp_err_t err = audio_storage_begin(replacing);
    if (err == ESP_OK) collect_bank_ack();
    return err;
}

static sf_bank *recover(void)
{
    bank_meta_t meta;
    unsigned slot;
    s_recovered = NULL;
    if (bank_store_recover(&flash, recover_payload, &slot, &meta) == BANK_OK) {
        s_slot = slot;
        s_saved = meta;
        ESP_LOGI(TAG, "Recovered slot=%u flash_generation=%" PRIu64 " bytes=%" PRIu32,
                 slot, meta.generation, meta.size);
    }
    sf_bank *bank = s_recovered;
    s_recovered = NULL;
    return bank;
}

/* If neither slot could be prepared, protect the newest structural candidate.
 * It may still be usable on a later boot with a different heap layout. */
static unsigned protected_slot(void)
{
    if (s_slot != BANK_SLOT_NONE) return s_slot;
    unsigned slot = BANK_SLOT_NONE;
    uint64_t generation = 0;
    for (unsigned i = 0; i < BANK_SLOT_COUNT; ++i) {
        bank_meta_t meta;
        if (bank_store_probe(&flash, i, &meta) == BANK_OK && meta.generation > generation) {
            generation = meta.generation;
            slot = i;
        }
    }
    return slot;
}

void app_storage_boot(void)
{
    s_parts[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "bank_a");
    s_parts[1] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x41, "bank_b");
    s_available = psa_crypto_init() == PSA_SUCCESS;
    for (unsigned i = 0; i < BANK_SLOT_COUNT; ++i)
        s_available = s_available && s_parts[i] && !s_parts[i]->encrypted &&
                      s_parts[i]->size >= BANK_SECTOR_BYTES + BANK_PAYLOAD_MAX;

    const int64_t deadline = esp_timer_get_time() + 250000;
    while (audio_get_status().state == AUDIO_PRIMING && esp_timer_get_time() < deadline) vTaskDelay(1);
    if (begin(false) != ESP_OK) {
        ESP_LOGW(TAG, "Boot storage skipped: audio did not park");
        return;
    }
    sf_bank *candidate = s_available ? recover() : NULL;
    startup_preset_t saved;
    esp_err_t preset_result = load_preset(&saved);
    audio_restore_t restore = {0};
    bool select = false;
    if (preset_result == ESP_OK) {
        restore = resolve_preset(&saved, candidate, &s_saved);
        select = true;
    } else {
        if (preset_result != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW(TAG, "Startup preset ignored: %s", esp_err_to_name(preset_result));
        if (candidate) select = first_sound(candidate, &restore);
    }
    esp_err_t err = candidate ? switch_bank(candidate, &s_saved, select ? &restore : NULL)
                              : audio_storage_end(select ? &restore : NULL);
    if (err != ESP_OK) ESP_LOGW(TAG, "Boot storage resume: %s", esp_err_to_name(err));
}

esp_err_t app_storage_upload(const void *data, size_t size)
{
    if (!s_available || !data || size < 12 || size > BANK_PAYLOAD_MAX) return ESP_ERR_INVALID_ARG;
    esp_err_t err = begin(true);
    if (err != ESP_OK) return err;
    sf_bank *candidate = NULL;
    uint8_t sha[BANK_SHA_BYTES];
    audio_restore_t restore = {0};
    err = prepare(data, size, &candidate);
    if (err == ESP_OK && (!first_sound(candidate, &restore) || !digest(data, size, sha))) err = ESP_FAIL;
    if (err == ESP_OK) {
        unsigned slot;
        bank_meta_t meta;
        if (bank_store_replace(&flash, protected_slot(), data, size, sha, &slot, &meta) == BANK_OK) {
            s_slot = slot;
            s_saved = meta;
            return switch_bank(candidate, &s_saved, &restore);
        }
        err = ESP_FAIL;
    }
    sf_bank_destroy(candidate);
    (void)audio_storage_end(NULL);
    return err;
}

esp_err_t app_storage_unload(void)
{
    esp_err_t err = begin(true);
    if (err != ESP_OK) return err;
    if (!s_catalog) return audio_storage_end(NULL);
    const bank_meta_t empty = {0};
    return switch_bank(NULL, &empty, NULL);
}

esp_err_t app_storage_reload(void)
{
    if (!s_available) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = begin(true);
    if (err != ESP_OK) return err;
    if (s_catalog) {
        (void)audio_storage_end(NULL);
        return ESP_ERR_INVALID_STATE;
    }
    sf_bank *candidate = recover();
    audio_restore_t restore = {0};
    if (candidate && first_sound(candidate, &restore)) return switch_bank(candidate, &s_saved, &restore);
    sf_bank_destroy(candidate);
    (void)audio_storage_end(NULL);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_storage_save_preset(void)
{
    esp_err_t err = begin(false);
    if (err != ESP_OK) return err;
    audio_control_status_t status;
    startup_preset_t preset = {0};
    uint8_t data[PRESET_BYTES];
    if (!audio_get_controls(&status)) {
        err = ESP_FAIL;
    } else {
        preset.restore = (audio_restore_t){.sound = status.sound, .config = status.config,
                                           .bars = status.looper.bars, .restore_config = true};
        memcpy(preset.bank_sha, s_catalog_meta.sha256, BANK_SHA_BYTES);
        if (!preset_encode(&preset, data)) err = ESP_ERR_INVALID_ARG;
    }
    startup_preset_t existing;
    uint8_t previous[PRESET_BYTES];
    if (err == ESP_OK && !(load_preset(&existing) == ESP_OK && preset_encode(&existing, previous) &&
                           !memcmp(data, previous, sizeof(data)))) {
        nvs_handle_t handle;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, NVS_PRESET, data, sizeof(data));
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
    }
    esp_err_t resumed = audio_storage_end(NULL);
    return err == ESP_OK ? resumed : err;
}

esp_err_t app_storage_restore_preset(void)
{
    esp_err_t err = begin(false);
    if (err != ESP_OK) return err;
    startup_preset_t saved;
    err = load_preset(&saved);
    audio_restore_t restore;
    if (err == ESP_OK) {
        restore = resolve_preset(&saved, s_catalog, &s_catalog_meta);
        if (restore.sound.engine == SOUND_SF2) restore.sound.bank_generation = s_catalog_generation;
    }
    esp_err_t resumed = audio_storage_end(err == ESP_OK ? &restore : NULL);
    return err == ESP_OK ? resumed : err;
}

app_bank_status_t app_storage_status(void)
{
    collect_bank_ack();
    return (app_bank_status_t){s_catalog_generation, sf_bank_preset_count(s_catalog),
                               s_slot != BANK_SLOT_NONE};
}

bool app_storage_program(unsigned index, unsigned *bank, unsigned *program, char name[21])
{
    return sf_bank_preset(s_catalog, index, bank, program, name);
}

esp_err_t app_storage_select(uint64_t generation, unsigned index)
{
    collect_bank_ack();
    unsigned id, program;
    char name[21];
    audio_control_status_t status;
    if (!generation || generation != s_catalog_generation) return ESP_ERR_INVALID_STATE;
    if (!app_storage_program(index, &id, &program, name)) return ESP_ERR_INVALID_ARG;
    if (!audio_get_controls(&status)) return ESP_ERR_INVALID_STATE;
    const audio_command_t command = {.kind = AUDIO_CMD_SOUND, .sound = {
        .engine = SOUND_SF2, .bank_generation = generation, .bank = id, .program = program,
        .synth_preset = status.preset,
    }};
    return audio_submit_control(command) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
