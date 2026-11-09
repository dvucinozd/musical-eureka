#pragma once

#include "audio_perf.h"

/* Audio owner. Publish is called only after record returns true. */
void audio_perf_begin_block(int64_t start_us, uint32_t dma_overflows, const sound_ref_t *sound);
bool audio_perf_record(uint32_t prepare_us, uint32_t write_us, int64_t end_us,
                       uint32_t dma_overflows, unsigned usb_events, bool web,
                       const sound_ref_t *sound);
void audio_perf_publish(uint32_t stack_high_water);
void audio_perf_abort(audio_perf_result_t reason, int64_t now_us, uint32_t dma_overflows);
