#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "usb_midi_desc.h"

static void test_null_args(void)
{
    usb_midi_endpoint_info_t ep;
    uint8_t dummy[16] = {0};
    assert(usb_midi_desc_find(NULL, 16, 64, &ep) == USB_MIDI_DESC_ERR_ARG);
    assert(usb_midi_desc_find(dummy, 16, 64, NULL) == USB_MIDI_DESC_ERR_ARG);
    assert(usb_midi_desc_find(dummy, 16, 0, &ep) == USB_MIDI_DESC_ERR_ARG);
}

static void test_total_length_bounds(void)
{
    usb_midi_endpoint_info_t ep;
    uint8_t cfg_short[8] = {9, 2, 9, 0, 1, 1, 0, 0x80};
    assert(usb_midi_desc_find(cfg_short, sizeof(cfg_short), 64, &ep) == USB_MIDI_DESC_ERR_TOTAL_LENGTH);

    uint8_t cfg_lie[9] = {9, 2, 20, 0, 1, 1, 0, 0x80, 50};
    assert(usb_midi_desc_find(cfg_lie, sizeof(cfg_lie), 64, &ep) == USB_MIDI_DESC_ERR_TOTAL_LENGTH);

    uint8_t cfg_bad_type[9] = {9, 3, 9, 0, 1, 1, 0, 0x80, 50};
    assert(usb_midi_desc_find(cfg_bad_type, sizeof(cfg_bad_type), 64, &ep) == USB_MIDI_DESC_ERR_TOTAL_LENGTH);
}

static void test_valid_midi_descriptor(void)
{
    // Typical standard USB MIDI Streaming config descriptor
    // Config (9) + Standard AC Intf (9) + Standard MS Intf (9) + Bulk IN EP (9) + Bulk OUT EP (9) = 45 bytes
    uint8_t cfg[] = {
        // Config descriptor (9 bytes)
        0x09, 0x02, 45, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
        // Standard AC Interface descriptor (9 bytes) - intf 0, alt 0, audio control
        0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
        // Standard MS Interface descriptor (9 bytes) - intf 1, alt 0, midi streaming
        0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
        // Standard Bulk IN Endpoint descriptor (9 bytes for MS EP or standard 7-9 bytes)
        0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
        // Standard Bulk OUT Endpoint descriptor (9 bytes)
        0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    };

    usb_midi_endpoint_info_t ep = {0};
    usb_midi_desc_result_t res = usb_midi_desc_find(cfg, sizeof(cfg), 64, &ep);
    assert(res == USB_MIDI_DESC_OK);
    assert(ep.interface_number == 1);
    assert(ep.alternate_setting == 0);
    assert(ep.in_ep_address == 0x81);
    assert(ep.in_max_packet == 64);
    assert(ep.has_out == true);
    assert(ep.out_ep_address == 0x01);
    assert(ep.out_max_packet == 64);
}

static void test_oversized_packet_rejected(void)
{
    uint8_t cfg[] = {
        0x09, 0x02, 27, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x01, 0x03, 0x00, 0x00,
        0x09, 0x05, 0x81, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00, // 128 bytes MPS!
    };
    usb_midi_endpoint_info_t ep = {0};
    // capacity is 64 bytes
    usb_midi_desc_result_t res = usb_midi_desc_find(cfg, sizeof(cfg), 64, &ep);
    assert(res == USB_MIDI_DESC_ERR_MAX_PACKET);
}

static void test_non_midi_descriptor(void)
{
    uint8_t cfg[] = {
        0x09, 0x02, 27, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x08, 0x06, 0x50, 0x00, // Mass storage
        0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    };
    usb_midi_endpoint_info_t ep = {0};
    usb_midi_desc_result_t res = usb_midi_desc_find(cfg, sizeof(cfg), 64, &ep);
    assert(res == USB_MIDI_DESC_ERR_NO_MIDI_STREAMING);
}

int main(void)
{
    test_null_args();
    test_total_length_bounds();
    test_valid_midi_descriptor();
    test_oversized_packet_rejected();
    test_non_midi_descriptor();
    printf("All usb_midi_desc tests passed!\n");
    return 0;
}
