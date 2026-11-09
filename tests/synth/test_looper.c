#include "synth.h"
#include "synth_looper.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static synth_t s;
static synth_looper_t looper;
static float out[128];

int main(void)
{
    printf("=== Running 4-Track Multi-Timbral Looper Unit Tests ===\n");

    synth_init(&s, NULL);
    looper_init(&looper, 120);
    s.looper = &looper;

    assert(!looper.transport_running);
    assert(looper.bars == 2);
    assert(looper.bpm == 120);
    /* 2 bars at 120 BPM (8 beats = 4.0 seconds) = 192000 samples */
    assert(looper.loop_samples == 192000);

    /* 1. Arm Track 0 for recording with Moog Bass */
    synth_set_preset(&s, SYNTH_PRESET_MOOG_BASS);
    looper_track_trigger(&looper, 0, &s);
    assert(looper.tracks[0].state == LOOPER_STATE_REC);
    assert(looper.transport_running);
    assert(synth_is_sounding(&s));

    /* Record a bass note on Track 0 */
    synth_note_on(&s, 36, 110);
    for (int i = 0; i < 20; ++i) synth_render(&s, out, 128);
    synth_note_off(&s, 36);
    assert(looper.tracks[0].event_count == 2);
    assert(looper.tracks[0].preset == SYNTH_PRESET_MOOG_BASS);

    /* Switch Track 0 from REC -> PLAY */
    looper_track_trigger(&looper, 0, &s);
    assert(looper.tracks[0].state == LOOPER_STATE_PLAY);

    /* 2. Switch live synth to Retro 80s and arm Track 1 for recording */
    synth_set_preset(&s, SYNTH_PRESET_RETRO_80S);
    looper_track_trigger(&looper, 1, &s);
    assert(looper.tracks[1].state == LOOPER_STATE_REC);
    assert(looper.tracks[0].state == LOOPER_STATE_PLAY);

    synth_note_on(&s, 60, 100);
    synth_note_on(&s, 64, 100);
    for (int i = 0; i < 10; ++i) synth_render(&s, out, 128);
    synth_note_off(&s, 60);
    synth_note_off(&s, 64);
    assert(looper.tracks[1].event_count == 4);
    assert(looper.tracks[1].preset == SYNTH_PRESET_RETRO_80S);

    /* Wrap around full loop cycle and verify Track 1 auto-transitions REC -> PLAY and Track 0 retriggers */
    synth_all_notes_off(&s);
    for (int i = 0; i < 1500; ++i) {
        synth_render(&s, out, 128);
        for (int j = 0; j < 128; ++j) {
            assert(isfinite(out[j]) && fabsf(out[j]) <= SYNTH_OUTPUT_PEAK);
        }
    }
    assert(looper.tracks[1].state == LOOPER_STATE_PLAY);

    /* 3. Overdub on Track 0 */
    looper_track_trigger(&looper, 0, &s);
    assert(looper.tracks[0].state == LOOPER_STATE_OVERDUB);
    synth_note_on(&s, 48, 100);
    synth_render(&s, out, 128);
    synth_note_off(&s, 48);
    assert(looper.tracks[0].event_count == 4);
    looper_track_trigger(&looper, 0, &s);
    assert(looper.tracks[0].state == LOOPER_STATE_PLAY);

    /* 4. Mute, Volume, Preset change and Tempo scaling */
    looper_track_toggle_mute(&looper, 0);
    assert(looper.tracks[0].muted);
    looper_track_set_volume(&looper, 1, 50);
    assert(looper.tracks[1].volume_pct == 50);
    looper_track_set_preset(&looper, 2, SYNTH_PRESET_PLUCK_ARP);
    assert(looper.tracks[2].preset == SYNTH_PRESET_PLUCK_ARP);

    synth_set_arp_bpm(&s, 240);
    assert(looper.bpm == 240 && looper.loop_samples == 96000);

    looper_set_bars(&looper, 4);
    assert(looper.bars == 4 && looper.loop_samples == 192000);

    /* 5. MIDI CC 80..84 integration */
    midi_event_t cc82 = {.type = MIDI_EVENT_CONTROL_CHANGE, .cc = {.controller = 82, .value = 127}};
    synth_handle_midi_event(&s, &cc82);
    assert(looper.tracks[2].state == LOOPER_STATE_REC);

    midi_event_t cc84 = {.type = MIDI_EVENT_CONTROL_CHANGE, .cc = {.controller = 84, .value = 127}};
    synth_handle_midi_event(&s, &cc84);
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        assert(looper.tracks[t].state == LOOPER_STATE_EMPTY);
        assert(looper.tracks[t].event_count == 0);
    }
    assert(!looper.transport_running);

    printf("4-Track Multi-Timbral Looper unit tests PASSED!\n");
    return 0;
}
