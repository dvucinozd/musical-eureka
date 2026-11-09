#include "synth_web_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static synth_t synth;
static synth_web_keys_t keys;
int main(void)
{
    synth_init(&synth, NULL);
    synth_web_key(&keys, &synth, 60, true, 100);
    synth_web_key(&keys, &synth, 64, true, 100);
    assert(synth.note_on_count == 2);
    synth_web_key(&keys, &synth, 60, true, 500100);
    assert(synth.note_on_count == 2); /* Renewal must not retrigger envelope. */
    synth_web_keys_expire(&keys, &synth, 1500100);
    assert(keys.expires[60] && !keys.expires[64] && synth.note_off_count == 1);
    synth_web_keys_expire(&keys, &synth, 2000100);
    assert(!keys.expires[60] && synth.note_off_count == 2);
    synth_web_key(&keys, &synth, 60, false, 2000200);
    assert(synth.note_off_count == 2); /* Duplicate release is harmless. */
    synth_set_arp_mode(&synth, SYNTH_ARP_UP);
    synth_web_key(&keys, &synth, 60, true, 3000000);
    assert(synth.arp.held_count == 1);
    synth_web_keys_expire(&keys, &synth, 4500000);
    assert(synth.arp.held_count == 0);
    /* HOLD at its exact deadline, before the normal expiry scan, cannot revive. */
    synth_set_arp_mode(&synth, SYNTH_ARP_OFF);
    synth_web_key(&keys, &synth, 60, true, 6000000);
    unsigned on = synth.note_on_count, off = synth.note_off_count;
    synth_web_key_action(&keys, &synth, 60, SYNTH_WEB_KEY_HOLD, 7500000);
    assert(!keys.expires[60] && synth.note_on_count == on && synth.note_off_count == off + 1);
    uint32_t mask[4] = {0}; mask[1] = UINT32_C(1) << 28; /* C4 */
    synth_web_keys_hold_mask(&keys, &synth, mask, synth.input_epoch, 7600000);
    assert(!keys.expires[60] && synth.note_on_count == on);
    synth_web_key(&keys, &synth, 255, true, 8000000);
    assert(synth.arp.held_count == 0);
    puts("Web key chords, refresh, timeout release, arp and bounds PASSED");
    return 0;
}
