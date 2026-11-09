#include "midi_parser.h"
#include <string.h>

#define USB_MIDI_CIN_MASK       0x0Fu
#define USB_MIDI_CIN_NOTE_OFF   0x08u
#define USB_MIDI_CIN_NOTE_ON    0x09u
#define USB_MIDI_CIN_POLY_PRESS 0x0Au
#define USB_MIDI_CIN_CC         0x0Bu
#define USB_MIDI_CIN_PROG_CHG   0x0Cu
#define USB_MIDI_CIN_CHAN_PRESS 0x0Du
#define USB_MIDI_CIN_PITCH_BEND 0x0Eu

#define MIDI_STATUS_MASK        0xF0u
#define MIDI_CHANNEL_MASK       0x0Fu

#define MIDI_STATUS_NOTE_OFF    0x80u
#define MIDI_STATUS_NOTE_ON     0x90u
#define MIDI_STATUS_POLY_PRESS  0xA0u
#define MIDI_STATUS_CC          0xB0u
#define MIDI_STATUS_PROG_CHG    0xC0u
#define MIDI_STATUS_CHAN_PRESS  0xD0u
#define MIDI_STATUS_PITCH_BEND  0xE0u

const char *midi_event_type_name(midi_event_type_t type)
{
    switch (type) {
    case MIDI_EVENT_NONE: return "NONE";
    case MIDI_EVENT_NOTE_ON: return "NOTE_ON";
    case MIDI_EVENT_NOTE_OFF: return "NOTE_OFF";
    case MIDI_EVENT_CONTROL_CHANGE: return "CC";
    case MIDI_EVENT_PITCH_BEND: return "PITCH_BEND";
    case MIDI_EVENT_PROGRAM_CHANGE: return "PROGRAM_CHANGE";
    case MIDI_EVENT_CHANNEL_PRESSURE: return "CHANNEL_PRESSURE";
    case MIDI_EVENT_POLY_PRESSURE: return "POLY_PRESSURE";
    case MIDI_EVENT_SYSTEM_RESET: return "SYSTEM_RESET";
    }
    return "UNKNOWN";
}

bool midi_parser_parse_usb_packet(const uint8_t packet[4],
                                  uint32_t session,
                                  int64_t timestamp_us,
                                  midi_event_t *out_event)
{
    if (!packet || !out_event) {
        return false;
    }

    const uint8_t cin = packet[0] & USB_MIDI_CIN_MASK;
    const uint8_t status = packet[1];
    const uint8_t data1 = packet[2] & 0x7Fu;
    const uint8_t data2 = packet[3] & 0x7Fu;

    /* Status byte must have MSB set (0x80 - 0xEF for voice messages) */
    if ((status & 0x80u) == 0u) {
        return false;
    }

    const uint8_t status_type = status & MIDI_STATUS_MASK;
    const uint8_t channel = status & MIDI_CHANNEL_MASK;

    midi_event_t ev = {
        .session = session,
        .timestamp_us = timestamp_us,
        .channel = channel,
    };

    if (cin == 0x0f && status == 0xff) {
        ev.type = MIDI_EVENT_SYSTEM_RESET;
        ev.channel = 0;
        *out_event = ev;
        return true;
    }

    switch (cin) {
    case USB_MIDI_CIN_NOTE_ON:
        if (status_type != MIDI_STATUS_NOTE_ON) return false;
        if (data2 == 0u) {
            /* Velocity 0 is semantically Note Off */
            ev.type = MIDI_EVENT_NOTE_OFF;
            ev.note.note = data1;
            ev.note.velocity = 0u;
        } else {
            ev.type = MIDI_EVENT_NOTE_ON;
            ev.note.note = data1;
            ev.note.velocity = data2;
        }
        break;

    case USB_MIDI_CIN_NOTE_OFF:
        if (status_type != MIDI_STATUS_NOTE_OFF) return false;
        ev.type = MIDI_EVENT_NOTE_OFF;
        ev.note.note = data1;
        ev.note.velocity = data2;
        break;

    case USB_MIDI_CIN_CC:
        if (status_type != MIDI_STATUS_CC) return false;
        ev.type = MIDI_EVENT_CONTROL_CHANGE;
        ev.cc.controller = data1;
        ev.cc.value = data2;
        break;

    case USB_MIDI_CIN_PITCH_BEND: {
        if (status_type != MIDI_STATUS_PITCH_BEND) return false;
        /* 14-bit pitch bend: data1 = LSB (7 bits), data2 = MSB (7 bits) */
        const uint16_t raw14 = (uint16_t)(data1 | ((uint16_t)data2 << 7));
        ev.type = MIDI_EVENT_PITCH_BEND;
        ev.pitch_bend.raw_14bit = raw14;
        /* Center is 0x2000 (8192) -> normalizes to -8192 .. +8191 */
        ev.pitch_bend.value = (int16_t)((int32_t)raw14 - 8192);
        break;
    }

    case USB_MIDI_CIN_PROG_CHG:
        if (status_type != MIDI_STATUS_PROG_CHG) return false;
        ev.type = MIDI_EVENT_PROGRAM_CHANGE;
        ev.program_change.program = data1;
        break;

    case USB_MIDI_CIN_CHAN_PRESS:
        if (status_type != MIDI_STATUS_CHAN_PRESS) return false;
        ev.type = MIDI_EVENT_CHANNEL_PRESSURE;
        ev.channel_pressure.pressure = data1;
        break;

    case USB_MIDI_CIN_POLY_PRESS:
        if (status_type != MIDI_STATUS_POLY_PRESS) return false;
        ev.type = MIDI_EVENT_POLY_PRESSURE;
        ev.poly_pressure.note = data1;
        ev.poly_pressure.pressure = data2;
        break;

    default:
        /* Ignore other CINs (SysEx, RealTime clock, etc.) for synth engine */
        return false;
    }

    *out_event = ev;
    return true;
}
