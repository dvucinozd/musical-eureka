#pragma once
#include "esp_err.h"

typedef struct { const char *label; } esp_partition_t;
typedef enum { ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID } esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *part, esp_ota_img_states_t *state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
