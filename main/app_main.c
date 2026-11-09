#include <inttypes.h>
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
#include "usb_midi_host.h"

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
    ESP_LOGI(tag, "project=nektarina_s31 stage=P03 idf=%s", esp_get_idf_version());
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

void app_main(void)
{
    log_boot();
    esp_err_t err = audio_start();
    if (err != ESP_OK) {
        ESP_LOGE("S31_P03", "Audio setup failed: %s", esp_err_to_name(err));
    } else {
        err = midi_ingress_start();
        if (err != ESP_OK) ESP_LOGE("S31_P03", "USB MIDI setup failed: %s", esp_err_to_name(err));
    }

    /* UART0 uses the same pins and baud rate as the boot console. */
    err = uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE("S31_P03", "Console setup failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI("S31_P03", "UART0 commands: ? status, t one low-level C4 probe before live MIDI");
    for (;;) {
        uint8_t command;
        if (uart_read_bytes(UART_NUM_0, &command, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if (command == '?') {
            log_audio();
        } else if (command == 't') {
            if (audio_request_probe()) {
                ESP_LOGI("S31_P03", "Probe accepted");
            } else {
                ESP_LOGW("S31_P03", "Probe rejected. Query audio status with ?");
            }
        }
    }
}

