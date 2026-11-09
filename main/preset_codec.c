#include "preset_codec.h"

#include <string.h>

/* Version 1 stores IEEE-754 binary32 and integers as little-endian words. */
#define CONFIG_FIELDS(I, F) \
    I(sample_rate) I(waveform) I(master_gain_permille) I(attack_ms) I(decay_ms) \
    F(sustain_ratio) I(release_ms) I(pitch_bend_range) F(base_cutoff_hz) F(resonance) \
    F(filter_env_octaves) F(mod_wheel_octaves) I(lfo_enable) F(lfo_freq_hz) \
    F(lfo_vibrato_depth) I(chorus_enable) F(chorus_wet) I(delay_enable) I(delay_ms) \
    F(delay_feedback) F(delay_wet) I(arp_mode) I(arp_rate) I(arp_octaves) I(arp_bpm) \
    F(pwm_duty) F(pwm_lfo_depth) F(sub_osc_level) I(sub_osc_octaves) F(noise_level) \
    I(filter_type) I(velocity_curve) I(midi_channel) I(glide_ms) F(unison_detune) I(arp_latch)

#define COUNT(field) + 1
_Static_assert(PRESET_BYTES == 4 + 4 * (5 CONFIG_FIELDS(COUNT, COUNT)) + BANK_SHA_BYTES,
               "Preset format size mismatch");
#undef COUNT
_Static_assert(sizeof(float) == 4, "Preset format requires binary32");

static uint8_t *put_word(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (8 * i);
    return p + 4;
}

static uint32_t get_word(const uint8_t **p)
{
    const uint8_t *b = *p;
    *p += 4;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

bool preset_encode(const startup_preset_t *preset, uint8_t out[PRESET_BYTES])
{
    const audio_restore_t *r = &preset->restore;
    const sound_ref_t *s = &r->sound;
    if (s->sound_missing || s->synth_preset >= SYNTH_PRESET_COUNT ||
        (s->engine != SOUND_SYNTH && s->engine != SOUND_SF2) ||
        (s->engine == SOUND_SYNTH && (s->bank || s->program)) ||
        s->bank > 16383 || s->program > 127 ||
        (r->bars != 1 && r->bars != 2 && r->bars != 4 && r->bars != 8) ||
        !synth_config_is_valid(&r->config)) return false;

    memcpy(out, "NSP1", 4);
    uint8_t *p = put_word(out + 4, s->engine);
    p = put_word(p, s->synth_preset);
    p = put_word(p, s->bank);
    p = put_word(p, s->program);
    p = put_word(p, r->bars);
    const synth_config_t *c = &r->config;
#define PUT_I(field) p = put_word(p, c->field);
#define PUT_F(field) do { uint32_t bits; memcpy(&bits, &c->field, 4); p = put_word(p, bits); } while (0);
    CONFIG_FIELDS(PUT_I, PUT_F)
#undef PUT_I
#undef PUT_F
    if (s->engine == SOUND_SF2) memcpy(p, preset->bank_sha, BANK_SHA_BYTES);
    else memset(p, 0, BANK_SHA_BYTES);
    return true;
}

bool preset_decode(const uint8_t *data, size_t size, startup_preset_t *preset)
{
    if (size != PRESET_BYTES || memcmp(data, "NSP1", 4)) return false;
    startup_preset_t decoded = {.restore.restore_config = true};
    const uint8_t *p = data + 4;
    decoded.restore.sound.engine = get_word(&p);
    decoded.restore.sound.synth_preset = get_word(&p);
    decoded.restore.sound.bank = get_word(&p);
    decoded.restore.sound.program = get_word(&p);
    decoded.restore.bars = get_word(&p);
    synth_config_t *c = &decoded.restore.config;
#define GET_I(field) c->field = get_word(&p);
#define GET_F(field) do { uint32_t bits = get_word(&p); memcpy(&c->field, &bits, 4); } while (0);
    CONFIG_FIELDS(GET_I, GET_F)
#undef GET_I
#undef GET_F
    memcpy(decoded.bank_sha, p, BANK_SHA_BYTES);

    /* Reject invalid fields and noncanonical integer narrowing/boolean values. */
    uint8_t canonical[PRESET_BYTES];
    if (!preset_encode(&decoded, canonical) || memcmp(canonical, data, sizeof(canonical))) return false;
    *preset = decoded;
    return true;
}
