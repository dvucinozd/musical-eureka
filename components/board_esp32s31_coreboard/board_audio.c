#include "board_audio.h"
#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S31
#error "This board profile requires ESP32-S31"
#endif

const board_audio_pins_t *board_audio_pins(void)
{
    /* CoreBoard-1 V1.0 schematic, sheets 2–3. See docs/HARDWARE.md. */
    static const board_audio_pins_t pins = {
        .sda = 51,
        .scl = 50,
        .mclk = 52,
        .bclk = 53,
        .ws = 55,
        .dout = 56,
        .din = 54,
        .pa_enable = 57,
        .pa_active_high = true,
        .codec_address_7bit = 0x18,
    };
    return &pins;
}
