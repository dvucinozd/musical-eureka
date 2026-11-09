#pragma once

/* Pure USB-MIDI configuration-descriptor validation. No ESP-IDF dependency so
 * the same code runs in firmware and in PC tests (tests/usb_midi_host). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full-speed bulk endpoints are at most 64 bytes. The IN transfer buffer is
 * allocated once with this capacity and reused across sessions, so any
 * endpoint advertising a larger packet is rejected rather than overrun. */
#define USB_MIDI_IN_BUFFER_BYTES 64u

typedef enum {
    USB_MIDI_DESC_OK = 0,
    USB_MIDI_DESC_ERR_ARG,
    USB_MIDI_DESC_ERR_TOTAL_LENGTH,
    USB_MIDI_DESC_ERR_MALFORMED,
    USB_MIDI_DESC_ERR_NO_MIDI_STREAMING,
    USB_MIDI_DESC_ERR_UNSUPPORTED_ALT,
    USB_MIDI_DESC_ERR_NO_IN_ENDPOINT,
    USB_MIDI_DESC_ERR_ENDPOINT_TYPE,
    USB_MIDI_DESC_ERR_MAX_PACKET,
} usb_midi_desc_result_t;

typedef struct {
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t in_ep_address;
    uint8_t in_ep_attributes;
    uint16_t in_max_packet;
    bool has_out;
    uint8_t out_ep_address;
    uint16_t out_max_packet;
} usb_midi_endpoint_info_t;

/* Finds the first Audio/MIDIStreaming interface (class 0x01, subclass 0x03,
 * alternate setting 0) with a bulk or interrupt IN endpoint whose packet size
 * fits in in_buffer_capacity.
 *
 * config:        raw configuration descriptor (starts with bLength=9, type 2)
 * available_len: bytes readable at config; wTotalLength must not exceed it
 * Every descriptor header is bounds-checked before it is interpreted. */
usb_midi_desc_result_t usb_midi_desc_find(const uint8_t *config,
                                          size_t available_len,
                                          size_t in_buffer_capacity,
                                          usb_midi_endpoint_info_t *out);

const char *usb_midi_desc_result_name(usb_midi_desc_result_t result);

#ifdef __cplusplus
}
#endif
