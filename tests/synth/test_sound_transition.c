#include "synth_sound.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned validates, panics, commits, rendered, committed_at;
    uint8_t part;
    sound_result_t validation;
    bool commit_ok;
    sound_engine_t old_engine;
    sound_ref_t selected;
} mock_t;

static sound_result_t validate(void *ctx, uint8_t part, const sound_ref_t *r)
{
    mock_t *m = ctx;
    ++m->validates;
    assert(part == m->part);
    /* Explicit catalog: bank2/program7 only; no implicit preset0 fallback. */
    if (r->engine == SOUND_SF2 && (r->bank != 2 || r->program != 7))
        return SOUND_INVALID;
    return m->validation;
}
static void panic_part(void *ctx, uint8_t part, const sound_ref_t *r)
{
    mock_t *m = ctx;
    assert(part == m->part && r->engine == m->old_engine);
    ++m->panics;
}
static bool commit(void *ctx, uint8_t part, const sound_ref_t *r)
{
    mock_t *m = ctx;
    assert(part == m->part && m->panics == m->commits + 1);
    m->committed_at = m->rendered;
    ++m->commits;
    m->selected = *r;
    return m->commit_ok;
}
static const sound_ref_t synth = {.engine = SOUND_SYNTH, .synth_preset = 4};
static const sound_ref_t sf = {.engine = SOUND_SF2, .bank_generation = 9,
    .bank = 2, .program = 7, .synth_preset = 4};

static sound_part_t fresh(mock_t *m, uint8_t part, uint32_t epoch)
{
    *m = (mock_t){.part = part, .validation = SOUND_OK, .commit_ok = true};
    sound_transition_ops_t ops = {validate, panic_part, commit, m};
    sound_part_t p;
    assert(sound_part_init(&p, part, &synth, epoch, &ops));
    return p;
}

static void unchanged(sound_part_t *p, uint32_t epoch, uint64_t request,
                      const sound_ref_t *ref, sound_result_t expected)
{
    sound_part_t before = *p;
    assert(sound_select_rt(p, epoch, request, ref) == expected);
    assert(memcmp(&before, p, sizeof before) == 0);
}

static void segmentation(unsigned chunk, float out[480])
{
    mock_t m;
    sound_part_t p = fresh(&m, 3, 42);
    sound_ref_t requested = sf;
    assert(sound_select_rt(&p, 42, 123, &requested) == SOUND_OK);
    requested.program = 99; /* caller's subsequent mutation cannot change pending */
    assert(p.input_epoch == 43 && p.current.engine == SOUND_SYNTH);
    unsigned done = 0, completed = 0;
    while (done < 480) {
        unsigned n = sound_segment_boundary(&p, chunk);
        assert(n > 0 && n <= chunk);
        assert(!sound_note_on_allowed(&p, 43));
        assert(!sound_note_off_allowed(&p, 42));
        assert(!sound_note_off_allowed(&p, 43));
        unchanged(&p, 43, 99, &sf, SOUND_BUSY);
        for (unsigned i = 0; i < n; ++i) {
            /* Renderer identity cannot swap before sample240 has been emitted. */
            assert((done + i < 240) == (p.current.engine == SOUND_SYNTH));
            out[done + i] = sound_sample_gain(&p, i);
        }
        m.rendered += n;
        done += n;
        sound_step_t step = sound_advance_rt(&p, n);
        if (done == 240) assert(step == SOUND_COMMITTED);
        else if (done == 480) { assert(step == SOUND_COMPLETED); ++completed; }
        else assert(step == SOUND_ADVANCED);
    }
    assert(m.panics == 1 && m.commits == 1 && m.committed_at == 240);
    assert(m.selected.program == 7 && completed == 1 && p.request_id == 123);
    assert(sound_ref_equal(&p.current, &sf));
    assert(sound_note_on_allowed(&p, 43));
    assert(sound_note_off_allowed(&p, 43));
    assert(!sound_note_off_allowed(&p, 42));
    assert(sound_sample_gain(&p, 0) == 1.0f);
    assert(sound_segment_boundary(&p, 128) == 128);
}

static void rejects(void)
{
    mock_t m;
    sound_part_t p = fresh(&m, 0, 2);
    unchanged(&p, 2, 1, &synth, SOUND_NOOP);
    assert(m.validates == 0 && m.panics == 0);
    unchanged(&p, 1, 1, &sf, SOUND_STALE);
    unchanged(&p, 2, 0, &sf, SOUND_INVALID);
    sound_ref_t bad = sf;
    bad.program = 128;
    unchanged(&p, 2, 1, &bad, SOUND_INVALID);
    bad = sf; bad.bank_generation = 0;
    unchanged(&p, 2, 1, &bad, SOUND_INVALID);
    bad = sf; bad.program = 0;
    unchanged(&p, 2, 1, &bad, SOUND_INVALID);
    m.validation = SOUND_STALE;
    unchanged(&p, 2, 1, &sf, SOUND_STALE);
    m.validation = SOUND_BUSY;
    unchanged(&p, 2, 1, &sf, SOUND_BUSY);
    p.input_epoch = UINT32_MAX;
    unchanged(&p, UINT32_MAX, 1, &sf, SOUND_EPOCH_LIMIT);
    unchanged(&p, UINT32_MAX, 1, &synth, SOUND_NOOP);
    assert(m.panics == 0 && m.commits == 0);
    assert(sound_advance_rt(&p, 0) == SOUND_BAD_SEGMENT);
}

static void failure_and_isolation(void)
{
    mock_t a, b;
    sound_part_t p = fresh(&a, 4, UINT32_MAX - 1);
    sound_part_t other = fresh(&b, 1, 77), before = other;
    a.commit_ok = false;
    assert(sound_select_rt(&p, UINT32_MAX - 1, 8, &sf) == SOUND_OK);
    assert(p.input_epoch == UINT32_MAX);
    sound_part_t copy = p;
    assert(sound_advance_rt(&p, 241) == SOUND_BAD_SEGMENT);
    assert(memcmp(&copy, &p, sizeof p) == 0);
    a.rendered = 240;
    assert(sound_advance_rt(&p, 240) == SOUND_COMMIT_FAULT);
    assert(p.current.sound_missing && p.phase == SOUND_STABLE);
    assert(sound_sample_gain(&p, 0) == 0.0f);
    assert(!sound_note_on_allowed(&p, UINT32_MAX));
    assert(memcmp(&before, &other, sizeof other) == 0);
    assert(b.panics == 0 && b.commits == 0 && other.input_epoch == 77);
    unchanged(&p, UINT32_MAX, 9, &synth, SOUND_EPOCH_LIMIT);
    /* A null/uninitialized object cannot produce a render segment. */
    sound_part_t zero = {0};
    assert(sound_segment_boundary(&zero, 128) == 0);
    assert(sound_sample_gain(&zero, 0) == 0.0f);
    assert(!sound_note_on_allowed(&zero, 0));
    assert(sound_advance_rt(&zero, 1) == SOUND_BAD_SEGMENT);
}

static void reverse_transition(void)
{
    mock_t m;
    sound_part_t p = fresh(&m, 2, 10);
    p.current = sf; /* known-valid binding normally installed by audio adapter */
    p.pending = sf;
    m.old_engine = SOUND_SF2;
    assert(sound_select_rt(&p, 10, 5, &synth) == SOUND_OK);
    assert(sound_note_on_allowed(&p, 11) == false);
    m.rendered = 239;
    assert(sound_advance_rt(&p, 239) == SOUND_ADVANCED);
    assert(p.current.engine == SOUND_SF2 && m.panics == 0);
    m.rendered = 240;
    assert(sound_advance_rt(&p, 1) == SOUND_COMMITTED);
    assert(sound_ref_equal(&p.current, &synth));
    assert(m.panics == 1 && m.commits == 1);
    assert(sound_advance_rt(&p, 240) == SOUND_COMPLETED);
    assert(sound_note_on_allowed(&p, 11));
    unchanged(&p, 11, 6, &synth, SOUND_NOOP);
}

int main(void)
{
    float one[480], sixtyfour[480], block[480];
    segmentation(1, one);
    segmentation(64, sixtyfour);
    segmentation(128, block);
    assert(memcmp(one, sixtyfour, sizeof one) == 0);
    assert(memcmp(one, block, sizeof one) == 0);
    assert(one[239] == 0.0f && one[479] == 1.0f);
    for (unsigned i = 1; i < 240; ++i) assert(one[i] < one[i - 1]);
    for (unsigned i = 241; i < 480; ++i) assert(one[i] > one[i - 1]);
    rejects();
    failure_and_isolation();
    reverse_transition();
    puts("sound_transition: PASS (pure host core; no audio/ingress integration)");
    return 0;
}
