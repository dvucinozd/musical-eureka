#ifndef SYNTH_SOUND_H
#define SYNTH_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#define SOUND_PART_COUNT 5u
#define SOUND_RAMP_FRAMES 240u /* exactly 5 ms at 48000 Hz */

typedef enum { SOUND_SYNTH, SOUND_SF2 } sound_engine_t;
/* Value identity only: never owns a bank pointer. Caller copies, never persists
 * this runtime struct as a raw blob. synth_preset is an explicit fallback ID. */
typedef struct {
    sound_engine_t engine;
    uint64_t bank_generation;
    uint16_t bank;
    uint8_t program;
    uint8_t synth_preset;
    bool sound_missing;
} sound_ref_t;

typedef enum { SOUND_STABLE, SOUND_RAMP_DOWN, SOUND_RAMP_UP } sound_phase_t;
typedef enum {
    SOUND_OK, SOUND_NOOP, SOUND_INVALID, SOUND_STALE, SOUND_BUSY,
    SOUND_EPOCH_LIMIT, SOUND_FAULT
} sound_result_t;
typedef enum { SOUND_ADVANCED, SOUND_COMMITTED, SOUND_COMPLETED,
               SOUND_COMMIT_FAULT, SOUND_BAD_SEGMENT } sound_step_t;

/* All calls belong to ONE audio owner. No synchronization is provided here.
 * Callbacks must be bounded, non-reentrant, no alloc/free/log/I/O/wait.
 * validate is a read-only preflight (program/catalog/generation/busy checks).
 * On success the adapter pins identity/catalog until COMPLETED or COMMIT_FAULT;
 * lifecycle attach/unload must reject while this reservation is held.
 * panic clears ONLY this part's old voices/controllers; commit binds/selects
 * exactly requested program and resets ONLY this part's new controllers.
 * A false commit must leave no sounding new voices or dangling binding.
 * The owner releases the external reservation after observing terminal step.
 */
typedef struct {
    sound_result_t (*validate)(void *, uint8_t, const sound_ref_t *);
    void (*panic)(void *, uint8_t, const sound_ref_t *);
    bool (*commit)(void *, uint8_t, const sound_ref_t *);
    void *context;
} sound_transition_ops_t;

typedef struct {
    sound_ref_t current, pending;
    uint32_t input_epoch;
    uint64_t request_id;
    uint16_t frames_left;
    uint8_t part;
    sound_phase_t phase;
    bool initialized;
    sound_transition_ops_t ops;
} sound_part_t;

/* init requires a known-valid current identity; it does not bind a renderer. */
bool sound_part_init(sound_part_t *, uint8_t part, const sound_ref_t *,
                     uint32_t input_epoch, const sound_transition_ops_t *);
bool sound_ref_equal(const sound_ref_t *, const sound_ref_t *);
sound_result_t sound_select_rt(sound_part_t *, uint32_t expected_epoch,
                               uint64_t request_id, const sound_ref_t *);

/* Render at most boundary(requested) frames with current renderer, applying
 * sample_gain(offset), then advance EXACTLY those rendered frames. advance may
 * panic/commit at the end of the segment; never call it before rendering.
 * Down gains are 239/240 ... 0; up gains are 1/240 ... 1. Endpoints therefore
 * take exactly 240 samples each, independent of block segmentation.
 * Stable gain1 can be bypassed to preserve synth PCM arithmetic.
 */
uint32_t sound_segment_boundary(const sound_part_t *, uint32_t requested);
float sound_sample_gain(const sound_part_t *, uint32_t offset);
sound_step_t sound_advance_rt(sound_part_t *, uint32_t rendered_frames);

/* Admission only, NOT note ownership. No pending-note queue exists.
 * Note On is rejected during either ramp. A Note Off must also match an
 * ingress-owned press/lease claim before this guard can authorize dispatch.
 * Old claims must become tombstones at acceptance (new epoch); consume their
 * later offs without dispatch. MIDI1 has no press ID: quarantine same physical
 * source/channel/key until its old off; never stamp a queued old event with the
 * new epoch. Web refresh needs original epoch + press token. Clearing claims
 * alone is unsafe. This core deliberately does not implement ingress claims.
 */
bool sound_note_on_allowed(const sound_part_t *, uint32_t captured_epoch);
bool sound_note_off_allowed(const sound_part_t *, uint32_t claim_epoch);

#endif
