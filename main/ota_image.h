#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_APP_PROJECT "nektarina_s31"
#define OTA_PROFILE_MARKER "NEKTAR-S31-P07"
enum { OTA_IMAGE_LIMIT = 3 * 1024 * 1024, OTA_DIGEST_BYTES = 32, OTA_FIRST_SEGMENT = 32 };

typedef enum { OTA_IMAGE_OK, OTA_IMAGE_FORMAT, OTA_IMAGE_TARGET, OTA_IMAGE_PROFILE,
               OTA_IMAGE_DIGEST } ota_image_result_t;
typedef bool (*ota_digest_fn)(const void *, size_t, uint8_t *);

ota_image_result_t ota_image_check(const uint8_t *image, size_t size,
                                   const uint8_t expected[OTA_DIGEST_BYTES], ota_digest_fn digest);
bool ota_digest_parse(const char *hex, uint8_t digest[OTA_DIGEST_BYTES]);
