#pragma once

#include <stdbool.h>
#include <stdatomic.h>

typedef enum {
    STORAGE_IDLE, STORAGE_REQUESTED, STORAGE_CLAIMED, STORAGE_REJECTED,
    STORAGE_CANCELLED, STORAGE_HELD, STORAGE_RESUME, STORAGE_FAULT,
} storage_phase_t;

/* Cancellation and the mute/stop ACK contend for the same transition. Once
 * HELD is visible, only the caller that holds the lease may request resume. */
static inline bool storage_ack(_Atomic storage_phase_t *gate)
{
    storage_phase_t expected = STORAGE_CLAIMED;
    return atomic_compare_exchange_strong(gate, &expected, STORAGE_HELD);
}

static inline bool storage_cancel(_Atomic storage_phase_t *gate, storage_phase_t phase)
{
    if (phase != STORAGE_REQUESTED && phase != STORAGE_CLAIMED) return false;
    const storage_phase_t next = phase == STORAGE_REQUESTED ? STORAGE_IDLE : STORAGE_CANCELLED;
    return atomic_compare_exchange_strong(gate, &phase, next);
}
