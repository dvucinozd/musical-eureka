#pragma once

#include "audio_storage.h"
#include "bank_store.h"

enum { PRESET_BYTES = 4 + 4 * (5 + 36) + BANK_SHA_BYTES };

typedef struct {
    audio_restore_t restore;
    uint8_t bank_sha[BANK_SHA_BYTES];
} startup_preset_t;

bool preset_encode(const startup_preset_t *preset, uint8_t out[PRESET_BYTES]);
bool preset_decode(const uint8_t *data, size_t size, startup_preset_t *preset);
