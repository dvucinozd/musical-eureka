#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "midi_event.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_EVENT_QUEUE_CAPACITY 128u

typedef struct {
    uint32_t pushed;
    uint32_t popped;
    uint32_t dropped;
    uint32_t high_water;
} midi_queue_stats_t;

/* Initializes the static MIDI event queue. Call once at startup. */
esp_err_t midi_queue_init(void);

/* Pushes a parsed MIDI event into the queue. Non-blocking. The queued copy is
 * stamped with the capture epoch, so an event captured before a live sound
 * change is never relabelled with the new epoch when it is popped.
 * Returns true if accepted, false if dropped due to queue overflow. */
bool midi_queue_push(const midi_event_t *event);

/* Audio owner publishes the live input epoch; producers never set it. */
void midi_queue_set_capture_epoch(uint32_t epoch);

/* Pops the next MIDI event. If queue is empty, waits up to timeout_ms.
 * Returns true if an event was popped, false on timeout. */
bool midi_queue_pop(midi_event_t *out_event, uint32_t timeout_ms);

/* Resets the queue (e.g. on device disconnect or all notes off). */
void midi_queue_reset(void);
uint32_t midi_queue_dropped_count(void);

void midi_queue_get_stats(midi_queue_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
