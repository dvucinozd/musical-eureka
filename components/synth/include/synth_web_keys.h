#pragma once
#include "synth.h"

/* Audio-task owned leases. Browser renews held keys; disconnect releases them. */
#define SYNTH_WEB_KEY_LEASE_US 1500000
typedef struct {
    int64_t expires[128]; uint32_t observed_epoch;
    uint32_t presses, holds, stale_holds, releases; /* diagnostics: commands received */
} synth_web_keys_t;
/* PRESS starts a note (or renews a held one); HOLD only renews a held note of
 * the current live epoch and never starts one, so a late renewal after a sound
 * change or an expired lease cannot retrigger a key. */
typedef enum { SYNTH_WEB_KEY_RELEASE, SYNTH_WEB_KEY_PRESS, SYNTH_WEB_KEY_HOLD } synth_web_key_action_t;
void synth_web_key_action(synth_web_keys_t *keys, synth_t *synth, uint8_t note, synth_web_key_action_t action, int64_t now);
/* down: PRESS, else RELEASE. */
void synth_web_key(synth_web_keys_t *keys, synth_t *synth, uint8_t note, bool down, int64_t now);
void synth_web_keys_expire(synth_web_keys_t *keys, synth_t *synth, int64_t now);
/* One bounded renewal of selected notes. Bit n%32 in mask[n/32] selects n.
 * Captured epoch must still match; expired/absent leases never start a note. */
void synth_web_keys_hold_mask(synth_web_keys_t *keys, synth_t *synth,
                              const uint32_t mask[4], uint32_t epoch, int64_t now);
