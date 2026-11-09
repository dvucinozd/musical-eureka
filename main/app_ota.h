#pragma once

#include "esp_err.h"
#include "ota_image.h"

bool app_ota_enabled(void);
void *app_ota_buffer(size_t size);
/* Success selects the inactive slot and keeps audio parked until restart. */
esp_err_t app_ota_install(const uint8_t *data, size_t size, const uint8_t expected[OTA_DIGEST_BYTES]);
void app_ota_check_boot(bool startup_ok);
