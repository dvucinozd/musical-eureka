#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "synth_sound.h"

enum {
    AUDIO_PERF_BLOCKS = 4096,
    AUDIO_PERF_TARGET_US = 2200,
    AUDIO_PERF_DEADLINE_US = 2667,
    AUDIO_PERF_GAP_US = 2 * AUDIO_PERF_DEADLINE_US,
};

typedef enum {
    AUDIO_PERF_COMPLETE, AUDIO_PERF_MAINTENANCE, AUDIO_PERF_FAULT, AUDIO_PERF_SOUND_CHANGED,
} audio_perf_result_t;

typedef struct {
    uint32_t blocks, max_us, over_target, deadlines;
} audio_perf_bucket_t;

typedef struct {
    audio_perf_result_t result;
    uint32_t id;
    int64_t start_us, end_us;
    sound_ref_t sound;
    audio_perf_bucket_t all, event16, steady32;
    uint32_t invalid_trace, usb_events, web_blocks;
    uint32_t write_max_us, gap_max_us, long_gaps, dma_overflows;
    uint32_t stack_high_water;
} audio_perf_window_t;

/* UART/control owner. Only available in the diagnostic build. */
bool audio_perf_request(void);
bool audio_perf_take(audio_perf_window_t *out);
