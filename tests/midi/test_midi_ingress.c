#include <assert.h>
#include <stdio.h>

#include "midi_ingress.h"
#include "midi_queue.h"
#include "usb_midi_host.h"

static uint32_t s_active;
static bool s_packet_pending;
static usb_midi_packet_t s_packet;

uint32_t usb_midi_host_active_session(void) { return s_active; }
esp_err_t usb_midi_host_start(void) { return ESP_OK; }

bool usb_midi_host_receive(usb_midi_packet_t *out, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_packet_pending) return false;
    s_packet_pending = false;
    *out = s_packet;
    return true;
}

/* Exercise the actual worker iteration and audio-owner drain without a scheduler. */
#include "../../components/midi_ingress/midi_ingress.c"

static void packet(uint32_t session, uint8_t cin, uint8_t status, uint8_t a, uint8_t b)
{
    s_packet = (usb_midi_packet_t){
        .session = session, .timestamp_us = 1, .packet = {cin, status, a, b},
    };
    s_packet_pending = true;
    assert(ingress_service(0) == 1);
}

int main(void)
{
    static synth_t synth;
    midi_ingress_state_t state = {0};
    synth_init(&synth, NULL);
    synth_set_arp_mode(&synth, SYNTH_ARP_OFF);
    assert(midi_queue_init() == ESP_OK);

    s_active = 1;
    packet(1, 9, 0x90, 60, 64);
    midi_ingress_drain(&state, &synth, true);
    assert(state.applied == 1 && state.panics == 0);
    assert(synth_is_sounding(&synth));
    packet(1, 0xb, 0xb0, 64, 127);
    packet(1, 8, 0x80, 60, 0);
    midi_ingress_drain(&state, &synth, true);
    assert(synth.sustain_pedal && synth_is_sounding(&synth));

    /* Both a queued old-session note and one arriving from USB after
     * disconnect are rejected. No subsequent packet is needed to panic. */
    packet(1, 9, 0x90, 61, 64);
    s_active = 0;
    packet(1, 9, 0x90, 62, 64);
    midi_ingress_drain(&state, &synth, true);
    assert(state.panics == 1 && state.rejected == 1 && state.applied == 3);
    assert(!synth.sustain_pedal && !synth_is_sounding(&synth));

    s_active = 3;
    midi_queue_set_capture_epoch(synth.input_epoch - 1);
    packet(3, 9, 0x90, 62, 64);
    midi_ingress_drain(&state, &synth, true);
    assert(state.rejected == 2 && !synth_is_sounding(&synth));
    packet(3, 9, 0x90, 64, 64);
    midi_ingress_drain(&state, &synth, true);
    assert(state.applied == 4 && synth_is_sounding(&synth));

    /* A lost Note Off in the semantic queue clears held notes and backlog. */
    midi_event_t event = {.type = MIDI_EVENT_NOTE_OFF, .session = 3, .note = {64, 0}};
    for (unsigned i = 0; i < MIDI_EVENT_QUEUE_CAPACITY; ++i) assert(midi_queue_push(&event));
    assert(!midi_queue_push(&event));
    midi_ingress_drain(&state, &synth, true);
    assert(state.panics == 2 && state.dropped == 1 && state.applied == 4);
    assert(!synth_is_sounding(&synth) && !midi_queue_pop(&event, 0));

    for (unsigned i = 0; i < MIDI_INGRESS_EVENTS_PER_BLOCK + 2; ++i) assert(midi_queue_push(&event));
    midi_ingress_drain(&state, &synth, true);
    assert(state.applied == 4 + MIDI_INGRESS_EVENTS_PER_BLOCK);
    midi_ingress_drain(&state, &synth, true);
    assert(state.applied == 6 + MIDI_INGRESS_EVENTS_PER_BLOCK);

    /* The probe has exclusive use of the synth while live input is disabled. */
    midi_ingress_drain(&state, &synth, false);
    packet(3, 9, 0x90, 67, 64);
    midi_ingress_drain(&state, &synth, false);
    assert(!synth_is_sounding(&synth) && state.rejected == 3);
    /* An overflow while the probe owns the synth discards the USB backlog
     * without issuing a panic that would cut the probe note. */
    synth_note_on(&synth, 60, 64);
    for (unsigned i = 0; i < MIDI_EVENT_QUEUE_CAPACITY; ++i) assert(midi_queue_push(&event));
    assert(!midi_queue_push(&event));
    midi_ingress_drain(&state, &synth, false);
    assert(state.panics == 3 && state.dropped == 2 && !midi_queue_pop(&event, 0));
    assert(synth_is_sounding(&synth));
    assert(state.receive_to_dispatch_max_us > 0);
    puts("PASS USB MIDI ingress: sustain, disconnect, stale epoch, overflow, block budget, probe");
    return 0;
}
