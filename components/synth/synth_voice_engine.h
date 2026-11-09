#pragma once
#include "synth.h"
/* Shared dry voice engine; caller owns allocation, transport and global FX. */
void synth_render_voices(const synth_config_t *, synth_voice_t *, unsigned, synth_lfo_t *, float, uint8_t, float, float *, size_t);
void synth_retarget_voices(synth_voice_t *, unsigned, const synth_config_t *, const synth_config_t *);

void synth_voice_start(synth_voice_t *, const synth_config_t *, int16_t, uint8_t, uint8_t, float);
