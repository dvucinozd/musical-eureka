#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "midi_event.h"
#include "synth_sampler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNTH_SAMPLE_RATE 48000
#define SYNTH_VOICES_COUNT 16
#define SYNTH_MAX_GAIN_PERMILLE 50     /* Legacy control units: 0..50 maps to 0..100% */
#define SYNTH_DEFAULT_GAIN_PERMILLE 35 /* 70% of the master range */
#define SYNTH_OUTPUT_PEAK 0.55f       /* -5.19 dBFS ceiling, before codec attenuation */
#define SYNTH_FX_HEADROOM 0.05f       /* Preserve existing FX feedback operating level */

typedef enum {
    SYNTH_WAVE_SINE = 0,
    SYNTH_WAVE_TRIANGLE,
    SYNTH_WAVE_SAW,
    SYNTH_WAVE_SQUARE,
    SYNTH_WAVE_COUNT
} synth_waveform_t;

typedef enum {
    SYNTH_FILTER_LP = 0,
    SYNTH_FILTER_BP,
    SYNTH_FILTER_HP,
    SYNTH_FILTER_COUNT
} synth_filter_type_t;

typedef enum {
    SYNTH_VEL_NORMAL = 0,
    SYNTH_VEL_SOFT,
    SYNTH_VEL_HARD,
    SYNTH_VEL_FIXED,
    SYNTH_VEL_COUNT
} synth_vel_curve_t;

typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_ATTACK,
    VOICE_STATE_DECAY,
    VOICE_STATE_SUSTAIN,
    VOICE_STATE_RELEASE
} voice_state_t;

typedef enum {
    SYNTH_PRESET_ANALOG_BRASS = 0,
    SYNTH_PRESET_WARM_PAD,
    SYNTH_PRESET_MOOG_BASS,
    SYNTH_PRESET_BRIGHT_LEAD,
    SYNTH_PRESET_RETRO_80S,
    SYNTH_PRESET_PLUCK_ARP,
    SYNTH_PRESET_CHURCH_ORGAN,
    SYNTH_PRESET_SPACE_STRINGS,
    SYNTH_PRESET_SUB_BASS,
    SYNTH_PRESET_JUNO_STRINGS,
    SYNTH_PRESET_PERC_ORGAN,
    SYNTH_PRESET_SCI_FI_SWEEP,
    SYNTH_PRESET_ACID_BASS,
    SYNTH_PRESET_RUBBER_BASS,
    SYNTH_PRESET_PWM_BASS,
    SYNTH_PRESET_DETUNED_BASS,
    SYNTH_PRESET_SOFT_SUB,
    SYNTH_PRESET_LASER_LEAD,
    SYNTH_PRESET_GLIDE_LEAD,
    SYNTH_PRESET_CHIP_LEAD,
    SYNTH_PRESET_HOLLOW_LEAD,
    SYNTH_PRESET_WIDE_SAW,
    SYNTH_PRESET_VELVET_PAD,
    SYNTH_PRESET_DIGITAL_PAD,
    SYNTH_PRESET_SLOW_MOTION,
    SYNTH_PRESET_RESONANT_PAD,
    SYNTH_PRESET_CRYSTAL_PLUCK,
    SYNTH_PRESET_PULSE_PLUCK,
    SYNTH_PRESET_SYNTH_KEYS,
    SYNTH_PRESET_SOFT_BELL,
    SYNTH_PRESET_NOISE_SWEEP,
    SYNTH_PRESET_SPACE_DRONE,
    SYNTH_PRESET_COUNT
} synth_preset_t;


/* State-Variable Filter (SVF) state per voice */
typedef struct {
    float s1;
    float s2;
} synth_svf_t;

typedef struct {
    voice_state_t state;
    uint8_t note;
    uint8_t velocity;
    bool sustain_held;         /* Held by sustain pedal after key release */
    uint32_t age_samples;      /* Age counter for LRU voice stealing */

    /* Frequency and oscillator */
    float base_freq_hz;
    float target_freq_hz;
    float current_freq_hz;
    uint32_t phase;
    uint32_t phase_inc;
    uint32_t sub_phase;
    uint32_t unison_phase;

    /* ADSR envelope */
    float env_level;           /* current amplitude 0.0 .. 1.0 */
    float env_target;          /* peak amplitude for attack */
    float attack_step;
    float decay_step;
    float sustain_level;
    float release_step;
    float env_goal, env_delta;
    uint32_t env_remaining;

    /* Per-voice resonant filter state */
    synth_svf_t filter;

    /* Per-voice noise generator seed */
    uint32_t noise_seed;
} synth_voice_t;


#define SYNTH_DELAY_MAX_SAMPLES 16800 /* 350 ms at 48 kHz */
#define SYNTH_CHORUS_MAX_SAMPLES 1440 /* 30 ms at 48 kHz */

typedef struct {
    float buffer[SYNTH_DELAY_MAX_SAMPLES];
    uint32_t active_samples;       /* Buffered samples above the audible tail threshold */
    uint32_t write_idx;
    uint32_t delay_samples;        /* delay in samples (e.g. 9600 = 200 ms) */
    float feedback;                /* 0.0 .. 0.75 */
    float dampening;               /* one-pole damping 0.0 .. 0.8 */
    float damp_state;              /* filter memory */
    float wet_mix;                 /* dry/wet ratio 0.0 .. 0.5 */
    bool enabled;
} synth_delay_t;

typedef struct {
    float buffer[SYNTH_CHORUS_MAX_SAMPLES];
    uint32_t active_samples;       /* Buffered samples above the audible tail threshold */
    uint32_t write_idx;
    float lfo_phase;
    float lfo_phase_inc;           /* e.g. 0.8 Hz / 48000 */
    float base_delay_samples;      /* e.g. 480 = 10 ms */
    float depth_samples;           /* e.g. 192 = 4 ms */
    float wet_mix;                 /* dry/wet ratio 0.0 .. 0.5 */
    bool enabled;
} synth_chorus_t;

typedef struct {
    float phase;
    float phase_inc;               /* freq / sample_rate */
    float freq_hz;                 /* 0.1 .. 20.0 Hz */
    float vibrato_semitones;       /* max vibrato depth e.g. 0.5 semitone */
    float filter_octaves;          /* max filter lfo sweep */
    bool enabled;
} synth_lfo_t;

typedef enum {
    SYNTH_ARP_OFF = 0,
    SYNTH_ARP_UP,
    SYNTH_ARP_DOWN,
    SYNTH_ARP_UP_DOWN,
    SYNTH_ARP_RANDOM,
    SYNTH_ARP_CHORD,
    SYNTH_ARP_MODE_COUNT
} synth_arp_mode_t;

typedef enum {
    SYNTH_ARP_RATE_1_4 = 0,
    SYNTH_ARP_RATE_1_8,
    SYNTH_ARP_RATE_1_16,
    SYNTH_ARP_RATE_1_32,
    SYNTH_ARP_RATE_COUNT
} synth_arp_rate_t;

#define SYNTH_ARP_MAX_HELD 16

typedef struct {
    synth_arp_mode_t mode;
    synth_arp_rate_t rate;
    uint8_t octaves;               /* 1, 2, 3, or 4 */
    uint16_t bpm;                  /* e.g. 120 (40 .. 240) */
    float gate_ratio;              /* 0.1 .. 0.95 (e.g. 0.65) */

    /* Internal step sequencer state */
    uint8_t held_notes[SYNTH_ARP_MAX_HELD];
    uint8_t held_velocities[SYNTH_ARP_MAX_HELD];
    bool held_physical[SYNTH_ARP_MAX_HELD];
    uint8_t held_count;
    uint8_t physical_held_count;
    bool latch;

    uint32_t step_samples;         /* samples per step */
    uint32_t sample_counter;       /* progress within step */
    int16_t current_step_idx;
    int16_t current_octave;
    bool direction_up;             /* for UP_DOWN mode */
    uint8_t last_playing_notes[SYNTH_ARP_MAX_HELD];
    uint8_t last_playing_count;
    bool note_active;
} synth_arp_t;

typedef struct {
    uint32_t sample_rate;
    synth_waveform_t waveform;
    uint16_t master_gain_permille; /* 0..50 */
    uint16_t attack_ms;            /* e.g. 5 ms */
    uint16_t decay_ms;             /* e.g. 40 ms */
    float sustain_ratio;           /* 0.0 .. 1.0 */
    uint16_t release_ms;           /* e.g. 80 ms */
    int8_t pitch_bend_range;       /* semitones, e.g. 2 */

    /* Timbre and Filter parameters */
    float base_cutoff_hz;          /* Base filter cutoff frequency (Hz) */
    float resonance;               /* Filter Q (0.5 = flat, 1.0..4.0 = resonant) */
    float filter_env_octaves;      /* Envelope modulation depth in octaves */
    float mod_wheel_octaves;       /* Modulation wheel sweep depth in octaves */

    /* Advanced Modulation & FX parameters */
    bool lfo_enable;
    float lfo_freq_hz;
    float lfo_vibrato_depth;       /* semitones */
    bool chorus_enable;
    float chorus_wet;
    bool delay_enable;
    uint16_t delay_ms;
    float delay_feedback;
    float delay_wet;

    /* Arpeggiator default config */
    synth_arp_mode_t arp_mode;
    synth_arp_rate_t arp_rate;
    uint8_t arp_octaves;
    uint16_t arp_bpm;

    /* M15 DSP: Pulse-Width Modulation, Sub-Oscillator & Noise Generator */
    float pwm_duty;            /* 0.05 .. 0.95 (default 0.50) */
    float pwm_lfo_depth;       /* 0.0 .. 0.45 (LFO PWM modulation depth) */
    float sub_osc_level;       /* 0.0 .. 1.0 (sub-oscillator mix level) */
    uint8_t sub_osc_octaves;   /* 1 or 2 octaves below */
    float noise_level;         /* 0.0 .. 1.0 (white noise generator mix level) */

    /* Extended synthesis & performance controls */
    synth_filter_type_t filter_type; /* LP (0), BP (1), HP (2) */
    synth_vel_curve_t velocity_curve;/* NORMAL (0), SOFT (1), HARD (2), FIXED (3) */
    uint8_t midi_channel;            /* 0 = Omni, 1..16 = specific MIDI channel */
    uint16_t glide_ms;               /* 0 .. 2000 ms portamento time */
    float unison_detune;             /* 0.0 .. 0.50 semitones stereo/fat detune */
    bool arp_latch;                  /* Hold arpeggiator notes after key release */
} synth_config_t;


typedef struct {
    synth_config_t config;
    uint32_t input_epoch;
    float output_gain;            /* Ramped final gain, normalized 0..1 */

    /* Polyphonic voice pool */
    synth_voice_t voices[SYNTH_VOICES_COUNT];

    /* Advanced Modulation & Audio FX engines */
    synth_lfo_t lfo;
    synth_chorus_t chorus;
    synth_delay_t delay;

    /* Performance Arpeggiator engine */
    synth_arp_t arp;

    /* Global pitch bend and modulation */
    int16_t pitch_bend_value;      /* -8192 .. +8191 */
    float pitch_bend_mult;
    uint8_t mod_wheel;             /* 0..127 from CC 1 */
    float mod_wheel_mult;          /* Cached 2^((mod_wheel/127)*mod_wheel_octaves) */

    /* Controller state */
    bool sustain_pedal;            /* CC 64 */
    synth_preset_t current_preset;

    /* Telemetry and statistics */
    int16_t last_note;
    uint32_t note_on_count;
    uint32_t note_off_count;
    uint32_t voice_steal_count;
    uint32_t rendered_frames;

    /* Optional 4-Track Performance Looper instance (owned by audio_hal or test) */
    struct synth_looper_s *looper;
    /* Inert unless the host experiment API installs a sampler. Not persisted. */
    const synth_sampler_ops_t *sampler;
    sound_part_t live_sound;
    uint32_t sampler_rejected;
    /* Sampler route note ownership per source and key: live input epoch + 1 of
     * the press, 0 = not held. Cleared by panic. */
    uint32_t key_claims[3][128];
    bool sound_committing;
} synth_t;


typedef struct {
    int16_t last_note;
    bool sounding;                 /* true if any voice is sounding (PA ON) */
    uint8_t active_voices;         /* number of active voices (0..16) */
    synth_waveform_t waveform;
    synth_preset_t preset;
    uint16_t master_gain_permille;
    float current_freq_hz;         /* frequency of last played note */
    float current_cutoff_hz;       /* current modulated cutoff frequency */
    float resonance;
    uint8_t mod_wheel;
    bool delay_active;
    bool chorus_active;
    bool lfo_active;
    synth_arp_mode_t arp_mode;
    synth_arp_rate_t arp_rate;
    bool arp_latch;
    uint16_t arp_bpm;
    uint8_t arp_octaves;
    synth_filter_type_t filter_type;
    uint32_t note_on_count;
    uint32_t note_off_count;
    uint32_t voice_steal_count;
} synth_telemetry_t;

/* Initialize polyphonic synthesizer state and lookup tables */
void synth_init(synth_t *s, const synth_config_t *cfg);
void synth_normalize_config(synth_config_t *cfg);
bool synth_config_is_valid(const synth_config_t *cfg);
void synth_panic(synth_t *s);

/* Sampler route; install once after init at 48k. Ops/context outlive synth.
 * NULL is the unchanged normal synth-only path. An attached looper receives the
 * same ops: a track capturing a live SoundFont plays it on part track+1 (attach
 * the looper before installing). Only the alpha image installs it.
 * No legacy MIDI/Web ingress may dispatch while installed: use the admitted
 * event API after validating source press ownership/tombstones externally.
 * Epoch alone does NOT solve physical MIDI1 overlapping key/off ambiguity.
 */
bool synth_sampler_install_host(synth_t *, const synth_sampler_ops_t *);
sound_result_t synth_sound_select_host(synth_t *, uint32_t expected_epoch,
                                      uint64_t request, const sound_ref_t *);
bool synth_sound_event_host(synth_t *, uint32_t capture_epoch, const midi_event_t *);
/* Note sources for ownership (docs/SOUNDFONT_RELEASE_PLAN_2026-11-01.md, SF06):
 * a Note Off ends only its own source's press from the current epoch, and the
 * engine releases the key only when no other source holds it. A USB press from
 * before a sound change is a tombstone: its Note Off only forgets it, and a new
 * USB Note On of that key is rejected until then (MIDI1 has no press id). */
typedef enum { SYNTH_SOURCE_LOCAL, SYNTH_SOURCE_USB, SYNTH_SOURCE_WEB, SYNTH_SOURCE_COUNT } synth_source_t;
bool synth_sound_event_from(synth_t *, synth_source_t, uint32_t capture_epoch, const midi_event_t *);

/* Handle semantic MIDI event in real time (Note On/Off, Pitch Bend, CC, Program Change) */
void synth_handle_midi_event(synth_t *s, const midi_event_t *ev);

/* Trusted audio-owner/internal arp note dispatch. With the host sampler
 * installed, external sources MUST use synth_sound_event_host after ownership
 * validation; these raw calls have no captured epoch/press token. */
void synth_note_on(synth_t *s, uint8_t note, uint8_t velocity);
void synth_note_off(synth_t *s, uint8_t note);
void synth_all_notes_off(synth_t *s);

/* Performance Arpeggiator APIs */
void synth_set_arp_mode(synth_t *s, synth_arp_mode_t mode);
synth_arp_mode_t synth_get_arp_mode(const synth_t *s);
void synth_set_arp_bpm(synth_t *s, uint16_t bpm);
uint16_t synth_get_arp_bpm(const synth_t *s);
void synth_set_arp_octaves(synth_t *s, uint8_t octaves);
uint8_t synth_get_arp_octaves(const synth_t *s);
void synth_set_arp_rate(synth_t *s, synth_arp_rate_t rate);
synth_arp_rate_t synth_get_arp_rate(const synth_t *s);
void synth_set_arp_latch(synth_t *s, bool latch);
bool synth_get_arp_latch(const synth_t *s);
const char *synth_arp_mode_name(synth_arp_mode_t mode);


/* Preset management */
void synth_set_preset(synth_t *s, synth_preset_t preset);
void synth_next_preset(synth_t *s);
synth_preset_t synth_get_preset(const synth_t *s);
const char *synth_preset_name(synth_preset_t preset);
const synth_config_t *synth_get_preset_config(synth_preset_t preset);
void synth_get_config(const synth_t *s, synth_config_t *out_cfg);
void synth_apply_config(synth_t *s, const synth_config_t *cfg);

/* Filter and parameter adjustments */
void synth_set_cutoff(synth_t *s, float cutoff_hz);
float synth_get_cutoff(const synth_t *s);
void synth_set_resonance(synth_t *s, float q);
float synth_get_resonance(const synth_t *s);
void synth_set_mod_wheel(synth_t *s, uint8_t val);
uint8_t synth_get_mod_wheel(const synth_t *s);

/* Audio FX controls */
void synth_set_delay(synth_t *s, bool enable, uint16_t delay_ms, float feedback, float wet_mix);
void synth_set_chorus(synth_t *s, bool enable, float wet_mix);
void synth_set_lfo(synth_t *s, bool enable, float freq_hz, float vibrato_depth);

/* M15 DSP controls */
void synth_set_pwm(synth_t *s, float duty, float lfo_depth);
void synth_set_sub_osc(synth_t *s, float level, uint8_t octaves);
void synth_set_noise(synth_t *s, float level);

/* Extended synthesis & performance controls */
void synth_set_filter_type(synth_t *s, synth_filter_type_t type);
synth_filter_type_t synth_get_filter_type(const synth_t *s);
const char *synth_filter_type_name(synth_filter_type_t type);
void synth_set_adsr(synth_t *s, uint16_t attack_ms, uint16_t decay_ms, float sustain_ratio, uint16_t release_ms);
void synth_set_filter_env(synth_t *s, float octaves);
void synth_set_glide(synth_t *s, uint16_t glide_ms);
uint16_t synth_get_glide(const synth_t *s);
void synth_set_unison(synth_t *s, float detune_semitones);
float synth_get_unison(const synth_t *s);
void synth_set_velocity_curve(synth_t *s, synth_vel_curve_t curve);
synth_vel_curve_t synth_get_velocity_curve(const synth_t *s);
void synth_set_midi_channel(synth_t *s, uint8_t channel);
uint8_t synth_get_midi_channel(const synth_t *s);

void synth_set_master_gain(synth_t *s, uint16_t gain_permille);
void synth_set_waveform(synth_t *s, synth_waveform_t wf);
void synth_set_pitch_bend_range(synth_t *s, int8_t semitones);
const char *synth_waveform_name(synth_waveform_t wf);

/* Render audio block: mixes filtered active voices with soft-clipping protection */
void synth_render(synth_t *s, float *out_frames, size_t num_frames);

/* Check if any voice is producing sound */
bool synth_is_sounding(const synth_t *s);

/* Snapshot telemetry for diagnostics and logging */
void synth_get_telemetry(const synth_t *s, synth_telemetry_t *out);

/* Helper: MIDI note number (0..127) to frequency in Hz (A4 = 440.0 Hz) */
float synth_note_to_freq(uint8_t note);

#ifdef __cplusplus
}
#endif

void synth_handle_local_note(synth_t *s, uint8_t note, uint8_t velocity, bool down);
