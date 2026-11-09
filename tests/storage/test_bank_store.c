#include "bank_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SLOT_BYTES = 3 * BANK_SECTOR_BYTES, PAYLOAD_BYTES = 2501 };
typedef struct {
    uint8_t flash[BANK_SLOT_COUNT][SLOT_BYTES];
    unsigned fail_on;
    unsigned mutations;
    bool corrupt_readback;
} model_t;

static bool in_range(unsigned slot, uint32_t offset, size_t size)
{
    return slot < BANK_SLOT_COUNT && offset <= SLOT_BYTES && size <= SLOT_BYTES - offset;
}

static bool read_flash(void *context, unsigned slot, uint32_t offset, void *data, size_t size)
{
    model_t *m = context;
    assert(in_range(slot, offset, size));
    memcpy(data, m->flash[slot] + offset, size);
    if (m->corrupt_readback && slot == 0 && offset == BANK_SECTOR_BYTES && size)
        ((uint8_t *)data)[0] ^= 1;
    return true;
}

static bool erase_flash(void *context, unsigned slot, uint32_t offset, size_t size)
{
    model_t *m = context;
    assert(in_range(slot, offset, size));
    assert(offset % BANK_SECTOR_BYTES == 0 && size % BANK_SECTOR_BYTES == 0);
    bool interrupted = ++m->mutations == m->fail_on;
    memset(m->flash[slot] + offset, 0xff, interrupted ? size / 2 : size);
    return !interrupted;
}

static bool write_flash(void *context, unsigned slot, uint32_t offset, const void *data, size_t size)
{
    model_t *m = context;
    const uint8_t *src = data;
    assert(in_range(slot, offset, size));
    bool interrupted = ++m->mutations == m->fail_on;
    for (size_t i = 0; i < (interrupted ? size / 2 : size); ++i) {
        uint8_t *byte = &m->flash[slot][offset + i];
        assert((*byte & src[i]) == src[i]); /* NOR can only clear bits. */
        *byte &= src[i];
    }
    return !interrupted;
}

static bank_io_t model_io(model_t *model)
{
    return (bank_io_t){model, read_flash, erase_flash, write_flash};
}

/* The host fixture encodes its expected byte in the metadata. The S31
 * adapter supplies real SHA-256 and SF2 verification at the same boundary. */
static bool check_fixture(const bank_io_t *io, unsigned slot, const bank_meta_t *meta)
{
    uint8_t payload[PAYLOAD_BYTES];
    if (bank_store_read(io, slot, meta, payload, sizeof(payload)) != BANK_OK) return false;
    for (unsigned i = 0; i < meta->size; ++i)
        if (payload[i] != meta->sha256[0]) return false;
    return true;
}

int main(void)
{
    static model_t baseline, trial;
    uint8_t old_payload[PAYLOAD_BYTES], new_payload[PAYLOAD_BYTES], recovered[PAYLOAD_BYTES];
    uint8_t sha[BANK_SHA_BYTES] = {0x42, 0x05};
    memset(baseline.flash, 0xff, sizeof(baseline.flash));
    memset(old_payload, 0x42, sizeof(old_payload));
    memset(new_payload, 0x63, sizeof(new_payload));
    bank_io_t io = model_io(&baseline);
    bank_meta_t meta;
    unsigned written;
    assert(bank_store_probe(&io, 0, &meta) == BANK_EMPTY);
    assert(bank_store_replace(&io, BANK_SLOT_NONE, old_payload, BANK_PAYLOAD_MAX + 1u, sha,
                              &written, &meta) == BANK_INVALID);
    assert(!baseline.mutations);
    assert(bank_store_replace(&io, BANK_SLOT_NONE, old_payload, sizeof(old_payload), sha,
                              &written, &meta) == BANK_OK);
    assert(written == 0 && meta.generation == 1 && !memcmp(meta.sha256, sha, sizeof(sha)));
    assert(bank_store_replace(&io, BANK_SLOT_NONE, old_payload, sizeof(old_payload), sha,
                              &written, &meta) == BANK_STALE);
    assert(bank_store_replace(&io, 0, old_payload, sizeof(old_payload), sha,
                              &written, &meta) == BANK_OK);
    assert(written == 1 && meta.generation == 2);
    baseline.mutations = 0;

    /* Cut power during each mutating step of A replacement while B stays active. */
    trial = baseline;
    io = model_io(&trial);
    sha[0] = 0x63;
    assert(bank_store_replace(&io, 1, new_payload, sizeof(new_payload), sha,
                              &written, &meta) == BANK_OK);
    unsigned steps = trial.mutations;
    assert(steps == 6 && written == 0 && meta.generation == 3);
    assert(bank_store_read(&io, 0, &meta, recovered, sizeof(recovered)) == BANK_OK);
    assert(!memcmp(recovered, new_payload, sizeof(recovered)));
    assert(bank_store_recover(&io, check_fixture, &written, &meta) == BANK_OK && written == 0);
    trial.flash[0][BANK_SECTOR_BYTES + 11] ^= 1;
    assert(bank_store_recover(&io, check_fixture, &written, &meta) == BANK_OK &&
           written == 1 && meta.generation == 2);

    for (unsigned step = 1; step <= steps; ++step) {
        trial = baseline;
        trial.fail_on = step;
        assert(bank_store_replace(&io, 1, new_payload, sizeof(new_payload), sha,
                                  &written, &meta) == BANK_IO_ERROR);
        assert(written == BANK_SLOT_NONE);
        assert(bank_store_probe(&io, 1, &meta) == BANK_OK && meta.generation == 2);
        assert(!memcmp(trial.flash[1], baseline.flash[1], SLOT_BYTES));
        assert(bank_store_probe(&io, 0, &meta) == BANK_EMPTY);
    }
    trial = baseline;
    trial.corrupt_readback = true;
    assert(bank_store_replace(&io, 1, new_payload, sizeof(new_payload), sha,
                              &written, &meta) == BANK_IO_ERROR);
    assert(bank_store_probe(&io, 0, &meta) == BANK_EMPTY);
    assert(bank_store_probe(&io, 1, &meta) == BANK_OK && meta.generation == 2);
    trial.flash[1][20] ^= 1;
    assert(bank_store_probe(&io, 1, &meta) == BANK_CORRUPT);
    puts("PASS bank store: bounds, slot generations, 6 torn operations, readback, header CRC, older-slot recovery");
    return 0;
}
