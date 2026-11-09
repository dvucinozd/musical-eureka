#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bt_pcm.h"
#include "bt_route.h"

static int16_t sample(const uint8_t *data)
{
    return (int16_t)((unsigned)data[0] | (unsigned)data[1] << 8);
}

static void fill(bt_pcm_t *pcm, unsigned blocks, int16_t base, int16_t step)
{
    int16_t block[BT_PCM_BLOCK][2] = {0};
    for (unsigned n = 0; n < blocks; ++n) {
        for (unsigned i = 0; i < BT_PCM_BLOCK; ++i) {
            block[i][0] = base + (int)(n * BT_PCM_BLOCK + i) * step;
            block[i][1] = -777; /* BT always uses the instrument's mono channel. */
        }
        assert(bt_pcm_push(pcm, block));
    }
}

static void zero(const uint8_t *data, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) assert(data[i] == 0);
}

static void test_capacity_and_reconnect(void)
{
    bt_pcm_t pcm = {0};
    uint8_t data[BT_PCM_CAPACITY * 4];
    int16_t block[BT_PCM_BLOCK][2] = {0};
    assert(!bt_pcm_push(&pcm, block));
    bt_pcm_enable(&pcm, true);
    fill(&pcm, BT_PCM_CAPACITY / BT_PCM_BLOCK, 200, 0);
    assert(!bt_pcm_push(&pcm, block));
    assert(atomic_load(&pcm.dropped_blocks) == 1);

    bt_pcm_read(&pcm, data, 16, 48000, 2);
    for (unsigned i = 0; i < 16; i += 2) assert(sample(data + i) == 200);
    bt_pcm_enable(&pcm, false);
    bt_pcm_enable(&pcm, true); /* Reconnect before the consumer sees the inactive gate. */
    memset(data, 0xff, 16);
    bt_pcm_read(&pcm, data, 16, 48000, 2);
    zero(data, 16);
    fill(&pcm, BT_PCM_PREFILL / BT_PCM_BLOCK, -1234, 0);
    bt_pcm_read(&pcm, data, sizeof(data), 48000, 1);
    for (unsigned i = 0; i < BT_PCM_PREFILL; ++i) assert(sample(data + 2 * i) == -1234);
    zero(data + BT_PCM_PREFILL * 2, sizeof(data) - BT_PCM_PREFILL * 2);
    assert(atomic_load(&pcm.silent_frames) > 0);

    bt_pcm_enable(&pcm, false);
    memset(data, 0xff, 16);
    bt_pcm_read(&pcm, data, 16, 48000, 2);
    zero(data, 16);
}

static void test_interpolation_and_wrap(void)
{
    bt_pcm_t pcm = {0};
    const uint32_t start = UINT32_MAX - 63;
    atomic_store(&pcm.read_pos, start);
    atomic_store(&pcm.write_pos, start);
    atomic_store(&pcm.cutoff, start);
    bt_pcm_enable(&pcm, true);
    fill(&pcm, BT_PCM_PREFILL / BT_PCM_BLOCK, -400, 10);
    uint8_t data[441 * 4 + 1];
    bt_pcm_read(&pcm, data, 103 * 4, 44100, 2);
    bt_pcm_read(&pcm, data + 103 * 4, sizeof(data) - 103 * 4, 44100, 2);
    for (unsigned i = 0; i < 441; ++i) {
        const int want = -400 + (int)(i * 1600 / 147);
        assert(sample(data + i * 4) == want);
        assert(sample(data + i * 4 + 2) == want);
    }
    assert(data[sizeof(data) - 1] == 0);
    assert(atomic_load(&pcm.read_pos) == (uint32_t)(start + 480u));
    memset(data, 0xff, sizeof(data));
    bt_pcm_read(&pcm, data, sizeof(data), 32000, 2);
    zero(data, sizeof(data));
}

static void test_route(void)
{
    enum { DMA_BLOCKS = 6 };
    bt_route_t route = {0};
    bt_route_block_t block = bt_route_step(&route, bt_route_target(false, true), DMA_BLOCKS);
    assert(block.sink == BT_ROUTE_LOCAL && block.gain == BT_GAIN_STEADY);
    block = bt_route_step(&route, bt_route_target(true, true), DMA_BLOCKS);
    assert(block.sink == BT_ROUTE_LOCAL && block.gain == BT_GAIN_DOWN);
    for (unsigned i = 0; i < DMA_BLOCKS; ++i) {
        block = bt_route_step(&route, BT_ROUTE_WIRELESS, DMA_BLOCKS);
        assert(block.sink == BT_ROUTE_MUTED && block.hold_pa);
    }
    block = bt_route_step(&route, BT_ROUTE_WIRELESS, DMA_BLOCKS);
    assert(block.sink == BT_ROUTE_WIRELESS && block.gain == BT_GAIN_UP && !block.hold_pa);

    assert(bt_route_target(true, false) == BT_ROUTE_MUTED);
    for (unsigned i = 0; i < DMA_BLOCKS + 2; ++i) {
        block = bt_route_step(&route, bt_route_target(true, false), DMA_BLOCKS);
        assert(block.sink == BT_ROUTE_MUTED && !block.hold_pa);
    }
    block = bt_route_step(&route, bt_route_target(false, false), DMA_BLOCKS);
    assert(block.sink == BT_ROUTE_LOCAL && block.gain == BT_GAIN_UP);
}

int main(void)
{
    test_capacity_and_reconnect();
    test_interpolation_and_wrap();
    test_route();
    puts("P06B host PASS: local default, DMA drain, mute on link loss, explicit local recovery");
    puts("P06B host PASS: queue cap, wrap, generation cutoff, 48k and 44.1k PCM, channel duplication, silence");
}
