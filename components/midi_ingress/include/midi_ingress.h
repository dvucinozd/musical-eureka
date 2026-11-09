#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "synth.h"

enum { MIDI_INGRESS_EVENTS_PER_BLOCK = 16 };

typedef struct {
    uint32_t session;
    uint32_t dropped;
    uint32_t applied;
    uint32_t rejected;
    uint32_t panics;
    uint32_t receive_to_dispatch_max_us;
    int64_t resume_after_us;
} midi_ingress_state_t;

esp_err_t midi_ingress_start(void);
void midi_ingress_pause(bool pause);

/* Audio-task only. Both accepted and discarded events consume the budget. */
void midi_ingress_drain(midi_ingress_state_t *state, synth_t *synth, bool live);
