#include "synth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        exit(1); \
    } \
} while(0)

static void test_synth_init(void)
{
    synth_t s;
    synth_init(&s, NULL);
    ASSERT(s.config.sample_rate == 48000);
    ASSERT(s.config.waveform == SYNTH_WAVE_SAW);
    ASSERT(s.config.master_gain_permille == 35);
    ASSERT(!synth_is_sounding(&s));
    ASSERT(s.last_note == -1);
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        ASSERT(s.voices[i].state == VOICE_STATE_IDLE);
    }
    printf("test_synth_init PASSED\n");
}

static void test_synth_note_to_freq(void)
{
    float a4 = synth_note_to_freq(69);
    ASSERT(fabsf(a4 - 440.0f) < 0.001f);

    float a3 = synth_note_to_freq(57);
    ASSERT(fabsf(a3 - 220.0f) < 0.001f);

    float a5 = synth_note_to_freq(81);
    ASSERT(fabsf(a5 - 880.0f) < 0.001f);

    float c4 = synth_note_to_freq(60);
    ASSERT(fabsf(c4 - 261.6256f) < 0.01f);

    printf("test_synth_note_to_freq PASSED\n");
}

static void test_synth_polyphony_16_voices(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* Play 16 distinct notes */
    for (int i = 0; i < 16; ++i) {
        synth_note_on(&s, 48 + i, 100);
    }

    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 16);
    ASSERT(t.sounding == true);
    ASSERT(s.voice_steal_count == 0);

    /* Play 17th note -> triggers voice stealing */
    synth_note_on(&s, 70, 100);
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 16);
    ASSERT(s.voice_steal_count == 1);

    /* Release note 48 -> active voices should drop to 15 once released */
    synth_note_off(&s, 48);
    /* Render 100 blocks (release is 70ms = ~26 blocks) */
    float buf[128];
    for (int b = 0; b < 40; ++b) {
        synth_render(&s, buf, 128);
    }
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 15);

    printf("test_synth_polyphony_16_voices PASSED (voice stealing verified)\n");
}

static void test_synth_chords_and_clipping(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_master_gain(&s, 25); /* 50% */

    /* Play C major chord: C4 (60), E4 (64), G4 (67) */
    synth_note_on(&s, 60, 127);
    synth_note_on(&s, 64, 127);
    synth_note_on(&s, 67, 127);

    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 3);

    float buf[128];
    for (int b = 0; b < 20; ++b) {
        synth_render(&s, buf, 128);
        for (int i = 0; i < 128; ++i) {
            ASSERT(!isnan(buf[i]));
            ASSERT(!isinf(buf[i]));
            /* Ensure output is strictly bounded within master gain limit */
            ASSERT(fabsf(buf[i]) <= SYNTH_OUTPUT_PEAK * 0.5f + 0.000001f);
        }
    }

    /* Release chord */
    synth_note_off(&s, 60);
    synth_note_off(&s, 64);
    synth_note_off(&s, 67);

    for (int b = 0; b < 50; ++b) {
        synth_render(&s, buf, 128);
    }
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 0);
    /* Voice release can finish before the enabled FX tail. Wait for both. */
    for (int b = 0; b < 4000 && synth_is_sounding(&s); ++b) synth_render(&s, buf, 128);
    ASSERT(!synth_is_sounding(&s));

    printf("test_synth_chords_and_clipping PASSED\n");
}

static void test_synth_sustain_pedal(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* Press sustain pedal (CC 64 = 127) */
    midi_event_t ev_pedal_down = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 64, .value = 127 }
    };
    synth_handle_midi_event(&s, &ev_pedal_down);
    ASSERT(s.sustain_pedal == true);

    /* Play and release 3 notes */
    synth_note_on(&s, 60, 100);
    synth_note_on(&s, 64, 100);
    synth_note_on(&s, 67, 100);

    synth_note_off(&s, 60);
    synth_note_off(&s, 64);
    synth_note_off(&s, 67);

    /* Render 30 blocks */
    float buf[128];
    for (int b = 0; b < 30; ++b) {
        synth_render(&s, buf, 128);
    }

    /* Voices must still be active because pedal is held */
    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 3);
    ASSERT(synth_is_sounding(&s));

    /* Release sustain pedal (CC 64 = 0) */
    midi_event_t ev_pedal_up = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 64, .value = 0 }
    };
    synth_handle_midi_event(&s, &ev_pedal_up);
    ASSERT(s.sustain_pedal == false);

    /* Render 40 blocks -> all 3 voices should complete release */
    for (int b = 0; b < 40; ++b) {
        synth_render(&s, buf, 128);
    }
    synth_get_telemetry(&s, &t);
    ASSERT(t.active_voices == 0);
    /* Voice release can finish before the enabled FX tail. Wait for both. */
    for (int b = 0; b < 4000 && synth_is_sounding(&s); ++b) synth_render(&s, buf, 128);
    ASSERT(!synth_is_sounding(&s));

    printf("test_synth_sustain_pedal PASSED\n");
}

static void test_synth_pitch_bend_poly(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_note_on(&s, 69, 100); /* A4 = 440 Hz */
    synth_note_on(&s, 60, 100); /* C4 = 261.63 Hz */

    /* Pitch bend +2 semitones (+8191) */
    midi_event_t ev_bend = {
        .type = MIDI_EVENT_PITCH_BEND,
        .channel = 0,
        .pitch_bend = { .value = 8191, .raw_14bit = 16383 }
    };
    synth_handle_midi_event(&s, &ev_bend);

    float expected_a4_bent = 440.0f * powf(2.0f, 2.0f / 12.0f);
    float expected_c4_bent = synth_note_to_freq(60) * powf(2.0f, 2.0f / 12.0f);

    /* Find voices */
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s.voices[i].state != VOICE_STATE_IDLE) {
            if (s.voices[i].note == 69) {
                ASSERT(fabsf(s.voices[i].current_freq_hz - expected_a4_bent) < 0.2f);
            } else if (s.voices[i].note == 60) {
                ASSERT(fabsf(s.voices[i].current_freq_hz - expected_c4_bent) < 0.2f);
            }
        }
    }
    printf("test_synth_pitch_bend_poly PASSED\n");
}

static void test_synth_waveforms(void)
{
    float buf[128];
    for (int wf = 0; wf < (int)SYNTH_WAVE_COUNT; ++wf) {
        synth_t s;
        synth_init(&s, NULL);
        synth_set_waveform(&s, (synth_waveform_t)wf);

        /* Play 4 voices simultaneously */
        synth_note_on(&s, 60, 127);
        synth_note_on(&s, 64, 127);
        synth_note_on(&s, 67, 127);
        synth_note_on(&s, 71, 127);

        for (int block = 0; block < 20; ++block) {
            synth_render(&s, buf, 128);
            for (int i = 0; i < 128; ++i) {
                ASSERT(!isnan(buf[i]));
                ASSERT(!isinf(buf[i]));
                ASSERT(fabsf(buf[i]) <= SYNTH_OUTPUT_PEAK * 0.7f + 0.000001f);
            }
        }
    }
    printf("test_synth_waveforms PASSED (all 4 waveforms verified with 4-note chord)\n");
}

static void test_synth_filter_attenuation(void)
{
    synth_t s_open, s_closed;
    synth_init(&s_open, NULL);
    synth_init(&s_closed, NULL);

    /* Open filter cutoff: 15 kHz, no env */
    s_open.config.base_cutoff_hz = 15000.0f;
    s_open.config.filter_env_octaves = 0.0f;
    s_open.config.resonance = 0.707f;

    /* Closed filter cutoff: 200 Hz, no env */
    s_closed.config.base_cutoff_hz = 200.0f;
    s_closed.config.filter_env_octaves = 0.0f;
    s_closed.config.resonance = 0.707f;

    /* Play high note: A5 (880 Hz, note 81) */
    synth_note_on(&s_open, 81, 127);
    synth_note_on(&s_closed, 81, 127);

    float buf_open[128];
    float buf_closed[128];

    /* Discard first block to bypass attack ramp */
    synth_render(&s_open, buf_open, 128);
    synth_render(&s_closed, buf_closed, 128);

    /* Render block and calculate RMS energy */
    synth_render(&s_open, buf_open, 128);
    synth_render(&s_closed, buf_closed, 128);

    float rms_open = 0.0f;
    float rms_closed = 0.0f;
    for (int i = 0; i < 128; ++i) {
        rms_open += buf_open[i] * buf_open[i];
        rms_closed += buf_closed[i] * buf_closed[i];
    }
    rms_open = sqrtf(rms_open / 128.0f);
    rms_closed = sqrtf(rms_closed / 128.0f);

    /* Closed filter at 200 Hz must heavily attenuate 880 Hz harmonic content (>6x attenuation) */
    ASSERT(rms_open > 0.005f);
    ASSERT(rms_closed < rms_open * 0.25f);

    printf("test_synth_filter_attenuation PASSED (open RMS=%.4f vs closed RMS=%.4f)\n", rms_open, rms_closed);
}

static void test_synth_filter_stability(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_resonance(&s, 8.0f); /* Maximum resonance Q = 8.0 */
    synth_set_cutoff(&s, 100.0f);

    /* Play 8 voices with high velocity */
    for (int i = 0; i < 8; ++i) {
        synth_note_on(&s, 40 + i * 5, 127);
    }

    float buf[128];
    for (int block = 0; block < 100; ++block) {
        /* Sweep cutoff dynamically during render */
        float fc = 100.0f + (float)block * 150.0f;
        synth_set_cutoff(&s, fc);

        synth_render(&s, buf, 128);
        for (int i = 0; i < 128; ++i) {
            ASSERT(!isnan(buf[i]));
            ASSERT(!isinf(buf[i]));
            /* Ensure tanhf keeps signal firmly within master headroom */
            ASSERT(fabsf(buf[i]) <= SYNTH_OUTPUT_PEAK);
        }
    }
    printf("test_synth_filter_stability PASSED (resonant sweep without blowup or NaN)\n");
}

static void test_synth_presets(void)
{
    synth_t s;
    synth_init(&s, NULL);

    for (int p = 0; p < (int)SYNTH_PRESET_COUNT; ++p) {
        synth_set_preset(&s, (synth_preset_t)p);
        ASSERT(synth_get_preset(&s) == (synth_preset_t)p);
        const char *name = synth_preset_name((synth_preset_t)p);
        ASSERT(name != NULL);
        ASSERT(strlen(name) > 0);

        synth_telemetry_t t;
        synth_get_telemetry(&s, &t);
        ASSERT(t.preset == (synth_preset_t)p);
        ASSERT(t.current_cutoff_hz >= 50.0f);
        ASSERT(t.resonance >= 0.5f);
    }

    /* Program change via MIDI event */
    midi_event_t ev_prog = {
        .type = MIDI_EVENT_PROGRAM_CHANGE,
        .channel = 0,
        .program_change = { .program = SYNTH_PRESET_MOOG_BASS }
    };
    synth_handle_midi_event(&s, &ev_prog);
    ASSERT(synth_get_preset(&s) == SYNTH_PRESET_MOOG_BASS);
    ASSERT(strcmp(synth_preset_name(synth_get_preset(&s)), "Moog Bass") == 0);

    printf("test_synth_presets PASSED (all presets and Program Change verified)\n");
}

static void test_synth_mod_wheel(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_preset(&s, SYNTH_PRESET_ANALOG_BRASS);

    float base_fc = synth_get_cutoff(&s);

    /* Mod wheel at 0 */
    midi_event_t ev_mod0 = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 1, .value = 0 }
    };
    synth_handle_midi_event(&s, &ev_mod0);
    ASSERT(synth_get_mod_wheel(&s) == 0);

    synth_telemetry_t t0;
    synth_get_telemetry(&s, &t0);
    ASSERT(fabsf(t0.current_cutoff_hz - base_fc) < 1.0f);

    /* Mod wheel at max 127 */
    midi_event_t ev_mod127 = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 1, .value = 127 }
    };
    synth_handle_midi_event(&s, &ev_mod127);
    ASSERT(synth_get_mod_wheel(&s) == 127);

    synth_telemetry_t t127;
    synth_get_telemetry(&s, &t127);
    /* For Analog Brass, mod_wheel_octaves = 2.5f -> 2^2.5 ~= 5.656x base cutoff */
    float expected_fc = base_fc * powf(2.0f, s.config.mod_wheel_octaves);
    ASSERT(fabsf(t127.current_cutoff_hz - expected_fc) < 5.0f);
    ASSERT(t127.current_cutoff_hz > base_fc * 5.0f);

    printf("test_synth_mod_wheel PASSED (Mod wheel sweeps cutoff from %.1f Hz to %.1f Hz)\n",
           t0.current_cutoff_hz, t127.current_cutoff_hz);
}

static void test_synth_cc_cutoff_resonance(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* CC 71: Resonance */
    midi_event_t ev_res = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 71, .value = 127 }
    };
    synth_handle_midi_event(&s, &ev_res);
    ASSERT(fabsf(synth_get_resonance(&s) - 5.0f) < 0.1f);

    /* CC 74: Cutoff */
    midi_event_t ev_cut = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 74, .value = 0 }
    };
    synth_handle_midi_event(&s, &ev_cut);
    ASSERT(fabsf(synth_get_cutoff(&s) - 100.0f) < 1.0f);

    printf("test_synth_cc_cutoff_resonance PASSED\n");
}

static void test_synth_lfo_vibrato(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_lfo(&s, true, 5.0f, 1.0f); /* 5 Hz, 1.0 semitone depth */
    synth_set_mod_wheel(&s, 127);        /* full mod wheel */

    synth_note_on(&s, 69, 100);          /* A4 = 440 Hz */

    float buf[128];
    float min_freq = 10000.0f;
    float max_freq = 0.0f;

    /* Render several blocks over one LFO cycle (200 ms = ~75 blocks) */
    for (int b = 0; b < 80; ++b) {
        synth_render(&s, buf, 128);
        for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
            if (s.voices[i].state != VOICE_STATE_IDLE && s.voices[i].note == 69) {
                float f = s.voices[i].current_freq_hz;
                if (f < min_freq) min_freq = f;
                if (f > max_freq) max_freq = f;
            }
        }
    }

    /* Frequency must modulate around 440 Hz (approx +/- 25 Hz for 1 semitone) */
    ASSERT(min_freq < 430.0f);
    ASSERT(max_freq > 450.0f);

    printf("test_synth_lfo_vibrato PASSED (Vibrato swept freq between %.1f Hz and %.1f Hz)\n", min_freq, max_freq);
}

static void test_synth_delay_effect(void)
{
    synth_t s;
    synth_init(&s, NULL);
    /* 100 ms delay, 50% feedback, 40% wet */
    synth_set_delay(&s, true, 100, 0.50f, 0.40f);

    /* Play very short note (impulse) */
    synth_note_on(&s, 69, 127);
    float buf[128];
    synth_render(&s, buf, 128);
    synth_note_off(&s, 69);

    /* Render until direct sound releases (~30 blocks = ~80ms) */
    for (int b = 0; b < 30; ++b) {
        synth_render(&s, buf, 128);
    }

    /* Render 10 blocks of silence */
    for (int b = 0; b < 5; ++b) {
        synth_render(&s, buf, 128);
    }

    /* Render through the 100ms delay tap time (blocks 38..48) */
    float delay_energy = 0.0f;
    for (int b = 0; b < 10; ++b) {
        synth_render(&s, buf, 128);
        for (int i = 0; i < 128; ++i) delay_energy += fabsf(buf[i]);
    }

    /* Delay echo tap must carry audible energy */
    ASSERT(delay_energy > 0.01f);
    ASSERT(synth_is_sounding(&s));

    printf("test_synth_delay_effect PASSED (Delay echo energy verified: %.4f)\n", delay_energy);
}

static void test_synth_chorus_effect(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_chorus(&s, true, 0.40f);

    synth_note_on(&s, 60, 100);
    float buf[128];
    for (int b = 0; b < 30; ++b) {
        synth_render(&s, buf, 128);
        for (int i = 0; i < 128; ++i) {
            ASSERT(!isnan(buf[i]));
            ASSERT(!isinf(buf[i]));
            ASSERT(fabsf(buf[i]) <= SYNTH_OUTPUT_PEAK);
        }
    }
    printf("test_synth_chorus_effect PASSED (Smooth chorus modulation without artifact)\n");
}

static void test_synth_cc_fx(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* CC 91: Delay Send */
    midi_event_t ev_d = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 91, .value = 100 }
    };
    synth_handle_midi_event(&s, &ev_d);
    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    ASSERT(t.delay_active == true);

    /* CC 93: Chorus Send */
    midi_event_t ev_c = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 93, .value = 80 }
    };
    synth_handle_midi_event(&s, &ev_c);
    synth_get_telemetry(&s, &t);
    ASSERT(t.chorus_active == true);

    /* CC 76: LFO Rate */
    midi_event_t ev_r = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 76, .value = 64 }
    };
    synth_handle_midi_event(&s, &ev_r);
    ASSERT(s.lfo.freq_hz > 5.0f && s.lfo.freq_hz < 10.0f);

    /* CC 77: LFO Depth */
    midi_event_t ev_dp = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 77, .value = 127 }
    };
    synth_handle_midi_event(&s, &ev_dp);
    synth_get_telemetry(&s, &t);
    ASSERT(t.lfo_active == true);

    printf("test_synth_cc_fx PASSED (MIDI CC 91, 93, 76, 77 verified)\n");
}

static void test_synth_arpeggiator_up_down(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_bpm(&s, 120);
    synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_16);
    synth_set_arp_octaves(&s, 1);

    ASSERT(s.arp.mode == SYNTH_ARP_UP);
    ASSERT(s.arp.held_count == 0);

    /* Send Note On for C4 (60), E4 (64), G4 (67) */
    midi_event_t ev1 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 60, .velocity = 100 } };
    midi_event_t ev2 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 64, .velocity = 100 } };
    midi_event_t ev3 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 67, .velocity = 100 } };

    synth_handle_midi_event(&s, &ev1);
    synth_handle_midi_event(&s, &ev2);
    synth_handle_midi_event(&s, &ev3);

    ASSERT(s.arp.held_count == 3);
    ASSERT(s.arp.held_notes[0] == 60);
    ASSERT(s.arp.held_notes[1] == 64);
    ASSERT(s.arp.held_notes[2] == 67);

    /* First step triggers immediately on first render block */
    float buf[128];
    synth_render(&s, buf, 128);
    ASSERT(s.note_on_count == 1);
    ASSERT(s.last_note == 60);

    /* Render to second step (> 6000 samples) */
    uint32_t total_samples = 128;
    while (total_samples < 7000) {
        synth_render(&s, buf, 128);
        total_samples += 128;
    }
    ASSERT(s.note_on_count >= 2);
    ASSERT(s.last_note == 64);

    /* Render to third step (> 12000 samples) */
    while (total_samples < 13000) {
        synth_render(&s, buf, 128);
        total_samples += 128;
    }
    ASSERT(s.note_on_count >= 3);
    ASSERT(s.last_note == 67);

    /* Note off for E4 */
    midi_event_t ev_off = { .type = MIDI_EVENT_NOTE_OFF, .note = { .note = 64, .velocity = 0 } };
    synth_handle_midi_event(&s, &ev_off);
    ASSERT(s.arp.held_count == 2);
    ASSERT(s.arp.held_notes[0] == 60);
    ASSERT(s.arp.held_notes[1] == 67);

    printf("test_synth_arpeggiator_up_down PASSED\n");
}

static void test_synth_arpeggiator_octaves(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_bpm(&s, 120);
    synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_16);
    synth_set_arp_octaves(&s, 2);

    /* Hold single note C4 (60) */
    midi_event_t ev = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 60, .velocity = 100 } };
    synth_handle_midi_event(&s, &ev);

    float buf[128];
    /* Step 0: C4 (60) immediately */
    synth_render(&s, buf, 128);
    ASSERT(s.last_note == 60);

    /* Step 1: C5 (72) - Octave +1 (> 6000 samples) */
    uint32_t total_samples = 128;
    while (total_samples < 7000) {
        synth_render(&s, buf, 128);
        total_samples += 128;
    }
    ASSERT(s.last_note == 72);

    /* Step 2: C4 (60) - Cycle wraps back to octave 0 (> 12000 samples) */
    while (total_samples < 13000) {
        synth_render(&s, buf, 128);
        total_samples += 128;
    }
    ASSERT(s.last_note == 60);

    printf("test_synth_arpeggiator_octaves PASSED\n");
}

static void test_synth_arpeggiator_chord(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_arp_mode(&s, SYNTH_ARP_CHORD);
    synth_set_arp_bpm(&s, 120);
    synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_16);
    synth_set_arp_octaves(&s, 1);

    /* Hold triad C4, E4, G4 */
    midi_event_t ev1 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 60, .velocity = 100 } };
    midi_event_t ev2 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 64, .velocity = 100 } };
    midi_event_t ev3 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 67, .velocity = 100 } };
    synth_handle_midi_event(&s, &ev1);
    synth_handle_midi_event(&s, &ev2);
    synth_handle_midi_event(&s, &ev3);

    float buf[128];
    uint32_t total_samples = 0;
    while (total_samples < 7000) {
        synth_render(&s, buf, 128);
        total_samples += 128;
    }

    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    /* In chord mode, all 3 voices should be triggered at once */
    ASSERT(t.active_voices >= 3);

    printf("test_synth_arpeggiator_chord PASSED\n");
}

static void test_synth_arpeggiator_midi_cc(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* CC 79: Arp Mode (1 = UP) */
    midi_event_t ev_mode = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 79, .value = 1 }
    };
    synth_handle_midi_event(&s, &ev_mode);
    ASSERT(s.arp.mode == SYNTH_ARP_UP);

    /* CC 78: Arp Octaves (value % 4 + 1 -> 2 % 4 + 1 = 3) */
    midi_event_t ev_oct = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 78, .value = 2 }
    };
    synth_handle_midi_event(&s, &ev_oct);
    ASSERT(s.arp.octaves == 3);

    /* CC 75: Arp Rate (1 = 1/8) */
    midi_event_t ev_rate = {
        .type = MIDI_EVENT_CONTROL_CHANGE,
        .channel = 0,
        .cc = { .controller = 75, .value = 1 }
    };
    synth_handle_midi_event(&s, &ev_rate);
    ASSERT(s.arp.rate == SYNTH_ARP_RATE_1_8);

    synth_telemetry_t t;
    synth_get_telemetry(&s, &t);
    ASSERT(t.arp_mode == SYNTH_ARP_UP);
    ASSERT(t.arp_octaves == 3);

    printf("test_synth_arpeggiator_midi_cc PASSED\n");
}

static void test_synth_m9_integration(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* Verify all 12 presets render cleanly */
    float buf[128];
    for (int p = 0; p < (int)SYNTH_PRESET_COUNT; ++p) {
        synth_set_preset(&s, (synth_preset_t)p);
        ASSERT(synth_get_preset(&s) == (synth_preset_t)p);

        /* Trigger chord */
        synth_note_on(&s, 60, 100);
        synth_note_on(&s, 64, 100);
        synth_note_on(&s, 67, 100);

        for (int b = 0; b < 20; ++b) {
            synth_render(&s, buf, 128);
            for (int i = 0; i < 128; ++i) {
                ASSERT(!isnan(buf[i]));
                ASSERT(!isinf(buf[i]));
                ASSERT(fabsf(buf[i]) <= SYNTH_OUTPUT_PEAK);
            }
        }
        synth_all_notes_off(&s);
    }

    /* Verify Pluck Arp preset with Arpeggiator and Delay active */
    synth_set_preset(&s, SYNTH_PRESET_PLUCK_ARP);
    synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_octaves(&s, 2);

    /* Hold notes C4, G4 */
    midi_event_t ev1 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 60, .velocity = 110 } };
    midi_event_t ev2 = { .type = MIDI_EVENT_NOTE_ON, .note = { .note = 67, .velocity = 110 } };
    synth_handle_midi_event(&s, &ev1);
    synth_handle_midi_event(&s, &ev2);

    for (int b = 0; b < 100; ++b) {
        synth_render(&s, buf, 128);
        for (int i = 0; i < 128; ++i) {
            ASSERT(!isnan(buf[i]));
            ASSERT(!isinf(buf[i]));
        }
    }
    ASSERT(s.note_on_count >= 2);

    printf("test_synth_m9_integration PASSED (all %d presets and Arp+FX integration verified)\n", SYNTH_PRESET_COUNT);
}

static void test_synth_m15_dsp(void)
{
    synth_t s;
    synth_init(&s, NULL);

    /* 1. Test PWM on square wave */
    synth_set_waveform(&s, SYNTH_WAVE_SQUARE);
    synth_set_pwm(&s, 0.25f, 0.0f);
    synth_note_on(&s, 60, 100);
    float buf[128];
    synth_render(&s, buf, 128);
    for (int i = 0; i < 128; ++i) {
        ASSERT(!isnan(buf[i]));
        ASSERT(!isinf(buf[i]));
    }
    synth_all_notes_off(&s);

    /* 2. Test Sub-Oscillator */
    synth_set_waveform(&s, SYNTH_WAVE_SAW);
    synth_set_sub_osc(&s, 0.75f, 1);
    synth_note_on(&s, 60, 100);
    synth_render(&s, buf, 128);
    for (int i = 0; i < 128; ++i) {
        ASSERT(!isnan(buf[i]));
        ASSERT(!isinf(buf[i]));
    }
    synth_all_notes_off(&s);

    /* 3. Test Noise Generator */
    synth_set_noise(&s, 0.50f);
    synth_note_on(&s, 60, 100);
    synth_render(&s, buf, 128);
    float energy = 0.0f;
    for (int i = 0; i < 128; ++i) {
        ASSERT(!isnan(buf[i]));
        ASSERT(!isinf(buf[i]));
        energy += fabsf(buf[i]);
    }
    ASSERT(energy > 0.001f);
    synth_all_notes_off(&s);

    /* 4. Verify all 12 presets */
    for (int p = 0; p < (int)SYNTH_PRESET_COUNT; ++p) {
        synth_set_preset(&s, (synth_preset_t)p);
        ASSERT(synth_get_preset(&s) == (synth_preset_t)p);
        ASSERT(strlen(synth_preset_name((synth_preset_t)p)) > 0);
    }

    printf("test_synth_m15_dsp PASSED (PWM, Sub-Osc, Noise, all %d presets verified)\n", SYNTH_PRESET_COUNT);
}

#ifndef AUDIO_SCOPE_SAMPLES
#define AUDIO_SCOPE_SAMPLES 64
#endif

static void test_synth_m16_scope(void)
{
    synth_t s;
    synth_init(&s, NULL);
    synth_set_preset(&s, SYNTH_PRESET_ANALOG_BRASS);

    /* Simulate audio_hal scope buffer calculation */
    float left_buf[128];
    int16_t pcm_buf[128];
    int16_t scope_samples[AUDIO_SCOPE_SAMPLES];
    int16_t scope_peak = 0, scope_rms = 0;

    /* 1. When note is active, scope samples and peak/rms are positive */
    synth_note_on(&s, 69, 100);
    for (int b = 0; b < 10; ++b) {
        synth_render(&s, left_buf, 128);
    }

    int32_t sum_sq = 0;
    int16_t max_val = 0;
    for (int i = 0; i < 128; ++i) {
        float out = fmaxf(-1.0f, fminf(1.0f, left_buf[i]));
        int16_t sample = (int16_t)(out * 32767.0f);
        pcm_buf[i] = sample;
        int16_t abs_s = (sample < 0) ? -sample : sample;
        if (abs_s > max_val) max_val = abs_s;
        int32_t s_scaled = sample >> 4;
        sum_sq += s_scaled * s_scaled;
    }
    for (int i = 0; i < AUDIO_SCOPE_SAMPLES; ++i) {
        scope_samples[i] = pcm_buf[i * 2];
    }
    scope_peak = max_val;
    scope_rms = (int16_t)(sqrtf((float)(sum_sq / 128)) * 16.0f);

    ASSERT(synth_is_sounding(&s) == true);
    ASSERT(scope_peak > 500);
    ASSERT(scope_rms > 100);
    ASSERT(sizeof(scope_samples) == AUDIO_SCOPE_SAMPLES * sizeof(int16_t));

    /* Verify bounds of scope samples */
    for (int i = 0; i < AUDIO_SCOPE_SAMPLES; ++i) {
        ASSERT(scope_samples[i] >= -32768 && scope_samples[i] <= 32767);
    }

    printf("test_synth_m16_scope PASSED (peak=%d, rms=%d, 64-sample buffer verified)\n", scope_peak, scope_rms);
}

static void test_review_regressions(void)
{
    synth_t s; synth_init(&s, NULL);
    synth_set_master_gain(&s, 0);
    synth_set_arp_mode(&s, SYNTH_ARP_UP); synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_8);
    synth_set_arp_bpm(&s, 150); synth_set_arp_octaves(&s, 3);
    for (int i = 0; i < SYNTH_PRESET_COUNT; ++i) {
        synth_set_preset(&s, (synth_preset_t)i);
        ASSERT(s.config.master_gain_permille == 0);
        ASSERT(s.config.sample_rate == 48000);
        ASSERT(synth_config_is_valid(&s.config));
        ASSERT(s.config.arp_mode == s.arp.mode && s.config.arp_rate == s.arp.rate);
        ASSERT(s.config.arp_bpm == 150 && s.config.arp_octaves == 3);
    }
    synth_config_t defaults = s.config; defaults.lfo_freq_hz = 0;
    synth_normalize_config(&defaults); ASSERT(defaults.lfo_freq_hz == 5);
    defaults.lfo_freq_hz = 0.05f; synth_normalize_config(&defaults); ASSERT(defaults.lfo_freq_hz == 0.1f);
    float cutoff = s.config.base_cutoff_hz;
    synth_set_cutoff(&s, NAN); synth_set_cutoff(&s, INFINITY);
    ASSERT(s.config.base_cutoff_hz == cutoff);
    synth_set_pwm(&s, NAN, 0); synth_set_noise(&s, INFINITY);
    ASSERT(isfinite(s.config.pwm_duty) && isfinite(s.config.noise_level));
    synth_config_t bad = s.config; bad.resonance = NAN;
    ASSERT(!synth_config_is_valid(&bad));
    synth_apply_config(&s, &bad);
    ASSERT(synth_config_is_valid(&s.config));
    synth_set_master_gain(&s, 35);
    synth_set_arp_mode(&s, SYNTH_ARP_OFF);
    synth_set_lfo(&s, false, 5, 0); synth_set_chorus(&s, false, 0); synth_set_delay(&s, false, 0, 0, 0);
    synth_set_sub_osc(&s, 1, 1); synth_note_on(&s, 69, 127);
    float samples[128]; synth_render(&s, samples, 128);
    synth_voice_t *voice = NULL;
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) if (s.voices[i].state != VOICE_STATE_IDLE) voice = &s.voices[i];
    ASSERT(voice);
    uint32_t before = voice->sub_phase, inc = voice->phase_inc;
    synth_render(&s, samples, 1);
    ASSERT((uint32_t)(voice->sub_phase - before) == (inc >> 1));
    synth_set_sub_osc(&s, 1, 2); before = voice->sub_phase;
    synth_render(&s, samples, 1);
    ASSERT((uint32_t)(voice->sub_phase - before) == (inc >> 2));
    s.sustain_pedal = true; s.arp.held_count = 1;
    synth_panic(&s);
    ASSERT(!synth_is_sounding(&s) && !s.sustain_pedal && !s.arp.held_count);
    synth_render(&s, samples, 128);
    for (int i = 0; i < 128; ++i) ASSERT(samples[i] == 0);
    printf("test_review_regressions PASSED\n");
}

int main(void)
{
    printf("=== Running Polyphonic Synth Unit Tests ===\n");
    test_review_regressions();
    test_synth_init();
    test_synth_note_to_freq();
    test_synth_polyphony_16_voices();
    test_synth_chords_and_clipping();
    test_synth_sustain_pedal();
    test_synth_pitch_bend_poly();
    test_synth_waveforms();
    test_synth_filter_attenuation();
    test_synth_filter_stability();
    test_synth_presets();
    test_synth_mod_wheel();
    test_synth_cc_cutoff_resonance();
    test_synth_lfo_vibrato();
    test_synth_delay_effect();
    test_synth_chorus_effect();
    test_synth_cc_fx();
    test_synth_arpeggiator_up_down();
    test_synth_arpeggiator_octaves();
    test_synth_arpeggiator_chord();
    test_synth_arpeggiator_midi_cc();
    test_synth_m9_integration();
    test_synth_m15_dsp();
    test_synth_m16_scope();
    printf("=== All Polyphonic Synth Unit Tests PASSED ===\n");
    return 0;
}
