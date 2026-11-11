#include <assert.h>
#include <stdio.h>

#include "es8311_readback.h"

static int failing_read = -1;
static int values[256] = {
    [0x00] = 0x80, [0x01] = 0x3f, [0x03] = 0x10, [0x04] = 0x10,
    [0x06] = 0x03, [0x08] = 0xff, [0x09] = 0x0c, [0x32] = 0xae,
};

static int read_register(const audio_codec_if_t *codec, int reg, int *out)
{
    (void)codec;
    if (reg == failing_read) return -1;
    *out = values[reg];
    return 0;
}

int main(void)
{
    const audio_codec_if_t codec = {.get_reg = read_register};
    assert(es8311_check_playback_registers(&codec) == -1);
    /* LRCK low byte remains at reset value after a lost divider write.
     * The old SDP-only read would still accept the codec. */
    values[0x08] = 0;
    assert(es8311_check_playback_registers(&codec) == 0x08);
    values[0x08] = 0xff;
    values[0x31] = 0x60;
    assert(es8311_check_playback_registers(&codec) == 0x31);
    values[0x31] = 0;
    failing_read = 0x04;
    assert(es8311_check_playback_registers(&codec) == 0x04);
    puts("ES8311 PASS: good register state, lost divider write, DAC mute and I2C read failure");
}
