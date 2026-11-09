#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "bt_pcm.h"
#include "bt_route.h"

enum { BT_OUTPUT_DEVICES = 8, BT_ADDRESS_BYTES = 6, BT_NAME_BYTES = 25 };

typedef enum {
    BT_BOOTING, BT_IDLE, BT_SCANNING, BT_CONNECTING, BT_STREAMING, BT_CLOSING, BT_FAILED,
} bt_output_state_t;

typedef enum {
    BT_ERROR_NONE, BT_ERROR_SCAN, BT_ERROR_CONNECT, BT_ERROR_CODEC, BT_ERROR_TIMEOUT,
    BT_ERROR_LOST, BT_ERROR_PAIRING, BT_ERROR_QUEUE, BT_ERROR_START,
} bt_output_error_t;

typedef struct {
    uint8_t address[BT_ADDRESS_BYTES];
    char name[BT_NAME_BYTES];
} bt_output_device_t;

typedef struct {
    bt_output_state_t state;
    bt_output_error_t error;
    bt_route_sink_t route;
    bool requested, ready;
    unsigned rate, channels, count;
    uint32_t dropped_blocks, silent_frames;
    bt_output_device_t devices[BT_OUTPUT_DEVICES];
    uint8_t peer[BT_ADDRESS_BYTES];
} bt_output_status_t;

/* Boot owner, before audio starts. Control APIs submit to one Bluetooth worker. */
esp_err_t bt_output_start(void);
bt_output_status_t bt_output_status(void);
esp_err_t bt_output_scan(void);
esp_err_t bt_output_connect(const uint8_t address[BT_ADDRESS_BYTES]);
esp_err_t bt_output_disconnect(void);
esp_err_t bt_output_select(bool wireless);

/* Audio owner. No Bluetooth SDK calls, allocation, I/O, or waits. */
bt_route_sink_t bt_output_target(void);
void bt_output_route(bt_route_sink_t sink);
void bt_output_write(const int16_t pcm[BT_PCM_BLOCK][2]);
