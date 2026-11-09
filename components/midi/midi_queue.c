#include "midi_queue.h"
#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static StaticQueue_t s_static_queue;
static uint8_t s_queue_storage[MIDI_EVENT_QUEUE_CAPACITY * sizeof(midi_event_t)];
static QueueHandle_t s_midi_queue = NULL;

static atomic_uint_fast32_t s_cnt_pushed;
static atomic_uint_fast32_t s_cnt_popped;
static atomic_uint_fast32_t s_cnt_dropped;
static atomic_uint_fast32_t s_cnt_high_water;

esp_err_t midi_queue_init(void)
{
    if (s_midi_queue) {
        return ESP_OK;
    }
    s_midi_queue = xQueueCreateStatic(MIDI_EVENT_QUEUE_CAPACITY,
                                      sizeof(midi_event_t),
                                      s_queue_storage,
                                      &s_static_queue);
    if (!s_midi_queue) {
        return ESP_ERR_NO_MEM;
    }
    atomic_store(&s_cnt_pushed, 0u);
    atomic_store(&s_cnt_popped, 0u);
    atomic_store(&s_cnt_dropped, 0u);
    atomic_store(&s_cnt_high_water, 0u);
    return ESP_OK;
}

static _Atomic uint32_t s_capture_epoch;

void midi_queue_set_capture_epoch(uint32_t epoch)
{
    atomic_store_explicit(&s_capture_epoch, epoch, memory_order_release);
}

bool midi_queue_push(const midi_event_t *event)
{
    if (!s_midi_queue || !event) {
        return false;
    }
    midi_event_t stamped = *event;
    stamped.epoch = atomic_load_explicit(&s_capture_epoch, memory_order_acquire);
    if (xQueueSend(s_midi_queue, &stamped, 0) == pdTRUE) {
        atomic_fetch_add_explicit(&s_cnt_pushed, 1u, memory_order_relaxed);
        uint32_t waiting = (uint32_t)uxQueueMessagesWaiting(s_midi_queue);
        uint32_t prev_hw = atomic_load_explicit(&s_cnt_high_water, memory_order_relaxed);
        while (waiting > prev_hw &&
               !atomic_compare_exchange_weak_explicit(&s_cnt_high_water, &prev_hw, waiting,
                                                      memory_order_relaxed, memory_order_relaxed)) {
        }
        return true;
    }
    atomic_fetch_add_explicit(&s_cnt_dropped, 1u, memory_order_relaxed);
    return false;
}

bool midi_queue_pop(midi_event_t *out_event, uint32_t timeout_ms)
{
    if (!s_midi_queue || !out_event) {
        return false;
    }
    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(s_midi_queue, out_event, wait) == pdTRUE) {
        atomic_fetch_add_explicit(&s_cnt_popped, 1u, memory_order_relaxed);
        return true;
    }
    return false;
}

void midi_queue_reset(void)
{
    if (s_midi_queue) {
        xQueueReset(s_midi_queue);
    }
}

uint32_t midi_queue_dropped_count(void)
{
    return (uint32_t)atomic_load(&s_cnt_dropped);
}

void midi_queue_get_stats(midi_queue_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    out_stats->pushed = (uint32_t)atomic_load_explicit(&s_cnt_pushed, memory_order_relaxed);
    out_stats->popped = (uint32_t)atomic_load_explicit(&s_cnt_popped, memory_order_relaxed);
    out_stats->dropped = (uint32_t)atomic_load_explicit(&s_cnt_dropped, memory_order_relaxed);
    out_stats->high_water = (uint32_t)atomic_load_explicit(&s_cnt_high_water, memory_order_relaxed);
}
