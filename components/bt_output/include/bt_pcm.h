#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

enum { BT_PCM_BLOCK = 128, BT_PCM_CAPACITY = 4096, BT_PCM_PREFILL = 1024 };

typedef struct {
    int16_t samples[BT_PCM_CAPACITY];
    _Atomic uint32_t read_pos, write_pos, gate, cutoff;
    _Atomic uint32_t dropped_blocks, silent_frames;
    uint32_t seen_gate;       /* Consumer-owned. */
    unsigned phase, rate, channels;
    bool primed;
} bt_pcm_t;

/* The audio task owns the gate and pushes one 48 kHz mono block at a time.
 * The Bluedroid callback alone reads. An inactive gate invalidates old PCM. */
void bt_pcm_enable(bt_pcm_t *pcm, bool enable);
bool bt_pcm_push(bt_pcm_t *pcm, const int16_t samples[BT_PCM_BLOCK][2]);
void bt_pcm_read(bt_pcm_t *pcm, uint8_t *data, size_t bytes, unsigned rate, unsigned channels);
