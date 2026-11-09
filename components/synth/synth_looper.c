#include "synth_looper.h"
#include "synth_voice_engine.h"
#include "synth_profile.h"
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void finalize_at(synth_looper_t *l, looper_track_t *trk, uint32_t pos);

static void update_loop_timing(synth_looper_t *l)
{
    uint32_t bpm = (l->bpm >= 40 && l->bpm <= 300) ? l->bpm : 120;
    uint32_t bars = (l->bars >= 1 && l->bars <= 8) ? l->bars : 2;
    uint32_t old_len = l->loop_samples;
    /* 1 bar in 4/4 time = 4 quarter notes = 4 * (60 / bpm) * sample_rate */
    uint32_t new_len = (uint32_t)(((uint64_t)SYNTH_SAMPLE_RATE * 240ULL * bars) / bpm);
    if (new_len < 4800) new_len = 4800;

    if (old_len > 0 && old_len != new_len) {
        l->cursor_samples = (uint32_t)(((uint64_t)l->cursor_samples * new_len) / old_len);
        if (l->cursor_samples >= new_len) l->cursor_samples = 0;
        for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
            looper_track_t *trk = &l->tracks[t];
            for (uint16_t e = 0; e < trk->event_count; ++e) {
                uint32_t scaled = (uint32_t)(((uint64_t)trk->events[e].sample_offset * new_len) / old_len);
                if (trk->events[e].sample_offset == old_len && trk->events[e].type == 0) scaled = new_len;
                else if (scaled >= new_len) scaled = new_len - 1;
                trk->events[e].sample_offset = scaled;
            }
        }
    }
    l->loop_samples = new_len;
}

static void refresh_transport_state(synth_looper_t *l)
{
    bool any_active = false;
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        looper_track_state_t st = l->tracks[t].state;
        if (st == LOOPER_STATE_REC || st == LOOPER_STATE_PLAY || st == LOOPER_STATE_OVERDUB) {
            any_active = true;
            break;
        }
    }
    if (!any_active) {
        l->transport_running = false;
        l->cursor_samples = 0;
    } else if (!l->transport_running) {
        l->transport_running = true;
        l->cursor_samples = 0;
    }
}

static void track_voice_release(const looper_track_t *trk, synth_voice_t *v)
{
    v->env_remaining=0;
    v->state = VOICE_STATE_RELEASE;
    uint32_t rel_frames = (uint32_t)((float)trk->config.release_ms * 0.001f * (float)SYNTH_SAMPLE_RATE);
    if (rel_frames == 0) rel_frames = 1;
    v->release_step = (v->env_level > 0.0f) ? (v->env_level / (float)rel_frames) : 0.01f;
}

static void track_all_notes_off(looper_track_t *trk)
{
    memset(trk->held_notes, 0, sizeof(trk->held_notes));
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].state != VOICE_STATE_IDLE) {
            track_voice_release(trk, &trk->voices[i]);
        }
    }
}

static synth_voice_t *track_allocate_voice(looper_track_t *trk, uint8_t note)
{
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].state != VOICE_STATE_IDLE && trk->voices[i].note == note) {
            return &trk->voices[i];
        }
    }
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].state == VOICE_STATE_IDLE) {
            return &trk->voices[i];
        }
    }
    int best_rel = -1;
    float lowest_amp = 2.0f;
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].state == VOICE_STATE_RELEASE && trk->voices[i].env_level < lowest_amp) {
            lowest_amp = trk->voices[i].env_level;
            best_rel = i;
        }
    }
    if (best_rel >= 0) return &trk->voices[best_rel];

    int oldest = 0;
    uint32_t max_age = 0;
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].age_samples >= max_age) {
            max_age = trk->voices[i].age_samples;
            oldest = i;
        }
    }
    return &trk->voices[oldest];
}

static void track_note_on(looper_track_t *trk, uint8_t note, uint8_t velocity)
{
    if (note > 127) note = 127;
    if (velocity == 0) {
        trk->held_notes[note] = false;
        for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
            if (trk->voices[i].state != VOICE_STATE_IDLE && trk->voices[i].note == note) {
                track_voice_release(trk, &trk->voices[i]);
            }
        }
        return;
    }

    trk->held_notes[note] = true;
    synth_voice_t *v = track_allocate_voice(trk, note);
    synth_voice_start(v,&trk->config,trk->last_note,note,velocity,1);

    trk->last_note = (int16_t)note;
}

static void track_note_off(looper_track_t *trk, uint8_t note)
{
    if (note > 127) note = 127;
    trk->held_notes[note] = false;
    for (int i = 0; i < LOOPER_VOICES_PER_TRACK; ++i) {
        if (trk->voices[i].state != VOICE_STATE_IDLE && trk->voices[i].note == note) {
            track_voice_release(trk, &trk->voices[i]);
        }
    }
}

/* SoundFont tracks: sampler part track+1, bounded audio-owned callbacks. The
 * synth voice path above stays untouched; callers branch on track_sf(). */
static bool track_sf(const looper_track_t *trk) { return trk->sound.engine == SOUND_SF2; }
static unsigned track_part(const synth_looper_t *l, const looper_track_t *trk) { return 1u + (unsigned)(trk - l->tracks); }

static void sf_note_on(synth_looper_t *l, looper_track_t *trk, uint8_t note, uint8_t velocity)
{
    if (note > 127) note = 127;
    if (velocity == 0) {
        trk->held_notes[note] = false;
        if (!trk->sound.sound_missing) l->sampler->note_off(l->sampler->context, track_part(l, trk), note);
        return;
    }
    trk->held_notes[note] = true;
    if (!trk->sound.sound_missing) (void)l->sampler->note_on(l->sampler->context, track_part(l, trk), note, velocity);
    trk->last_note = (int16_t)note;
}

static void sf_note_off(synth_looper_t *l, looper_track_t *trk, uint8_t note)
{
    sf_note_on(l, trk, note, 0);
}

/* Release (not cut) every held note, like track_all_notes_off for synth voices. */
static void sf_all_notes_off(synth_looper_t *l, looper_track_t *trk)
{
    for (unsigned n = 0; n < 128; ++n) if (trk->held_notes[n]) sf_note_off(l, trk, (uint8_t)n);
}

/* Stop and unbind the track part; the track falls back to its synth preset. */
static void sf_release(synth_looper_t *l, looper_track_t *trk)
{
    if (!track_sf(trk)) return;
    l->sampler->panic(l->sampler->context, track_part(l, trk));
    l->sampler->unbind(l->sampler->context, track_part(l, trk));
    memset(trk->held_notes, 0, sizeof(trk->held_notes));
    trk->sound = (sound_ref_t){.engine = SOUND_SYNTH, .synth_preset = (uint8_t)trk->preset};
}

/* Capture the live sound with the timbre (REC start and first recorded note). A
 * live SoundFont program binds the track part to the same bank generation; a
 * failed bind leaves the track on its synth timbre. */
static void capture_sound(synth_looper_t *l, looper_track_t *trk, const synth_t *live)
{
    const sound_ref_t *cur = &live->live_sound.current;
    bool sf = l->sampler && live->sampler == l->sampler && cur->engine == SOUND_SF2 && !cur->sound_missing;
    if (sf && track_sf(trk) && !trk->sound.sound_missing && trk->sound.bank_generation == cur->bank_generation &&
        trk->sound.bank == cur->bank && trk->sound.program == cur->program) return;
    sf_release(l, trk);
    if (!sf) return;
    unsigned part = track_part(l, trk);
    if (!l->sampler->bind(l->sampler->context, part, cur->bank_generation) ||
        !l->sampler->program(l->sampler->context, part, cur->bank, cur->program)) {
        l->sampler->panic(l->sampler->context, part);
        l->sampler->unbind(l->sampler->context, part);
        return;
    }
    l->sampler->control(l->sampler->context, part, 121, 0);
    trk->sound = *cur;
}

static void sf_render(synth_looper_t *l, looper_track_t *trk, float *out, size_t n)
{
    unsigned part = track_part(l, trk);
    if (trk->sound.sound_missing || l->sampler->render(l->sampler->context, part, out, (unsigned)n)) return;
    l->sampler->panic(l->sampler->context, part);
    l->sampler->unbind(l->sampler->context, part);
    trk->sound.sound_missing = true; /* silent until CLEAR or a synth preset */
    memset(out, 0, n * sizeof(float));
}

void looper_init(synth_looper_t *l, uint16_t bpm)
{
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->bars = 2;
    l->bpm = (bpm >= 40 && bpm <= 300) ? bpm : 120;
    static const synth_preset_t default_presets[LOOPER_TRACK_COUNT] = {
        SYNTH_PRESET_MOOG_BASS,
        SYNTH_PRESET_RETRO_80S,
        SYNTH_PRESET_PLUCK_ARP,
        SYNTH_PRESET_BRIGHT_LEAD
    };
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        l->tracks[t].state = LOOPER_STATE_EMPTY;
        l->tracks[t].preset = default_presets[t];
        l->tracks[t].config = *synth_get_preset_config(default_presets[t]);
        synth_normalize_config(&l->tracks[t].config);
        l->tracks[t].volume_pct = 85;
        l->tracks[t].muted = false;
        l->tracks[t].last_note = -1;
        l->tracks[t].sound = (sound_ref_t){.engine = SOUND_SYNTH, .synth_preset = (uint8_t)default_presets[t]};
    }
    update_loop_timing(l);
}

void looper_panic(synth_looper_t *l)
{
    if (!l) return;
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        if (track_sf(&l->tracks[t])) l->sampler->panic(l->sampler->context, track_part(l, &l->tracks[t]));
        memset(l->tracks[t].voices, 0, sizeof(l->tracks[t].voices));
        memset(l->tracks[t].held_notes, 0, sizeof(l->tracks[t].held_notes));
        finalize_at(l, &l->tracks[t], l->cursor_samples);
        l->tracks[t].last_note = -1;
        l->tracks[t].state = l->tracks[t].event_count ? LOOPER_STATE_STOPPED : LOOPER_STATE_EMPTY;
    }
    l->transport_running = false;
    l->cursor_samples = 0;
}

void looper_set_bpm(synth_looper_t *l, uint16_t bpm)
{
    if (!l) return;
    if (bpm < 40) bpm = 40;
    if (bpm > 300) bpm = 300;
    l->bpm = bpm;
    update_loop_timing(l);
}

void looper_set_bars(synth_looper_t *l, uint8_t bars)
{
    if (!l) return;
    if (bars <= 1) bars = 1;
    else if (bars <= 2) bars = 2;
    else if (bars <= 4) bars = 4;
    else bars = 8;
    l->bars = bars;
    update_loop_timing(l);
}

uint8_t looper_get_bars(const synth_looper_t *l)
{
    return l ? l->bars : 2;
}

static void insert_event(looper_track_t *trk, looper_event_t ev)
{
    if (trk->event_count >= LOOPER_MAX_EVENTS) return;
    uint16_t i = trk->event_count++;
    while (i && trk->events[i-1].sample_offset > ev.sample_offset) {
        trk->events[i] = trk->events[i-1]; --i;
    }
    trk->events[i] = ev;
}

static void finalize_at(synth_looper_t *l, looper_track_t *trk, uint32_t pos)
{
    (void)l;
    for (unsigned n=0; n<128; ++n) if (trk->record_held[n]) {
        insert_event(trk, (looper_event_t){.sample_offset=pos, .note=n});
        trk->record_held[n]=false;
    }
    trk->record_held_count=0;
}

void looper_close_recorded_notes(synth_looper_t *l)
{
    if (!l) return;
    for (unsigned t=0;t<LOOPER_TRACK_COUNT;++t) finalize_at(l,&l->tracks[t],l->cursor_samples);
}

void looper_track_trigger(synth_looper_t *l, uint8_t track, const synth_t *live_synth)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    looper_track_t *trk = &l->tracks[track];

    /* Disarm any other track that was in REC/OVERDUB so only 1 track records at a time */
    for (int i = 0; i < LOOPER_TRACK_COUNT; ++i) {
        if (i != (int)track) {
            if (l->tracks[i].state == LOOPER_STATE_REC) {
                finalize_at(l, &l->tracks[i], l->cursor_samples);
                l->tracks[i].state = (l->tracks[i].event_count > 0) ? LOOPER_STATE_PLAY : LOOPER_STATE_EMPTY;
            } else if (l->tracks[i].state == LOOPER_STATE_OVERDUB) {
                finalize_at(l, &l->tracks[i], l->cursor_samples);
                l->tracks[i].state = LOOPER_STATE_PLAY;
            }
        }
    }

    switch (trk->state) {
    case LOOPER_STATE_EMPTY:
        trk->event_count = 0;
        trk->full = false;
        trk->lfo.phase=0;
        if (live_synth) {
            trk->lfo.phase=0;
            trk->preset = live_synth->current_preset;
            trk->config = live_synth->config;
            capture_sound(l, trk, live_synth);
        }
        trk->state = LOOPER_STATE_REC;
        break;
    case LOOPER_STATE_REC:
        finalize_at(l, trk, l->cursor_samples);
        trk->state = (trk->event_count > 0) ? LOOPER_STATE_PLAY : LOOPER_STATE_EMPTY;
        break;
    case LOOPER_STATE_PLAY:
        trk->state = LOOPER_STATE_OVERDUB;
        break;
    case LOOPER_STATE_OVERDUB:
        finalize_at(l, trk, l->cursor_samples);
        trk->state = LOOPER_STATE_PLAY;
        break;
    case LOOPER_STATE_STOPPED:
        trk->state = (trk->event_count > 0) ? LOOPER_STATE_PLAY : LOOPER_STATE_REC;
        break;
    }
    refresh_transport_state(l);
}

void looper_track_stop(synth_looper_t *l, uint8_t track)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    looper_track_t *trk = &l->tracks[track];
    if (trk->state == LOOPER_STATE_REC || trk->state == LOOPER_STATE_OVERDUB) {
        finalize_at(l, trk, l->cursor_samples);
    }
    if (track_sf(trk)) sf_all_notes_off(l, trk);
    else track_all_notes_off(trk);
    trk->state = (trk->event_count > 0) ? LOOPER_STATE_STOPPED : LOOPER_STATE_EMPTY;
    refresh_transport_state(l);
}

void looper_track_clear(synth_looper_t *l, uint8_t track)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    looper_track_t *trk = &l->tracks[track];
    sf_release(l, trk);
    track_all_notes_off(trk);
    memset(trk->voices, 0, sizeof(trk->voices));
    trk->event_count = 0;
    trk->full = false;
    trk->lfo.phase=0;
    memset(trk->record_held,0,sizeof(trk->record_held));
    trk->record_held_count=0;
    trk->state = LOOPER_STATE_EMPTY;
    trk->last_note = -1;
    refresh_transport_state(l);
}

void looper_clear_all(synth_looper_t *l)
{
    if (!l) return;
    for (uint8_t t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        looper_track_clear(l, t);
    }
}

void looper_track_set_mute(synth_looper_t *l, uint8_t track, bool muted)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    l->tracks[track].muted = muted;

}

void looper_track_toggle_mute(synth_looper_t *l, uint8_t track)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    looper_track_set_mute(l, track, !l->tracks[track].muted);
}

void looper_track_set_volume(synth_looper_t *l, uint8_t track, uint8_t volume_pct)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    if (volume_pct > 100) volume_pct = 100;
    l->tracks[track].volume_pct = volume_pct;
}

void looper_track_set_preset(synth_looper_t *l, uint8_t track, synth_preset_t preset)
{
    if (!l || track >= LOOPER_TRACK_COUNT) return;
    if ((unsigned)preset >= SYNTH_PRESET_COUNT) preset = SYNTH_PRESET_ANALOG_BRASS;
    looper_track_t *trk = &l->tracks[track];
    synth_config_t old=trk->config;
    trk->lfo.phase=0;
    trk->preset = preset;
    sf_release(l, trk); /* an explicit synth preset replaces a SoundFont track sound */
    trk->sound.synth_preset = (uint8_t)preset;
    trk->config = *synth_get_preset_config(preset);
    synth_normalize_config(&trk->config);
    synth_retarget_voices(trk->voices,LOOPER_VOICES_PER_TRACK,&old,&trk->config);
}

void looper_record_note(synth_looper_t *l, uint8_t note, uint8_t velocity, bool is_note_on, const synth_t *live_synth)
{
    if (!l || !l->transport_running || note > 127) return;
    if (is_note_on && velocity == 0) is_note_on = false;

    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        looper_track_t *trk = &l->tracks[t];
        if (trk->state != LOOPER_STATE_REC && trk->state != LOOPER_STATE_OVERDUB) continue;

        /* Capture current live synth timbre on the first recorded note of a fresh REC track */
        if (trk->state == LOOPER_STATE_REC && trk->event_count == 0 && live_synth) {
            trk->lfo.phase=0;
            trk->preset = live_synth->current_preset;
            trk->config = live_synth->config;
            capture_sound(l, trk, live_synth);
            /* Restart loop cursor on the very first recorded note if no other track has events yet */
            bool other_has_events = false;
            for (int o = 0; o < LOOPER_TRACK_COUNT; ++o) {
                if (o != t && l->tracks[o].event_count > 0) {
                    other_has_events = true;
                    break;
                }
            }
            if (!other_has_events && is_note_on) {
                l->cursor_samples = 0;
            }
        }

        if (!is_note_on && !trk->record_held[note]) continue;
        unsigned extra = is_note_on && !trk->record_held[note];
        if (is_note_on && (unsigned)trk->event_count + trk->record_held_count + 1 + extra > LOOPER_MAX_EVENTS) {
            trk->full=true;
            finalize_at(l,trk,l->cursor_samples);
            trk->state=trk->event_count ? LOOPER_STATE_PLAY : LOOPER_STATE_EMPTY;
            continue;
        }
        insert_event(trk,(looper_event_t){.sample_offset=l->cursor_samples,
            .type=is_note_on, .note=note, .velocity=is_note_on ? velocity : 0});
        if (is_note_on && !trk->record_held[note]) ++trk->record_held_count;
        if (!is_note_on) --trk->record_held_count;
        trk->record_held[note]=is_note_on;
    }
}

bool looper_is_sounding(const synth_looper_t *l)
{
    if (!l) return false;
    if (l->transport_running) return true;
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        const looper_track_t *trk = &l->tracks[t];
        if (trk->muted || trk->volume_pct == 0) continue;
        if (track_sf(trk)) {
            if (!trk->sound.sound_missing && l->sampler->voice_count(l->sampler->context, track_part(l, trk))) return true;
            continue;
        }
        for (int v = 0; v < LOOPER_VOICES_PER_TRACK; ++v) {
            if (trk->voices[v].state != VOICE_STATE_IDLE || trk->voices[v].env_level > 0.0001f) {
                return true;
            }
        }
    }
    return false;
}

static bool active(const looper_track_t *t)
{
    return t->state==LOOPER_STATE_REC || t->state==LOOPER_STATE_PLAY || t->state==LOOPER_STATE_OVERDUB;
}
static uint16_t lower_bound(const looper_track_t *t, uint32_t pos)
{
    uint16_t lo=0,hi=t->event_count;
    while(lo<hi) { uint16_t mid=lo+(hi-lo)/2; if(t->events[mid].sample_offset<pos) lo=mid+1; else hi=mid; }
    return lo;
}
static void dispatch_at(synth_looper_t *l, uint32_t pos)
{
    for(unsigned t=0;t<LOOPER_TRACK_COUNT;++t) {
        looper_track_t *trk=&l->tracks[t]; if(!active(trk)) continue;
        for(uint16_t e=lower_bound(trk,pos);e<trk->event_count && trk->events[e].sample_offset==pos;++e) {
            const looper_event_t *ev=&trk->events[e];
            if(track_sf(trk)) { if(ev->type) sf_note_on(l,trk,ev->note,ev->velocity); else sf_note_off(l,trk,ev->note); }
            else if(ev->type) track_note_on(trk,ev->note,ev->velocity); else track_note_off(trk,ev->note);
        }
    }
}
uint32_t looper_frames_to_boundary(const synth_looper_t *l)
{
    if(!l || !l->transport_running || !l->loop_samples) return UINT32_MAX;
    uint32_t next=l->loop_samples;
    for(unsigned t=0;t<LOOPER_TRACK_COUNT;++t) if(active(&l->tracks[t])) {
        const looper_track_t *trk=&l->tracks[t];
        uint16_t e=lower_bound(trk,l->cursor_samples+1);
        if(e<trk->event_count && trk->events[e].sample_offset<next) next=trk->events[e].sample_offset;
    }
    return next-l->cursor_samples;
}
static void render_voices(synth_looper_t *l, float *mix_buf, size_t num_frames, float lfo_sin)
{
    (void)lfo_sin;
    for(unsigned t=0;t<LOOPER_TRACK_COUNT;++t) {
        looper_track_t *trk=&l->tracks[t];
        float scratch[128];
        size_t done=0;
        while(done<num_frames) {
            size_t n=num_frames-done; if(n>128) n=128;
            memset(scratch,0,n*sizeof(float));
            if(track_sf(trk)) sf_render(l,trk,scratch,n);
            else {
                unsigned voice_parent = synth_profile_enter(SP_SYNTH_VOICES);
                trk->lfo.enabled=trk->config.lfo_enable;
                trk->lfo.phase_inc=trk->config.lfo_freq_hz/SYNTH_SAMPLE_RATE;
                trk->lfo.vibrato_semitones=trk->config.lfo_vibrato_depth;
                synth_render_voices(&trk->config,trk->voices,LOOPER_VOICES_PER_TRACK,&trk->lfo,1,0,1,scratch,n);
                synth_profile_leave(voice_parent);
            }
            float gain=trk->muted ? 0 : trk->volume_pct*0.01f;
            for(size_t i=0;i<n;++i) mix_buf[done+i]+=scratch[i]*gain;
            done+=n;
        }
    }
}

void looper_render_mix(synth_looper_t *l, float *mix_buf, size_t frames, float lfo_sin)
{
    if(!l || !mix_buf) return;
    unsigned profile_parent = synth_profile_enter(SP_TRACK_MIX);
    size_t done=0;
    while(done<frames) {
        size_t count=frames-done;
        if(l->transport_running && l->loop_samples) {
            unsigned event_parent = synth_profile_enter(SP_LOOP_EVENT);
            dispatch_at(l,l->cursor_samples);
            synth_profile_leave(event_parent);
            uint32_t boundary=looper_frames_to_boundary(l);
            if(count>boundary) count=boundary;
        }
        if(!count) { synth_profile_leave(profile_parent); return; } /* Invalid transport state: bounded failure. */
        render_voices(l,mix_buf+done,count,lfo_sin);
        done+=count;
        if(l->transport_running && l->loop_samples) {
            l->cursor_samples+=(uint32_t)count;
            if(l->cursor_samples==l->loop_samples) {
                unsigned event_parent = synth_profile_enter(SP_LOOP_EVENT);
                for(unsigned t=0;t<LOOPER_TRACK_COUNT;++t) if(active(&l->tracks[t]))
                    finalize_at(l,&l->tracks[t],l->loop_samples);
                dispatch_at(l,l->loop_samples);
                for(unsigned t=0;t<LOOPER_TRACK_COUNT;++t) if(active(&l->tracks[t])) {
                    if(track_sf(&l->tracks[t])) sf_all_notes_off(l,&l->tracks[t]);
                    else track_all_notes_off(&l->tracks[t]);
                    if(l->tracks[t].state==LOOPER_STATE_REC && l->tracks[t].event_count)
                        l->tracks[t].state=LOOPER_STATE_PLAY;
                }
                l->cursor_samples=0;
                synth_profile_leave(event_parent);
            }
        }
    }
    synth_profile_leave(profile_parent);
}

void looper_get_status(const synth_looper_t *l, looper_status_t *out)
{
    if (!l || !out) return;
    out->bars = l->bars;
    out->bpm = l->bpm;
    out->loop_samples = l->loop_samples;
    out->cursor_samples = l->cursor_samples;
    out->transport_running = l->transport_running;
    for (int t = 0; t < LOOPER_TRACK_COUNT; ++t) {
        const looper_track_t *trk = &l->tracks[t];
        uint8_t active = 0;
        if (track_sf(trk)) {
            if (!trk->sound.sound_missing) active = (uint8_t)l->sampler->voice_count(l->sampler->context, (unsigned)t + 1u);
        } else for (int v = 0; v < LOOPER_VOICES_PER_TRACK; ++v) {
            if (trk->voices[v].state != VOICE_STATE_IDLE || trk->voices[v].env_level > 0.0001f) {
                active++;
            }
        }
        out->tracks[t] = (looper_track_status_t){
            .state = trk->state,
            .preset = trk->preset,
            .volume_pct = trk->volume_pct,
            .muted = trk->muted,
            .full = trk->full,
            .event_count = trk->event_count,
            .active_voices = active,
            .sf = track_sf(trk),
        };
    }
}

unsigned looper_sampler_tracks(const synth_looper_t *l)
{
    unsigned n = 0;
    if (l) for (unsigned t = 0; t < LOOPER_TRACK_COUNT; ++t) n += track_sf(&l->tracks[t]);
    return n;
}

const char *looper_state_name(looper_track_state_t st)
{
    switch (st) {
    case LOOPER_STATE_EMPTY:   return "EMPTY";
    case LOOPER_STATE_REC:     return "REC";
    case LOOPER_STATE_PLAY:    return "PLAY";
    case LOOPER_STATE_OVERDUB: return "OVD";
    case LOOPER_STATE_STOPPED: return "STOP";
    default:                   return "EMPTY";
    }
}
