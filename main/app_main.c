#include <inttypes.h>
#include "app_storage.h"
#include "app_wifi.h"
#include "app_web.h"
#include "audio_hal.h"
#include "driver/uart.h"
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "midi_ingress.h"
#include "nvs_flash.h"
#include "usb_midi_host.h"

#ifdef CONFIG_NEKTAR_P07_OTA
#include "app_ota.h"
#endif
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
#include "bt_output.h"
#endif
#ifdef CONFIG_NEKTAR_P06_TELEMETRY
#include "audio_perf.h"
#include "audio_controls.h"
#endif

#if !CONFIG_IDF_TARGET_ESP32S31
#error "NEKTARINA S31 requires ESP32-S31"
#endif

static void log_boot(void)
{
    const char *tag = "S31_BOOTSTRAP";
    esp_chip_info_t chip;
    uint32_t flash_bytes = 0;
    esp_chip_info(&chip);
    esp_err_t flash_result = esp_flash_get_size(NULL, &flash_bytes);
#if defined(CONFIG_NEKTAR_P07_OTA)
    ESP_LOGI(tag, "project=nektarina_s31 stage=P07-ota idf=%s", esp_get_idf_version());
#elif defined(CONFIG_NEKTAR_P06B_BLUETOOTH)
    ESP_LOGI(tag, "project=nektarina_s31 stage=P06B-bluetooth idf=%s", esp_get_idf_version());
#elif defined(CONFIG_NEKTAR_P06_TELEMETRY)
    ESP_LOGI(tag, "project=nektarina_s31 stage=P06-telemetry idf=%s", esp_get_idf_version());
#else
    ESP_LOGI(tag, "project=nektarina_s31 stage=P05b idf=%s", esp_get_idf_version());
#endif
    ESP_LOGI(tag, "target=%s cores=%u revision=%u cpu_mhz=%d reset_reason=%d",
             CONFIG_IDF_TARGET, (unsigned)chip.cores, (unsigned)chip.revision,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, (int)esp_reset_reason());
    ESP_LOGI(tag, "flash_query=%s flash_bytes=%" PRIu32 " psram_bytes=%zu",
             esp_err_to_name(flash_result), flash_bytes, esp_psram_get_size());
    ESP_LOGI(tag, "internal_free=%zu internal_largest=%zu dma_free=%zu psram_free=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void log_audio(void)
{
    audio_status_t status = audio_get_status();
    ESP_LOGI("S31_P03", "state=%d blocks=%" PRIu32 " prep_max_us=%" PRIu32
             " deadlines=%" PRIu32 " dma_overflows=%" PRIu32 " probes=%" PRIu32 " fault=%s",
             (int)status.state, status.blocks, status.prepare_max_us, status.prepare_deadlines,
             status.dma_overflows, status.probes_completed, esp_err_to_name(status.fault));
    ESP_LOGI("S31_AUDIO", "probe_pcm_peak=%" PRIu32 " note_accepted=%u pa_enable_command_ok=%u pa_pad_high=%u",
             status.probe_pcm_peak, (unsigned)status.probe_note_accepted,
             (unsigned)status.probe_pa_enabled, (unsigned)status.probe_pa_high);
    ESP_LOGI("S31_AUDIO", "probe_rising_crossings=%" PRIu32, status.probe_rising_crossings);
    usb_midi_host_stats_t usb;
    usb_midi_host_get_stats(&usb);
    ESP_LOGI("S31_P03", "usb_ready=%d session=%" PRIu32 " vid=%04x pid=%04x speed=%u"
             " raw_drops=%" PRIu32 " stale_raw=%" PRIu32 " in_errors=%" PRIu32,
             (int)usb.ready, usb.session, (unsigned)usb.vid, (unsigned)usb.pid,
             (unsigned)usb.speed, usb.queue_drops, usb.stale_drops, usb.in_errors);
    ESP_LOGI("S31_P03", "midi_applied=%" PRIu32 " rejected=%" PRIu32
             " semantic_drops=%" PRIu32 " panics=%" PRIu32
             " rx_to_dispatch_max_us=%" PRIu32,
             status.midi_applied, status.midi_rejected, status.midi_queue_drops, status.midi_panics,
             status.midi_receive_to_dispatch_max_us);
}

#ifdef CONFIG_NEKTAR_P06_TELEMETRY
static void log_perf_bucket(uint32_t id, const char *name, const audio_perf_bucket_t *bucket)
{
    ESP_LOGI("S31_P06", "window=%" PRIu32 " group=%s blocks=%" PRIu32
             " max_prepare_us=%" PRIu32 " over_2200=%" PRIu32 " deadlines=%" PRIu32,
             id, name, bucket->blocks, bucket->max_us, bucket->over_target, bucket->deadlines);
}

static void log_perf_heap(const char *name, uint32_t caps)
{
    ESP_LOGI("S31_P06", "heap=%s free=%zu min_free_since_boot=%zu largest=%zu", name,
             heap_caps_get_free_size(caps), heap_caps_get_minimum_free_size(caps),
             heap_caps_get_largest_free_block(caps));
}

static void arm_perf(void)
{
    audio_control_status_t controls;
    if (audio_get_status().state != AUDIO_LIVE || !audio_get_controls(&controls) ||
        !audio_perf_request()) {
        ESP_LOGW("S31_P06", "Arm rejected: audio must be live and previous window collected");
        return;
    }
    ESP_LOGI("S31_P06", "armed blocks=%u sf=%d generation=%" PRIu64 " bank=%u program=%u "
             "arp=%u delay=%u chorus=%u gain=%u bpm=%u bars=%u",
             AUDIO_PERF_BLOCKS, controls.sound.engine == SOUND_SF2, controls.sound.bank_generation,
             (unsigned)controls.sound.bank, (unsigned)controls.sound.program,
             (unsigned)controls.config.arp_mode, (unsigned)controls.config.delay_enable,
             (unsigned)controls.config.chorus_enable, (unsigned)controls.config.master_gain_permille,
             (unsigned)controls.looper.bpm, (unsigned)controls.looper.bars);
}

static void log_perf(void)
{
    audio_perf_window_t window;
    if (!audio_perf_take(&window)) {
        ESP_LOGW("S31_P06", "No completed window");
        return;
    }
    ESP_LOGI("S31_P06", "window=%" PRIu32 " result=%u start_us=%" PRIi64 " end_us=%" PRIi64
             " generation=%" PRIu64 " bank=%u program=%u"
             " invalid_trace=%" PRIu32 " usb_dispatches=%" PRIu32 " web_blocks=%" PRIu32
             " write_path_max_us=%" PRIu32 " start_gap_max_us=%" PRIu32 " long_gaps=%" PRIu32
             " dma_queue_overflows=%" PRIu32 " stack_min_free_bytes=%" PRIu32,
             window.id, (unsigned)window.result, window.start_us, window.end_us,
             window.sound.bank_generation, (unsigned)window.sound.bank, (unsigned)window.sound.program,
             window.invalid_trace, window.usb_events, window.web_blocks, window.write_max_us,
             window.gap_max_us, window.long_gaps, window.dma_overflows, window.stack_high_water);
    log_perf_bucket(window.id, "all", &window.all);
    log_perf_bucket(window.id, "event16", &window.event16);
    log_perf_bucket(window.id, "steady32", &window.steady32);
    log_perf_heap("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    log_perf_heap("dma", MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    log_perf_heap("psram", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    log_audio();
}
#endif

void app_main(void)
{
    log_boot();
    esp_err_t nvs = nvs_flash_init();
    /* Radio/PHY initialization can touch NVS. Finish it before audio starts. */
    esp_err_t wifi = nvs == ESP_OK ? app_wifi_start() : nvs;
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
    esp_err_t bt = bt_output_start();
    if (bt != ESP_OK) ESP_LOGE("S31_BT", "Bluetooth setup failed: %s", esp_err_to_name(bt));
#endif
    esp_err_t err = audio_start();
    if (err != ESP_OK) {
        ESP_LOGE("S31_P03", "Audio setup failed: %s", esp_err_to_name(err));
    } else app_storage_boot();
    esp_err_t usb = ESP_ERR_INVALID_STATE;
    esp_err_t web = ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        usb = midi_ingress_start();
        if (usb != ESP_OK) ESP_LOGE("S31_P03", "USB MIDI setup failed: %s", esp_err_to_name(usb));
        web = wifi == ESP_OK ? app_web_start() : wifi;
        if (web != ESP_OK) ESP_LOGE("S31_P04", "Web setup failed: %s", esp_err_to_name(web));
    }
#ifdef CONFIG_NEKTAR_P07_OTA
    bool startup_ok = err == ESP_OK && wifi == ESP_OK;
#ifdef CONFIG_NEKTAR_P06B_BLUETOOTH
    startup_ok = startup_ok && bt == ESP_OK;
#endif
    app_ota_check_boot(startup_ok, usb, web);
#endif
    if (usb == ESP_OK) midi_ingress_open_input();
    if (web == ESP_OK) app_web_open_commands();

    /* UART0 uses the same pins and baud rate as the boot console. */
    err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE("S31_P03", "Console setup failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("S31_P03", "UART0 commands: ? status, d diagnostics, t C4, h C6, s/l/r 1kHz both/left/right");
#ifdef CONFIG_NEKTAR_P06_TELEMETRY
    ESP_LOGI("S31_P06", "UART0 commands: p arm timing window, P read completed window");
#endif
    for (;;) {
        uint8_t command;
        if (uart_read_bytes(UART_NUM_0, &command, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if (command == '?') {
            log_audio();
        } else if (command == 'd') {
            audio_log_diagnostics();
        } else if (command == 't' || command == 'h') {
            if (command == 'h' ? audio_request_high_probe() : audio_request_probe()) {
                ESP_LOGI("S31_P03", "Probe accepted");
            } else {
                ESP_LOGW("S31_P03", "Probe rejected. Query audio status with ?");
            }
        } else if (command == 's' || command == 'l' || command == 'r') {
            unsigned slots = command == 's' ? 3 : command == 'l' ? 1 : 2;
            if (audio_request_test_tone(slots)) ESP_LOGI("S31_P03", "Stereo test accepted slots=%u", slots);
            else ESP_LOGW("S31_P03", "Stereo test rejected");
#ifdef CONFIG_NEKTAR_P06_TELEMETRY
        } else if (command == 'p') {
            arm_perf();
        } else if (command == 'P') {
            log_perf();
#endif
        }
    }
}

