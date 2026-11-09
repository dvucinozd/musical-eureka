#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOOPER_TRACK_COUNT 4
#define LOOPER_MAX_EVENTS  256
#define LOOPER_VOICES_PER_TRACK 4

typedef enum {
    LOOPER_STATE_EMPTY = 0,
    LOOPER_STATE_REC,
    LOOPER_STATE_PLAY,
    LOOPER_STATE_OVERDUB,
    LOOPER_STATE_STOPPED
} looper_track_state_t;

typedef struct {
    uint32_t sample_offset;
    uint8_t type;      /* 1 = NOTE_ON, 0 = NOTE_OFF */
    uint8_t note;
    uint8_t velocity;
    uint8_t reserved;
} looper_event_t;

typedef struct {
    looper_track_state_t state;
    synth_preset_t preset;
    synth_config_t config;
    uint8_t volume_pct; /* 0 .. 100 */
    bool muted;
    bool full;

    looper_event_t events[LOOPER_MAX_EVENTS];
    uint16_t event_count;

    /* Per-track 4-voice polyphonic sub-engine */
    synth_voice_t voices[LOOPER_VOICES_PER_TRACK];
    synth_lfo_t lfo;
    bool held_notes[128];
    bool record_held[128];
    uint16_t record_held_count;
    int16_t last_note;
    /* Captured with the timbre. SOUND_SF2 plays on sampler part track+1 instead of
     * the voices above; never a bank pointer. Synth-only unless a sampler is set. */
    sound_ref_t sound;
} looper_track_t;

typedef struct synth_looper_s {
    looper_track_t tracks[LOOPER_TRACK_COUNT];
    uint8_t bars;              /* 1, 2, 4, or 8 bars */
    uint16_t bpm;              /* Synced with synth arp BPM */
    uint32_t loop_samples;     /* Total samples in one full loop cycle */
    uint32_t cursor_samples;   /* Current transport position (0 .. loop_samples-1) */
    bool transport_running;
    /* Set by synth_sampler_install_host; NULL keeps every track synth-only. */
    const synth_sampler_ops_t *sampler;
} synth_looper_t;

typedef struct {
    looper_track_state_t state;
    synth_preset_t preset;
    uint8_t volume_pct;
    bool muted;
    bool full;
    uint16_t event_count;
    uint8_t active_voices;
    bool sf; /* track plays a SoundFont program */
} looper_track_status_t;

typedef struct {
    uint8_t bars;
    uint16_t bpm;
    uint32_t loop_samples;
    uint32_t cursor_samples;
    bool transport_running;
    looper_track_status_t tracks[LOOPER_TRACK_COUNT];
} looper_status_t;

void looper_init(synth_looper_t *l, uint16_t bpm);
void looper_panic(synth_looper_t *l);
void looper_set_bpm(synth_looper_t *l, uint16_t bpm);
void looper_set_bars(synth_looper_t *l, uint8_t bars);
uint8_t looper_get_bars(const synth_looper_t *l);

/* Track controls */
void looper_track_trigger(synth_looper_t *l, uint8_t track, const synth_t *live_synth);
void looper_track_stop(synth_looper_t *l, uint8_t track);
void looper_track_clear(synth_looper_t *l, uint8_t track);
void looper_clear_all(synth_looper_t *l);
void looper_track_set_mute(synth_looper_t *l, uint8_t track, bool muted);
void looper_track_toggle_mute(synth_looper_t *l, uint8_t track);
void looper_track_set_volume(synth_looper_t *l, uint8_t track, uint8_t volume_pct);
void looper_track_set_preset(synth_looper_t *l, uint8_t track, synth_preset_t preset);

/* Record incoming live Note On / Note Off into any REC or OVERDUB track */
void looper_record_note(synth_looper_t *l, uint8_t note, uint8_t velocity, bool is_note_on, const synth_t *live_synth);

/* Check if any looper track is actively sounding or running */
bool looper_is_sounding(const synth_looper_t *l);

/* Advance looper transport and mix active track voices into pre-FX buffer */
void looper_render_mix(synth_looper_t *l, float *mix_buf, size_t num_frames, float lfo_sin);

/* Snapshot status for UI / Telemetry */
void looper_get_status(const synth_looper_t *l, looper_status_t *out);
const char *looper_state_name(looper_track_state_t st);

#ifdef __cplusplus
}
#endif

uint32_t looper_frames_to_boundary(const synth_looper_t *l);
/* Number of tracks holding a SoundFont sound (their sampler parts stay bound). */
unsigned looper_sampler_tracks(const synth_looper_t *l);
void looper_close_recorded_notes(synth_looper_t *l);
