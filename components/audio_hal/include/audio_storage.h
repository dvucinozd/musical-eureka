#pragma once

#include "audio_controls.h"
#include "soundfont_engine.h"
#include "soundfont_lifecycle.h"

typedef struct {
    sound_ref_t sound;
    synth_config_t config;
    uint8_t bars;
    bool restore_config;
} audio_restore_t;

/* Single serialized storage owner. An OK begin holds the PA muted and I2S
 * stopped until end. No caller may exit or perform a flash operation between
 * a failed begin and a later successful begin. Replacing also requires all
 * SoundFont tracks to be unbound. */
esp_err_t audio_storage_begin(bool replacing);
esp_err_t audio_storage_end(const audio_restore_t *restore);
bool audio_storage_held(void);

/* After a successful stage the worker must call end, then take the ACK.
 * The candidate and any old bank must only be destroyed through the ACK. */
uint64_t audio_storage_stage_bank(sf_bank *bank);
bool audio_storage_bank_ack(sf_lc_ack *ack);
