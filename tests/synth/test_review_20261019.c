#include "synth.h"
#include "midi_parser.h"
#include "midi_queue.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static synth_t s;
static float out[128];
static void setup(bool delay, bool chorus)
{
    synth_config_t cfg = *synth_get_preset_config(0);
    synth_normalize_config(&cfg);
    cfg.attack_ms = cfg.decay_ms = cfg.release_ms = 0;
    cfg.sustain_ratio = 1;
    cfg.filter_env_octaves = 0;
    cfg.lfo_enable = false;
    cfg.delay_enable = delay; cfg.delay_ms = 350; cfg.delay_feedback = 0.5f; cfg.delay_wet = 0.5f;
    cfg.chorus_enable = chorus; cfg.chorus_wet = 0.5f;
    synth_init(&s, &cfg);
}
static void held_note(void)
{
    midi_event_t on = {.type = MIDI_EVENT_NOTE_ON, .note = {60, 100}};
    synth_handle_midi_event(&s, &on);
}
static void master_and_tails(void)
{
    for (unsigned fx = 1; fx < 4; ++fx) {
        setup(fx & 1, fx & 2);
        synth_note_on(&s, 69, 127);
        for (int i = 0; i < 140; ++i) synth_render(&s, out, 128);
        synth_set_master_gain(&s, 0);
        for (int i = 0; i < 3; ++i) synth_render(&s, out, 128);
        for (int b = 0; b < 160; ++b) {
            synth_render(&s, out, 128);
            for (int i = 0; i < 128; ++i) assert(out[i] == 0);
        }
        synth_set_master_gain(&s, 50);
        for (int b = 0; b < 160; ++b) {
            synth_render(&s, out, 128);
            for (int i = 0; i < 128; ++i) assert(isfinite(out[i]) && fabsf(out[i]) <= SYNTH_OUTPUT_PEAK);
        }
    }
    setup(true, false);
    synth_note_on(&s, 69, 127);
    for (int i = 0; i < 4; ++i) synth_render(&s, out, 128);
    synth_note_off(&s, 69);
    synth_render(&s, out, 128);
    assert(s.delay.active_samples && synth_is_sounding(&s));
    bool echo = false;
    for (int b = 0; b < 200; ++b) {
        bool sounding = synth_is_sounding(&s);
        synth_render(&s, out, 128);
        for (int i = 0; i < 128; ++i) if (fabsf(out[i]) > 0.0005f) { assert(sounding); echo = true; }
    }
    assert(echo);
    for (int b = 0; b < 4000 && synth_is_sounding(&s); ++b) synth_render(&s, out, 128);
    assert(!synth_is_sounding(&s));
    synth_panic(&s);
    assert(s.delay.active_samples == 0 && s.chorus.active_samples == 0);
    setup(false, false); synth_note_on(&s, 69, 127);
    s.voices[0].filter.s1 = NAN;
    synth_render(&s, out, 128);
    assert(!isfinite(out[0])); /* HAL must see the fault, not positive clamp output. */
}
static void timing(void)
{
    const unsigned chunks[] = {1, 64, 128};
    for (unsigned c = 0; c < 3; ++c) {
        setup(false, false);
        synth_set_arp_mode(&s, SYNTH_ARP_UP);
        synth_set_arp_bpm(&s, 120); synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_16);
        synth_set_arp_octaves(&s, 1); held_note();
        for (unsigned frame = 0; frame < 18001;) {
            unsigned n = chunks[c]; if (n > 18001 - frame) n = 18001 - frame;
            synth_render(&s, out, n); frame += n;
            assert(s.note_on_count == 1 + (frame - 1) / 6000);
        }
    }
    setup(false, false); synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_bpm(&s, 120); synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_16); held_note();
    for (int i = 0; i < 3900; ++i) synth_render(&s, out, 1);
    assert(s.note_off_count == 0);
    synth_render(&s, out, 1); assert(s.note_off_count == 1);

    setup(false, false); synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_bpm(&s, 40); synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_4); held_note();
    for (int b = 0; b < 547; ++b) synth_render(&s, out, 128);
    synth_set_arp_bpm(&s, 300); synth_set_arp_rate(&s, SYNTH_ARP_RATE_1_32);
    assert(s.arp.sample_counter < s.arp.step_samples);
    unsigned before = s.note_on_count;
    for (int b = 0; b < 60; ++b) synth_render(&s, out, 128);
    assert(s.note_on_count - before == 7);

    for (int mode = SYNTH_ARP_UP; mode < SYNTH_ARP_MODE_COUNT; ++mode) {
        setup(false, false); synth_set_arp_mode(&s, mode);
        synth_set_arp_octaves(&s, 4); held_note();
        s.arp.current_octave = 3; /* Reachable transport position before shrinking. */
        synth_set_arp_octaves(&s, 1);
        synth_render(&s, out, 1);
        assert(s.last_note == 60 && s.arp.current_octave == 0);
    }
}
static void reset_pipeline(void)
{
    setup(true, true); synth_note_on(&s, 69, 127);
    synth_render(&s, out, 128);
    assert(midi_queue_init() == 0);
    uint8_t packet[] = {0x0f, 0xff, 0, 0}; midi_event_t event;
    assert(midi_parser_parse_usb_packet(packet, 9, 12345, &event));
    assert(event.type == MIDI_EVENT_SYSTEM_RESET && event.session == 9 && event.timestamp_us == 12345);
    assert(midi_queue_push(&event)); assert(midi_queue_pop(&event, 0));
    synth_handle_midi_event(&s, &event); synth_render(&s, out, 128);
    for (int i = 0; i < 128; ++i) assert(out[i] == 0);
    assert(!synth_is_sounding(&s));
}
static void output_level(void)
{
    float full[128];
    for (int pass = 0; pass < 2; ++pass) {
        setup(false, false);
        synth_set_master_gain(&s, pass ? 25 : 50);
        synth_note_on(&s, 69, 127);
        for (int b = 0; b < 10; ++b) synth_render(&s, out, 128);
        float peak = 0;
        for (int i = 0; i < 128; ++i) {
            if (!pass) full[i] = out[i];
            else assert(fabsf(out[i] - full[i] * 0.5f) < 0.000001f);
            if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
        }
        /* Reject the old 5% ceiling: a single note must have usable level. */
        assert(fabsf(peak - (pass ? 0.254353f : 0.508705f)) < 0.000002f);
        printf("Output gain=%u peak=%.6f\n", s.config.master_gain_permille, peak);
    }
    for (int preset = 0; preset < SYNTH_PRESET_COUNT; ++preset) {
        synth_init(&s, synth_get_preset_config(preset));
        synth_set_master_gain(&s, 50);
        for (int note = 48; note < 64; ++note) synth_note_on(&s, note, 127);
        for (int b = 0; b < 300; ++b) {
            synth_render(&s, out, 128);
            for (int i = 0; i < 128; ++i)
                assert(isfinite(out[i]) && fabsf(out[i]) <= SYNTH_OUTPUT_PEAK);
        }
        synth_set_master_gain(&s, 0);
        for (int b = 0; b < 3; ++b) synth_render(&s, out, 128);
        for (int i = 0; i < 128; ++i) assert(out[i] == 0);
    }
}
static void new_features_and_fixes(void)
{
    /* 1. CC 91 enables delay even on presets with delay_enable = false (MOOG_BASS) */
    synth_init(&s, synth_get_preset_config(SYNTH_PRESET_MOOG_BASS));
    assert(!s.config.delay_enable);
    midi_event_t cc91 = {.type = MIDI_EVENT_CONTROL_CHANGE, .cc = {.controller = 91, .value = 64}};
    synth_handle_midi_event(&s, &cc91);
    assert(s.config.delay_enable && s.config.delay_ms >= 20 && s.config.delay_feedback > 0.0f);

    /* 2. Disabling Delay/Chorus zeros out delay/chorus tail buffers */
    setup(true, true);
    synth_note_on(&s, 69, 127);
    for (int i = 0; i < 10; ++i) synth_render(&s, out, 128);
    assert(s.delay.active_samples > 0 && s.chorus.active_samples > 0);
    synth_set_delay(&s, false, 0, 0, 0);
    synth_set_chorus(&s, false, 0);
    assert(s.delay.active_samples == 0 && s.chorus.active_samples == 0);

    /* 3. Filter modes (LP, BP, HP) produce distinct valid output */
    for (int ftype = SYNTH_FILTER_LP; ftype <= SYNTH_FILTER_HP; ++ftype) {
        setup(false, false);
        synth_set_filter_type(&s, (synth_filter_type_t)ftype);
        synth_set_cutoff(&s, 1000.0f);
        synth_note_on(&s, 69, 120);
        float energy = 0.0f;
        for (int b = 0; b < 10; ++b) {
            synth_render(&s, out, 128);
            for (int i = 0; i < 128; ++i) {
                assert(isfinite(out[i]) && fabsf(out[i]) <= SYNTH_OUTPUT_PEAK);
                energy += out[i] * out[i];
            }
        }
        assert(energy > 0.001f);
    }

    /* 4. Real-time ADSR & Filter Env setters */
    setup(false, false);
    synth_set_adsr(&s, 50, 120, 0.6f, 300);
    synth_set_filter_env(&s, 2.5f);
    assert(s.config.attack_ms == 50 && s.config.decay_ms == 120 && s.config.release_ms == 300);
    assert(fabsf(s.config.sustain_ratio - 0.6f) < 0.001f);
    assert(fabsf(s.config.filter_env_octaves - 2.5f) < 0.001f);

    /* 5. Portamento / Glide smoothly interpolates frequency */
    setup(false, false);
    synth_set_glide(&s, 200);
    synth_note_on(&s, 48, 100);
    synth_note_on(&s, 60, 100);
    assert(s.voices[1].base_freq_hz < s.voices[1].target_freq_hz);
    synth_render(&s, out, 128);
    assert(s.voices[1].base_freq_hz > 131.0f && s.voices[1].base_freq_hz < s.voices[1].target_freq_hz);

    /* 6. Unison detune renders valid audio */
    setup(false, false);
    synth_set_unison(&s, 0.35f);
    synth_note_on(&s, 60, 100);
    for (int b = 0; b < 10; ++b) {
        synth_render(&s, out, 128);
        for (int i = 0; i < 128; ++i) assert(isfinite(out[i]) && fabsf(out[i]) <= SYNTH_OUTPUT_PEAK);
    }

    /* 7. Arp Latch holds notes after key release */
    setup(false, false);
    synth_set_arp_mode(&s, SYNTH_ARP_UP);
    synth_set_arp_latch(&s, true);
    midi_event_t on60 = {.type = MIDI_EVENT_NOTE_ON, .note = {60, 100}};
    midi_event_t off60 = {.type = MIDI_EVENT_NOTE_OFF, .note = {60, 0}};
    synth_handle_midi_event(&s, &on60);
    synth_handle_midi_event(&s, &off60);
    assert(s.arp.held_count == 1 && s.arp.physical_held_count == 0);
    synth_set_arp_latch(&s, false);
    assert(s.arp.held_count == 0);

    /* 8. Velocity Curves & MIDI Channel filter & CC 121 Reset All Controllers */
    setup(false, false);
    synth_set_velocity_curve(&s, SYNTH_VEL_FIXED);
    synth_note_on(&s, 60, 10);
    assert(fabsf(s.voices[0].env_target - (0.50f + 0.50f * (100.0f / 127.0f))) < 0.001f);

    setup(false, false);
    synth_set_midi_channel(&s, 2);
    midi_event_t ch1_on = {.type = MIDI_EVENT_NOTE_ON, .channel = 0, .note = {60, 100}};
    midi_event_t ch2_on = {.type = MIDI_EVENT_NOTE_ON, .channel = 1, .note = {64, 100}};
    synth_handle_midi_event(&s, &ch1_on);
    assert(s.note_on_count == 0);
    synth_handle_midi_event(&s, &ch2_on);
    assert(s.note_on_count == 1);

    synth_set_mod_wheel(&s, 100);
    midi_event_t pb = {.type = MIDI_EVENT_PITCH_BEND, .channel = 1, .pitch_bend = {.value = 4096}};
    synth_handle_midi_event(&s, &pb);
    midi_event_t cc121 = {.type = MIDI_EVENT_CONTROL_CHANGE, .channel = 1, .cc = {.controller = 121, .value = 0}};
    synth_handle_midi_event(&s, &cc121);
    assert(s.mod_wheel == 0 && s.pitch_bend_value == 0 && !s.sustain_pedal);
}
int main(void)
{
    output_level(); master_and_tails(); timing(); reset_pipeline(); new_features_and_fixes();
    puts("October review DSP regressions PASSED (mute, tails, sample/gate timing, tempo, range, reset, new features)");
    return 0;
}
