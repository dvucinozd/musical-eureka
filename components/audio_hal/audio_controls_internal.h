#pragma once
#include "audio_controls.h"

esp_err_t audio_controls_init(void);
bool audio_enable_live_control(void);
bool audio_controls_drain(synth_t *s, int64_t now);
void audio_controls_publish(const synth_t *s);
bool audio_controls_pending(void);
void audio_controls_discard(synth_t *s);
