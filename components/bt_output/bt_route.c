#include "bt_route.h"

bt_route_block_t bt_route_step(bt_route_t *route, bt_route_sink_t target, unsigned dma_blocks)
{
    if (route->quiet_blocks) {
        --route->quiet_blocks;
        return (bt_route_block_t){BT_ROUTE_MUTED, BT_GAIN_STEADY, route->hold_pa};
    }
    if (route->current == target) return (bt_route_block_t){.sink = target};

    if (route->current != BT_ROUTE_MUTED) {
        const bool from_local = route->current == BT_ROUTE_LOCAL;
        route->current = BT_ROUTE_MUTED;
        route->quiet_blocks = dma_blocks;
        route->hold_pa = from_local;
        return (bt_route_block_t){
            from_local ? BT_ROUTE_LOCAL : BT_ROUTE_MUTED,
            from_local ? BT_GAIN_DOWN : BT_GAIN_STEADY,
            from_local,
        };
    }
    route->current = target;
    return (bt_route_block_t){.sink = target, .gain = BT_GAIN_UP};
}
