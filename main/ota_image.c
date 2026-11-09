#include "ota_image.h"

#include <string.h>

enum { HEADER = 24, SEGMENT_HEADER = 8, MAX_SEGMENTS = 16, APP_DESC = 256, PROJECT_OFFSET = 48 };

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

ota_image_result_t ota_image_check(const uint8_t *image, size_t size,
                                   const uint8_t expected[OTA_DIGEST_BYTES], ota_digest_fn digest)
{
    const size_t first_min = APP_DESC + sizeof(OTA_PROFILE_MARKER);
    if (size < OTA_FIRST_SEGMENT + first_min + 1 + OTA_DIGEST_BYTES || size > OTA_IMAGE_LIMIT ||
        image[0] != 0xe9 || !image[1] || image[1] > MAX_SEGMENTS || image[23] != 1)
        return OTA_IMAGE_FORMAT;
    if (image[12] != 0x20 || image[13] != 0) return OTA_IMAGE_TARGET;
    if (word(image + HEADER + 4) < first_min || word(image + OTA_FIRST_SEGMENT) != 0xabcd5432 ||
        memcmp(image + OTA_FIRST_SEGMENT + PROJECT_OFFSET, OTA_APP_PROJECT, sizeof(OTA_APP_PROJECT)) ||
        memcmp(image + OTA_FIRST_SEGMENT + APP_DESC, OTA_PROFILE_MARKER, sizeof(OTA_PROFILE_MARKER)))
        return OTA_IMAGE_PROFILE;

    const size_t hash_offset = size - OTA_DIGEST_BYTES;
    size_t pos = HEADER;
    uint8_t checksum = 0xef;
    for (unsigned i = 0; i < image[1]; ++i) {
        if (hash_offset - pos < SEGMENT_HEADER) return OTA_IMAGE_FORMAT;
        const size_t length = word(image + pos + 4);
        pos += SEGMENT_HEADER;
        if (length % 4 || length > hash_offset - pos) return OTA_IMAGE_FORMAT;
        for (size_t j = 0; j < length; ++j) checksum ^= image[pos + j];
        pos += length;
    }
    if (((pos + 16) & ~(size_t)15) != hash_offset || checksum != image[hash_offset - 1])
        return OTA_IMAGE_FORMAT;

    uint8_t actual[OTA_DIGEST_BYTES];
    if (!digest(image, hash_offset, actual) || memcmp(actual, image + hash_offset, sizeof(actual)) ||
        !digest(image, size, actual) || memcmp(actual, expected, sizeof(actual)))
        return OTA_IMAGE_DIGEST;
    return OTA_IMAGE_OK;
}

bool ota_digest_parse(const char *hex, uint8_t digest[OTA_DIGEST_BYTES])
{
    if (strlen(hex) != 2 * OTA_DIGEST_BYTES) return false;
    for (unsigned i = 0; i < 2 * OTA_DIGEST_BYTES; ++i) {
        unsigned c = (unsigned char)hex[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'a' && c <= 'f') c = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') c = c - 'A' + 10;
        else return false;
        if (i & 1u) digest[i / 2] |= c;
        else digest[i / 2] = c << 4;
    }
    return true;
}
