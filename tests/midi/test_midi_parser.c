#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "midi_parser.h"
#include "midi_event.h"

static void test_note_on_and_off(void)
{
    midi_event_t ev;

    // CIN 0x09: Note On, Ch 1 (status 0x90), Note C4 (60 = 0x3C), Velocity 100 (0x64)
    uint8_t pkt_on[4] = {0x09, 0x90, 0x3C, 0x64};
    assert(midi_parser_parse_usb_packet(pkt_on, 1, 1000, &ev) == true);
    assert(ev.type == MIDI_EVENT_NOTE_ON);
    assert(ev.channel == 0); // MIDI Channel 1 = index 0
    assert(ev.session == 1);
    assert(ev.timestamp_us == 1000);
    assert(ev.note.note == 60);
    assert(ev.note.velocity == 100);

    // CIN 0x09 with Velocity 0 -> Must normalize to Note Off!
    uint8_t pkt_on_vel0[4] = {0x09, 0x90, 0x3C, 0x00};
    assert(midi_parser_parse_usb_packet(pkt_on_vel0, 1, 1100, &ev) == true);
    assert(ev.type == MIDI_EVENT_NOTE_OFF);
    assert(ev.channel == 0);
    assert(ev.note.note == 60);
    assert(ev.note.velocity == 0);

    // CIN 0x08: Explicit Note Off, Ch 2 (status 0x81), Note D4 (62 = 0x3E), Velocity 64
    uint8_t pkt_off[4] = {0x08, 0x81, 0x3E, 0x40};
    assert(midi_parser_parse_usb_packet(pkt_off, 1, 1200, &ev) == true);
    assert(ev.type == MIDI_EVENT_NOTE_OFF);
    assert(ev.channel == 1);
    assert(ev.note.note == 62);
    assert(ev.note.velocity == 64);
}

static void test_control_change(void)
{
    midi_event_t ev;

    // CIN 0x0B: Control Change, Ch 1 (status 0xB0), Sustain Pedal (CC 64 = 0x40), Value 127
    uint8_t pkt_sustain[4] = {0x0B, 0xB0, 0x40, 0x7F};
    assert(midi_parser_parse_usb_packet(pkt_sustain, 2, 2000, &ev) == true);
    assert(ev.type == MIDI_EVENT_CONTROL_CHANGE);
    assert(ev.channel == 0);
    assert(ev.cc.controller == MIDI_CC_SUSTAIN_PEDAL);
    assert(ev.cc.value == 127);

    // Modulation Wheel (CC 1), Value 64
    uint8_t pkt_mod[4] = {0x0B, 0xB0, 0x01, 0x40};
    assert(midi_parser_parse_usb_packet(pkt_mod, 2, 2100, &ev) == true);
    assert(ev.type == MIDI_EVENT_CONTROL_CHANGE);
    assert(ev.cc.controller == MIDI_CC_MODULATION_WHEEL);
    assert(ev.cc.value == 64);
}

static void test_pitch_bend(void)
{
    midi_event_t ev;

    // Pitch bend center: LSB = 0x00, MSB = 0x40 -> 0x2000 (8192) -> normalizes to 0
    uint8_t pkt_center[4] = {0x0E, 0xE0, 0x00, 0x40};
    assert(midi_parser_parse_usb_packet(pkt_center, 1, 3000, &ev) == true);
    assert(ev.type == MIDI_EVENT_PITCH_BEND);
    assert(ev.pitch_bend.raw_14bit == 8192);
    assert(ev.pitch_bend.value == 0);

    // Pitch bend minimum: LSB = 0x00, MSB = 0x00 -> 0 -> normalizes to -8192
    uint8_t pkt_min[4] = {0x0E, 0xE0, 0x00, 0x00};
    assert(midi_parser_parse_usb_packet(pkt_min, 1, 3100, &ev) == true);
    assert(ev.type == MIDI_EVENT_PITCH_BEND);
    assert(ev.pitch_bend.raw_14bit == 0);
    assert(ev.pitch_bend.value == -8192);

    // Pitch bend maximum: LSB = 0x7F, MSB = 0x7F -> 0x3FFF (16383) -> normalizes to +8191
    uint8_t pkt_max[4] = {0x0E, 0xE0, 0x7F, 0x7F};
    assert(midi_parser_parse_usb_packet(pkt_max, 1, 3200, &ev) == true);
    assert(ev.type == MIDI_EVENT_PITCH_BEND);
    assert(ev.pitch_bend.raw_14bit == 16383);
    assert(ev.pitch_bend.value == 8191);
}

static void test_program_and_pressure(void)
{
    midi_event_t ev;

    // Program change: Program 12 on Channel 3 (0xC2)
    uint8_t pkt_prog[4] = {0x0C, 0xC2, 12, 0x00};
    assert(midi_parser_parse_usb_packet(pkt_prog, 1, 4000, &ev) == true);
    assert(ev.type == MIDI_EVENT_PROGRAM_CHANGE);
    assert(ev.channel == 2);
    assert(ev.program_change.program == 12);

    // Channel pressure: Pressure 88 on Channel 1 (0xD0)
    uint8_t pkt_press[4] = {0x0D, 0xD0, 88, 0x00};
    assert(midi_parser_parse_usb_packet(pkt_press, 1, 4100, &ev) == true);
    assert(ev.type == MIDI_EVENT_CHANNEL_PRESSURE);
    assert(ev.channel == 0);
    assert(ev.channel_pressure.pressure == 88);
}

static void test_invalid_packets(void)
{
    midi_event_t ev;

    // Null args
    assert(midi_parser_parse_usb_packet(NULL, 1, 0, &ev) == false);
    assert(midi_parser_parse_usb_packet((uint8_t[]){0x09, 0x90, 60, 100}, 1, 0, NULL) == false);

    // Status byte MSB is not set (e.g. 0x10 instead of 0x90)
    uint8_t pkt_bad_status[4] = {0x09, 0x10, 60, 100};
    assert(midi_parser_parse_usb_packet(pkt_bad_status, 1, 0, &ev) == false);

    // Mismatched CIN and status byte (e.g. CIN 0x09 Note On with Status 0xB0 CC)
    uint8_t pkt_mismatch[4] = {0x09, 0xB0, 60, 100};
    assert(midi_parser_parse_usb_packet(pkt_mismatch, 1, 0, &ev) == false);

    // Unknown or unsupported CIN (e.g. 0x01 or 0x0F)
    uint8_t pkt_unknown[4] = {0x01, 0x90, 60, 100};
    assert(midi_parser_parse_usb_packet(pkt_unknown, 1, 0, &ev) == false);
}

int main(void)
{
    test_note_on_and_off();
    test_control_change();
    test_pitch_bend();
    test_program_and_pressure();
    test_invalid_packets();

    printf("All midi_parser unit tests passed!\n");
    return 0;
}
