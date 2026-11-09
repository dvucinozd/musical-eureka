#include "synth.h"
#include "synth_looper.h"
#include "synth_voice_engine.h"
#include "synth_profile.h"
#include "synth_block_trace.h"
synth_block_trace_t synth_block_trace;
#ifdef NEKTAR_FINITE_CLAMP
#include "synth_finite_clamp.h"
#endif
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifdef NEKTAR_SAT_IRAM
#include "saturation_iram.h"
#define tanhf nektar_sat_tanhf
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SINE_LUT_SIZE 2048
#define SINE_LUT_MASK (SINE_LUT_SIZE - 1)

static float s_sine_table[SINE_LUT_SIZE];
static bool s_sine_init = false;

static void ensure_sine_table(void)
{
    if (s_sine_init) return;
    for (int i = 0; i < SINE_LUT_SIZE; ++i) {
        s_sine_table[i] = sinf((float)(2.0 * M_PI * (double)i / (double)SINE_LUT_SIZE));
    }
    s_sine_init = true;
}

static inline float lut_sine(float phase)
{
    phase -= floorf(phase);
    float pos = phase * (float)SINE_LUT_SIZE;
    uint32_t idx = ((uint32_t)pos) & SINE_LUT_MASK;
    float frac = pos - floorf(pos);
    float s0 = s_sine_table[idx];
    float s1 = s_sine_table[(idx + 1) & SINE_LUT_MASK];
    return s0 + (s1 - s0) * frac;
}

static const synth_config_t s_presets[SYNTH_PRESET_COUNT] = {
    [SYNTH_PRESET_ANALOG_BRASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 8,
        .decay_ms = 60,
        .sustain_ratio = 0.85f,
        .release_ms = 90,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1400.0f,
        .resonance = 2.0f,
        .filter_env_octaves = 2.0f,
        .mod_wheel_octaves = 3.5f,
        .lfo_enable = true,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.5f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 220,
        .delay_feedback = 0.35f,
        .delay_wet = 0.20f,
    },
    [SYNTH_PRESET_WARM_PAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_TRIANGLE,
        .master_gain_permille = 35,
        .attack_ms = 40,
        .decay_ms = 120,
        .sustain_ratio = 0.90f,
        .release_ms = 200,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 900.0f,
        .resonance = 1.2f,
        .filter_env_octaves = 1.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.5f,
        .lfo_vibrato_depth = 0.2f,
        .chorus_enable = true,
        .chorus_wet = 0.35f,
        .delay_enable = true,
        .delay_ms = 320,
        .delay_feedback = 0.45f,
        .delay_wet = 0.25f,
    },
    [SYNTH_PRESET_MOOG_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 3,
        .decay_ms = 80,
        .sustain_ratio = 0.60f,
        .release_ms = 60,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 700.0f,
        .resonance = 3.2f,
        .filter_env_octaves = 3.0f,
        .mod_wheel_octaves = 4.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
    },
    [SYNTH_PRESET_BRIGHT_LEAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 4,
        .decay_ms = 50,
        .sustain_ratio = 0.80f,
        .release_ms = 80,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 2500.0f,
        .resonance = 2.5f,
        .filter_env_octaves = 1.5f,
        .mod_wheel_octaves = 2.5f,
        .lfo_enable = true,
        .lfo_freq_hz = 5.5f,
        .lfo_vibrato_depth = 0.6f,
        .chorus_enable = true,
        .chorus_wet = 0.20f,
        .delay_enable = true,
        .delay_ms = 180,
        .delay_feedback = 0.40f,
        .delay_wet = 0.25f,
    },
    [SYNTH_PRESET_RETRO_80S] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 6,
        .decay_ms = 70,
        .sustain_ratio = 0.80f,
        .release_ms = 110,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1800.0f,
        .resonance = 2.8f,
        .filter_env_octaves = 2.5f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 6.0f,
        .lfo_vibrato_depth = 0.4f,
        .chorus_enable = true,
        .chorus_wet = 0.35f,
        .delay_enable = true,
        .delay_ms = 160,
        .delay_feedback = 0.35f,
        .delay_wet = 0.22f,
    },
    [SYNTH_PRESET_PLUCK_ARP] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 2,
        .decay_ms = 45,
        .sustain_ratio = 0.25f,
        .release_ms = 50,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 3200.0f,
        .resonance = 3.5f,
        .filter_env_octaves = 3.8f,
        .mod_wheel_octaves = 2.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.15f,
        .delay_enable = true,
        .delay_ms = 250,
        .delay_feedback = 0.50f,
        .delay_wet = 0.30f,
    },
    [SYNTH_PRESET_CHURCH_ORGAN] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SINE,
        .master_gain_permille = 35,
        .attack_ms = 5,
        .decay_ms = 10,
        .sustain_ratio = 0.98f,
        .release_ms = 25,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 6000.0f,
        .resonance = 0.7f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 1.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 6.2f,
        .lfo_vibrato_depth = 0.3f,
        .chorus_enable = true,
        .chorus_wet = 0.30f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
    },
    [SYNTH_PRESET_SPACE_STRINGS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 80,
        .decay_ms = 180,
        .sustain_ratio = 0.95f,
        .release_ms = 280,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1200.0f,
        .resonance = 1.4f,
        .filter_env_octaves = 1.2f,
        .mod_wheel_octaves = 3.5f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.4f,
        .lfo_vibrato_depth = 0.25f,
        .chorus_enable = true,
        .chorus_wet = 0.40f,
        .delay_enable = true,
        .delay_ms = 350,
        .delay_feedback = 0.58f,
        .delay_wet = 0.35f,
        .pwm_duty = 0.50f,
    },
    [SYNTH_PRESET_SUB_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 4,
        .decay_ms = 110,
        .sustain_ratio = 0.65f,
        .release_ms = 70,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 650.0f,
        .resonance = 2.4f,
        .filter_env_octaves = 2.5f,
        .mod_wheel_octaves = 3.5f,
        .sub_osc_level = 0.75f,
        .sub_osc_octaves = 1,
        .pwm_duty = 0.50f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
    },
    [SYNTH_PRESET_JUNO_STRINGS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 35,
        .decay_ms = 90,
        .sustain_ratio = 0.88f,
        .release_ms = 190,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 2400.0f,
        .resonance = 1.2f,
        .filter_env_octaves = 1.0f,
        .mod_wheel_octaves = 3.0f,
        .pwm_duty = 0.50f,
        .pwm_lfo_depth = 0.32f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.6f,
        .lfo_vibrato_depth = 0.15f,
        .chorus_enable = true,
        .chorus_wet = 0.40f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
    },
    [SYNTH_PRESET_PERC_ORGAN] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SINE,
        .master_gain_permille = 35,
        .attack_ms = 2,
        .decay_ms = 45,
        .sustain_ratio = 0.95f,
        .release_ms = 40,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 5000.0f,
        .resonance = 1.6f,
        .noise_level = 0.22f,
        .pwm_duty = 0.50f,
        .lfo_enable = true,
        .lfo_freq_hz = 6.0f,
        .lfo_vibrato_depth = 0.25f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
    },
    [SYNTH_PRESET_SCI_FI_SWEEP] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 60,
        .decay_ms = 200,
        .sustain_ratio = 0.90f,
        .release_ms = 250,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1400.0f,
        .resonance = 3.8f,
        .filter_env_octaves = 1.8f,
        .mod_wheel_octaves = 4.0f,
        .sub_osc_level = 0.55f,
        .sub_osc_octaves = 2,
        .noise_level = 0.28f,
        .pwm_duty = 0.50f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.3f,
        .lfo_vibrato_depth = 0.3f,
        .delay_enable = true,
        .delay_ms = 350,
        .delay_feedback = 0.55f,
        .delay_wet = 0.35f,
    },
    /* Electronic expansion: append-only IDs 12..31. */
    [SYNTH_PRESET_ACID_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 2,
        .decay_ms = 180,
        .sustain_ratio = 0.18f,
        .release_ms = 65,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 320.0f,
        .resonance = 4.2f,
        .filter_env_octaves = 4.5f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_RUBBER_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 3,
        .decay_ms = 240,
        .sustain_ratio = 0.35f,
        .release_ms = 100,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 420.0f,
        .resonance = 2.8f,
        .filter_env_octaves = 3.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.32f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.3f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_PWM_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 5,
        .decay_ms = 90,
        .sustain_ratio = 0.72f,
        .release_ms = 80,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 900.0f,
        .resonance = 1.5f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.65f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.35f,
        .pwm_lfo_depth = 0.22f,
        .sub_osc_level = 0.25f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_DETUNED_BASS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 4,
        .decay_ms = 110,
        .sustain_ratio = 0.65f,
        .release_ms = 100,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 750.0f,
        .resonance = 1.7f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.35f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.18f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_SOFT_SUB] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SINE,
        .master_gain_permille = 35,
        .attack_ms = 12,
        .decay_ms = 160,
        .sustain_ratio = 0.8f,
        .release_ms = 160,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 450.0f,
        .resonance = 0.7f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.18f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_LASER_LEAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 1,
        .decay_ms = 95,
        .sustain_ratio = 0.25f,
        .release_ms = 110,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 600.0f,
        .resonance = 4.0f,
        .filter_env_octaves = 5.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 140,
        .delay_feedback = 0.35f,
        .delay_wet = 0.2f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_GLIDE_LEAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 6,
        .decay_ms = 70,
        .sustain_ratio = 0.85f,
        .release_ms = 130,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 2200.0f,
        .resonance = 2.0f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 5.2f,
        .lfo_vibrato_depth = 0.2f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.18f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 140,
        .unison_detune = 0.12f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_CHIP_LEAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 1,
        .decay_ms = 35,
        .sustain_ratio = 0.7f,
        .release_ms = 35,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 10000.0f,
        .resonance = 0.7f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 120,
        .delay_feedback = 0.35f,
        .delay_wet = 0.12f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.25f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_HOLLOW_LEAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 8,
        .decay_ms = 130,
        .sustain_ratio = 0.75f,
        .release_ms = 170,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1400.0f,
        .resonance = 2.5f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 4.7f,
        .lfo_vibrato_depth = 0.3f,
        .chorus_enable = true,
        .chorus_wet = 0.15f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.42f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_BP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_WIDE_SAW] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 10,
        .decay_ms = 80,
        .sustain_ratio = 0.85f,
        .release_ms = 200,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 4200.0f,
        .resonance = 1.0f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.3f,
        .delay_enable = true,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.15f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.35f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_VELVET_PAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_TRIANGLE,
        .master_gain_permille = 35,
        .attack_ms = 700,
        .decay_ms = 600,
        .sustain_ratio = 0.8f,
        .release_ms = 1400,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1100.0f,
        .resonance = 1.0f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.3f,
        .lfo_vibrato_depth = 0.15f,
        .chorus_enable = true,
        .chorus_wet = 0.4f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.08f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_DIGITAL_PAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 450,
        .decay_ms = 450,
        .sustain_ratio = 0.65f,
        .release_ms = 1100,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 2600.0f,
        .resonance = 1.4f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.22f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.35f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.22f,
        .pwm_lfo_depth = 0.16f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.22f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_SLOW_MOTION] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 1600,
        .decay_ms = 900,
        .sustain_ratio = 0.85f,
        .release_ms = 2000,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 500.0f,
        .resonance = 2.2f,
        .filter_env_octaves = 3.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.3f,
        .delay_enable = true,
        .delay_ms = 350,
        .delay_feedback = 0.35f,
        .delay_wet = 0.22f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.14f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_RESONANT_PAD] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SAW,
        .master_gain_permille = 35,
        .attack_ms = 350,
        .decay_ms = 600,
        .sustain_ratio = 0.75f,
        .release_ms = 1200,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1600.0f,
        .resonance = 3.5f,
        .filter_env_octaves = 1.2f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.3f,
        .delay_enable = false,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.0f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_BP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.09f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_CRYSTAL_PLUCK] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_TRIANGLE,
        .master_gain_permille = 35,
        .attack_ms = 1,
        .decay_ms = 260,
        .sustain_ratio = 0.0f,
        .release_ms = 180,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 6500.0f,
        .resonance = 1.3f,
        .filter_env_octaves = 1.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 280,
        .delay_feedback = 0.48f,
        .delay_wet = 0.3f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_PULSE_PLUCK] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 2,
        .decay_ms = 140,
        .sustain_ratio = 0.08f,
        .release_ms = 95,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 700.0f,
        .resonance = 2.4f,
        .filter_env_octaves = 4.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 190,
        .delay_feedback = 0.35f,
        .delay_wet = 0.22f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.18f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_SYNTH_KEYS] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_TRIANGLE,
        .master_gain_permille = 35,
        .attack_ms = 4,
        .decay_ms = 600,
        .sustain_ratio = 0.25f,
        .release_ms = 300,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 3800.0f,
        .resonance = 0.8f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = true,
        .chorus_wet = 0.22f,
        .delay_enable = true,
        .delay_ms = 250,
        .delay_feedback = 0.35f,
        .delay_wet = 0.1f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.06f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_SOFT_BELL] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SINE,
        .master_gain_permille = 35,
        .attack_ms = 2,
        .decay_ms = 1200,
        .sustain_ratio = 0.0f,
        .release_ms = 650,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 8000.0f,
        .resonance = 0.6f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 310,
        .delay_feedback = 0.45f,
        .delay_wet = 0.28f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.08f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_NOISE_SWEEP] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_TRIANGLE,
        .master_gain_permille = 35,
        .attack_ms = 650,
        .decay_ms = 900,
        .sustain_ratio = 0.5f,
        .release_ms = 1300,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 1300.0f,
        .resonance = 2.3f,
        .filter_env_octaves = 2.4f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = false,
        .lfo_freq_hz = 5.0f,
        .lfo_vibrato_depth = 0.0f,
        .chorus_enable = false,
        .chorus_wet = 0.0f,
        .delay_enable = true,
        .delay_ms = 330,
        .delay_feedback = 0.35f,
        .delay_wet = 0.22f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.0f,
        .sub_osc_level = 0.0f,
        .sub_osc_octaves = 1,
        .noise_level = 0.7f,
        .filter_type = SYNTH_FILTER_HP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.0f,
        .arp_latch = false,
    },
    [SYNTH_PRESET_SPACE_DRONE] = {
        .sample_rate = SYNTH_SAMPLE_RATE,
        .waveform = SYNTH_WAVE_SQUARE,
        .master_gain_permille = 35,
        .attack_ms = 1000,
        .decay_ms = 1400,
        .sustain_ratio = 0.9f,
        .release_ms = 2400,
        .pitch_bend_range = 2,
        .base_cutoff_hz = 600.0f,
        .resonance = 2.8f,
        .filter_env_octaves = 0.0f,
        .mod_wheel_octaves = 3.0f,
        .lfo_enable = true,
        .lfo_freq_hz = 0.13f,
        .lfo_vibrato_depth = 0.25f,
        .chorus_enable = true,
        .chorus_wet = 0.4f,
        .delay_enable = true,
        .delay_ms = 350,
        .delay_feedback = 0.55f,
        .delay_wet = 0.3f,
        .arp_mode = SYNTH_ARP_OFF,
        .arp_rate = SYNTH_ARP_RATE_1_16,
        .arp_octaves = 2,
        .arp_bpm = 120,
        .pwm_duty = 0.5f,
        .pwm_lfo_depth = 0.3f,
        .sub_osc_level = 0.25f,
        .sub_osc_octaves = 2,
        .noise_level = 0.0f,
        .filter_type = SYNTH_FILTER_LP,
        .velocity_curve = SYNTH_VEL_NORMAL,
        .midi_channel = 0,
        .glide_ms = 0,
        .unison_detune = 0.27f,
        .arp_latch = false,
    },
};

static inline void update_mod_wheel_mult(synth_t *s)
{
    float mod_wheel_oct = ((float)s->mod_wheel / 127.0f) * s->config.mod_wheel_octaves;
    s->mod_wheel_mult = powf(2.0f, mod_wheel_oct);
}

float synth_note_to_freq(uint8_t note)
{
    if (note > 127) note = 127;
    return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

const char *synth_waveform_name(synth_waveform_t wf)
{
    switch (wf) {
    case SYNTH_WAVE_SINE:     return "SINE";
    case SYNTH_WAVE_TRIANGLE: return "TRIANGLE";
    case SYNTH_WAVE_SAW:      return "SAW";
    case SYNTH_WAVE_SQUARE:   return "SQUARE";
    default:                  return "UNKNOWN";
    }
}

const char *synth_filter_type_name(synth_filter_type_t type)
{
    switch (type) {
    case SYNTH_FILTER_LP: return "LP";
    case SYNTH_FILTER_BP: return "BP";
    case SYNTH_FILTER_HP: return "HP";
    default:              return "LP";
    }
}

const char *synth_preset_name(synth_preset_t p)
{
    switch (p) {
    case SYNTH_PRESET_ANALOG_BRASS:  return "Analog Brass";
    case SYNTH_PRESET_WARM_PAD:      return "Warm Pad";
    case SYNTH_PRESET_MOOG_BASS:     return "Moog Bass";
    case SYNTH_PRESET_BRIGHT_LEAD:   return "Bright Lead";
    case SYNTH_PRESET_RETRO_80S:     return "Retro 80s Synth";
    case SYNTH_PRESET_PLUCK_ARP:     return "Pluck Arp";
    case SYNTH_PRESET_CHURCH_ORGAN:  return "Church Organ";
    case SYNTH_PRESET_SPACE_STRINGS: return "Space Strings";
    case SYNTH_PRESET_SUB_BASS:      return "Sub Bass";
    case SYNTH_PRESET_JUNO_STRINGS:  return "Juno Strings";
    case SYNTH_PRESET_PERC_ORGAN:    return "Perc Organ";
    case SYNTH_PRESET_SCI_FI_SWEEP:  return "Sci-Fi Sweep";
    case SYNTH_PRESET_ACID_BASS: return "Acid Bass";
    case SYNTH_PRESET_RUBBER_BASS: return "Rubber Bass";
    case SYNTH_PRESET_PWM_BASS: return "PWM Bass";
    case SYNTH_PRESET_DETUNED_BASS: return "Detuned Bass";
    case SYNTH_PRESET_SOFT_SUB: return "Soft Sub";
    case SYNTH_PRESET_LASER_LEAD: return "Laser Lead";
    case SYNTH_PRESET_GLIDE_LEAD: return "Glide Lead";
    case SYNTH_PRESET_CHIP_LEAD: return "Chip Lead";
    case SYNTH_PRESET_HOLLOW_LEAD: return "Hollow Lead";
    case SYNTH_PRESET_WIDE_SAW: return "Wide Saw";
    case SYNTH_PRESET_VELVET_PAD: return "Velvet Pad";
    case SYNTH_PRESET_DIGITAL_PAD: return "Digital Pad";
    case SYNTH_PRESET_SLOW_MOTION: return "Slow Motion";
    case SYNTH_PRESET_RESONANT_PAD: return "Resonant Pad";
    case SYNTH_PRESET_CRYSTAL_PLUCK: return "Crystal Pluck";
    case SYNTH_PRESET_PULSE_PLUCK: return "Pulse Pluck";
    case SYNTH_PRESET_SYNTH_KEYS: return "Synth Keys";
    case SYNTH_PRESET_SOFT_BELL: return "Soft Bell";
    case SYNTH_PRESET_NOISE_SWEEP: return "Noise Sweep";
    case SYNTH_PRESET_SPACE_DRONE: return "Space Drone";
    default:                         return "Custom";
    }
}

const char *synth_arp_mode_name(synth_arp_mode_t mode)
{
    switch (mode) {
    case SYNTH_ARP_OFF:      return "OFF";
    case SYNTH_ARP_UP:       return "UP";
    case SYNTH_ARP_DOWN:     return "DOWN";
    case SYNTH_ARP_UP_DOWN:  return "UP/DOWN";
    case SYNTH_ARP_RANDOM:   return "RANDOM";
    case SYNTH_ARP_CHORD:    return "CHORD";
    default:                 return "UNKNOWN";
    }
}

static void update_arp_timing(synth_t *s)
{
    uint32_t div = 4; /* default 1/16 */
    switch (s->arp.rate) {
    case SYNTH_ARP_RATE_1_4:  div = 1; break;
    case SYNTH_ARP_RATE_1_8:  div = 2; break;
    case SYNTH_ARP_RATE_1_16: div = 4; break;
    case SYNTH_ARP_RATE_1_32: div = 8; break;
    default: div = 4; break;
    }
    uint32_t bpm = (s->arp.bpm >= 40 && s->arp.bpm <= 300) ? s->arp.bpm : 120;
    uint32_t old_step = s->arp.step_samples;
    uint32_t new_step = (uint32_t)(((uint64_t)s->config.sample_rate * 60) / (bpm * div));
    /* Preserve transport phase; an explicitly pending first step remains pending. */
    if (old_step) {
        if (s->arp.sample_counter >= old_step) s->arp.sample_counter = new_step;
        else s->arp.sample_counter = (uint32_t)((uint64_t)s->arp.sample_counter * new_step / old_step);
    }
    s->arp.step_samples = new_step;
}

static void arp_add_held_note(synth_t *s, uint8_t note, uint8_t velocity)
{
    /* If latch is active and all physical keys were released, a new key press starts a fresh chord */
    if (s->arp.latch && s->arp.physical_held_count == 0 && s->arp.held_count > 0) {
        s->arp.held_count = 0;
        memset(s->arp.held_physical, 0, sizeof(s->arp.held_physical));
    }

    for (int i = 0; i < s->arp.held_count; ++i) {
        if (s->arp.held_notes[i] == note) {
            s->arp.held_velocities[i] = velocity;
            if (!s->arp.held_physical[i]) {
                s->arp.held_physical[i] = true;
                s->arp.physical_held_count++;
            }
            return;
        }
    }
    if (s->arp.held_count < SYNTH_ARP_MAX_HELD) {
        int insert_idx = s->arp.held_count;
        for (int i = 0; i < s->arp.held_count; ++i) {
            if (note < s->arp.held_notes[i]) {
                insert_idx = i;
                break;
            }
        }
        for (int i = s->arp.held_count; i > insert_idx; --i) {
            s->arp.held_notes[i] = s->arp.held_notes[i - 1];
            s->arp.held_velocities[i] = s->arp.held_velocities[i - 1];
            s->arp.held_physical[i] = s->arp.held_physical[i - 1];
        }
        s->arp.held_notes[insert_idx] = note;
        s->arp.held_velocities[insert_idx] = velocity;
        s->arp.held_physical[insert_idx] = true;
        s->arp.held_count++;
        s->arp.physical_held_count++;

        if (s->arp.held_count == 1) {
            s->arp.sample_counter = s->arp.step_samples;
            s->arp.current_step_idx = 0;
            s->arp.current_octave = 0;
            s->arp.direction_up = true;
        }
    }
}

static void arp_remove_held_note(synth_t *s, uint8_t note)
{
    int found_idx = -1;
    for (int i = 0; i < s->arp.held_count; ++i) {
        if (s->arp.held_notes[i] == note) {
            found_idx = i;
            break;
        }
    }
    if (found_idx >= 0) {
        if (s->arp.held_physical[found_idx]) {
            s->arp.held_physical[found_idx] = false;
            if (s->arp.physical_held_count > 0) s->arp.physical_held_count--;
        }
        if (s->arp.latch) {
            /* Keep note in latched arp buffer until a new chord is triggered or latch is turned off */
            return;
        }
        for (int i = found_idx; i < s->arp.held_count - 1; ++i) {
            s->arp.held_notes[i] = s->arp.held_notes[i + 1];
            s->arp.held_velocities[i] = s->arp.held_velocities[i + 1];
            s->arp.held_physical[i] = s->arp.held_physical[i + 1];
        }
        s->arp.held_count--;
        if (s->arp.held_count == 0) {
            for (int i = 0; i < s->arp.last_playing_count; ++i) {
                synth_note_off(s, s->arp.last_playing_notes[i]);
            }
            s->arp.last_playing_count = 0;
            s->arp.note_active = false;
        }
    }
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) {
        float n = t / dt;
        return n + n - n * n - 1.0f;
    } else if (t > 1.0f - dt) {
        float n = (t - 1.0f) / dt;
        return n * n + n + n + 1.0f;
    }
    return 0.0f;
}

static inline float fast_noise(uint32_t *seed)
{
    uint32_t x = *seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *seed = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static void update_voice_frequency(const synth_t *s, synth_voice_t *v)
{
    v->current_freq_hz = v->base_freq_hz * s->pitch_bend_mult;
    if (v->current_freq_hz < 1.0f) v->current_freq_hz = 1.0f;
    if (v->current_freq_hz > (float)s->config.sample_rate * 0.48f) {
        v->current_freq_hz = (float)s->config.sample_rate * 0.48f;
    }
    double cycles_per_sample = (double)v->current_freq_hz / (double)s->config.sample_rate;
    v->phase_inc = (uint32_t)(cycles_per_sample * 4294967296.0);
}

static void apply_config_fx(synth_t *s)
{
    s->lfo.phase = 0.0f;
    s->lfo.freq_hz = (s->config.lfo_freq_hz > 0.05f) ? s->config.lfo_freq_hz : 5.0f;
    s->lfo.phase_inc = s->lfo.freq_hz / (float)s->config.sample_rate;
    s->lfo.vibrato_semitones = s->config.lfo_vibrato_depth;
    s->lfo.enabled = s->config.lfo_enable;

    s->chorus.active_samples = 0;
    s->chorus.write_idx = 0;
    memset(s->chorus.buffer, 0, sizeof(s->chorus.buffer));
    s->chorus.lfo_phase = 0.0f;
    s->chorus.lfo_phase_inc = 0.8f / (float)s->config.sample_rate;
    s->chorus.base_delay_samples = 480.0f; /* 10 ms */
    s->chorus.depth_samples = 192.0f;     /* 4 ms */
    s->chorus.wet_mix = s->config.chorus_wet;
    s->chorus.enabled = s->config.chorus_enable;

    s->delay.active_samples = 0;
    s->delay.write_idx = 0;
    memset(s->delay.buffer, 0, sizeof(s->delay.buffer));
    uint32_t ds = (uint32_t)((uint64_t)s->config.delay_ms * s->config.sample_rate / 1000);
    if (ds >= SYNTH_DELAY_MAX_SAMPLES) ds = SYNTH_DELAY_MAX_SAMPLES - 1;
    s->delay.delay_samples = ds;
    s->delay.feedback = s->config.delay_feedback;
    s->delay.dampening = 0.35f;
    s->delay.damp_state = 0.0f;
    s->delay.wet_mix = s->config.delay_wet;
    s->delay.enabled = s->config.delay_enable;

    update_mod_wheel_mult(s);
}

static float safe_range(float x, float lo, float hi, float fallback)
{
    if (!isfinite(x)) return fallback;
    return fmaxf(lo, fminf(hi, x));
}

void synth_normalize_config(synth_config_t *c)
{
    if (!c) return;
    c->sample_rate = SYNTH_SAMPLE_RATE;
    if ((unsigned)c->waveform >= SYNTH_WAVE_COUNT) c->waveform = SYNTH_WAVE_SAW;
    if (c->master_gain_permille > SYNTH_MAX_GAIN_PERMILLE) c->master_gain_permille = SYNTH_MAX_GAIN_PERMILLE;
    if (c->attack_ms > 10000) c->attack_ms = 10000;
    if (c->decay_ms > 10000) c->decay_ms = 10000;
    if (c->release_ms > 10000) c->release_ms = 10000;
    c->sustain_ratio = safe_range(c->sustain_ratio, 0, 1, 0.85f);
    c->base_cutoff_hz = safe_range(c->base_cutoff_hz, 50, 20000, 1400);
    c->resonance = safe_range(c->resonance, 0.5f, 8, 0.5f);
    c->filter_env_octaves = safe_range(c->filter_env_octaves, 0, 8, 0);
    c->mod_wheel_octaves = safe_range(c->mod_wheel_octaves, 0, 8, 0);
    c->lfo_enable = c->lfo_enable ? true : false;
    if (c->lfo_freq_hz == 0) c->lfo_freq_hz = 5;
    c->lfo_freq_hz = safe_range(c->lfo_freq_hz, 0.1f, 20, 5);
    c->lfo_vibrato_depth = safe_range(c->lfo_vibrato_depth, 0, 2, 0);
    c->chorus_enable = c->chorus_enable ? true : false;
    c->chorus_wet = safe_range(c->chorus_wet, 0, 0.5f, 0);
    c->delay_enable = c->delay_enable ? true : false;
    if (c->delay_ms > 350) c->delay_ms = 350;
    c->delay_feedback = safe_range(c->delay_feedback, 0, 0.75f, 0);
    c->delay_wet = safe_range(c->delay_wet, 0, 0.5f, 0);
    if ((unsigned)c->arp_mode >= SYNTH_ARP_MODE_COUNT) c->arp_mode = SYNTH_ARP_OFF;
    if ((unsigned)c->arp_rate >= SYNTH_ARP_RATE_COUNT) c->arp_rate = SYNTH_ARP_RATE_1_16;
    if (c->arp_octaves < 1 || c->arp_octaves > 4) c->arp_octaves = 2;
    if (c->arp_bpm < 40 || c->arp_bpm > 300) c->arp_bpm = 120;
    if (c->pitch_bend_range < 1 || c->pitch_bend_range > 24) c->pitch_bend_range = 2;
    if (c->pwm_duty == 0) c->pwm_duty = 0.5f;
    c->pwm_duty = safe_range(c->pwm_duty, 0.05f, 0.95f, 0.5f);
    c->pwm_lfo_depth = safe_range(c->pwm_lfo_depth, 0, 0.45f, 0);
    c->sub_osc_level = safe_range(c->sub_osc_level, 0, 1, 0);
    if (c->sub_osc_octaves < 1 || c->sub_osc_octaves > 2) c->sub_osc_octaves = 1;
    c->noise_level = safe_range(c->noise_level, 0, 1, 0);
    if ((unsigned)c->filter_type >= SYNTH_FILTER_COUNT) c->filter_type = SYNTH_FILTER_LP;
    if ((unsigned)c->velocity_curve >= SYNTH_VEL_COUNT) c->velocity_curve = SYNTH_VEL_NORMAL;
    if (c->midi_channel > 16) c->midi_channel = 0;
    if (c->glide_ms > 2000) c->glide_ms = 2000;
    c->unison_detune = safe_range(c->unison_detune, 0.0f, 0.50f, 0.0f);
    c->arp_latch = c->arp_latch ? true : false;
}

bool synth_config_is_valid(const synth_config_t *c)
{
    if (!c) return false;
    synth_config_t n = *c;
    synth_normalize_config(&n);
#define SAME(field) (c->field == n.field)
    return SAME(sample_rate) && SAME(waveform) && SAME(master_gain_permille) &&
        SAME(attack_ms) && SAME(decay_ms) && SAME(release_ms) &&
        SAME(sustain_ratio) && SAME(base_cutoff_hz) && SAME(resonance) &&
        SAME(filter_env_octaves) && SAME(mod_wheel_octaves) &&
        SAME(lfo_enable) && SAME(lfo_freq_hz) && SAME(lfo_vibrato_depth) &&
        SAME(chorus_enable) && SAME(chorus_wet) &&
        SAME(delay_enable) && SAME(delay_ms) && SAME(delay_feedback) && SAME(delay_wet) &&
        SAME(arp_mode) && SAME(arp_rate) && SAME(arp_octaves) && SAME(arp_bpm) &&
        SAME(pitch_bend_range) && SAME(pwm_duty) && SAME(pwm_lfo_depth) &&
        SAME(sub_osc_level) && SAME(sub_osc_octaves) && SAME(noise_level) &&
        SAME(filter_type) && SAME(velocity_curve) && SAME(midi_channel) &&
        SAME(glide_ms) && SAME(unison_detune) && SAME(arp_latch);
#undef SAME
}

static sound_result_t sampler_validate(void *context,uint8_t part,const sound_ref_t *ref)
{
    synth_t *s=context;
    return s->sampler->validate(s->sampler->context,part,ref);
}

static void sampler_panic_old(void *context,uint8_t part,const sound_ref_t *old)
{
    synth_t *s=context;
    if(old->engine==SOUND_SF2) s->sampler->panic(s->sampler->context,part);
    else memset(s->voices,0,sizeof(s->voices));
    s->sustain_pedal=false;s->pitch_bend_value=0;s->pitch_bend_mult=1.0f;
    s->mod_wheel=0;s->mod_wheel_mult=1.0f;s->last_note=-1;
}

static bool sampler_commit(void *context,uint8_t part,const sound_ref_t *next)
{
    synth_t *s=context;
    if(s->live_sound.current.engine==SOUND_SF2)
        s->sampler->unbind(s->sampler->context,part);
    if(next->engine==SOUND_SF2){
        if(!s->sampler->bind(s->sampler->context,part,next->bank_generation)||
           !s->sampler->program(s->sampler->context,part,next->bank,next->program)){
            s->sampler->panic(s->sampler->context,part);
            s->sampler->unbind(s->sampler->context,part);return false;
        }
        s->sampler->control(s->sampler->context,part,121,0);
    }else{
        synth_config_t old=s->config;
        s->sound_committing=true;
        synth_set_preset(s,(synth_preset_t)next->synth_preset);
        s->sound_committing=false;
        /* Source selection does not replace shared FX configuration or tails. */
        s->config.chorus_enable=old.chorus_enable;s->config.chorus_wet=old.chorus_wet;
        s->config.delay_enable=old.delay_enable;s->config.delay_ms=old.delay_ms;
        s->config.delay_feedback=old.delay_feedback;s->config.delay_wet=old.delay_wet;
        synth_set_lfo(s,s->config.lfo_enable,s->config.lfo_freq_hz,s->config.lfo_vibrato_depth);
    }
    return true;
}

bool synth_sampler_install(synth_t *s,const synth_sampler_ops_t *ops)
{
    if(!s||!ops||s->sampler||s->config.sample_rate!=48000||
       !ops->validate||!ops->bind||!ops->program||!ops->unbind||!ops->note_on||
       !ops->note_off||!ops->control||!ops->pitch||!ops->panic||!ops->render||
       !ops->voice_count||!ops->complete)return false;
    sound_ref_t initial={.engine=SOUND_SYNTH,.synth_preset=(uint8_t)s->current_preset};
    sound_transition_ops_t transition={sampler_validate,sampler_panic_old,sampler_commit,s};
    if(!sound_part_init(&s->live_sound,0,&initial,s->input_epoch,&transition))return false;
    s->sampler=ops;
    if(s->looper)s->looper->sampler=ops; /* SoundFont tracks use parts 1..4 */
    return true;
}

bool synth_sampler_install_host(synth_t *s,const synth_sampler_ops_t *ops)
{
    return synth_sampler_install(s,ops);
}

sound_result_t synth_sound_select(synth_t *s,uint32_t epoch,uint64_t id,const sound_ref_t *ref)
{
    if(!s||!s->sampler)return SOUND_INVALID;
    sound_result_t result=sound_select_rt(&s->live_sound,epoch,id,ref);
    if(result==SOUND_OK){
        /* The ramp cuts held live notes: close them in recording tracks now. */
        if(s->looper)looper_close_recorded_notes(s->looper);
        s->arp.held_count=s->arp.last_playing_count=s->arp.physical_held_count=0;
        memset(s->arp.held_physical,0,sizeof(s->arp.held_physical));
        s->arp.note_active=false;
    }
    return result;
}

sound_result_t synth_sound_select_host(synth_t *s,uint32_t epoch,uint64_t id,const sound_ref_t *ref)
{
    return synth_sound_select(s,epoch,id,ref);
}

void synth_panic(synth_t *s)
{
    if (!s) return;
    if(s->sampler){
        if(s->live_sound.current.engine==SOUND_SF2)s->sampler->panic(s->sampler->context,0);
        if(s->live_sound.phase!=SOUND_STABLE)s->sampler->complete(s->sampler->context,0,s->live_sound.request_id,true);
        s->live_sound.phase=SOUND_STABLE;s->live_sound.frames_left=0;
        if(s->live_sound.input_epoch==UINT32_MAX)s->live_sound.current.sound_missing=true;
        else ++s->live_sound.input_epoch;
        s->live_sound.pending=s->live_sound.current;
    }
    memset(s->key_claims,0,sizeof(s->key_claims));
    memset(s->voices, 0, sizeof(s->voices));
    memset(s->delay.buffer, 0, sizeof(s->delay.buffer));
    memset(s->chorus.buffer, 0, sizeof(s->chorus.buffer));
    s->delay.active_samples = s->chorus.active_samples = 0;
    s->delay.write_idx = s->chorus.write_idx = 0;
    s->delay.damp_state = 0;
    s->sustain_pedal = false;
    s->pitch_bend_value = 0;
    s->pitch_bend_mult = 1.0f;
    s->mod_wheel = 0;
    s->mod_wheel_mult = 1.0f;
    s->arp.held_count = s->arp.last_playing_count = 0;
    s->arp.physical_held_count = 0;
    memset(s->arp.held_physical, 0, sizeof(s->arp.held_physical));
    s->arp.note_active = false;
    s->last_note = -1;
    ++s->input_epoch;
    if (s->looper) looper_panic(s->looper);
}

void synth_init(synth_t *s, const synth_config_t *cfg)
{
    ensure_sine_table();
    memset(s, 0, sizeof(*s));

    if (cfg) {
        s->config = *cfg;
    } else {
        s->config = s_presets[SYNTH_PRESET_ANALOG_BRASS];
    }

    if (s->config.master_gain_permille > SYNTH_MAX_GAIN_PERMILLE) {
        s->config.master_gain_permille = SYNTH_MAX_GAIN_PERMILLE;
    }
    if (s->config.sample_rate == 0) s->config.sample_rate = SYNTH_SAMPLE_RATE;
    synth_normalize_config(&s->config);
    if (s->config.resonance < 0.5f) s->config.resonance = 0.5f;

    s->output_gain = (float)s->config.master_gain_permille / SYNTH_MAX_GAIN_PERMILLE;
    s->last_note = -1;
    s->pitch_bend_value = 0;
    s->pitch_bend_mult = 1.0f;
    s->mod_wheel = 0;
    s->mod_wheel_mult = 1.0f;
    s->sustain_pedal = false;
    s->current_preset = SYNTH_PRESET_ANALOG_BRASS;

    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        s->voices[i].state = VOICE_STATE_IDLE;
        s->voices[i].base_freq_hz = 440.0f;
        s->voices[i].target_freq_hz = 440.0f;
        s->voices[i].filter.s1 = 0.0f;
        s->voices[i].filter.s2 = 0.0f;
        update_voice_frequency(s, &s->voices[i]);
    }

    apply_config_fx(s);

    s->arp.mode = s->config.arp_mode;
    s->arp.rate = (s->config.arp_rate < SYNTH_ARP_RATE_COUNT) ? s->config.arp_rate : SYNTH_ARP_RATE_1_16;
    s->arp.octaves = (s->config.arp_octaves >= 1 && s->config.arp_octaves <= 4) ? s->config.arp_octaves : 2;
    s->arp.bpm = (s->config.arp_bpm >= 40 && s->config.arp_bpm <= 300) ? s->config.arp_bpm : 120;
    s->arp.latch = s->config.arp_latch;
    s->arp.gate_ratio = 0.65f;
    s->arp.held_count = 0;
    s->arp.physical_held_count = 0;
    s->arp.sample_counter = 0;
    s->arp.current_step_idx = 0;
    s->arp.current_octave = 0;
    s->arp.direction_up = true;
    s->arp.last_playing_count = 0;
    s->arp.note_active = false;
    update_arp_timing(s);
}

void synth_set_arp_mode(synth_t *s, synth_arp_mode_t mode)
{
    if (mode >= SYNTH_ARP_MODE_COUNT) mode = SYNTH_ARP_OFF;
    if (s->arp.mode != mode) {
        synth_all_notes_off(s);
        s->arp.held_count = 0;
        s->arp.physical_held_count = 0;
        memset(s->arp.held_physical, 0, sizeof(s->arp.held_physical));
        s->arp.note_active = false;
        s->arp.last_playing_count = 0;
    }
    s->arp.mode = mode;
    s->config.arp_mode = mode;
}

synth_arp_mode_t synth_get_arp_mode(const synth_t *s)
{
    return s->arp.mode;
}

void synth_set_arp_bpm(synth_t *s, uint16_t bpm)
{
    if (bpm < 40) bpm = 40;
    if (bpm > 300) bpm = 300;
    s->arp.bpm = bpm;
    s->config.arp_bpm = bpm;
    update_arp_timing(s);
    if (s->looper) looper_set_bpm(s->looper, bpm);
}

uint16_t synth_get_arp_bpm(const synth_t *s)
{
    return s->arp.bpm;
}

void synth_set_arp_octaves(synth_t *s, uint8_t octaves)
{
    if (octaves < 1) octaves = 1;
    if (octaves > 4) octaves = 4;
    s->arp.octaves = octaves;
    if (s->arp.current_octave < 0 || s->arp.current_octave >= octaves) {
        s->arp.current_octave = octaves - 1;
        s->arp.direction_up = false;
    }
    s->config.arp_octaves = octaves;
}

uint8_t synth_get_arp_octaves(const synth_t *s)
{
    return s->arp.octaves;
}

void synth_set_arp_rate(synth_t *s, synth_arp_rate_t rate)
{
    if (rate >= SYNTH_ARP_RATE_COUNT) rate = SYNTH_ARP_RATE_1_16;
    s->arp.rate = rate;
    s->config.arp_rate = rate;
    update_arp_timing(s);
}

synth_arp_rate_t synth_get_arp_rate(const synth_t *s)
{
    return s ? s->arp.rate : SYNTH_ARP_RATE_1_16;
}

void synth_set_arp_latch(synth_t *s, bool latch)
{
    if (!s) return;
    bool was_latch = s->arp.latch;
    s->arp.latch = latch;
    s->config.arp_latch = latch;
    if (was_latch && !latch) {
        /* Remove any notes that are no longer physically held */
        int w = 0;
        for (int r = 0; r < s->arp.held_count; ++r) {
            if (s->arp.held_physical[r]) {
                s->arp.held_notes[w] = s->arp.held_notes[r];
                s->arp.held_velocities[w] = s->arp.held_velocities[r];
                s->arp.held_physical[w] = true;
                w++;
            }
        }
        s->arp.held_count = (uint8_t)w;
        if (s->arp.held_count == 0) {
            for (int i = 0; i < s->arp.last_playing_count; ++i) {
                synth_note_off(s, s->arp.last_playing_notes[i]);
            }
            s->arp.last_playing_count = 0;
            s->arp.note_active = false;
        }
    }
}

bool synth_get_arp_latch(const synth_t *s)
{
    return s ? s->arp.latch : false;
}

void synth_set_preset(synth_t *s, synth_preset_t preset)
{
    if (s->sampler && !s->sound_committing &&
        (s->live_sound.phase != SOUND_STABLE || s->live_sound.current.engine != SOUND_SYNTH)) return;
    if (preset >= SYNTH_PRESET_COUNT) preset = SYNTH_PRESET_ANALOG_BRASS;
    synth_config_t previous=s->config;
    s->current_preset = preset;
    if (s->sampler && !s->sound_committing) {
        s->live_sound.current.synth_preset = preset;
        s->live_sound.pending = s->live_sound.current;
    }
    uint16_t saved_gain = s->config.master_gain_permille;
    uint8_t saved_midi_ch = s->config.midi_channel;
    synth_vel_curve_t saved_vel_curve = s->config.velocity_curve;
    s->config = s_presets[preset];
    synth_normalize_config(&s->config);
    s->config.master_gain_permille = saved_gain;
    s->config.midi_channel = saved_midi_ch;
    s->config.velocity_curve = saved_vel_curve;
    /* Preset selection changes timbre while preserving live performance settings. */
    s->config.arp_mode = s->arp.mode;
    s->config.arp_rate = s->arp.rate;
    s->config.arp_octaves = s->arp.octaves;
    s->config.arp_bpm = s->arp.bpm;
    s->config.arp_latch = s->arp.latch;
    synth_retarget_voices(s->voices,SYNTH_VOICES_COUNT,&previous,&s->config);

    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        s->voices[i].filter.s1 = 0.0f;
        s->voices[i].filter.s2 = 0.0f;
        if (s->voices[i].state != VOICE_STATE_IDLE) {
            update_voice_frequency(s, &s->voices[i]);
        }
    }

    if(!s->sound_committing)apply_config_fx(s);
}

void synth_next_preset(synth_t *s)
{
    if (!s) return;
    synth_set_preset(s, (synth_preset_t)(((unsigned)s->current_preset + 1u) % SYNTH_PRESET_COUNT));
}

synth_preset_t synth_get_preset(const synth_t *s)
{
    return s->current_preset;
}

void synth_set_cutoff(synth_t *s, float cutoff_hz)
{
    if (!s || !isfinite(cutoff_hz)) return;
    if (cutoff_hz < 50.0f) cutoff_hz = 50.0f;
    if (cutoff_hz > 20000.0f) cutoff_hz = 20000.0f;
    s->config.base_cutoff_hz = cutoff_hz;
}

float synth_get_cutoff(const synth_t *s)
{
    return s->config.base_cutoff_hz;
}

void synth_set_resonance(synth_t *s, float q)
{
    if (!s || !isfinite(q)) return;
    if (q < 0.5f) q = 0.5f;
    if (q > 8.0f) q = 8.0f;
    s->config.resonance = q;
}

float synth_get_resonance(const synth_t *s)
{
    return s->config.resonance;
}

void synth_set_filter_type(synth_t *s, synth_filter_type_t type)
{
    if (!s) return;
    if ((unsigned)type >= SYNTH_FILTER_COUNT) type = SYNTH_FILTER_LP;
    s->config.filter_type = type;
}

synth_filter_type_t synth_get_filter_type(const synth_t *s)
{
    return s ? s->config.filter_type : SYNTH_FILTER_LP;
}

void synth_retarget_voices(synth_voice_t *voices, unsigned count, const synth_config_t *old, const synth_config_t *next)
{
    for(unsigned i=0;i<count;++i) {
        synth_voice_t *v=&voices[i]; if(v->state==VOICE_STATE_IDLE) continue;
        bool changed=false; uint32_t frames=1; float goal=v->env_level;
        if(v->state!=VOICE_STATE_RELEASE) v->sustain_level=v->env_target*next->sustain_ratio;
        if(v->state==VOICE_STATE_ATTACK) {
            uint32_t n=next->decay_ms ? (uint32_t)next->decay_ms*48 : 1;
            v->decay_step=(v->env_target-v->sustain_level)/(float)n;
        }
        if(v->state==VOICE_STATE_ATTACK && old->attack_ms!=next->attack_ms) {
            changed=true; frames=(uint32_t)next->attack_ms*48; goal=v->env_target;
        } else if(v->state==VOICE_STATE_DECAY && (old->decay_ms!=next->decay_ms || old->sustain_ratio!=next->sustain_ratio)) {
            changed=true; frames=(uint32_t)next->decay_ms*48; goal=v->sustain_level;
        } else if(v->state==VOICE_STATE_SUSTAIN && old->sustain_ratio!=next->sustain_ratio) {
            changed=true; frames=240; goal=v->sustain_level;
        } else if(v->state==VOICE_STATE_RELEASE && old->release_ms!=next->release_ms) {
            changed=true; frames=(uint32_t)next->release_ms*48; goal=0;
        }
        if(changed) {
            if(!frames) frames=1;
            v->env_goal=goal; v->env_remaining=frames; v->env_delta=(goal-v->env_level)/(float)frames;
        }
    }
}

void synth_set_adsr(synth_t *s, uint16_t attack_ms, uint16_t decay_ms, float sustain_ratio, uint16_t release_ms)
{
    if (!s || !isfinite(sustain_ratio)) return;
    if (attack_ms > 10000) attack_ms = 10000;
    if (decay_ms > 10000) decay_ms = 10000;
    if (release_ms > 10000) release_ms = 10000;
    if (sustain_ratio < 0.0f) sustain_ratio = 0.0f;
    if (sustain_ratio > 1.0f) sustain_ratio = 1.0f;
    synth_config_t next=s->config;
    next.attack_ms=attack_ms; next.decay_ms=decay_ms;
    next.sustain_ratio=sustain_ratio; next.release_ms=release_ms;
    synth_retarget_voices(s->voices,SYNTH_VOICES_COUNT,&s->config,&next);
    s->config.attack_ms = attack_ms;
    s->config.decay_ms = decay_ms;
    s->config.sustain_ratio = sustain_ratio;
    s->config.release_ms = release_ms;
}

void synth_set_filter_env(synth_t *s, float octaves)
{
    if (!s || !isfinite(octaves)) return;
    if (octaves < 0.0f) octaves = 0.0f;
    if (octaves > 8.0f) octaves = 8.0f;
    s->config.filter_env_octaves = octaves;
}

void synth_set_glide(synth_t *s, uint16_t glide_ms)
{
    if (!s) return;
    if (glide_ms > 2000) glide_ms = 2000;
    s->config.glide_ms = glide_ms;
}

uint16_t synth_get_glide(const synth_t *s)
{
    return s ? s->config.glide_ms : 0;
}

void synth_set_unison(synth_t *s, float detune_semitones)
{
    if (!s || !isfinite(detune_semitones)) return;
    if (detune_semitones < 0.0f) detune_semitones = 0.0f;
    if (detune_semitones > 0.50f) detune_semitones = 0.50f;
    s->config.unison_detune = detune_semitones;
}

float synth_get_unison(const synth_t *s)
{
    return s ? s->config.unison_detune : 0.0f;
}

void synth_set_velocity_curve(synth_t *s, synth_vel_curve_t curve)
{
    if (!s) return;
    if ((unsigned)curve >= SYNTH_VEL_COUNT) curve = SYNTH_VEL_NORMAL;
    s->config.velocity_curve = curve;
}

synth_vel_curve_t synth_get_velocity_curve(const synth_t *s)
{
    return s ? s->config.velocity_curve : SYNTH_VEL_NORMAL;
}

void synth_set_midi_channel(synth_t *s, uint8_t ch)
{
    if (!s) return;
    if(s->sampler)return; /* ingress/channel cutover has no host ownership adapter */
    if (ch > 16) ch = 0;
    if(s->config.midi_channel != ch) {
        if(s->looper) looper_close_recorded_notes(s->looper);
        s->sustain_pedal=false;
        synth_all_notes_off(s);
        for(unsigned i=0;i<SYNTH_VOICES_COUNT;++i) if(s->voices[i].state!=VOICE_STATE_IDLE)
            s->voices[i].release_step=s->voices[i].env_level/240.0f;
        s->arp.held_count=s->arp.physical_held_count=s->arp.last_playing_count=0;
        memset(s->arp.held_physical,0,sizeof(s->arp.held_physical));
        s->arp.note_active=false;
        s->pitch_bend_value=0; s->pitch_bend_mult=1;
        s->mod_wheel=0; s->mod_wheel_mult=1; s->last_note=-1;
        ++s->input_epoch;
    }
    s->config.midi_channel = ch;
}

uint8_t synth_get_midi_channel(const synth_t *s)
{
    return s ? s->config.midi_channel : 0;
}

void synth_set_mod_wheel(synth_t *s, uint8_t val)
{
    if (val > 127) val = 127;
    s->mod_wheel = val;
    update_mod_wheel_mult(s);
}

uint8_t synth_get_mod_wheel(const synth_t *s)
{
    return s->mod_wheel;
}

void synth_set_delay(synth_t *s, bool enable, uint16_t delay_ms, float feedback, float wet_mix)
{
    if (!s || !isfinite(feedback) || !isfinite(wet_mix)) return;
    if (!enable && s->delay.enabled) {
        memset(s->delay.buffer, 0, sizeof(s->delay.buffer));
        s->delay.active_samples = 0;
        s->delay.write_idx = 0;
        s->delay.damp_state = 0.0f;
    }
    s->delay.enabled = enable;
    s->config.delay_enable = enable;
    if (delay_ms > 0) {
        if (delay_ms > 350) delay_ms = 350;
        uint32_t ds = (uint32_t)((uint64_t)delay_ms * s->config.sample_rate / 1000);
        if (ds >= SYNTH_DELAY_MAX_SAMPLES) ds = SYNTH_DELAY_MAX_SAMPLES - 1;
        s->delay.delay_samples = ds;
        s->config.delay_ms = delay_ms;
    } else if (!enable) {
        s->delay.delay_samples = 0;
        s->config.delay_ms = 0;
    }
    if (feedback < 0.0f) feedback = 0.0f;
    if (feedback > 0.75f) feedback = 0.75f;
    s->delay.feedback = feedback;
    s->config.delay_feedback = feedback;
    if (wet_mix < 0.0f) wet_mix = 0.0f;
    if (wet_mix > 0.5f) wet_mix = 0.5f;
    s->delay.wet_mix = wet_mix;
    s->config.delay_wet = wet_mix;
}

void synth_set_chorus(synth_t *s, bool enable, float wet_mix)
{
    if (!s || !isfinite(wet_mix)) return;
    if (!enable && s->chorus.enabled) {
        memset(s->chorus.buffer, 0, sizeof(s->chorus.buffer));
        s->chorus.active_samples = 0;
        s->chorus.write_idx = 0;
    }
    s->chorus.enabled = enable;
    s->config.chorus_enable = enable;
    if (wet_mix < 0.0f) wet_mix = 0.0f;
    if (wet_mix > 0.5f) wet_mix = 0.5f;
    s->chorus.wet_mix = wet_mix;
    s->config.chorus_wet = wet_mix;
}

void synth_set_lfo(synth_t *s, bool enable, float freq_hz, float vibrato_depth)
{
    if (!s || !isfinite(freq_hz) || !isfinite(vibrato_depth)) return;
    s->lfo.enabled = enable;
    s->config.lfo_enable = enable;
    if (freq_hz < 0.1f) freq_hz = 0.1f;
    if (freq_hz > 20.0f) freq_hz = 20.0f;
    s->lfo.freq_hz = freq_hz;
    s->lfo.phase_inc = freq_hz / (float)s->config.sample_rate;
    s->config.lfo_freq_hz = freq_hz;
    if (vibrato_depth < 0.0f) vibrato_depth = 0.0f;
    if (vibrato_depth > 2.0f) vibrato_depth = 2.0f;
    s->lfo.vibrato_semitones = vibrato_depth;
    s->config.lfo_vibrato_depth = vibrato_depth;
}

void synth_set_master_gain(synth_t *s, uint16_t gain_permille)
{
    if (gain_permille > SYNTH_MAX_GAIN_PERMILLE) {
        gain_permille = SYNTH_MAX_GAIN_PERMILLE;
    }
    s->config.master_gain_permille = gain_permille;
}

void synth_set_waveform(synth_t *s, synth_waveform_t wf)
{
    if (wf >= SYNTH_WAVE_COUNT) wf = SYNTH_WAVE_SAW;
    s->config.waveform = wf;
}

void synth_set_pitch_bend_range(synth_t *s, int8_t semitones)
{
    if (semitones < 1) semitones = 1;
    if (semitones > 24) semitones = 24;
    s->config.pitch_bend_range = semitones;
    float st = ((float)s->pitch_bend_value / 8192.0f) * (float)s->config.pitch_bend_range;
    s->pitch_bend_mult = powf(2.0f, st / 12.0f);
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state != VOICE_STATE_IDLE) {
            update_voice_frequency(s, &s->voices[i]);
        }
    }
}

static synth_voice_t *allocate_voice(synth_t *s, uint8_t note)
{
    /* 1. Reuse existing voice playing this note if any */
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state != VOICE_STATE_IDLE && s->voices[i].note == note) {
            return &s->voices[i];
        }
    }

    /* 2. Find free (IDLE) voice */
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state == VOICE_STATE_IDLE) {
            return &s->voices[i];
        }
    }

    /* 3. Steal from voices in RELEASE state with lowest amplitude */
    int best_rel = -1;
    float lowest_amp = 2.0f;
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state == VOICE_STATE_RELEASE) {
            if (s->voices[i].env_level < lowest_amp) {
                lowest_amp = s->voices[i].env_level;
                best_rel = i;
            }
        }
    }
    if (best_rel >= 0) {
        s->voice_steal_count++;
        return &s->voices[best_rel];
    }

    /* 4. Steal oldest voice (highest age_samples) */
    int oldest_idx = 0;
    uint32_t max_age = 0;
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].age_samples >= max_age) {
            max_age = s->voices[i].age_samples;
            oldest_idx = i;
        }
    }
    s->voice_steal_count++;
    return &s->voices[oldest_idx];
}

static void trigger_voice_release(const synth_t *s, synth_voice_t *v)
{
    v->env_remaining=0;
    v->state = VOICE_STATE_RELEASE;
    uint32_t rel_frames = (uint32_t)((float)s->config.release_ms * 0.001f * (float)s->config.sample_rate);
    if (rel_frames == 0) rel_frames = 1;
    v->release_step = (v->env_level > 0.0f) ? (v->env_level / (float)rel_frames) : 0.01f;
}

void synth_voice_start(synth_voice_t *v, const synth_config_t *cfg, int16_t last_note, uint8_t note, uint8_t velocity, float pitch)
{
    v->env_remaining=0;
    v->note = note;
    v->velocity = velocity;
    v->sustain_held = false;
    v->age_samples = 0;

    float target_hz = synth_note_to_freq(note);
    v->target_freq_hz = target_hz;
    if (cfg->glide_ms > 0 && last_note >= 0) {
        v->base_freq_hz = synth_note_to_freq((uint8_t)last_note);
    } else {
        v->base_freq_hz = target_hz;
    }
    v->current_freq_hz=v->base_freq_hz*pitch;
    v->phase_inc=(uint32_t)(((double)v->current_freq_hz/SYNTH_SAMPLE_RATE)*4294967296.0);

    /* Reset filter memory on voice trigger for crisp transient */
    v->filter.s1 = 0.0f;
    v->filter.s2 = 0.0f;
    v->noise_seed = 0x12345678U ^ ((uint32_t)note * 1103515245U + (uint32_t)velocity);

    /* Velocity scaling according to selected velocity curve (0.50 to 1.0) */
    float norm_v = (float)velocity / 127.0f;
    float shaped_v = norm_v;
    switch (cfg->velocity_curve) {
    case SYNTH_VEL_SOFT:
        shaped_v = sqrtf(norm_v);
        break;
    case SYNTH_VEL_HARD:
        shaped_v = norm_v * norm_v;
        break;
    case SYNTH_VEL_FIXED:
        shaped_v = 100.0f / 127.0f;
        break;
    case SYNTH_VEL_NORMAL:
    default:
        shaped_v = norm_v;
        break;
    }
    float vel_scale = 0.50f + 0.50f * shaped_v;
    v->env_target = vel_scale;
    v->sustain_level = vel_scale * cfg->sustain_ratio;

    uint32_t att_frames = (uint32_t)((float)cfg->attack_ms * 0.001f * (float)cfg->sample_rate);
    if (att_frames == 0) att_frames = 1;

    float delta = v->env_target - v->env_level;
    if (delta > 0.0f) {
        v->attack_step = delta / (float)att_frames;
        v->state = VOICE_STATE_ATTACK;
    } else {
        v->env_level = v->env_target;
        v->state = VOICE_STATE_DECAY;
    }

    uint32_t dec_frames = (uint32_t)((float)cfg->decay_ms * 0.001f * (float)cfg->sample_rate);
    if (dec_frames == 0) dec_frames = 1;
    v->decay_step = (v->env_target - v->sustain_level) / (float)dec_frames;
    if (v->decay_step < 0.0f) v->decay_step = 0.0f;

    uint32_t rel_frames = (uint32_t)((float)cfg->release_ms * 0.001f * (float)cfg->sample_rate);
    if (rel_frames == 0) rel_frames = 1;
    v->release_step = v->sustain_level / (float)rel_frames;

}

void synth_note_on(synth_t *s, uint8_t note, uint8_t velocity)
{
    if (velocity == 0) {
        synth_note_off(s, note);
        return;
    }
    if (note > 127) note = 127;
    if(s->sampler){
        if(!sound_note_on_allowed(&s->live_sound,s->live_sound.input_epoch)){++s->sampler_rejected;return;}
        if(s->live_sound.current.engine==SOUND_SF2){
            if(!s->sampler->note_on(s->sampler->context,0,note,velocity)){++s->sampler_rejected;return;}
            s->last_note=note;++s->note_on_count;
            if(s->looper)looper_record_note(s->looper,note,velocity,true,s);
            return;
        }
    }

    synth_voice_t *v = allocate_voice(s, note);
    synth_voice_start(v,&s->config,s->last_note,note,velocity,s->pitch_bend_mult);

    s->last_note = (int16_t)note;
    s->note_on_count++;
    if (s->looper) looper_record_note(s->looper, note, velocity, true, s);
}

void synth_note_off(synth_t *s, uint8_t note)
{
    if(s->sampler){
        if(!sound_note_off_allowed(&s->live_sound,s->live_sound.input_epoch))return;
        if(s->live_sound.current.engine==SOUND_SF2){
            s->sampler->note_off(s->sampler->context,0,note);++s->note_off_count;
            if(s->looper)looper_record_note(s->looper,note,0,false,s);
            return;
        }
    }
    s->note_off_count++;
    if (s->looper) looper_record_note(s->looper, note, 0, false, s);
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        synth_voice_t *v = &s->voices[i];
        if (v->state != VOICE_STATE_IDLE && v->note == note) {
            if (s->sustain_pedal) {
                v->sustain_held = true;
            } else {
                trigger_voice_release(s, v);
            }
        }
    }
}

void synth_all_notes_off(synth_t *s)
{
    if(s->sampler&&s->live_sound.current.engine==SOUND_SF2){s->sampler->control(s->sampler->context,0,123,0);return;}
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        synth_voice_t *v = &s->voices[i];
        if (v->state != VOICE_STATE_IDLE) {
            v->sustain_held = false;
            trigger_voice_release(s, v);
        }
    }
}

static void synth_handle_event_unfiltered(synth_t *s, const midi_event_t *ev);

bool synth_sound_event_host(synth_t *s,uint32_t epoch,const midi_event_t *ev)
{
    return synth_sound_event_from(s,SYNTH_SOURCE_LOCAL,epoch,ev);
}

uint32_t synth_live_epoch(const synth_t *s)
{
    return s->sampler ? s->live_sound.input_epoch : s->input_epoch;
}

static bool shared_oscillator_key(const synth_t *s, synth_source_t source, uint8_t note, uint32_t claim)
{
    if (s->sampler && s->live_sound.current.engine == SOUND_SF2) return false;
    for (unsigned owner = 0; owner < SYNTH_SOURCE_COUNT; ++owner) {
        if (owner != (unsigned)source && s->key_claims[owner][note] == claim) return true;
    }
    return false;
}

bool synth_sound_event_from(synth_t *s,synth_source_t src,uint32_t epoch,const midi_event_t *ev)
{
    if(!s||!ev||(unsigned)src>=SYNTH_SOURCE_COUNT)return false;
    const uint32_t current = synth_live_epoch(s);
    bool on=ev->type==MIDI_EVENT_NOTE_ON&&ev->note.velocity;
    bool off=ev->type==MIDI_EVENT_NOTE_OFF||(ev->type==MIDI_EVENT_NOTE_ON&&!ev->note.velocity);
    uint32_t live=current+1u;
    uint32_t *claim=(on||off)&&ev->note.note<128?&s->key_claims[src][ev->note.note]:NULL;
    /* A press from before the sound change: the old sound already stopped it. */
    if(claim&&off&&*claim&&*claim!=live){*claim=0;return true;}
    if(claim&&on&&src==SYNTH_SOURCE_USB&&*claim&&*claim!=live){++s->sampler_rejected;return false;}
    if(epoch!=current||!live||(s->sampler&&(s->live_sound.phase!=SOUND_STABLE||s->live_sound.current.sound_missing))){++s->sampler_rejected;return false;}
    if((ev->type==MIDI_EVENT_NOTE_ON||ev->type==MIDI_EVENT_NOTE_OFF)&&
       (ev->note.note>127||ev->note.velocity>127)){++s->sampler_rejected;return false;}
    if((ev->type==MIDI_EVENT_PITCH_BEND&&(ev->pitch_bend.value < -8192||ev->pitch_bend.value > 8191))||
       (ev->type==MIDI_EVENT_CONTROL_CHANGE&&(ev->cc.controller>127||ev->cc.value>127))){++s->sampler_rejected;return false;}
    if(s->sampler&&s->live_sound.current.engine==SOUND_SF2&&ev->type==MIDI_EVENT_PROGRAM_CHANGE){
        ++s->sampler_rejected;return false; /* SF2 selection uses explicit bank/program identity */
    }
    if(claim&&off){
        if(!*claim)return true; /* this source holds nothing on that key */
        /* Each source's press started its own sampler voice, and a sampler
         * Note Off ends one (the oldest) instance of the key. Forward one Note
         * Off per press, even while another source still holds the key: that
         * source's own voice keeps sounding, and none is left without an end. */
        *claim=0;
        if(shared_oscillator_key(s,src,ev->note.note,live))return true;
    }else if(claim&&*claim==live)return true; /* repeated press without release: one voice per source */
    else if(claim&&on&&shared_oscillator_key(s,src,ev->note.note,live)){
        *claim=live;
        return true;
    }
    unsigned rejected=s->sampler_rejected;
    synth_handle_event_unfiltered(s,ev);
    /* Claim only a voice that started: a press rejected for want of a voice
     * must not later end another source's instance of the key. */
    if(claim&&on&&s->sampler_rejected==rejected)*claim=live;
    return s->sampler_rejected==rejected;
}

void synth_release_source(synth_t *s, synth_source_t source)
{
    if (!s || (unsigned)source >= SYNTH_SOURCE_COUNT) return;
    const uint32_t live = synth_live_epoch(s) + 1u;
    if (source == SYNTH_SOURCE_USB) {
        const midi_event_t reset = {.type = MIDI_EVENT_CONTROL_CHANGE, .cc = {121, 0}};
        synth_handle_event_unfiltered(s, &reset);
    }
    /* Transport loss is a forced release of physically held keys. Keep the
     * operator's latch setting and any other source's arpeggiator keys. */
    const bool latch = s->arp.latch;
    s->arp.latch = false;
    for (unsigned note = 0; note < 128; ++note) {
        uint32_t claim = s->key_claims[source][note];
        s->key_claims[source][note] = 0;
        if (!claim || claim != live || shared_oscillator_key(s, source, note, live)) continue;
        const midi_event_t release = {.type = MIDI_EVENT_NOTE_OFF, .note = {note, 0}};
        synth_handle_event_unfiltered(s, &release);
    }
    s->arp.latch = latch;
}

void synth_handle_local_note(synth_t *s, uint8_t note, uint8_t velocity, bool down)
{
    if(s->sampler){++s->sampler_rejected;return;}
    if(note>127) return;
    midi_event_t ev={.type=down && velocity ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF, .note={.note=note,.velocity=velocity}};
    synth_handle_event_unfiltered(s,&ev);
}

void synth_handle_midi_event(synth_t *s, const midi_event_t *ev)
{
    if (!ev) return;
    if(s->sampler){++s->sampler_rejected;return;}
    if (ev->type != MIDI_EVENT_SYSTEM_RESET &&
        s->config.midi_channel > 0 &&
        s->config.midi_channel <= 16 &&
        (uint8_t)(ev->channel + 1) != s->config.midi_channel) {
        return;
    }

    synth_handle_event_unfiltered(s,ev);
}

static void synth_handle_event_unfiltered(synth_t *s, const midi_event_t *ev)
{
    if(s->sampler&&s->live_sound.current.engine==SOUND_SF2){
        if(ev->type==MIDI_EVENT_PITCH_BEND){s->pitch_bend_value=ev->pitch_bend.value;s->sampler->pitch(s->sampler->context,0,(unsigned)((int)ev->pitch_bend.value+8192));return;}
        if(ev->type==MIDI_EVENT_CONTROL_CHANGE){unsigned cc=ev->cc.controller;
            if(cc==64||cc==121){s->sampler->control(s->sampler->context,0,cc,ev->cc.value);s->sustain_pedal=cc==64&&ev->cc.value>=64;if(cc==121)s->pitch_bend_value=0;return;}
            /* No oscillator-only timbre controls are applied to sampler audio. */
            if(cc!=7&&cc!=20&&cc!=75&&cc!=79&&cc!=91&&cc!=93&&cc!=120&&cc!=123)return;
        }
    }
    switch (ev->type) {
    case MIDI_EVENT_NOTE_ON:
        if (s->arp.mode != SYNTH_ARP_OFF) {
            arp_add_held_note(s, ev->note.note, ev->note.velocity);
        } else {
            synth_note_on(s, ev->note.note, ev->note.velocity);
        }
        break;
    case MIDI_EVENT_NOTE_OFF:
        if (s->arp.mode != SYNTH_ARP_OFF) {
            arp_remove_held_note(s, ev->note.note);
        } else {
            synth_note_off(s, ev->note.note);
        }
        break;
    case MIDI_EVENT_PITCH_BEND: {
        s->pitch_bend_value = ev->pitch_bend.value;
        float semitones = ((float)s->pitch_bend_value / 8192.0f) * (float)s->config.pitch_bend_range;
        s->pitch_bend_mult = powf(2.0f, semitones / 12.0f);
        for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
            if (s->voices[i].state != VOICE_STATE_IDLE) {
                update_voice_frequency(s, &s->voices[i]);
            }
        }
        break;
    }
    case MIDI_EVENT_CONTROL_CHANGE:
        if (ev->cc.controller == 1) {
            /* Modulation Wheel: sweeps filter cutoff */
            synth_set_mod_wheel(s, ev->cc.value);
        } else if (ev->cc.controller == 5) {
            /* Portamento Time: 0 .. 1000 ms */
            uint16_t glide = (uint16_t)(((uint32_t)ev->cc.value * 1000U) / 127U);
            synth_set_glide(s, glide);
        } else if (ev->cc.controller == 64) {
            /* Sustain pedal */
            bool pressed = (ev->cc.value >= 64);
            if (s->sustain_pedal && !pressed) {
                for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
                    if (s->voices[i].sustain_held) {
                        s->voices[i].sustain_held = false;
                        trigger_voice_release(s, &s->voices[i]);
                    }
                }
            }
            s->sustain_pedal = pressed;
        } else if (ev->cc.controller == 65) {
            /* Portamento On/Off */
            if (ev->cc.value < 64) {
                synth_set_glide(s, 0);
            } else if (s->config.glide_ms == 0) {
                synth_set_glide(s, 120);
            }
        } else if (ev->cc.controller == 7 || ev->cc.controller == 20) {
            /* Master volume from CC slider/knob */
            uint16_t g = (uint16_t)(((uint32_t)ev->cc.value * SYNTH_MAX_GAIN_PERMILLE) / 127);
            synth_set_master_gain(s, g);
        } else if (ev->cc.controller == 71) {
            /* Timbre / Filter Resonance: CC 71 -> Q 0.7 .. 5.0 */
            float q = 0.7f + 4.3f * ((float)ev->cc.value / 127.0f);
            synth_set_resonance(s, q);
        } else if (ev->cc.controller == 72) {
            /* Release Time: 5 .. 3000 ms */
            uint16_t rel = (uint16_t)(5U + ((uint32_t)ev->cc.value * 2995U) / 127U);
            synth_set_adsr(s, s->config.attack_ms, s->config.decay_ms, s->config.sustain_ratio, rel);
        } else if (ev->cc.controller == 73) {
            /* Attack Time: 1 .. 2000 ms */
            uint16_t att = (uint16_t)(1U + ((uint32_t)ev->cc.value * 1999U) / 127U);
            synth_set_adsr(s, att, s->config.decay_ms, s->config.sustain_ratio, s->config.release_ms);
        } else if (ev->cc.controller == 74) {
            /* Filter Cutoff frequency: CC 74 -> 100 Hz .. 8000 Hz */
            float fc = 100.0f * powf(80.0f, (float)ev->cc.value / 127.0f);
            synth_set_cutoff(s, fc);
        } else if (ev->cc.controller == 75) {
            /* Arpeggiator Rate / Division */
            synth_set_arp_rate(s, (synth_arp_rate_t)(ev->cc.value % SYNTH_ARP_RATE_COUNT));
        } else if (ev->cc.controller == 76) {
            /* LFO Rate: 0.2 Hz .. 15.0 Hz */
            float rate = 0.2f + 14.8f * ((float)ev->cc.value / 127.0f);
            synth_set_lfo(s, s->lfo.enabled, rate, s->lfo.vibrato_semitones);
        } else if (ev->cc.controller == 77) {
            /* LFO Vibrato Depth: 0.0 .. 1.0 semitones */
            float depth = 1.0f * ((float)ev->cc.value / 127.0f);
            synth_set_lfo(s, (depth > 0.01f), s->lfo.freq_hz, depth);
        } else if (ev->cc.controller == 78) {
            /* Arpeggiator Octave Range (1..4) */
            synth_set_arp_octaves(s, 1 + (ev->cc.value % 4));
        } else if (ev->cc.controller == 79) {
            /* Arpeggiator Mode (OFF, UP, DOWN, UP_DOWN, RANDOM, CHORD) */
            synth_set_arp_mode(s, (synth_arp_mode_t)(ev->cc.value % SYNTH_ARP_MODE_COUNT));
        } else if (ev->cc.controller >= 80 && ev->cc.controller <= 83) {
            if (s->looper && ev->cc.value >= 64) {
                looper_track_trigger(s->looper, (uint8_t)(ev->cc.controller - 80), s);
            }
        } else if (ev->cc.controller == 84) {
            if (s->looper && ev->cc.value >= 64) {
                looper_clear_all(s->looper);
            }
        } else if (ev->cc.controller == 91) {
            /* Delay / FX Send: wet 0.0 .. 0.50 */
            float wet = 0.50f * ((float)ev->cc.value / 127.0f);
            uint16_t d_ms = (s->config.delay_ms > 0) ? s->config.delay_ms : 250;
            float fb = (s->delay.feedback > 0.01f) ? s->delay.feedback : 0.35f;
            synth_set_delay(s, (wet > 0.01f), d_ms, fb, wet);
        } else if (ev->cc.controller == 93) {
            /* Chorus Send: wet 0.0 .. 0.50 */
            float wet = 0.50f * ((float)ev->cc.value / 127.0f);
            synth_set_chorus(s, (wet > 0.01f), wet);
        } else if (ev->cc.controller == 120) {
            synth_panic(s);
        } else if (ev->cc.controller == 121) {
            /* Reset All Controllers */
            synth_set_mod_wheel(s, 0);
            s->pitch_bend_value = 0;
            s->pitch_bend_mult = 1.0f;
            if (s->sustain_pedal) {
                s->sustain_pedal = false;
                for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
                    if (s->voices[i].sustain_held) {
                        s->voices[i].sustain_held = false;
                        trigger_voice_release(s, &s->voices[i]);
                    }
                }
            }
            for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
                if (s->voices[i].state != VOICE_STATE_IDLE) {
                    update_voice_frequency(s, &s->voices[i]);
                }
            }
        } else if (ev->cc.controller == 123) {
            /* All Notes Off */
            synth_all_notes_off(s);
            s->arp.held_count = 0;
            s->arp.physical_held_count = 0;
            memset(s->arp.held_physical, 0, sizeof(s->arp.held_physical));
            s->arp.note_active = false;
            s->arp.last_playing_count = 0;
        }
        break;
    case MIDI_EVENT_SYSTEM_RESET:
        synth_panic(s);
        break;
    case MIDI_EVENT_PROGRAM_CHANGE:
        synth_set_preset(s, (synth_preset_t)(ev->program_change.program % SYNTH_PRESET_COUNT));
        break;
    default:
        break;
    }
}

bool synth_is_sounding(const synth_t *s)
{
    if (s->config.master_gain_permille == 0 && s->output_gain <= 0) return false;
    return synth_has_pending_audio(s);
}

bool synth_has_pending_audio(const synth_t *s)
{
    if(s->sampler&&!s->live_sound.current.sound_missing&&s->live_sound.current.engine==SOUND_SF2&&s->sampler->voice_count(s->sampler->context,0))return true;
    if (s->arp.mode != SYNTH_ARP_OFF && s->arp.held_count) return true;
    if (s->looper && looper_is_sounding(s->looper)) return true;
    if (s->chorus.enabled && s->chorus.wet_mix > 0.001f && s->chorus.active_samples) return true;
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state != VOICE_STATE_IDLE || s->voices[i].env_level > 0.0001f) {
            return true;
        }
    }
    if (s->delay.enabled && s->delay.wet_mix > 0.001f && (s->delay.active_samples || fabsf(s->delay.damp_state) > 0.00001f)) {
        return true;
    }
    return false;
}

void synth_get_telemetry(const synth_t *s, synth_telemetry_t *out)
{
    if (!out) return;
    uint8_t active = 0;
    for (int i = 0; i < SYNTH_VOICES_COUNT; ++i) {
        if (s->voices[i].state != VOICE_STATE_IDLE || s->voices[i].env_level > 0.0001f) {
            active++;
        }
    }
    out->last_note = s->last_note;
    out->sounding = synth_is_sounding(s);
    out->active_voices = active;
    out->waveform = s->config.waveform;
    out->preset = s->current_preset;
    out->master_gain_permille = s->config.master_gain_permille;
    out->current_freq_hz = (s->last_note >= 0) ? synth_note_to_freq((uint8_t)s->last_note) * s->pitch_bend_mult : 0.0f;
    float mod_factor = (s->mod_wheel_mult > 0.0f) ? s->mod_wheel_mult : 1.0f;
    out->current_cutoff_hz = s->config.base_cutoff_hz * mod_factor;
    out->resonance = s->config.resonance;
    out->mod_wheel = s->mod_wheel;
    out->delay_active = s->delay.enabled && (s->delay.wet_mix > 0.001f);
    out->chorus_active = s->chorus.enabled && (s->chorus.wet_mix > 0.001f);
    out->lfo_active = s->lfo.enabled;
    out->arp_mode = s->arp.mode;
    out->arp_rate = s->arp.rate;
    out->arp_bpm = s->arp.bpm;
    out->arp_octaves = s->arp.octaves;
    out->arp_latch = s->arp.latch;
    out->filter_type = s->config.filter_type;
    out->note_on_count = s->note_on_count;
    out->note_off_count = s->note_off_count;
    out->voice_steal_count = s->voice_steal_count;
}

static void arp_step(synth_t *s)
{
    for (int i = 0; i < s->arp.last_playing_count; ++i) {
        synth_note_off(s, s->arp.last_playing_notes[i]);
    }
    s->arp.last_playing_count = 0;

    uint8_t count = s->arp.held_count;
    uint8_t oct_range = (s->arp.octaves >= 1 && s->arp.octaves <= 4) ? s->arp.octaves : 1;

    if (s->arp.mode == SYNTH_ARP_CHORD) {
        for (int i = 0; i < count; ++i) {
            int16_t n = (int16_t)s->arp.held_notes[i] + (int16_t)(s->arp.current_octave * 12);
            if (n <= 127) {
                synth_note_on(s, (uint8_t)n, s->arp.held_velocities[i]);
                s->arp.last_playing_notes[s->arp.last_playing_count++] = (uint8_t)n;
            }
        }
        s->arp.current_octave = (s->arp.current_octave + 1) % oct_range;
    } else if (s->arp.mode == SYNTH_ARP_RANDOM) {
        uint8_t r_idx = (uint8_t)(rand() % count);
        uint8_t r_oct = (uint8_t)(rand() % oct_range);
        int16_t n = (int16_t)s->arp.held_notes[r_idx] + (int16_t)(r_oct * 12);
        if (n > 127) n = 127;
        synth_note_on(s, (uint8_t)n, s->arp.held_velocities[r_idx]);
        s->arp.last_playing_notes[s->arp.last_playing_count++] = (uint8_t)n;
    } else {
        if (s->arp.current_step_idx >= count) s->arp.current_step_idx = 0;
        if (s->arp.current_step_idx < 0) s->arp.current_step_idx = count - 1;

        int16_t n = (int16_t)s->arp.held_notes[s->arp.current_step_idx] + (int16_t)(s->arp.current_octave * 12);
        if (n > 127) n = 127;
        synth_note_on(s, (uint8_t)n, s->arp.held_velocities[s->arp.current_step_idx]);
        s->arp.last_playing_notes[s->arp.last_playing_count++] = (uint8_t)n;

        if (s->arp.mode == SYNTH_ARP_UP) {
            s->arp.current_step_idx++;
            if (s->arp.current_step_idx >= count) {
                s->arp.current_step_idx = 0;
                s->arp.current_octave = (s->arp.current_octave + 1) % oct_range;
            }
        } else if (s->arp.mode == SYNTH_ARP_DOWN) {
            s->arp.current_step_idx--;
            if (s->arp.current_step_idx < 0) {
                s->arp.current_step_idx = count - 1;
                s->arp.current_octave--;
                if (s->arp.current_octave < 0) s->arp.current_octave = oct_range - 1;
            }
        } else if (s->arp.mode == SYNTH_ARP_UP_DOWN) {
            if (s->arp.direction_up) {
                s->arp.current_step_idx++;
                if (s->arp.current_step_idx >= count) {
                    s->arp.current_octave++;
                    if (s->arp.current_octave >= oct_range) {
                        s->arp.direction_up = false;
                        s->arp.current_octave = oct_range - 1;
                        s->arp.current_step_idx = count > 1 ? count - 2 : 0;
                    } else {
                        s->arp.current_step_idx = 0;
                    }
                }
            } else {
                s->arp.current_step_idx--;
                if (s->arp.current_step_idx < 0) {
                    s->arp.current_octave--;
                    if (s->arp.current_octave < 0) {
                        s->arp.direction_up = true;
                        s->arp.current_octave = 0;
                        s->arp.current_step_idx = count > 1 ? 1 : 0;
                    } else {
                        s->arp.current_step_idx = count - 1;
                    }
                }
            }
        }
    }
    s->arp.note_active = true;
}

static inline float generate_osc_sample(const synth_config_t *cfg, uint32_t phase, float dt, float pwm_duty_mod)
{
    switch (cfg->waveform) {
    case SYNTH_WAVE_SINE: {
        uint32_t idx = (phase >> 21) & SINE_LUT_MASK;
        uint32_t frac = (phase >> 5) & 0xffff;
        float s0 = s_sine_table[idx];
        float s1 = s_sine_table[(idx + 1) & SINE_LUT_MASK];
        return s0 + (s1 - s0) * ((float)frac / 65536.0f);
    }
    case SYNTH_WAVE_TRIANGLE: {
        float p = (float)phase * (1.0f / 4294967296.0f);
        return 2.0f * fabsf(2.0f * p - 1.0f) - 1.0f;
    }
    case SYNTH_WAVE_SAW: {
        float p = (float)phase * (1.0f / 4294967296.0f);
        return (2.0f * p - 1.0f) - polyblep(p, dt);
    }
    case SYNTH_WAVE_SQUARE: {
        float p = (float)phase * (1.0f / 4294967296.0f);
        float sample = (p < pwm_duty_mod) ? 1.0f : -1.0f;
        sample += polyblep(p, dt);
        float p_fall = p - pwm_duty_mod;
        if (p_fall < 0.0f) p_fall += 1.0f;
        sample -= polyblep(p_fall, dt);
        return sample;
    }
    default:
        return 0.0f;
    }
}

void synth_render_voices(const synth_config_t *cfg, synth_voice_t *voices, unsigned voice_count,
                         synth_lfo_t *lfo, float pitch_bend_mult, uint8_t mod_wheel,
                         float mod_wheel_mult_input, float *out_frames, size_t num_frames)
{
    /* 1. LFO computation: Vibrato, PWM modulation, and Mod Wheel integration */
    float vibrato_mult = 1.0f;
    float lfo_val = 0.0f;
    if (lfo->enabled) {
        lfo_val = lut_sine(lfo->phase);
        lfo->phase += lfo->phase_inc * (float)num_frames;
        if (lfo->phase >= 1.0f) lfo->phase -= floorf(lfo->phase);

        if (lfo->vibrato_semitones > 0.0f) {
            /* Mod wheel blends in expressive vibrato depth */
            float mod_ratio = (float)mod_wheel / 127.0f;
            float vib_semi = lfo_val * lfo->vibrato_semitones * (0.25f + 0.75f * mod_ratio);
            vibrato_mult = powf(2.0f, vib_semi / 12.0f);
        }
    }

    float pwm_duty_mod = (cfg->pwm_duty > 0.01f) ? cfg->pwm_duty : 0.50f;
    if (lfo->enabled && cfg->pwm_lfo_depth > 0.001f) {
        pwm_duty_mod += cfg->pwm_lfo_depth * lut_sine(lfo->phase);
    }
    if (pwm_duty_mod < 0.05f) pwm_duty_mod = 0.05f;
    if (pwm_duty_mod > 0.95f) pwm_duty_mod = 0.95f;

    /* Mod wheel cutoff factor (cached in synth_t) */
    float mod_wheel_mult = (mod_wheel_mult_input > 0.0f) ? mod_wheel_mult_input : 1.0f;

    bool unison_active = (cfg->unison_detune > 0.001f);
    float unison_mult = unison_active ? powf(2.0f, cfg->unison_detune / 12.0f) : 1.0f;

    /* 2. Mix and filter all active voices */
    for (unsigned v_idx = 0; v_idx < voice_count; ++v_idx) {
        synth_voice_t *v = &voices[v_idx];
        if (v->state == VOICE_STATE_IDLE && v->env_level <= 0.0001f) {
            continue;
        }

        /* Portamento / Glide pitch interpolation per segment */
        if (cfg->glide_ms > 0 && v->target_freq_hz > 0.0f &&
            fabsf(v->base_freq_hz - v->target_freq_hz) > 0.01f) {
            float tau_samples = (float)cfg->glide_ms * 0.001f * (float)cfg->sample_rate * 0.33f;
            if (tau_samples < 1.0f) tau_samples = 1.0f;
            float alpha = (float)num_frames / (tau_samples + (float)num_frames);
            v->base_freq_hz += alpha * (v->target_freq_hz - v->base_freq_hz);
        } else if (v->target_freq_hz > 0.0f) {
            v->base_freq_hz = v->target_freq_hz;
        }

        /* Update frequency with vibrato */
        float voice_freq = v->base_freq_hz * pitch_bend_mult * vibrato_mult;
        if (voice_freq < 1.0f) voice_freq = 1.0f;
        if (voice_freq > (float)cfg->sample_rate * 0.48f) {
            voice_freq = (float)cfg->sample_rate * 0.48f;
        }
        double cycles_per_sample = (double)voice_freq / (double)cfg->sample_rate;
        v->phase_inc = (uint32_t)(cycles_per_sample * 4294967296.0);
        v->current_freq_hz = voice_freq;
        float dt = voice_freq / (float)cfg->sample_rate;

        uint32_t unison_inc = 0;
        float unison_dt = dt;
        if (unison_active) {
            float u_freq = voice_freq * unison_mult;
            if (u_freq > (float)cfg->sample_rate * 0.48f) {
                u_freq = (float)cfg->sample_rate * 0.48f;
            }
            unison_inc = (uint32_t)(((double)u_freq / (double)cfg->sample_rate) * 4294967296.0);
            unison_dt = u_freq / (float)cfg->sample_rate;
        }

        /* Calculate dynamic voice filter cutoff with envelope tracking */
        float env_oct = cfg->filter_env_octaves * v->env_level;
        float fc = cfg->base_cutoff_hz * mod_wheel_mult * powf(2.0f, env_oct);
        if (fc < 50.0f) fc = 50.0f;
        if (fc > (float)cfg->sample_rate * 0.45f) fc = (float)cfg->sample_rate * 0.45f;

        /* Cytomic SVF filter coefficients */
        float g = tanf((float)M_PI * fc / (float)cfg->sample_rate);
        float k = 1.0f / cfg->resonance;
        float a1 = 1.0f / (1.0f + g * (g + k));
        float a2 = g * a1;
        float a3 = g * a2;
        synth_filter_type_t ftype = cfg->filter_type;

        for (size_t i = 0; i < num_frames; ++i) {
            /* Advance voice envelope */
            if(v->env_remaining) {
                v->env_level+=v->env_delta;
                if(!--v->env_remaining) {
                    v->env_level=v->env_goal;
                    if(v->state==VOICE_STATE_ATTACK) {
                        v->state=VOICE_STATE_DECAY;
                        uint32_t n=cfg->decay_ms ? (uint32_t)cfg->decay_ms*48 : 1;
                        v->decay_step=(v->env_target-v->sustain_level)/(float)n;
                    } else if(v->state==VOICE_STATE_DECAY) v->state=VOICE_STATE_SUSTAIN;
                    else if(v->state==VOICE_STATE_RELEASE) v->state=VOICE_STATE_IDLE;
                }
            } else if (v->state == VOICE_STATE_ATTACK) {
                v->env_level += v->attack_step;
                if (v->env_level >= v->env_target) {
                    v->env_level = v->env_target;
                    v->state = VOICE_STATE_DECAY;
                }
            } else if (v->state == VOICE_STATE_DECAY) {
                v->env_level -= v->decay_step;
                if (v->env_level <= v->sustain_level) {
                    v->env_level = v->sustain_level;
                    v->state = VOICE_STATE_SUSTAIN;
                }
            } else if (v->state == VOICE_STATE_RELEASE) {
                v->env_level -= v->release_step;
                if (v->env_level <= 0.0001f) {
                    v->env_level = 0.0f;
                    v->state = VOICE_STATE_IDLE;
                }
            }

            if (v->env_level <= 0.0f) {
                continue;
            }

            /* Waveform generator */
            float sample = generate_osc_sample(cfg, v->phase, dt, pwm_duty_mod);
            if (unison_active) {
                float u_sample = generate_osc_sample(cfg, v->unison_phase, unison_dt, pwm_duty_mod);
                v->unison_phase += unison_inc;
                sample = 0.65f * (sample + u_sample);
            }

            /* Sub-Oscillator (Anti-aliased square wave 1 or 2 octaves below) */
            if (cfg->sub_osc_level > 0.001f) {
                uint8_t shift = (cfg->sub_osc_octaves >= 2) ? 2 : 1;
                float sub_dt = dt * (shift == 2 ? 0.25f : 0.5f);
                float sub_p = (float)v->sub_phase * (1.0f / 4294967296.0f);
                v->sub_phase += v->phase_inc >> shift;
                float sub_sample = (sub_p < 0.5f) ? 1.0f : -1.0f;
                sub_sample += polyblep(sub_p, sub_dt);
                float sub_p2 = sub_p + 0.5f;
                if (sub_p2 >= 1.0f) sub_p2 -= 1.0f;
                sub_sample -= polyblep(sub_p2, sub_dt);
                sample = sample * (1.0f - 0.35f * cfg->sub_osc_level) + sub_sample * cfg->sub_osc_level;
            }

            /* Noise Generator (White noise) */
            if (cfg->noise_level > 0.001f) {
                float n_val = fast_noise(&v->noise_seed);
                sample += n_val * cfg->noise_level;
            }

            v->phase += v->phase_inc;
            v->age_samples++;

            /* Apply resonant Cytomic SVF filter per voice (LP / BP / HP) */
            float v0 = sample;
            float v1 = a1 * v->filter.s1 + a2 * (v0 - v->filter.s2);
            float v2 = v->filter.s2 + a2 * v->filter.s1 + a3 * (v0 - v->filter.s2);
            v->filter.s1 = 2.0f * v1 - v->filter.s1;
            v->filter.s2 = 2.0f * v2 - v->filter.s2;

            float filtered = v2;
            if (ftype == SYNTH_FILTER_BP) {
                filtered = v1;
            } else if (ftype == SYNTH_FILTER_HP) {
                filtered = v0 - k * v1 - v2;
            }

            out_frames[i] += filtered * v->env_level;
        }
    }

}

static void render_segment(synth_t *s, float *out_frames, size_t num_frames)
{
    unsigned profile_parent = synth_profile_enter(SP_SEGMENT);
    memset(out_frames, 0, num_frames * sizeof(float));

    if(s->sampler&&s->live_sound.current.engine==SOUND_SF2){
        if(!s->live_sound.current.sound_missing&&
           !s->sampler->render(s->sampler->context,0,out_frames,(unsigned)num_frames)){
            s->sampler->panic(s->sampler->context,0);
            s->sampler->unbind(s->sampler->context,0);
            if(s->live_sound.phase!=SOUND_STABLE)s->sampler->complete(s->sampler->context,0,s->live_sound.request_id,true);
            s->live_sound.current.sound_missing=true;
            s->live_sound.phase=SOUND_STABLE;s->live_sound.frames_left=0;
            memset(out_frames,0,num_frames*sizeof(float));
        }
    }else{
        unsigned voice_parent = synth_profile_enter(SP_SYNTH_VOICES);
        synth_render_voices(&s->config,s->voices,SYNTH_VOICES_COUNT,&s->lfo,
                            s->pitch_bend_mult,s->mod_wheel,s->mod_wheel_mult,out_frames,num_frames);
        synth_profile_leave(voice_parent);
    }
    if(s->sampler){
        if(s->live_sound.current.sound_missing)memset(out_frames,0,num_frames*sizeof(float));
        else if(s->live_sound.phase!=SOUND_STABLE)
            for(size_t i=0;i<num_frames;i++)out_frames[i]*=sound_sample_gain(&s->live_sound,(uint32_t)i);
    }
    if (s->looper) {
        looper_render_mix(s->looper, out_frames, num_frames, 0);
    }

    unsigned fx_parent = synth_profile_enter(SP_FX);
    unsigned sat_parent = synth_profile_enter(SP_FX_SAT);
    /* 3. Fixed headroom for the FX feedback path; master gain is applied last. */
    for (size_t i = 0; i < num_frames; ++i) {
        out_frames[i] = tanhf(out_frames[i]) * SYNTH_FX_HEADROOM;
    }

    synth_profile_leave(sat_parent);
    unsigned chorus_parent = synth_profile_enter(SP_FX_CHORUS);
    /* 4. Apply Chorus effect */
    if (s->chorus.enabled && s->chorus.wet_mix > 0.001f) {
        float wet = s->chorus.wet_mix;
        float dry = 1.0f - wet;
        for (size_t i = 0; i < num_frames; ++i) {
            float in_sample = out_frames[i];
            if (fabsf(s->chorus.buffer[s->chorus.write_idx]) > 0.00001f) --s->chorus.active_samples;
            s->chorus.buffer[s->chorus.write_idx] = in_sample;
            if (fabsf(in_sample) > 0.00001f) ++s->chorus.active_samples;

            /* LFO modulated read tap with linear interpolation */
            s->chorus.lfo_phase += s->chorus.lfo_phase_inc;
            if (s->chorus.lfo_phase >= 1.0f) s->chorus.lfo_phase -= 1.0f;
            float lfo_c = lut_sine(s->chorus.lfo_phase);
            float cur_delay = s->chorus.base_delay_samples + s->chorus.depth_samples * lfo_c;

            float r_pos = (float)s->chorus.write_idx - cur_delay;
            while (r_pos < 0.0f) r_pos += (float)SYNTH_CHORUS_MAX_SAMPLES;
            uint32_t i0 = (uint32_t)r_pos % SYNTH_CHORUS_MAX_SAMPLES;
            uint32_t i1 = (i0 + 1) % SYNTH_CHORUS_MAX_SAMPLES;
            float frac = r_pos - floorf(r_pos);
            float delayed = s->chorus.buffer[i0] * (1.0f - frac) + s->chorus.buffer[i1] * frac;

            s->chorus.write_idx = (s->chorus.write_idx + 1) % SYNTH_CHORUS_MAX_SAMPLES;
            out_frames[i] = dry * in_sample + wet * delayed;
        }
    }

    synth_profile_leave(chorus_parent);
    unsigned delay_parent = synth_profile_enter(SP_FX_DELAY);
    /* 5. Apply Tape Delay effect with damping and saturation */
    if (s->delay.enabled && s->delay.wet_mix > 0.001f && s->delay.delay_samples > 0) {
        float wet = s->delay.wet_mix;
        float dry = 1.0f - wet;
        uint32_t ds = s->delay.delay_samples;
        float fb = s->delay.feedback;
        float damp = s->delay.dampening;

        for (size_t i = 0; i < num_frames; ++i) {
            float in_sample = out_frames[i];
            uint32_t r_idx = (s->delay.write_idx + SYNTH_DELAY_MAX_SAMPLES - ds) % SYNTH_DELAY_MAX_SAMPLES;
            float delayed_out = s->delay.buffer[r_idx];

            /* Damping low-pass filter in feedback loop */
            s->delay.damp_state += (1.0f - damp) * (delayed_out - s->delay.damp_state);

            /* Soft-clipped feedback write */
            float to_buffer = in_sample + fb * s->delay.damp_state;
            if (to_buffer > 1.0f) to_buffer = 1.0f;
            else if (to_buffer < -1.0f) to_buffer = -1.0f;

            if (fabsf(s->delay.buffer[s->delay.write_idx]) > 0.00001f) --s->delay.active_samples;
            s->delay.buffer[s->delay.write_idx] = to_buffer;
            if (fabsf(to_buffer) > 0.00001f) ++s->delay.active_samples;
            s->delay.write_idx = (s->delay.write_idx + 1) % SYNTH_DELAY_MAX_SAMPLES;

            out_frames[i] = dry * in_sample + wet * delayed_out;
        }
    }

    synth_profile_leave(delay_parent);
    unsigned gain_parent = synth_profile_enter(SP_FX_GAIN);
    float target = (float)s->config.master_gain_permille / SYNTH_MAX_GAIN_PERMILLE;
    const float ramp_step = 1.0f / (SYNTH_SAMPLE_RATE / 200); /* 5 ms full-scale ramp */
    for (size_t i = 0; i < num_frames; ++i) {
        float delta = target - s->output_gain;
        if (fabsf(delta) <= ramp_step) s->output_gain = target;
        else s->output_gain += delta > 0 ? ramp_step : -ramp_step;
        /* Preserve a nonfinite fault for the HAL's silence/panic/error latch.
         * fminf/fmaxf alone would convert NaN to a positive full-scale sample. */
        /* Recover 20 dB of bring-up attenuation after FX; keep feedback unchanged. */
        if (isfinite(out_frames[i]))
#ifdef NEKTAR_FINITE_CLAMP
            out_frames[i] = synth_finite_clamp(out_frames[i], -SYNTH_FX_HEADROOM, SYNTH_FX_HEADROOM)
#else
            out_frames[i] = fmaxf(-SYNTH_FX_HEADROOM, fminf(SYNTH_FX_HEADROOM, out_frames[i]))
#endif
                * (SYNTH_OUTPUT_PEAK / SYNTH_FX_HEADROOM) * s->output_gain;
    }
    s->rendered_frames += num_frames;
    synth_profile_leave(gain_parent);
    synth_profile_leave(fx_parent);
    synth_profile_leave(profile_parent);
}

void synth_render(synth_t *s, float *out_frames, size_t num_frames)
{
    unsigned profile_parent = synth_profile_enter(SP_SYNTH_MISC);
    size_t offset = 0;
    while (offset < num_frames) {
        size_t count = num_frames - offset;
        bool arp = s->arp.mode != SYNTH_ARP_OFF && s->arp.held_count > 0;
        if (arp) {
            if (s->arp.sample_counter >= s->arp.step_samples) {
                s->arp.sample_counter %= s->arp.step_samples;
                arp_step(s);
            }
            uint32_t gate = (uint32_t)lroundf(s->arp.step_samples * s->arp.gate_ratio);
            if (s->arp.note_active && s->arp.sample_counter >= gate) {
                for (unsigned i = 0; i < s->arp.last_playing_count; ++i)
                    synth_note_off(s, s->arp.last_playing_notes[i]);
                s->arp.last_playing_count = 0;
                s->arp.note_active = false;
            }
            uint32_t boundary = s->arp.note_active && s->arp.sample_counter < gate ? gate : s->arp.step_samples;
            uint32_t until_boundary = boundary - s->arp.sample_counter;
            if (count > until_boundary) count = until_boundary;
        }
        uint32_t loop_boundary=looper_frames_to_boundary(s->looper);
        if(count>loop_boundary) count=loop_boundary;
        if(s->sampler){
            if(count>128)count=128; /* sampler bounded scratch/render contract */
            uint32_t sound_boundary=sound_segment_boundary(&s->live_sound,(uint32_t)count);
            if(count>sound_boundary)count=sound_boundary;
        }
        if(count==0){memset(out_frames+offset,0,(num_frames-offset)*sizeof(float));synth_profile_leave(profile_parent);return;}
        synth_profile_segment((unsigned)count);
        if (synth_block_trace.active) {
            ++synth_block_trace.segments;
            synth_block_trace.segment_frames += (unsigned)count;
        }
        render_segment(s, out_frames + offset, count);
        if(s->sampler&&s->live_sound.phase!=SOUND_STABLE){
            sound_step_t step=sound_advance_rt(&s->live_sound,(uint32_t)count);
            if(step==SOUND_COMPLETED||step==SOUND_COMMIT_FAULT)
                s->sampler->complete(s->sampler->context,0,s->live_sound.request_id,step==SOUND_COMMIT_FAULT);
        }
        if (arp) s->arp.sample_counter += count;
        offset += count;
    }
    synth_profile_leave(profile_parent);
}

const synth_config_t *synth_get_preset_config(synth_preset_t preset)
{
    if ((int)preset >= 0 && preset < SYNTH_PRESET_COUNT) {
        return &s_presets[preset];
    }
    return &s_presets[0];
}

void synth_get_config(const synth_t *s, synth_config_t *out_cfg)
{
    if (s && out_cfg) {
        *out_cfg = s->config;
    }
}

void synth_apply_config(synth_t *s, const synth_config_t *cfg)
{
    if (!s || !cfg) return;
    synth_config_t normalized = *cfg;
    synth_normalize_config(&normalized);
    cfg = &normalized;
    synth_config_t previous=s->config;
    synth_set_midi_channel(s,cfg->midi_channel);
    s->config = *cfg;
    synth_retarget_voices(s->voices,SYNTH_VOICES_COUNT,&previous,cfg);
    synth_set_waveform(s, cfg->waveform);
    synth_set_master_gain(s, cfg->master_gain_permille);
    synth_set_cutoff(s, cfg->base_cutoff_hz);
    synth_set_resonance(s, cfg->resonance);
    synth_set_filter_type(s, cfg->filter_type);
    synth_set_adsr(s, cfg->attack_ms, cfg->decay_ms, cfg->sustain_ratio, cfg->release_ms);
    synth_set_filter_env(s, cfg->filter_env_octaves);
    synth_set_delay(s, cfg->delay_enable, cfg->delay_ms, cfg->delay_feedback, cfg->delay_wet);
    synth_set_chorus(s, cfg->chorus_enable, cfg->chorus_wet);
    synth_set_lfo(s, cfg->lfo_enable, cfg->lfo_freq_hz, cfg->lfo_vibrato_depth);
    synth_set_arp_mode(s, cfg->arp_mode);
    synth_set_arp_bpm(s, cfg->arp_bpm);
    synth_set_arp_octaves(s, cfg->arp_octaves);
    synth_set_arp_rate(s, cfg->arp_rate);
    synth_set_arp_latch(s, cfg->arp_latch);
    synth_set_pwm(s, cfg->pwm_duty, cfg->pwm_lfo_depth);
    synth_set_sub_osc(s, cfg->sub_osc_level, cfg->sub_osc_octaves);
    synth_set_noise(s, cfg->noise_level);
    synth_set_pitch_bend_range(s, cfg->pitch_bend_range);
    synth_set_glide(s, cfg->glide_ms);
    synth_set_unison(s, cfg->unison_detune);
    synth_set_velocity_curve(s, cfg->velocity_curve);
    synth_set_midi_channel(s, cfg->midi_channel);
    update_mod_wheel_mult(s);
    /* A config load may also change release/arp. Keep channel-transition
     * release bounded even after those setters retarget the old voices. */
    if (previous.midi_channel != cfg->midi_channel) {
        for (unsigned i = 0; i < SYNTH_VOICES_COUNT; ++i) {
            synth_voice_t *v = &s->voices[i];
            if (v->state != VOICE_STATE_IDLE) {
                v->env_remaining = 0;
                v->state = VOICE_STATE_RELEASE;
                v->release_step = v->env_level / 240.0f;
            }
        }
    }
}

void synth_set_pwm(synth_t *s, float duty, float lfo_depth)
{
    if (!s || !isfinite(duty) || !isfinite(lfo_depth)) return;
    if (duty < 0.05f) duty = 0.05f;
    if (duty > 0.95f) duty = 0.95f;
    if (lfo_depth < 0.0f) lfo_depth = 0.0f;
    if (lfo_depth > 0.45f) lfo_depth = 0.45f;
    s->config.pwm_duty = duty;
    s->config.pwm_lfo_depth = lfo_depth;
}

void synth_set_sub_osc(synth_t *s, float level, uint8_t octaves)
{
    if (!s || !isfinite(level)) return;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    if (octaves < 1) octaves = 1;
    if (octaves > 2) octaves = 2;
    s->config.sub_osc_level = level;
    s->config.sub_osc_octaves = octaves;
}

void synth_set_noise(synth_t *s, float level)
{
    if (!s || !isfinite(level)) return;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    s->config.noise_level = level;
}
