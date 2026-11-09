#pragma once

#include "audio_storage.h"

bool audio_sampler_init(synth_t *synth);
sound_result_t audio_sampler_select(synth_t *synth, const sound_ref_t *sound);
bool audio_sampler_resume(synth_t *synth, const audio_restore_t *restore);
void audio_sampler_begin_block(void);
void audio_sampler_end_block(void);
