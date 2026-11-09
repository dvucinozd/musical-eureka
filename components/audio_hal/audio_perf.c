#include "audio_perf_internal.h"

#include <stdatomic.h>

#include "synth_block_trace.h"

typedef enum { IDLE, REQUESTED, CAPTURING, READY, READING } perf_state_t;
static _Atomic perf_state_t s_state;
static audio_perf_window_t s_window;
static uint32_t s_next_id, s_dma_start;
static int64_t s_previous_start, s_block_start;

bool audio_perf_request(void)
{
    perf_state_t expected = IDLE;
    return atomic_compare_exchange_strong(&s_state, &expected, REQUESTED);
}

bool audio_perf_take(audio_perf_window_t *out)
{
    perf_state_t expected = READY;
    if (!out || !atomic_compare_exchange_strong(&s_state, &expected, READING)) return false;
    *out = s_window;
    atomic_store(&s_state, IDLE);
    return true;
}

static void start_window(int64_t now_us, uint32_t dma)
{
    s_window = (audio_perf_window_t){.id = ++s_next_id, .start_us = now_us};
    s_dma_start = dma;
    s_previous_start = now_us;
    atomic_store(&s_state, CAPTURING);
}

void audio_perf_begin_block(int64_t start_us, uint32_t dma_overflows, const sound_ref_t *sound)
{
    perf_state_t state = atomic_load(&s_state);
    if (state == REQUESTED) {
        start_window(start_us, dma_overflows);
        s_window.sound = *sound;
    } else if (state != CAPTURING) return;
    s_block_start = start_us;
    synth_block_trace_begin(&synth_block_trace);
}

static void count(audio_perf_bucket_t *bucket, uint32_t us)
{
    ++bucket->blocks;
    if (us > bucket->max_us) bucket->max_us = us;
    bucket->over_target += us > AUDIO_PERF_TARGET_US;
    bucket->deadlines += us >= AUDIO_PERF_DEADLINE_US;
}

static bool event16(const synth_block_trace_t *t)
{
    if (!t->note_ons[0] || t->frames[0] != 128 ||
        t->min_voices[0] != 16 || t->max_voices[0] != 16) return false;
    for (unsigned part = 1; part < 5; ++part)
        if (t->frames[part] || t->events[part]) return false;
    return true;
}

static bool steady32(const synth_block_trace_t *t)
{
    if (!synth_block_trace_full32(t)) return false;
    for (unsigned part = 0; part < 5; ++part)
        if (t->events[part]) return false;
    return true;
}

bool audio_perf_record(uint32_t prepare_us, uint32_t write_us, int64_t end_us,
                       uint32_t dma_overflows, unsigned usb_events, bool web,
                       const sound_ref_t *sound)
{
    synth_block_trace_t *trace = &synth_block_trace;
    if (!trace->active) return false;
    if (!sound_ref_equal(sound, &s_window.sound)) {
        audio_perf_abort(AUDIO_PERF_SOUND_CHANGED, end_us, dma_overflows);
        return false;
    }
    trace->active = false;
    count(&s_window.all, prepare_us);
    if (!trace->valid || trace->segment_frames != 128 || !trace->segments) ++s_window.invalid_trace;
    else if (event16(trace)) count(&s_window.event16, prepare_us);
    else if (steady32(trace)) count(&s_window.steady32, prepare_us);

    const uint32_t gap = (uint32_t)(s_block_start - s_previous_start);
    s_previous_start = s_block_start;
    if (gap > s_window.gap_max_us) s_window.gap_max_us = gap;
    if (gap > AUDIO_PERF_GAP_US) ++s_window.long_gaps;
    if (write_us > s_window.write_max_us) s_window.write_max_us = write_us;
    s_window.dma_overflows = dma_overflows - s_dma_start;
    s_window.usb_events += usb_events;
    s_window.web_blocks += web;
    s_window.end_us = end_us;
    return s_window.all.blocks == AUDIO_PERF_BLOCKS;
}

void audio_perf_publish(uint32_t stack_high_water)
{
    s_window.stack_high_water = stack_high_water;
    atomic_store(&s_state, READY);
}

void audio_perf_abort(audio_perf_result_t reason, int64_t now_us, uint32_t dma_overflows)
{
    const perf_state_t state = atomic_load(&s_state);
    if (state == REQUESTED) start_window(now_us, dma_overflows);
    else if (state != CAPTURING) return;
    synth_block_trace.active = false;
    s_window.result = reason;
    s_window.end_us = now_us;
    s_window.dma_overflows = dma_overflows - s_dma_start;
    audio_perf_publish(0);
}
