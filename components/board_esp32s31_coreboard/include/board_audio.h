#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int sda;
    int scl;
    int mclk;
    int bclk;
    int ws;
    int dout;
    int din;
    int pa_enable;
    bool pa_active_high;
    uint8_t codec_address_7bit;
} board_audio_pins_t;

const board_audio_pins_t *board_audio_pins(void);
