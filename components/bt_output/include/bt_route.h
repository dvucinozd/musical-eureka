#pragma once

#include <stdbool.h>

typedef enum { BT_ROUTE_LOCAL, BT_ROUTE_WIRELESS, BT_ROUTE_MUTED } bt_route_sink_t;
typedef enum { BT_GAIN_STEADY, BT_GAIN_UP, BT_GAIN_DOWN } bt_route_gain_t;

typedef struct {
    bt_route_sink_t current;
    unsigned quiet_blocks;
    bool hold_pa;
} bt_route_t;

typedef struct {
    bt_route_sink_t sink;
    bt_route_gain_t gain;
    bool hold_pa;
} bt_route_block_t;

bt_route_block_t bt_route_step(bt_route_t *route, bt_route_sink_t target, unsigned dma_blocks);

static inline bt_route_sink_t bt_route_target(bool requested, bool link_ready)
{
    if (!requested) return BT_ROUTE_LOCAL;
    return link_ready ? BT_ROUTE_WIRELESS : BT_ROUTE_MUTED;
}
