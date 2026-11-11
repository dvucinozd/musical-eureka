#include <assert.h>
#include <stdio.h>

#include "app_ota.h"
#include "audio_storage.h"
#include "esp_ota_ops.h"

static const esp_partition_t partition = {.label = "ota_1"};
static esp_ota_img_states_t state;
static unsigned restarts, confirmations, blocks;
static bool parked;

const esp_partition_t *esp_ota_get_running_partition(void) { return &partition; }

esp_err_t esp_ota_get_state_partition(const esp_partition_t *part, esp_ota_img_states_t *out)
{
    assert(part == &partition);
    *out = state;
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    assert(parked && state == ESP_OTA_IMG_PENDING_VERIFY);
    ++confirmations;
    state = ESP_OTA_IMG_VALID;
    return ESP_OK;
}

size_t esp_psram_get_size(void) { return 16u * 1024u * 1024u; }
void esp_restart(void) { ++restarts; }

audio_status_t audio_get_status(void)
{
    return (audio_status_t){.state = AUDIO_MUTED, .blocks = ++blocks};
}

esp_err_t audio_storage_begin(bool replacing)
{
    assert(!replacing && !parked);
    parked = true;
    return ESP_OK;
}

esp_err_t audio_storage_end(const audio_restore_t *restore)
{
    assert(parked && !restore);
    parked = false;
    return ESP_OK;
}

int main(void)
{
    state = ESP_OTA_IMG_PENDING_VERIFY;
    app_ota_check_boot(true, ESP_ERR_NO_MEM, ESP_OK);
    assert(restarts == 1 && confirmations == 0 && !parked);
    assert(state == ESP_OTA_IMG_PENDING_VERIFY);
    app_ota_check_boot(true, ESP_OK, ESP_ERR_NO_MEM);
    assert(restarts == 2 && confirmations == 0 && !parked);
    assert(state == ESP_OTA_IMG_PENDING_VERIFY);

    app_ota_check_boot(true, ESP_OK, ESP_OK);
    assert(restarts == 2 && confirmations == 1 && !parked);
    assert(state == ESP_OTA_IMG_VALID);
    app_ota_check_boot(true, ESP_ERR_NO_MEM, ESP_ERR_NO_MEM);
    assert(restarts == 2 && confirmations == 1 && !parked);
    puts("OTA boot PASS: USB/Web startup failures restart pending image, success confirms while parked");
}
