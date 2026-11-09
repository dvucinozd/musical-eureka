#pragma once
#include "audio_hal.h"
#include "synth.h"
#include "synth_looper.h"

typedef enum {
    AUDIO_CMD_PRESS, AUDIO_CMD_RELEASE, AUDIO_CMD_HOLD, AUDIO_CMD_PANIC,
    AUDIO_CMD_PRESET, AUDIO_CMD_GAIN, AUDIO_CMD_WAVE, AUDIO_CMD_CUTOFF, AUDIO_CMD_RESONANCE,
    AUDIO_CMD_FILTER, AUDIO_CMD_ATTACK, AUDIO_CMD_DECAY, AUDIO_CMD_SUSTAIN, AUDIO_CMD_ENV_RELEASE,
    AUDIO_CMD_DELAY, AUDIO_CMD_CHORUS, AUDIO_CMD_LFO, AUDIO_CMD_ARP, AUDIO_CMD_ARP_RATE,
    AUDIO_CMD_ARP_OCTAVES, AUDIO_CMD_ARP_LATCH, AUDIO_CMD_BPM, AUDIO_CMD_LOOP_BARS,
    AUDIO_CMD_LOOP_TRIGGER, AUDIO_CMD_LOOP_STOP, AUDIO_CMD_LOOP_CLEAR, AUDIO_CMD_LOOP_MUTE,
    AUDIO_CMD_LOOP_VOLUME, AUDIO_CMD_LOOP_PRESET,
    AUDIO_CMD_SOUND,
} audio_command_kind_t;

typedef struct {
    audio_command_kind_t kind;
    unsigned index;
    int value;
    uint32_t mask[4];
    uint32_t epoch;
    uint32_t storage_epoch;
    sound_ref_t sound;
} audio_command_t;

typedef struct {
    synth_config_t config;
    synth_preset_t preset;
    looper_status_t looper;
    sound_ref_t sound;
} audio_control_status_t;

bool audio_submit_control(audio_command_t command);
bool audio_get_controls(audio_control_status_t *status);
