#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <bcrypt.h>

#include "ota_image.h"
#include "ota_transaction.h"

static BCRYPT_ALG_HANDLE sha;
static uint8_t image[OTA_IMAGE_LIMIT + 1];

static bool digest(const void *data, size_t size, uint8_t *out)
{
    return BCryptHash(sha, NULL, 0, (PUCHAR)data, size, out, OTA_DIGEST_BYTES) == 0;
}

static size_t load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t count = fread(image, 1, sizeof(image), file);
    assert(feof(file) && !ferror(file) && fclose(file) == 0);
    return count;
}

typedef struct {
    size_t total, offset;
    unsigned steps, fail_at;
    bool open, aborted, verified, selected;
} fake_t;

static bool begin(void *ctx, size_t size)
{
    fake_t *f = ctx;
    assert(!f->open && size == f->total);
    return f->open = ++f->steps != f->fail_at;
}

static bool write_chunk(void *ctx, const uint8_t *bytes, size_t size)
{
    fake_t *f = ctx;
    assert(f->open && !f->verified && !f->selected && bytes == image + f->offset);
    assert(size && size <= 1024 && size <= f->total - f->offset);
    if (++f->steps == f->fail_at) return false;
    f->offset += size;
    return true;
}

static void abort_write(void *ctx)
{
    fake_t *f = ctx;
    assert(f->open && !f->verified && !f->selected);
    f->aborted = true;
    f->open = false;
}

static bool finish(void *ctx)
{
    fake_t *f = ctx;
    assert(f->open && !f->aborted && f->offset == f->total);
    f->open = false;
    return f->verified = ++f->steps != f->fail_at;
}

static bool select_boot(void *ctx)
{
    fake_t *f = ctx;
    assert(f->verified && !f->open);
    return f->selected = ++f->steps != f->fail_at;
}

static void transaction(void)
{
    fake_t flash;
    const ota_transaction_t tx = {&flash, begin, write_chunk, abort_write, finish, select_boot};
    for (unsigned fail = 0; fail <= 6; ++fail) {
        flash = (fake_t){.total = 2049, .fail_at = fail};
        assert(ota_transaction_install(&tx, image, flash.total) == (fail == 0));
        assert(!flash.open && flash.selected == (fail == 0));
        assert(flash.aborted == (fail >= 2 && fail <= 4));
        if (fail) assert(flash.steps == fail);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    assert(BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0);
    size_t size = load(argv[1]);
    uint8_t expected[OTA_DIGEST_BYTES];
    assert(digest(image, size, expected));
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_OK);
    assert(ota_image_check(image, size - 1, expected, digest) == OTA_IMAGE_FORMAT);
    assert(ota_image_check(image, size + 1, expected, digest) == OTA_IMAGE_FORMAT);
    assert(ota_image_check(image, OTA_IMAGE_LIMIT + 1, expected, digest) == OTA_IMAGE_FORMAT);
    assert(ota_image_check(image, 31, expected, digest) == OTA_IMAGE_FORMAT);
    image[12] = 0x12; /* ESP32-P4 image header. */
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_TARGET);
    image[12] = 0x20;
    image[OTA_FIRST_SEGMENT + 48] ^= 1;
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_PROFILE);
    image[OTA_FIRST_SEGMENT + 48] ^= 1;
    image[28] ^= 1;
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_FORMAT);
    image[28] ^= 1;
    image[size - OTA_DIGEST_BYTES - 1] ^= 1;
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_FORMAT);
    image[size - OTA_DIGEST_BYTES - 1] ^= 1;
    image[size - 1] ^= 1;
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_DIGEST);
    image[size - 1] ^= 1;
    expected[0] ^= 1;
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_DIGEST);
    size = load(argv[2]); /* Actual P06B firmware without the P07 boot health. */
    assert(digest(image, size, expected));
    assert(ota_image_check(image, size, expected, digest) == OTA_IMAGE_PROFILE);
    const char hex[] = "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF";
    assert(ota_digest_parse(hex, expected) && expected[0] == 1 && expected[31] == 0xef);
    assert(!ota_digest_parse("012345", expected));
    assert(!ota_digest_parse("g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", expected));
    transaction();
    assert(BCryptCloseAlgorithmProvider(sha, 0) == 0);
    puts("P07 HOST PASS: real S31 image, P4 chip, old profile, size, truncation, segment, checksum, SHA-256");
    puts("P07 HOST PASS: successful commit and six injected failures before boot selection");
}
