#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "midi_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses one 4-byte USB-MIDI packet into a structured semantic MIDI event.
 * Returns true if the packet represents a valid voice/channel event.
 * Note-on with velocity 0 is normalized to MIDI_EVENT_NOTE_OFF.
 * Pitch bend is normalized to -8192..+8191.
 */
bool midi_parser_parse_usb_packet(const uint8_t packet[4],
                                  uint32_t session,
                                  int64_t timestamp_us,
                                  midi_event_t *out_event);

#ifdef __cplusplus
}
#endif
