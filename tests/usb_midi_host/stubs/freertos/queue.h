#pragma once

#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

typedef void *QueueHandle_t;

typedef struct {
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t capacity;
    uint32_t item_size;
    uint8_t storage[256 * 64];
} stub_queue_t;

static inline QueueHandle_t xQueueCreateStatic(uint32_t len, uint32_t size, uint8_t *storage, StaticQueue_t *state)
{
    (void)storage; (void)state;
    stub_queue_t *q = (stub_queue_t *)calloc(1, sizeof(stub_queue_t));
    if (!q) return NULL;
    q->capacity = len;
    q->item_size = size;
    return (QueueHandle_t)q;
}

static inline QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    return xQueueCreateStatic(length, item_size, NULL, NULL);
}

static inline BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)ticks;
    stub_queue_t *q = (stub_queue_t *)queue;
    if (!q || q->count >= q->capacity) return pdFALSE;
    memcpy(&q->storage[q->head * q->item_size], item, q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count++;
    return pdTRUE;
}

static inline BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    (void)ticks;
    stub_queue_t *q = (stub_queue_t *)queue;
    if (!q || q->count == 0) return pdFALSE;
    memcpy(item, &q->storage[q->tail * q->item_size], q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count--;
    return pdTRUE;
}

static inline BaseType_t xQueueReset(QueueHandle_t queue)
{
    stub_queue_t *q = (stub_queue_t *)queue;
    if (q) { q->head = q->tail = q->count = 0; }
    return pdPASS;
}

static inline unsigned uxQueueMessagesWaiting(QueueHandle_t queue)
{
    stub_queue_t *q = (stub_queue_t *)queue;
    return q ? q->count : 0u;
}

static inline void vQueueDelete(QueueHandle_t queue)
{
    free(queue);
}
