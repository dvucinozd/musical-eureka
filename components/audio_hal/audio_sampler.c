#include "audio_sampler.h"

#ifdef NEKTAR_P06_TELEMETRY
#include "synth_block_trace.h"
#define TRACE_EVENT(part, on, off, ok) synth_block_trace_event(&synth_block_trace, part, on, off, ok)
#else
#define TRACE_EVENT(part, on, off, ok) ((void)0)
#endif

static sf_lifecycle s_bank;
static uint64_t s_request, s_selection;
static unsigned s_events, s_release_overflow;

void audio_sampler_begin_block(void)
{
    s_events = 64;
    s_release_overflow = 0;
}

void audio_sampler_end_block(void)
{
    for (unsigned part = 0; s_release_overflow && part < SOUND_PART_COUNT; ++part) {
        const unsigned bit = 1u << part;
        if (s_release_overflow & bit) {
            sf_part_panic_rt(s_bank.part_refs[part], part);
            s_release_overflow &= ~bit;
        }
    }
}

static bool admit_event(void)
{
    if (!s_events) return false;
    --s_events;
    return true;
}

static sound_result_t validate(void *context, unsigned part, const sound_ref_t *sound)
{
    sf_lifecycle *bank = context;
    if (part) return SOUND_INVALID;
    if (sound->engine == SOUND_SYNTH) return SOUND_OK;
    if (!bank->active || sound->bank_generation != bank->active_generation) return SOUND_STALE;
    for (unsigned i = 0; i < sf_bank_preset_count(bank->active); ++i) {
        unsigned id, program;
        char name[21];
        if (sf_bank_preset(bank->active, i, &id, &program, name) &&
            id == sound->bank && program == sound->program) return SOUND_OK;
    }
    return SOUND_INVALID;
}

static bool bind(void *ctx, unsigned part, uint64_t gen)
{
    return sf_lc_audio_bind(ctx, part, gen);
}

static sf_bank *bound(void *ctx, unsigned part)
{
    return ((sf_lifecycle *)ctx)->part_refs[part];
}

static bool program(void *ctx, unsigned part, unsigned bank, unsigned program)
{
    bool ok = sf_part_set_program(bound(ctx, part), part, bank, program);
    TRACE_EVENT(part, false, false, ok);
    return ok;
}

static void panic(void *ctx, unsigned part)
{
    sf_bank *bank = bound(ctx, part);
    sf_part_panic_rt(bank, part);
    if (bank) TRACE_EVENT(part, false, false, true);
}

static void unbind(void *ctx, unsigned part)
{
    panic(ctx, part);
    (void)sf_lc_audio_unbind(ctx, part);
}

static bool note_on(void *ctx, unsigned part, unsigned key, unsigned velocity)
{
    bool ok = admit_event() && sf_part_note_on(bound(ctx, part), part, key, velocity);
    TRACE_EVENT(part, velocity != 0, velocity == 0, ok);
    return ok;
}

static void note_off(void *ctx, unsigned part, unsigned key)
{
    bool admitted = admit_event();
    sf_bank *bank = bound(ctx, part);
    if (admitted) sf_part_note_off(bank, part, key);
    else s_release_overflow |= 1u << part; /* Fail silent rather than strand a voice. */
    TRACE_EVENT(part, false, true, admitted && bank);
}

static void control(void *ctx, unsigned part, unsigned cc, unsigned value)
{
    if (admit_event()) {
        bool ok = sf_part_control(bound(ctx, part), part, cc, value);
        TRACE_EVENT(part, false, false, ok);
        (void)ok;
    } else {
        if (cc == 121 || cc == 123 || (cc == 64 && value < 64)) s_release_overflow |= 1u << part;
        TRACE_EVENT(part, false, false, false);
    }
}

static void pitch(void *ctx, unsigned part, unsigned value)
{
    bool ok = admit_event() && sf_part_pitch(bound(ctx, part), part, value);
    TRACE_EVENT(part, false, false, ok);
    (void)ok;
}

static bool render(void *ctx, unsigned part, float *out, unsigned frames)
{
    sf_bank *bank = bound(ctx, part);
#ifdef NEKTAR_P06_TELEMETRY
    if (synth_block_trace.active) {
        unsigned before = sf_part_get_stats(bank, part).voice_active;
        bool ok = sf_part_render(bank, part, out, frames);
        synth_block_trace_render(&synth_block_trace, part, frames, before,
                                 sf_part_get_stats(bank, part).voice_active, ok);
        return ok;
    }
#endif
    return sf_part_render(bank, part, out, frames);
}

static unsigned voices(void *ctx, unsigned part)
{
    return sf_part_get_stats(bound(ctx, part), part).voice_active;
}

static void complete(void *ctx, unsigned part, uint64_t request, bool fault)
{
    /* Maintenance never admits a swap with a transition in progress. */
    (void)ctx;
    (void)part;
    (void)request;
    (void)fault;
}

bool audio_sampler_init(synth_t *synth)
{
    static const synth_sampler_ops_t ops = {
        &s_bank, validate, bind, program, unbind, note_on, note_off,
        control, pitch, panic, render, voices, complete,
    };
    sf_lc_init(&s_bank, NULL, NULL);
    return synth_sampler_install(synth, &ops);
}

sound_result_t audio_sampler_select(synth_t *synth, const sound_ref_t *sound)
{
    return synth_sound_select(synth, synth_live_epoch(synth), ++s_selection, sound);
}

static bool select_muted(synth_t *synth, const sound_ref_t *sound)
{
    const sound_result_t result = audio_sampler_select(synth, sound);
    if (result == SOUND_NOOP) return true;
    if (result != SOUND_OK) return false;
    /* The PA and DMA are off. Consume the transition's two silent segments. */
    return sound_advance_rt(&synth->live_sound, SOUND_RAMP_FRAMES) == SOUND_COMMITTED &&
           sound_advance_rt(&synth->live_sound, SOUND_RAMP_FRAMES) == SOUND_COMPLETED;
}

bool audio_sampler_resume(synth_t *synth, const audio_restore_t *restore)
{
    if (atomic_load(&s_bank.state) == SF_LC_PENDING) {
        if (s_bank.detach) {
            const sound_ref_t fallback = {.synth_preset = synth->current_preset};
            bool fault = !select_muted(synth, &fallback);
            sf_lc_audio_boundary(&s_bank, false, false, fault, NULL);
            return !fault;
        }
        const sound_ref_t old = synth->live_sound.current;
        if (old.engine == SOUND_SF2) unbind(&s_bank, 0);
        sf_lc_audio_boundary(&s_bank, false, false, false, NULL);
        if (s_bank.ack.result != SF_LC_OK) {
            if (old.engine == SOUND_SF2)
                synth->live_sound.current.sound_missing = !(bind(&s_bank, 0, old.bank_generation) &&
                                                            program(&s_bank, 0, old.bank, old.program));
            return false;
        }
    }
    if (!restore) return true;
    if (!select_muted(synth, &restore->sound)) return false;
    if (restore->restore_config) {
        synth_apply_config(synth, &restore->config);
        looper_set_bars(synth->looper, restore->bars);
    }
    return true;
}

uint64_t audio_storage_stage_bank(sf_bank *bank)
{
    if (!audio_storage_held()) return 0;
    /* Lifecycle generation is strictly increasing. An explicitly unloaded
     * flash bank may be reattached with the same persistent generation. */
    const uint64_t request = ++s_request;
    const uint64_t generation = bank ? request : s_bank.active_generation;
    if (sf_lc_begin(&s_bank, request, generation, !bank) != SF_LC_OK) return 0;
    (void)sf_lc_publish(&s_bank, bank, true);
    return request;
}

bool audio_storage_bank_ack(sf_lc_ack *ack)
{
    return sf_lc_take_ack(&s_bank, ack);
}
