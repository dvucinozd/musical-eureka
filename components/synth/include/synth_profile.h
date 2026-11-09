#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Diagnostic build only. Exclusive buckets: entering a child pauses its parent.
 * Single audio owner; monitor copies/reset windows under one bounded lock.
 * No I/O, allocation or logging. Normal builds compile every hook away. */
enum synth_profile_stage {
    SP_HAL_PRE, SP_SYNTH_MISC, SP_SEGMENT, SP_LOOP_EVENT, SP_NOTE_EVENT,
    SP_SF0, SP_SF1, SP_SF2, SP_SF3, SP_SF4, SP_SYNTH_VOICES,
    SP_TRACK_MIX, SP_FX, SP_HAL_POST,
    SP_NOTE_ADMIT, SP_NOTE_GAIN, SP_NOTE_PITCH, SP_NOTE_ENV, SP_NOTE_FILTER, SP_NOTE_LFO,
    SP_FX_SAT, SP_FX_CHORUS, SP_FX_DELAY, SP_FX_GAIN, SP_STAGE_COUNT
};
typedef struct {
    uint32_t cycles[SP_STAGE_COUNT], total_cycles, switches;
    uint16_t segments, min_frames, max_frames, render_calls[5], note_ons[5];
    uint16_t frame_bins[5]; /* SF render calls: <=1, <=7, <=32, <=64, <=128 */
    uint8_t voices[5];
} synth_profile_block;
typedef struct {
    uint64_t cycles[SP_STAGE_COUNT];
    uint32_t blocks, switches, switch_overhead_cycles, publish_max_cycles;
    synth_profile_block worst;
    synth_profile_block steady32, event32; /* Separate maxima with exactly 32 ending voices. */
} synth_profile_window;

#ifdef NEKTAR_AUDIO_PROFILE
#ifdef NEKTAR_INNER_PROFILE
enum { SI_CONTROL, SI_AMP, SI_INTERP, SI_FILTER, SI_MIX, SI_PHASE, SI_ENV, SI_COUNT };
typedef struct { uint64_t cycles[SI_COUNT]; uint32_t selected, samples, cells, tick_cost; } synth_inner_window;
bool synth_profile_inner_select(void);
uint32_t synth_profile_inner_tick(float dependency);
void synth_profile_inner_add(unsigned kind, uint32_t cycles);
void synth_profile_inner_sample(const uint32_t ticks[6]);
bool synth_profile_inner_take(synth_inner_window *out);
#endif
void synth_profile_init(void);
void synth_profile_begin(void);
unsigned synth_profile_enter(unsigned stage);
void synth_profile_leave(unsigned parent);
void synth_profile_segment(unsigned frames);
void synth_profile_render(unsigned part, unsigned frames);
void synth_profile_note_on(unsigned part);
void synth_profile_finish(unsigned live_voices, const uint8_t track_voices[4]);
bool synth_profile_take(synth_profile_window *out);
#else
#define synth_profile_init() ((void)0)
#define synth_profile_begin() ((void)0)
#define synth_profile_enter(stage) (0u)
#define synth_profile_leave(parent) ((void)(parent))
#define synth_profile_segment(frames) ((void)0)
#define synth_profile_render(part, frames) ((void)0)
#define synth_profile_note_on(part) ((void)0)
#define synth_profile_finish(live, tracks) ((void)0)
#endif

#if defined(NEKTAR_INNER_PROFILE) && !defined(NEKTAR_AUDIO_PROFILE)
#error "Inner probes require audio profiling"
#endif
