/* SF06 note ownership on the sampler route (docs/SOUNDFONT_RELEASE_PLAN_2026-11-01.md):
 * S1 capture epoch, S2 per-source Note Off, S3 USB tombstones, S4 Web hold,
 * S5 no voice left without an end (shared key, repeated press, rejected press). */
#include "synth.h"
#include "synth_web_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned ons[128], offs[128];
static bool reserved, full; /* full: the sampler has no free voice (rejects a Note On) */
/* Like TSF, each accepted Note On is one voice and each Note Off ends one. */
static unsigned sounding(unsigned k) { return ons[k] - offs[k]; }
static sound_result_t validate(void *c, unsigned p, const sound_ref_t *r) { (void)c; (void)p; (void)r; if (reserved) return SOUND_BUSY; reserved = true; return SOUND_OK; }
static bool bind(void *c, unsigned p, uint64_t g) { (void)c; (void)p; return g == 1; }
static bool program(void *c, unsigned p, unsigned b, unsigned pr) { (void)c; (void)p; (void)b; (void)pr; return true; }
static void unbind(void *c, unsigned p) { (void)c; (void)p; }
static bool note_on(void *c, unsigned p, unsigned k, unsigned v) { (void)c; (void)p; (void)v; if (full) return false; ons[k]++; return true; }
static void note_off(void *c, unsigned p, unsigned k) { (void)c; (void)p; if (offs[k] < ons[k]) offs[k]++; }
static void control(void *c, unsigned p, unsigned cc, unsigned v) { (void)c; (void)p; (void)cc; (void)v; }
static void pitch(void *c, unsigned p, unsigned v) { (void)c; (void)p; (void)v; }
static void panic(void *c, unsigned p) { (void)c; (void)p; }
static bool render(void *c, unsigned p, float *o, unsigned n) { (void)c; (void)p; memset(o, 0, n * sizeof(float)); return true; }
static unsigned voices(void *c, unsigned p) { (void)c; (void)p; return 0; }
static void complete(void *c, unsigned p, uint64_t id, bool f) { (void)c; (void)p; (void)id; (void)f; reserved = false; }
static const synth_sampler_ops_t ops = {NULL, validate, bind, program, unbind, note_on, note_off, control, pitch, panic, render, voices, complete};

static synth_t s;
static uint64_t request;
static const sound_ref_t sf = {.engine = SOUND_SF2, .bank_generation = 1};
static const sound_ref_t osc = {.engine = SOUND_SYNTH, .synth_preset = 2};

static void run(unsigned frames) { float out[128]; while (frames) { unsigned n = frames > 128 ? 128 : frames; synth_render(&s, out, n); frames -= n; } }
static void select_ref(const sound_ref_t *r) { assert(synth_sound_select_host(&s, s.live_sound.input_epoch, ++request, r) == SOUND_OK); run(480); }
/* SF -> synth -> SF: two epochs later the live part plays the SoundFont again. */
static void change_sound(void) { select_ref(&osc); select_ref(&sf); }
static bool ev(synth_source_t src, uint32_t epoch, unsigned key, bool down)
{
    midi_event_t e = {.type = down ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF, .session = src == SYNTH_SOURCE_USB, .note = {(uint8_t)key, (uint8_t)(down ? 100 : 0)}};
    return synth_sound_event_from(&s, src, epoch, &e);
}
static uint32_t now_epoch(void) { return s.live_sound.input_epoch; }

static void setup(void)
{
    memset(ons, 0, sizeof(ons)); memset(offs, 0, sizeof(offs)); reserved = false; full = false; request = 0;
    synth_init(&s, NULL);
    assert(synth_sampler_install_host(&s, &ops));
    select_ref(&sf);
}

static void s2_sources(void)
{
    setup();
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, true) && ev(SYNTH_SOURCE_WEB, now_epoch(), 60, true));
    assert(ons[60] == 2);
    /* Each release ends one instance; 60 keeps sounding while the Web holds it. */
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, false) && offs[60] == 1 && sounding(60) == 1);
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 60, false) && offs[60] == 2 && sounding(60) == 0);
    assert(ev(SYNTH_SOURCE_LOCAL, now_epoch(), 61, false) && offs[61] == 0); /* nothing held */
    assert(ev(SYNTH_SOURCE_LOCAL, now_epoch(), 61, true) && ev(SYNTH_SOURCE_LOCAL, now_epoch(), 61, false) && offs[61] == 1);
}

static void s1_capture_epoch(void)
{
    setup();
    uint32_t before = now_epoch();
    change_sound();
    uint32_t rejected = s.sampler_rejected;
    assert(!ev(SYNTH_SOURCE_USB, before, 64, true) && ons[64] == 0 && s.sampler_rejected == rejected + 1);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 64, true) && ons[64] == 1);
}

static void s3_usb_tombstone(void)
{
    setup();
    uint32_t before = now_epoch();
    assert(ev(SYNTH_SOURCE_USB, before, 60, true) && ons[60] == 1);
    change_sound();
    /* The key is still physically down: a duplicate press is ambiguous in MIDI1. */
    assert(!ev(SYNTH_SOURCE_USB, now_epoch(), 60, true) && ons[60] == 1);
    /* Its release only forgets the old press; the new sound gets no Note Off. */
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, false) && offs[60] == 0);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, true) && ons[60] == 2);
    /* A Note Off captured before the change but popped after also clears it. */
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 62, true));
    uint32_t mid = now_epoch();
    change_sound();
    assert(ev(SYNTH_SOURCE_USB, mid, 62, false) && offs[62] == 0);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 62, true) && ons[62] == 2);
    /* Web presses are explicit: a stale Web press never blocks a new one. */
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 70, true));
    change_sound();
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 70, true) && ons[70] == 2);
    /* Panic forgets every press. */
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 72, true));
    synth_panic(&s);
    for (unsigned o = 0; o < SYNTH_SOURCE_COUNT; o++) for (unsigned k = 0; k < 128; k++) assert(!s.key_claims[o][k]);
}

static void s4_web_hold(void)
{
    setup();
    synth_web_keys_t keys; memset(&keys, 0, sizeof(keys));
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_PRESS, 100);
    assert(ons[60] == 1);
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_HOLD, 500000);
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_PRESS, 900000); /* old page: key_on as renewal */
    assert(ons[60] == 1 && keys.expires[60] == 900000 + SYNTH_WEB_KEY_LEASE_US);
    synth_web_keys_expire(&keys, &s, 900000 + SYNTH_WEB_KEY_LEASE_US);
    assert(offs[60] == 1 && !keys.expires[60]);
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_HOLD, 3000000); /* late renewal */
    assert(ons[60] == 1 && !keys.expires[60]);
    /* After a sound change, a held key's renewal and release do nothing. */
    synth_web_key_action(&keys, &s, 64, SYNTH_WEB_KEY_PRESS, 4000000);
    change_sound();
    synth_web_key_action(&keys, &s, 64, SYNTH_WEB_KEY_HOLD, 4100000);
    synth_web_key_action(&keys, &s, 64, SYNTH_WEB_KEY_RELEASE, 4200000);
    assert(ons[64] == 1 && offs[64] == 0 && !keys.expires[64]);
    synth_web_key_action(&keys, &s, 64, SYNTH_WEB_KEY_PRESS, 4300000);
    assert(ons[64] == 2);
}

static void batch_hold(void)
{
    setup(); synth_web_keys_t keys = {0};
    uint32_t mask[4] = {0};
    mask[1] = UINT32_C(1) << 28; /* 60 */
    mask[2] = UINT32_C(1) << 1;  /* absent 65 */
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_PRESS, 100);
    synth_web_key_action(&keys, &s, 64, SYNTH_WEB_KEY_PRESS, 100);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, true));
    unsigned on = ons[60];
    synth_web_keys_hold_mask(&keys, &s, mask, now_epoch(), 500000);
    assert(ons[60] == on && keys.expires[60] == 2000000 && keys.expires[64] == 1500100 && !keys.expires[65]);
    /* Unselected key expires; the Web release ends one instance of 60, the
     * USB press keeps sounding until the USB releases it. */
    synth_web_keys_hold_mask(&keys, &s, mask, now_epoch(), 2000000);
    assert(!keys.expires[60] && !keys.expires[64] && offs[60] == 1 && sounding(60) == 1 && offs[64] == 1);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 60, false) && offs[60] == 2 && sounding(60) == 0);
    uint32_t old = now_epoch();
    change_sound();
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_PRESS, 4000000);
    on = ons[60];
    synth_web_keys_hold_mask(&keys, &s, mask, old, 4100000);
    assert(keys.expires[60] == 5500000 && ons[60] == on); /* stale queued batch */
    synth_web_keys_hold_mask(&keys, &s, mask, now_epoch(), 4200000);
    assert(keys.expires[60] == 5700000 && ons[60] == on);
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_RELEASE, 4300000);
    synth_web_keys_hold_mask(&keys, &s, mask, now_epoch(), 4400000);
    assert(!keys.expires[60] && ons[60] == on);
    synth_web_key_action(&keys, &s, 60, SYNTH_WEB_KEY_PRESS, 4500000);
    change_sound();
    synth_web_keys_hold_mask(&keys, &s, mask, now_epoch(), 4600000);
    assert(!keys.expires[60] && ons[60] == on + 1); /* old lease invalidated */
}

/* Found on the board 2026-11-04 with a USB keyboard over held Web notes: one
 * Note Off for two presses left the second voice sounding. */
static void s5_no_voice_without_end(void)
{
    setup();
    for (unsigned k = 0; k < 128; k++) assert(!s.key_claims[SYNTH_SOURCE_USB][k] && !s.key_claims[SYNTH_SOURCE_WEB][k]);
    /* Shared key, released in either order. */
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 62, true) && ev(SYNTH_SOURCE_USB, now_epoch(), 62, true));
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 62, false) && sounding(62) == 1);
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 62, false) && sounding(62) == 0);
    /* A repeated press from one source without a release: one voice. */
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 63, true) && ev(SYNTH_SOURCE_USB, now_epoch(), 63, true));
    assert(ons[63] == 1 && ev(SYNTH_SOURCE_USB, now_epoch(), 63, false) && sounding(63) == 0);
    /* A press rejected for want of a voice claims nothing: its release must
     * not end the Web's held instance of the key. */
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 65, true));
    unsigned rejected = s.sampler_rejected;
    full = true;
    assert(!ev(SYNTH_SOURCE_USB, now_epoch(), 65, true) && s.sampler_rejected == rejected + 1 && ons[65] == 1);
    full = false;
    assert(ev(SYNTH_SOURCE_USB, now_epoch(), 65, false) && offs[65] == 0 && sounding(65) == 1);
    assert(ev(SYNTH_SOURCE_WEB, now_epoch(), 65, false) && sounding(65) == 0);
}

int main(void)
{
    s2_sources();
    s1_capture_epoch();
    s3_usb_tombstone();
    s4_web_hold();
    batch_hold();
    s5_no_voice_without_end();
    puts("SF06 note ownership PASS: S1 capture epoch, S2 per-source Note Off, S3 USB tombstones, S4 Web hold never starts a note, S5 no voice without an end");
    return 0;
}
