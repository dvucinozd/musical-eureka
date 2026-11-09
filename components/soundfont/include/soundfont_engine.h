#ifndef NEKTAR_SOUNDFONT_ENGINE_H
#define NEKTAR_SOUNDFONT_ENGINE_H
#include "sf2_validate.h"
typedef enum { SF_MEM_SAMPLES, SF_MEM_METADATA, SF_MEM_VOICES, SF_MEM_CHANNELS, SF_MEM_INSTANCE, SF_MEM_COUNT } sf_mem_kind;
/* Caller chooses capability placement for each kind. Production app uses PSRAM;
   the explicit experimental app mode moves only voice pools internally.
   Allocator callbacks/context must outlive bank. */
typedef struct {
    void *context;
    void *(*alloc)(void *, sf_mem_kind, size_t);
    void (*free)(void *, sf_mem_kind, void *);
} sf_allocator;
typedef struct sf_bank sf_bank;
typedef struct { uint32_t voice_active, voice_peak, sample_layers, note_rejected; } sf_part_stats;
/* Control owner only. Immutable data leased throughout prepare. No SD/HTTP here. */
sf_bank *sf_bank_prepare(const void *, size_t, bool, const sf_allocator *, sf2_info_t *);
void sf_bank_destroy(sf_bank *);
unsigned sf_bank_preset_count(const sf_bank *);
bool sf_bank_preset(const sf_bank *, unsigned, unsigned *bank, unsigned *program, char name[21]);
/* Single audio owner only. Five preallocated parts, limits 16/4/4/4/4.
   All calls bounded; no allocation/free/I/O. Render at most 128 mono frames.
   Event owner must limit bursts to 64 commands per audio block. */
bool sf_part_set_program(sf_bank *, unsigned part, unsigned bank, unsigned program);
bool sf_part_note_on(sf_bank *, unsigned part, unsigned key, unsigned velocity);
void sf_part_note_off(sf_bank *, unsigned part, unsigned key);
bool sf_part_control(sf_bank *, unsigned part, unsigned cc, unsigned value);
bool sf_part_pitch(sf_bank *, unsigned part, unsigned value);
void sf_part_panic_rt(sf_bank *, unsigned part);
void sf_part_reset_controllers_rt(sf_bank *, unsigned part);
bool sf_part_render(sf_bank *, unsigned part, float *, unsigned frames);
sf_part_stats sf_part_get_stats(const sf_bank *, unsigned part);
#endif
