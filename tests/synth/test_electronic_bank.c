#include "synth.h"
#include "synth_looper.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static synth_t synth;
static synth_looper_t loop;

int main(void)
{
    /* Stored IDs and existing user mappings must retain their meaning. */
    assert(SYNTH_PRESET_ANALOG_BRASS == 0);
    assert(SYNTH_PRESET_SCI_FI_SWEEP == 11);
    assert(SYNTH_PRESET_ACID_BASS == 12);
    assert(SYNTH_PRESET_SPACE_DRONE == 31);
    assert(SYNTH_PRESET_COUNT == 32);
    for (unsigned p = 0; p < SYNTH_PRESET_COUNT; ++p) {
        const synth_config_t *raw = synth_get_preset_config(p);
        if (p >= 12) assert(synth_config_is_valid(raw));
        assert(strcmp(synth_preset_name(p), "Custom") != 0);
        for (unsigned other = 0; other < p; ++other)
            assert(strcmp(synth_preset_name(p), synth_preset_name(other)) != 0);
        synth_init(&synth, NULL);
        synth_set_master_gain(&synth, 25);
        midi_event_t event = {.type = MIDI_EVENT_PROGRAM_CHANGE,
                             .program_change = {.program = p}};
        synth_handle_midi_event(&synth, &event);
        assert(synth_get_preset(&synth) == p);
        assert(synth.config.master_gain_permille == 25);
        assert(synth_config_is_valid(&synth.config));
        looper_init(&loop, 120);
        synth.looper = &loop;
        looper_track_trigger(&loop, 0, &synth);
        synth_note_on(&synth, 60, 100);
        assert(loop.tracks[0].preset == p);
        float energy = 0;
        for (unsigned b = 0; b < 700; ++b) {
            float frames[128];
            synth_render(&synth, frames, 128);
            for (unsigned i = 0; i < 128; ++i) {
                assert(isfinite(frames[i]));
                assert(fabsf(frames[i]) <= SYNTH_OUTPUT_PEAK);
                energy += frames[i] * frames[i];
            }
        }
        assert(energy > 1e-6f); /* Also catches accidentally zero initializers. */
        synth_panic(&synth);
        synth_next_preset(&synth);
        assert(synth_get_preset(&synth) == (p + 1) % SYNTH_PRESET_COUNT);
    }
    puts("32 electronic presets: stable IDs, valid configs, MIDI, cycle, looper and non-silent bounded audio PASS");
    return 0;
}
