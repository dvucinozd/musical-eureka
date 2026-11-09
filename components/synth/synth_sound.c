#include "synth_sound.h"
#include <limits.h>

static bool ref_valid(const sound_ref_t *r)
{
    return r && !r->sound_missing && r->synth_preset < 32u &&
        ((r->engine == SOUND_SYNTH && r->bank_generation == 0u &&
          r->bank == 0u && r->program == 0u) ||
         (r->engine == SOUND_SF2 && r->bank_generation != 0u &&
          r->bank <= 16383u && r->program <= 127u));
}

bool sound_ref_equal(const sound_ref_t *a, const sound_ref_t *b)
{
    return a && b && a->engine == b->engine &&
        a->bank_generation == b->bank_generation && a->bank == b->bank &&
        a->program == b->program && a->synth_preset == b->synth_preset &&
        a->sound_missing == b->sound_missing;
}

bool sound_part_init(sound_part_t *p, uint8_t part, const sound_ref_t *initial,
                     uint32_t epoch, const sound_transition_ops_t *ops)
{
    if (!p || part >= SOUND_PART_COUNT || !ref_valid(initial) || !ops ||
        !ops->validate || !ops->panic || !ops->commit) return false;
    sound_part_t fresh = {0};
    fresh.current = *initial;
    fresh.pending = *initial;
    fresh.input_epoch = epoch;
    fresh.part = part;
    fresh.phase = SOUND_STABLE;
    fresh.initialized = true;
    fresh.ops = *ops;
    *p = fresh;
    return true;
}

sound_result_t sound_select_rt(sound_part_t *p, uint32_t epoch,
                               uint64_t request, const sound_ref_t *requested)
{
    if (!p || !p->initialized || !ref_valid(requested) || request == 0u)
        return SOUND_INVALID;
    if (epoch != p->input_epoch) return SOUND_STALE;
    if (p->phase != SOUND_STABLE) return SOUND_BUSY;
    if (sound_ref_equal(&p->current, requested)) return SOUND_NOOP;
    if (p->input_epoch == UINT32_MAX) return SOUND_EPOCH_LIMIT;
    sound_result_t checked = p->ops.validate(p->ops.context, p->part, requested);
    if (checked != SOUND_OK) return checked == SOUND_NOOP ? SOUND_INVALID : checked;
    p->pending = *requested;
    p->request_id = request;
    ++p->input_epoch;
    p->phase = SOUND_RAMP_DOWN;
    p->frames_left = SOUND_RAMP_FRAMES;
    return SOUND_OK;
}

uint32_t sound_segment_boundary(const sound_part_t *p, uint32_t requested)
{
    if (!p || !p->initialized) return 0u;
    return p->phase != SOUND_STABLE && requested > p->frames_left
        ? p->frames_left : requested;
}

float sound_sample_gain(const sound_part_t *p, uint32_t offset)
{
    if (!p || !p->initialized || p->current.sound_missing) return 0.0f;
    if (p->phase == SOUND_STABLE) return 1.0f;
    if (offset >= p->frames_left) return 0.0f;
    uint32_t remaining = p->frames_left - offset - 1u;
    return p->phase == SOUND_RAMP_DOWN
        ? (float)remaining / (float)SOUND_RAMP_FRAMES
        : (float)(SOUND_RAMP_FRAMES - remaining) / (float)SOUND_RAMP_FRAMES;
}

sound_step_t sound_advance_rt(sound_part_t *p, uint32_t frames)
{
    if (!p || !p->initialized || frames == 0u ||
        (p->phase != SOUND_STABLE && frames > p->frames_left))
        return SOUND_BAD_SEGMENT;
    if (p->phase == SOUND_STABLE) return SOUND_ADVANCED;
    p->frames_left = (uint16_t)(p->frames_left - frames);
    if (p->frames_left != 0u) return SOUND_ADVANCED;
    if (p->phase == SOUND_RAMP_DOWN) {
        p->ops.panic(p->ops.context, p->part, &p->current);
        if (!p->ops.commit(p->ops.context, p->part, &p->pending)) {
            p->current = p->pending;
            p->current.sound_missing = true;
            p->phase = SOUND_STABLE;
            return SOUND_COMMIT_FAULT;
        }
        p->current = p->pending;
        p->phase = SOUND_RAMP_UP;
        p->frames_left = SOUND_RAMP_FRAMES;
        return SOUND_COMMITTED;
    }
    p->phase = SOUND_STABLE;
    return SOUND_COMPLETED;
}

bool sound_note_on_allowed(const sound_part_t *p, uint32_t epoch)
{
    return p && p->initialized && p->phase == SOUND_STABLE &&
        !p->current.sound_missing && epoch == p->input_epoch;
}

bool sound_note_off_allowed(const sound_part_t *p, uint32_t epoch)
{
    return sound_note_on_allowed(p, epoch);
}
