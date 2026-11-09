#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "midi_queue.h"

int main(void)
{
    assert(midi_queue_init() == ESP_OK);

    midi_queue_stats_t stats;
    midi_queue_get_stats(&stats);
    assert(stats.pushed == 0);
    assert(stats.popped == 0);
    assert(stats.dropped == 0);

    // Push 3 events
    midi_event_t ev1 = {.type = MIDI_EVENT_NOTE_ON, .channel = 0, .note = {.note = 60, .velocity = 100}};
    midi_event_t ev2 = {.type = MIDI_EVENT_NOTE_OFF, .channel = 0, .note = {.note = 60, .velocity = 0}};
    midi_event_t ev3 = {.type = MIDI_EVENT_CONTROL_CHANGE, .channel = 0, .cc = {.controller = 64, .value = 127}};

    assert(midi_queue_push(&ev1) == true);
    assert(midi_queue_push(&ev2) == true);
    assert(midi_queue_push(&ev3) == true);

    midi_queue_get_stats(&stats);
    assert(stats.pushed == 3);
    assert(stats.high_water == 3);

    // Pop and verify order (FIFO)
    midi_event_t out;
    assert(midi_queue_pop(&out, 0) == true);
    assert(out.type == MIDI_EVENT_NOTE_ON && out.note.note == 60);

    assert(midi_queue_pop(&out, 0) == true);
    assert(out.type == MIDI_EVENT_NOTE_OFF && out.note.note == 60);

    assert(midi_queue_pop(&out, 0) == true);
    assert(out.type == MIDI_EVENT_CONTROL_CHANGE && out.cc.controller == 64);

    // Queue empty
    assert(midi_queue_pop(&out, 0) == false);

    midi_queue_get_stats(&stats);
    assert(stats.popped == 3);

    // Fill to capacity and verify overflow drops
    for (uint32_t i = 0; i < MIDI_EVENT_QUEUE_CAPACITY; ++i) {
        assert(midi_queue_push(&ev1) == true);
    }
    // 129th push must fail (dropped)
    assert(midi_queue_push(&ev1) == false);

    midi_queue_get_stats(&stats);
    assert(stats.dropped == 1);
    assert(stats.high_water == MIDI_EVENT_QUEUE_CAPACITY);

    // Reset clears all
    midi_queue_reset();
    assert(midi_queue_pop(&out, 0) == false);

    printf("All midi_queue unit tests passed!\n");
    return 0;
}
