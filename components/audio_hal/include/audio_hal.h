#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

enum {
    AUDIO_RATE = 48000,
    AUDIO_FRAMES = 128,
};

typedef enum {
    AUDIO_OFF,
    AUDIO_PRIMING,
    AUDIO_MUTED,
    AUDIO_PROBE,
    AUDIO_FAULT,
    AUDIO_LIVE,
} audio_state_t;

typedef struct {
    audio_state_t state;
    uint32_t blocks;
    uint32_t prepare_max_us;
    uint32_t prepare_deadlines;
    uint32_t dma_overflows;
    uint32_t probes_completed;
    uint32_t midi_applied;
    uint32_t midi_rejected;
    uint32_t midi_queue_drops;
    uint32_t midi_panics;
    uint32_t midi_receive_to_dispatch_max_us;
    esp_err_t fault;
} audio_status_t;

/* One startup attempt per boot. Primes muted before allowing probe or live MIDI. */
esp_err_t audio_start(void);
bool audio_request_probe(void);
audio_status_t audio_get_status(void);
