#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BANK_SLOT_COUNT = 2,
    BANK_SLOT_NONE = 255,
    BANK_SECTOR_BYTES = 4096,
    BANK_PAYLOAD_MAX = 3 * 1024 * 1024,
    BANK_SHA_BYTES = 32,
};

typedef struct {
    void *context;
    bool (*read)(void *, unsigned slot, uint32_t offset, void *data, size_t size);
    bool (*erase)(void *, unsigned slot, uint32_t offset, size_t size);
    bool (*write)(void *, unsigned slot, uint32_t offset, const void *data, size_t size);
} bank_io_t;

typedef struct {
    uint64_t generation;
    uint32_t size;
    uint8_t sha256[BANK_SHA_BYTES];
} bank_meta_t;

typedef enum {
    BANK_OK, BANK_EMPTY, BANK_CORRUPT, BANK_IO_ERROR, BANK_INVALID, BANK_STALE,
} bank_result_t;

/* Header integrity only. Before selecting a bank, verify its payload SHA and SF2. */
bank_result_t bank_store_probe(const bank_io_t *io, unsigned slot, bank_meta_t *out);
bank_result_t bank_store_read(const bank_io_t *io, unsigned slot, const bank_meta_t *meta,
                              void *buffer, size_t capacity);
typedef bool (*bank_verify_fn)(const bank_io_t *, unsigned slot, const bank_meta_t *);
bank_result_t bank_store_recover(const bank_io_t *io, bank_verify_fn verify,
                                 unsigned *slot, bank_meta_t *meta);

/* Single control owner. Caller supplies verified payload and digest and the
 * active slot from recovery. Erase/write is only allowed in audio maintenance.
 * A failed commit may require a rescan to resolve the outcome after reset. */
bank_result_t bank_store_replace(const bank_io_t *io, unsigned active_slot,
                                 const void *payload, size_t size, const uint8_t sha[BANK_SHA_BYTES],
                                 unsigned *written_slot, bank_meta_t *written_meta);
