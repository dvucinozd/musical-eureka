#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MIDI_EVENT_NONE = 0,
    MIDI_EVENT_NOTE_ON,
    MIDI_EVENT_NOTE_OFF,
    MIDI_EVENT_CONTROL_CHANGE,
    MIDI_EVENT_PITCH_BEND,
    MIDI_EVENT_PROGRAM_CHANGE,
    MIDI_EVENT_CHANNEL_PRESSURE,
    MIDI_EVENT_POLY_PRESSURE,
    MIDI_EVENT_SYSTEM_RESET,
} midi_event_type_t;

/* Standard MIDI Control Change numbers */
typedef enum {
    MIDI_CC_BANK_SELECT_MSB = 0,
    MIDI_CC_MODULATION_WHEEL = 1,
    MIDI_CC_BREATH_CONTROLLER = 2,
    MIDI_CC_FOOT_CONTROLLER = 4,
    MIDI_CC_PORTAMENTO_TIME = 5,
    MIDI_CC_DATA_ENTRY_MSB = 6,
    MIDI_CC_CHANNEL_VOLUME = 7,
    MIDI_CC_BALANCE = 8,
    MIDI_CC_PAN = 10,
    MIDI_CC_EXPRESSION = 11,
    MIDI_CC_SUSTAIN_PEDAL = 64,     /* Hold 1: damper/sustain (>=64 on, <64 off) */
    MIDI_CC_PORTAMENTO_ENABLE = 65,
    MIDI_CC_SOSTENUTO = 66,
    MIDI_CC_SOFT_PEDAL = 67,
    MIDI_CC_RESONANCE = 71,
    MIDI_CC_RELEASE_TIME = 72,
    MIDI_CC_ATTACK_TIME = 73,
    MIDI_CC_CUTOFF_FREQUENCY = 74,
    MIDI_CC_DECAY_TIME = 75,
    MIDI_CC_REVERB_SEND = 91,
    MIDI_CC_CHORUS_SEND = 93,
    MIDI_CC_ALL_SOUND_OFF = 120,
    MIDI_CC_RESET_ALL_CONTROLLERS = 121,
    MIDI_CC_ALL_NOTES_OFF = 123,
} midi_cc_number_t;

typedef struct {
    midi_event_type_t type;
    uint8_t channel;          /* 0 - 15 (MIDI Channel 1 - 16) */
    uint32_t session;         /* Host USB session ID when event was captured */
    int64_t timestamp_us;     /* Time in microseconds when event arrived */
    uint32_t epoch;           /* Live input epoch when queued (midi_queue_push) */
    union {
        struct {
            uint8_t note;     /* 0 - 127 (60 = Middle C / C4) */
            uint8_t velocity; /* 1 - 127 for Note On, 0 - 127 for Note Off */
        } note;
        struct {
            uint8_t controller; /* 0 - 127 */
            uint8_t value;      /* 0 - 127 */
        } cc;
        struct {
            int16_t value;      /* -8192 to +8191 (0 = center/no bend) */
            uint16_t raw_14bit; /* 0 to 16383 (8192 = 0x2000 = center) */
        } pitch_bend;
        struct {
            uint8_t program;    /* 0 - 127 */
        } program_change;
        struct {
            uint8_t pressure;   /* 0 - 127 */
        } channel_pressure;
        struct {
            uint8_t note;       /* 0 - 127 */
            uint8_t pressure;   /* 0 - 127 */
        } poly_pressure;
    };
} midi_event_t;

const char *midi_event_type_name(midi_event_type_t type);

#ifdef __cplusplus
}
#endif
