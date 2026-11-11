#include "midi_ingress.h"
#include <stdatomic.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "midi_parser.h"
#include "midi_queue.h"
#include "usb_midi_host.h"

enum { RAW_BATCH = 64 };
static atomic_bool s_paused;
static atomic_bool s_input_open;
static int64_t s_opened_at_us;

void midi_ingress_open_input(void)
{
    if (atomic_load(&s_input_open)) return;
    s_opened_at_us = esp_timer_get_time();
    atomic_store(&s_input_open, true);
}

uint32_t midi_ingress_active_session(void)
{
    return atomic_load(&s_input_open) ? usb_midi_host_active_session() : 0;
}

void midi_ingress_pause(bool pause)
{
    atomic_store(&s_paused, pause);
}

/* Adapted from NEKTAR-P4 app_midi_ingress.c. The worker owns raw USB packets. */
static unsigned ingress_service(uint32_t wait_ms)
{
    usb_midi_packet_t packet;
    unsigned received = 0;
    while (received < RAW_BATCH && usb_midi_host_receive(&packet, received ? 0 : wait_ms)) {
        ++received;
        midi_event_t event;
        if (atomic_load(&s_input_open) && packet.timestamp_us > s_opened_at_us &&
            !atomic_load(&s_paused) && packet.session == usb_midi_host_active_session() &&
            midi_parser_parse_usb_packet(packet.packet, packet.session, packet.timestamp_us, &event)) {
            (void)midi_queue_push(&event);
        }
    }
    return received;
}

static void ingress_task(void *context)
{
    (void)context;
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
        if (ingress_service(portTICK_PERIOD_MS) >= RAW_BATCH) vTaskDelay(1);
    }
}

esp_err_t midi_ingress_start(void)
{
    TaskHandle_t task = NULL;
    if (xTaskCreatePinnedToCore(ingress_task, "midi_ingress", 4096, NULL, 7,
                                &task, 0) != pdPASS) return ESP_ERR_NO_MEM;
    esp_err_t err = usb_midi_host_start();
    if (err == ESP_OK) xTaskNotifyGive(task);
    else vTaskDelete(task);
    return err;
}

void midi_ingress_drain(midi_ingress_state_t *state, synth_t *synth, bool live)
{
    const uint32_t active = midi_ingress_active_session();
    const uint32_t session = live ? active : 0;
    const uint32_t dropped = midi_queue_dropped_count();
    const bool lost_events = dropped != state->dropped;
    if ((state->session && state->session != session) || (live && lost_events)) {
        synth_release_source(synth, SYNTH_SOURCE_USB);
        ++state->panics;
    }
    if (lost_events) midi_queue_reset();
    state->session = session;
    state->dropped = dropped;
    midi_queue_set_capture_epoch(synth_live_epoch(synth));

    for (unsigned n = 0; n < MIDI_INGRESS_EVENTS_PER_BLOCK; ++n) {
        /* If input changes during the drain, leave resynchronization to the
         * next block so a burst of failures cannot cause repeated panics here. */
        if (midi_ingress_active_session() != active || midi_queue_dropped_count() != dropped) break;
        midi_event_t event;
        if (!midi_queue_pop(&event, 0)) break;
        if (!session || event.session != session || event.epoch != synth_live_epoch(synth) ||
            (state->resume_after_us && event.timestamp_us <= state->resume_after_us) ||
            midi_ingress_active_session() != active || midi_queue_dropped_count() != dropped) {
            ++state->rejected;
            continue;
        }

        const uint8_t channel = synth_get_midi_channel(synth);
        if (event.type != MIDI_EVENT_SYSTEM_RESET && channel && event.channel + 1 != channel) {
            ++state->rejected;
            continue;
        }
        if (event.type == MIDI_EVENT_SYSTEM_RESET ||
            (event.type == MIDI_EVENT_CONTROL_CHANGE && (event.cc.controller == 120 || event.cc.controller == 123))) {
            synth_release_source(synth, SYNTH_SOURCE_USB);
        } else {
            (void)synth_sound_event_from(synth, SYNTH_SOURCE_USB, event.epoch, &event);
        }
        ++state->applied;
        midi_queue_set_capture_epoch(synth_live_epoch(synth));
        const int64_t age = esp_timer_get_time() - event.timestamp_us;
        if (age > 0 && age <= UINT32_MAX && (uint32_t)age > state->receive_to_dispatch_max_us) {
            state->receive_to_dispatch_max_us = (uint32_t)age;
        }
    }
}
