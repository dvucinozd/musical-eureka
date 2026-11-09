#include "synth_web_keys.h"

static uint32_t input_epoch(const synth_t *synth)
{
    return synth->sampler ? synth->live_sound.input_epoch : synth->input_epoch;
}

static void sync_epoch(synth_web_keys_t *keys, const synth_t *synth)
{
    if (keys->observed_epoch != input_epoch(synth)) {
        /* A sound change or panic already stopped these notes; their leases end. */
        for (unsigned i = 0; i < 128; ++i) keys->expires[i] = 0;
        keys->observed_epoch = input_epoch(synth);
    }
}

void synth_web_key_action(synth_web_keys_t *keys, synth_t *synth, uint8_t note, synth_web_key_action_t action, int64_t now)
{
    sync_epoch(keys, synth);
    if (note > 127) return;
    if (action == SYNTH_WEB_KEY_HOLD) {
        /* Controls run before the block's expiry scan: a late HOLD must not
         * revive an overdue slot merely because that scan has not run yet. */
        if (keys->expires[note] && now >= keys->expires[note])
            synth_web_key_action(keys, synth, note, SYNTH_WEB_KEY_RELEASE, now);
        ++keys->holds;
        if (keys->expires[note]) keys->expires[note] = now + SYNTH_WEB_KEY_LEASE_US;
        else ++keys->stale_holds;
        return;
    }
    if (action == SYNTH_WEB_KEY_PRESS) ++keys->presses; else ++keys->releases;
    bool down = action == SYNTH_WEB_KEY_PRESS;
    if (down && keys->expires[note]) {
        keys->expires[note] = now + SYNTH_WEB_KEY_LEASE_US;
        return;
    }
    if (!down && !keys->expires[note]) return;
    keys->expires[note] = down ? now + SYNTH_WEB_KEY_LEASE_US : 0;
    midi_event_t event = {.type = down ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF,
        .timestamp_us = now, .note = {.note = note, .velocity = down ? 100 : 0}};
    if (synth->sampler) { (void)synth_sound_event_from(synth, SYNTH_SOURCE_WEB, synth->live_sound.input_epoch, &event); return; }
    synth_handle_local_note(synth,event.note.note,event.note.velocity,down);
}

void synth_web_key(synth_web_keys_t *keys, synth_t *synth, uint8_t note, bool down, int64_t now)
{
    synth_web_key_action(keys, synth, note, down ? SYNTH_WEB_KEY_PRESS : SYNTH_WEB_KEY_RELEASE, now);
}

void synth_web_keys_expire(synth_web_keys_t *keys, synth_t *synth, int64_t now)
{
    sync_epoch(keys, synth);
    for (unsigned note = 0; note < 128; ++note)
        if (keys->expires[note] && now >= keys->expires[note])
            synth_web_key_action(keys, synth, note, SYNTH_WEB_KEY_RELEASE, now);
}

void synth_web_keys_hold_mask(synth_web_keys_t *keys, synth_t *synth,
                              const uint32_t mask[4], uint32_t epoch, int64_t now)
{
    synth_web_keys_expire(keys, synth, now);
    for (unsigned note = 0; note < 128; ++note) {
        if (!(mask[note / 32] & (UINT32_C(1) << (note % 32)))) continue;
        if (epoch != input_epoch(synth)) { ++keys->holds; ++keys->stale_holds; continue; }
        synth_web_key_action(keys, synth, (uint8_t)note, SYNTH_WEB_KEY_HOLD, now);
    }
}
