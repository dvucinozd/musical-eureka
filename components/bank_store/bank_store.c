#include "bank_store.h"
#include <string.h>

enum {
    FORMAT_VERSION = 1,
    GENERATION_OFF = 8,
    SIZE_OFF = 16,
    SHA_OFF = 20,
    CRC_OFF = 56,
    MARKER_OFF = 60,
    HEADER_BYTES = 64,
    CHUNK_BYTES = 1024,
};

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_u32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

static bool valid_size(size_t size)
{
    return size >= 12 && size <= BANK_PAYLOAD_MAX;
}

bank_result_t bank_store_probe(const bank_io_t *io, unsigned slot, bank_meta_t *out)
{
    if (!io || !io->read || !out || slot >= BANK_SLOT_COUNT) return BANK_INVALID;
    *out = (bank_meta_t){0};
    uint8_t header[HEADER_BYTES];
    if (!io->read(io->context, slot, 0, header, sizeof(header))) return BANK_IO_ERROR;
    if (get_u32(header + MARKER_OFF) != 0) return BANK_EMPTY;
    if (memcmp(header, "NSF2", 4) || get_u32(header + 4) != FORMAT_VERSION ||
        get_u32(header + CRC_OFF) != crc32(header, CRC_OFF)) return BANK_CORRUPT;
    const uint64_t generation = (uint64_t)get_u32(header + GENERATION_OFF + 4) << 32 |
                                get_u32(header + GENERATION_OFF);
    const uint32_t size = get_u32(header + SIZE_OFF);
    if (!generation || !valid_size(size)) return BANK_CORRUPT;
    out->generation = generation;
    out->size = size;
    memcpy(out->sha256, header + SHA_OFF, BANK_SHA_BYTES);
    return BANK_OK;
}

bank_result_t bank_store_read(const bank_io_t *io, unsigned slot, const bank_meta_t *meta,
                              void *buffer, size_t capacity)
{
    if (!io || !io->read || !meta || !buffer || slot >= BANK_SLOT_COUNT ||
        !valid_size(meta->size) || capacity < meta->size) return BANK_INVALID;
    return io->read(io->context, slot, BANK_SECTOR_BYTES, buffer, meta->size) ? BANK_OK : BANK_IO_ERROR;
}

bank_result_t bank_store_recover(const bank_io_t *io, bank_verify_fn verify,
                                 unsigned *slot, bank_meta_t *meta)
{
    if (!io || !io->read || !verify || !slot || !meta) return BANK_INVALID;
    *slot = BANK_SLOT_NONE;
    *meta = (bank_meta_t){0};
    bank_meta_t candidates[BANK_SLOT_COUNT];
    bank_result_t status[BANK_SLOT_COUNT];
    for (unsigned i = 0; i < BANK_SLOT_COUNT; ++i)
        status[i] = bank_store_probe(io, i, &candidates[i]);

    const unsigned newest = candidates[1].generation > candidates[0].generation ? 1 : 0;
    for (unsigned n = 0; n < BANK_SLOT_COUNT; ++n) {
        unsigned i = newest ^ n;
        if (status[i] == BANK_OK && verify(io, i, &candidates[i])) {
            *slot = i;
            *meta = candidates[i];
            return BANK_OK;
        }
    }
    if (status[0] == BANK_IO_ERROR || status[1] == BANK_IO_ERROR) return BANK_IO_ERROR;
    if (status[0] == BANK_EMPTY && status[1] == BANK_EMPTY) return BANK_EMPTY;
    return BANK_CORRUPT;
}

bank_result_t bank_store_replace(const bank_io_t *io, unsigned active_slot,
                                 const void *payload, size_t size, const uint8_t sha[BANK_SHA_BYTES],
                                 unsigned *written_slot, bank_meta_t *written_meta)
{
    if (!io || !io->read || !io->erase || !io->write || !payload || !sha ||
        !valid_size(size) || !written_slot || !written_meta ||
        (active_slot >= BANK_SLOT_COUNT && active_slot != BANK_SLOT_NONE)) return BANK_INVALID;
    *written_slot = BANK_SLOT_NONE;
    *written_meta = (bank_meta_t){0};

    /* A caller must identify the active slot before replacing any committed
     * data. Even a structurally valid slot with bad payload contributes its
     * generation so a replacement will sort after it on next boot. */
    uint64_t latest = 0;
    for (unsigned i = 0; i < BANK_SLOT_COUNT; ++i) {
        bank_meta_t existing;
        bank_result_t result = bank_store_probe(io, i, &existing);
        if (result == BANK_IO_ERROR) return result;
        if ((i == active_slot && result != BANK_OK) ||
            (active_slot == BANK_SLOT_NONE && result == BANK_OK)) return BANK_STALE;
        if (result == BANK_OK && existing.generation > latest) latest = existing.generation;
    }
    if (latest == UINT64_MAX) return BANK_STALE;

    const unsigned slot = active_slot == BANK_SLOT_NONE ? 0 : active_slot ^ 1u;
    const size_t sectors = (size + BANK_SECTOR_BYTES - 1u) / BANK_SECTOR_BYTES;
    if (!io->erase(io->context, slot, 0, BANK_SECTOR_BYTES * (sectors + 1u))) return BANK_IO_ERROR;

    const uint8_t *data = payload;
    uint8_t verify[CHUNK_BYTES];
    for (size_t off = 0; off < size; off += CHUNK_BYTES) {
        size_t chunk = size - off < CHUNK_BYTES ? size - off : CHUNK_BYTES;
        uint32_t address = BANK_SECTOR_BYTES + (uint32_t)off;
        if (!io->write(io->context, slot, address, data + off, chunk) ||
            !io->read(io->context, slot, address, verify, chunk) ||
            memcmp(verify, data + off, chunk)) return BANK_IO_ERROR;
    }

    uint8_t header[HEADER_BYTES] = {0};
    memcpy(header, "NSF2", 4);
    put_u32(header + 4, FORMAT_VERSION);
    const uint64_t generation = latest + 1u;
    put_u32(header + GENERATION_OFF, (uint32_t)generation);
    put_u32(header + GENERATION_OFF + 4, (uint32_t)(generation >> 32));
    put_u32(header + SIZE_OFF, (uint32_t)size);
    memcpy(header + SHA_OFF, sha, BANK_SHA_BYTES);
    put_u32(header + CRC_OFF, crc32(header, CRC_OFF));

    /* Payload and CRC-protected header are durable before the final marker. */
    if (!io->write(io->context, slot, 0, header, MARKER_OFF) ||
        !io->read(io->context, slot, 0, verify, MARKER_OFF) ||
        memcmp(header, verify, MARKER_OFF) ||
        !io->write(io->context, slot, MARKER_OFF, header + MARKER_OFF, HEADER_BYTES - MARKER_OFF))
        return BANK_IO_ERROR;
    bank_result_t result = bank_store_probe(io, slot, written_meta);
    if (result == BANK_OK) *written_slot = slot;
    return result;
}
