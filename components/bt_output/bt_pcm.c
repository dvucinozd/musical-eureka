#include "bt_pcm.h"

#include <string.h>

enum { RING_MASK = BT_PCM_CAPACITY - 1, RATIO_DEN = 147, RATIO_NUM = 160 };
_Static_assert((BT_PCM_CAPACITY & RING_MASK) == 0, "PCM ring capacity must be a power of two");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "PCM indices must be lock-free");

void bt_pcm_enable(bt_pcm_t *pcm, bool enable)
{
    const uint32_t gate = atomic_load_explicit(&pcm->gate, memory_order_relaxed);
    if ((gate & 1u) == enable) return;
    if (!enable) {
        const uint32_t write = atomic_load_explicit(&pcm->write_pos, memory_order_relaxed);
        atomic_store_explicit(&pcm->cutoff, write, memory_order_relaxed);
    }
    atomic_store_explicit(&pcm->gate, gate + 1, memory_order_release);
}

bool bt_pcm_push(bt_pcm_t *pcm, const int16_t samples[BT_PCM_BLOCK][2])
{
    if (!(atomic_load_explicit(&pcm->gate, memory_order_relaxed) & 1u)) return false;
    const uint32_t write = atomic_load_explicit(&pcm->write_pos, memory_order_relaxed);
    const uint32_t read = atomic_load_explicit(&pcm->read_pos, memory_order_acquire);
    if (write - read > BT_PCM_CAPACITY - BT_PCM_BLOCK) {
        atomic_fetch_add_explicit(&pcm->dropped_blocks, 1, memory_order_relaxed);
        return false;
    }
    for (unsigned i = 0; i < BT_PCM_BLOCK; ++i)
        pcm->samples[(write + i) & RING_MASK] = samples[i][0];
    atomic_store_explicit(&pcm->write_pos, write + BT_PCM_BLOCK, memory_order_release);
    return true;
}

void bt_pcm_read(bt_pcm_t *pcm, uint8_t *data, size_t bytes, unsigned rate, unsigned channels)
{
    const uint32_t gate = atomic_load_explicit(&pcm->gate, memory_order_acquire);
    uint32_t read = atomic_load_explicit(&pcm->read_pos, memory_order_relaxed);
    if (gate != pcm->seen_gate) {
        /* The producer's cutoff can be behind a callback that finished just
         * before it observed the new gate. Never move the consumer backwards. */
        const uint32_t cutoff = atomic_load_explicit(&pcm->cutoff, memory_order_relaxed);
        if ((uint32_t)(cutoff - read) <= BT_PCM_CAPACITY) read = cutoff;
        pcm->seen_gate = gate;
        pcm->primed = false;
        pcm->phase = 0;
    }
    if (rate != pcm->rate || channels != pcm->channels) {
        pcm->rate = rate;
        pcm->channels = channels;
        pcm->primed = false;
        pcm->phase = 0;
    }
    const uint32_t write = atomic_load_explicit(&pcm->write_pos, memory_order_acquire);
    const bool active = (gate & 1u) && (rate == 48000 || rate == 44100) &&
                        (channels == 1 || channels == 2);
    if (active && write - read >= BT_PCM_PREFILL) pcm->primed = true;

    size_t offset = 0;
    if (active && pcm->primed) {
        const unsigned frame_bytes = 2 * channels;
        while (bytes - offset >= frame_bytes && write - read >= (rate == 44100 ? 2u : 1u)) {
            int32_t sample = pcm->samples[read & RING_MASK];
            if (rate == 44100) {
                const int32_t next = pcm->samples[(read + 1) & RING_MASK];
                sample += (next - sample) * (int32_t)pcm->phase / RATIO_DEN;
                pcm->phase += RATIO_NUM;
                read += pcm->phase / RATIO_DEN;
                pcm->phase %= RATIO_DEN;
            } else {
                ++read;
            }
            const uint16_t word = (uint16_t)sample;
            for (unsigned ch = 0; ch < channels; ++ch) {
                data[offset++] = (uint8_t)word;
                data[offset++] = (uint8_t)(word >> 8);
            }
        }
    }
    atomic_store_explicit(&pcm->read_pos, read, memory_order_release);
    if (gate != atomic_load_explicit(&pcm->gate, memory_order_acquire)) offset = 0;
    if (offset < bytes) {
        memset(data + offset, 0, bytes - offset);
        if (active && bytes - offset >= 2 * channels) {
            pcm->primed = false;
            atomic_fetch_add_explicit(&pcm->silent_frames, (bytes - offset) / (2 * channels),
                                      memory_order_relaxed);
        }
    }
}
