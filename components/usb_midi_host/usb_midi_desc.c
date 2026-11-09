#include "usb_midi_desc.h"

#define DESC_TYPE_CONFIGURATION 0x02u
#define DESC_TYPE_INTERFACE     0x04u
#define DESC_TYPE_ENDPOINT      0x05u
#define CLASS_AUDIO             0x01u
#define SUBCLASS_MIDISTREAMING  0x03u
#define EP_XFER_MASK            0x03u
#define EP_XFER_BULK            0x02u
#define EP_XFER_INTERRUPT       0x03u
#define EP_DIR_IN               0x80u
#define EP_MPS_MASK             0x07FFu

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

usb_midi_desc_result_t usb_midi_desc_find(const uint8_t *config,
                                          size_t available_len,
                                          size_t in_buffer_capacity,
                                          usb_midi_endpoint_info_t *out)
{
    if (!config || !out || in_buffer_capacity == 0u) return USB_MIDI_DESC_ERR_ARG;
    if (available_len < 9u || config[0] < 9u || config[1] != DESC_TYPE_CONFIGURATION) {
        return USB_MIDI_DESC_ERR_TOTAL_LENGTH;
    }
    const size_t total = rd16(&config[2]);
    if (total < 9u || total > available_len) return USB_MIDI_DESC_ERR_TOTAL_LENGTH;

    usb_midi_desc_result_t failure = USB_MIDI_DESC_ERR_NO_MIDI_STREAMING;
    usb_midi_endpoint_info_t candidate = {0};
    bool in_midi = false, found_in = false;

    size_t off = 0u;
    while (off < total) {
        if (total - off < 2u) return USB_MIDI_DESC_ERR_MALFORMED;
        const uint8_t len = config[off];
        const uint8_t type = config[off + 1u];
        if (len < 2u || len > total - off) return USB_MIDI_DESC_ERR_MALFORMED;
        const uint8_t *d = &config[off];

        if (type == DESC_TYPE_INTERFACE) {
            if (len < 9u) return USB_MIDI_DESC_ERR_MALFORMED;
            if (in_midi && found_in) break; /* complete candidate before next interface */
            in_midi = false;
            if (d[5] == CLASS_AUDIO && d[6] == SUBCLASS_MIDISTREAMING) {
                if (d[3] != 0u) {
                    /* Non-zero alternates require SET_INTERFACE, not part of M2. */
                    if (failure == USB_MIDI_DESC_ERR_NO_MIDI_STREAMING) {
                        failure = USB_MIDI_DESC_ERR_UNSUPPORTED_ALT;
                    }
                } else {
                    in_midi = true;
                    found_in = false;
                    candidate = (usb_midi_endpoint_info_t){
                        .interface_number = d[2],
                        .alternate_setting = d[3],
                    };
                    if (failure == USB_MIDI_DESC_ERR_NO_MIDI_STREAMING ||
                        failure == USB_MIDI_DESC_ERR_UNSUPPORTED_ALT) {
                        failure = USB_MIDI_DESC_ERR_NO_IN_ENDPOINT;
                    }
                }
            }
        } else if (type == DESC_TYPE_ENDPOINT && in_midi) {
            if (len < 7u) return USB_MIDI_DESC_ERR_MALFORMED;
            const uint8_t addr = d[2];
            const uint8_t attr = d[3];
            const uint16_t mps = (uint16_t)(rd16(&d[4]) & EP_MPS_MASK);
            const uint8_t xfer = attr & EP_XFER_MASK;
            const bool usable_type = xfer == EP_XFER_BULK || xfer == EP_XFER_INTERRUPT;
            if (addr & EP_DIR_IN) {
                if (!found_in) {
                    if (!usable_type) {
                        failure = USB_MIDI_DESC_ERR_ENDPOINT_TYPE;
                    } else if (mps < 4u || mps > in_buffer_capacity) {
                        failure = USB_MIDI_DESC_ERR_MAX_PACKET;
                    } else {
                        candidate.in_ep_address = addr;
                        candidate.in_ep_attributes = attr;
                        candidate.in_max_packet = mps;
                        found_in = true;
                    }
                }
            } else if (!candidate.has_out && usable_type && mps != 0u) {
                candidate.has_out = true;
                candidate.out_ep_address = addr;
                candidate.out_max_packet = mps;
            }
        }
        off += len;
    }

    if (in_midi && found_in) {
        *out = candidate;
        return USB_MIDI_DESC_OK;
    }
    return failure;
}

const char *usb_midi_desc_result_name(usb_midi_desc_result_t result)
{
    switch (result) {
    case USB_MIDI_DESC_OK: return "OK";
    case USB_MIDI_DESC_ERR_ARG: return "ARG";
    case USB_MIDI_DESC_ERR_TOTAL_LENGTH: return "TOTAL_LENGTH";
    case USB_MIDI_DESC_ERR_MALFORMED: return "MALFORMED";
    case USB_MIDI_DESC_ERR_NO_MIDI_STREAMING: return "NO_MIDI_STREAMING";
    case USB_MIDI_DESC_ERR_UNSUPPORTED_ALT: return "UNSUPPORTED_ALT";
    case USB_MIDI_DESC_ERR_NO_IN_ENDPOINT: return "NO_IN_ENDPOINT";
    case USB_MIDI_DESC_ERR_ENDPOINT_TYPE: return "ENDPOINT_TYPE";
    case USB_MIDI_DESC_ERR_MAX_PACKET: return "MAX_PACKET";
    }
    return "UNKNOWN";
}
