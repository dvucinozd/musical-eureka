#pragma once

#include "audio_codec_if.h"

/* CoreBoard profile: slave, external 12.288 MHz MCLK, 48 kHz, 16-bit Philips
 * I2S. Values follow esp_codec_dev 1.6.2's clock table and DAC startup.
 * Return the first register that failed to read or did not match. */
static inline int es8311_check_playback_registers(const audio_codec_if_t *codec)
{
    static const struct {
        unsigned address, mask, expected;
    } registers[] = {
        {0x00, 0x40, 0x00}, /* codec is clock slave */
        {0x01, 0xff, 0x3f}, /* clock enables and MCLK source */
        {0x02, 0xf8, 0x00}, /* MCLK pre-divider and multiplier */
        {0x03, 0x7f, 0x10}, /* sample mode and ADC OSR */
        {0x04, 0x7f, 0x10}, /* DAC OSR */
        {0x05, 0xff, 0x00}, /* ADC and DAC dividers */
        {0x06, 0x3f, 0x03}, /* BCLK divider and polarity */
        {0x07, 0x3f, 0x00}, /* LRCK divider, high bits */
        {0x08, 0xff, 0xff}, /* LRCK divider, low bits */
        {0x09, 0x5f, 0x0c}, /* DAC input enabled, 16-bit Philips I2S */
        {0x12, 0x02, 0x00}, /* DAC powered up */
        {0x31, 0x60, 0x00}, /* DAC digital unmute */
        {0x32, 0xff, 0xae}, /* -12 dB request with driver's default HW gain */
    };

    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        int actual;
        if (codec->get_reg(codec, registers[i].address, &actual) ||
            ((unsigned)actual & registers[i].mask) != registers[i].expected) {
            return registers[i].address;
        }
    }
    return -1;
}
