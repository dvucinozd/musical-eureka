#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/* One audio owner; reset before controls, stop after render. No sample hooks.
 * Voice counts are cached engine stats updated at every event/render boundary. */
typedef struct {
    bool active, valid;
    uint32_t segments, segment_frames, usb_events;
    uint32_t note_ons[5], note_offs[5], events[5];
    uint32_t calls[5], frames[5], min_voices[5], max_voices[5];
} synth_block_trace_t;
extern synth_block_trace_t synth_block_trace;

static inline void synth_block_trace_begin(synth_block_trace_t *t)
{
    memset(t, 0, sizeof(*t));
    t->active = t->valid = true;
    for (unsigned p = 0; p < 5; ++p) t->min_voices[p] = UINT32_MAX;
}
static inline void synth_block_trace_event(synth_block_trace_t *t, unsigned p,
    bool on, bool off, bool ok)
{
    if (!t->active) return;
    if (p >= 5 || !ok) { t->valid = false; return; }
    ++t->events[p];
    t->note_ons[p] += on;
    t->note_offs[p] += off;
}
static inline void synth_block_trace_render(synth_block_trace_t *t, unsigned p,
    unsigned frames, unsigned before, unsigned after, bool ok)
{
    if (!t->active) return;
    if (p >= 5 || !ok || !frames || frames > 128 || t->frames[p] > 128 - frames) {
        t->valid = false; return;
    }
    ++t->calls[p]; t->frames[p] += frames;
    unsigned low = before < after ? before : after, high = before > after ? before : after;
    if (low < t->min_voices[p]) t->min_voices[p] = low;
    if (high > t->max_voices[p]) t->max_voices[p] = high;
}
static inline bool synth_block_trace_full32(const synth_block_trace_t *t)
{
    if (!t->valid || !t->segments || t->segment_frames != 128) return false;
    for (unsigned p = 0; p < 5; ++p) {
        unsigned want = p ? 4 : 16;
        if (!t->calls[p] || t->frames[p] != 128 || t->min_voices[p] != want ||
            t->max_voices[p] != want) return false;
    }
    return true;
}
