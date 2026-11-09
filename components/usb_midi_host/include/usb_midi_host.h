#pragma once

/* NEKTARINA S31 USB-MIDI host adapter.
 *
 * Installs the ESP-IDF USB Host Library once, on the High-Speed DWC controller
 * (S31 peripheral 0, board connector J4 Type-A), registers one client and
 * delivers raw 4-byte USB-MIDI event packets through a bounded queue.
 * The ingress task parses MIDI outside the USB callback context. */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_MIDI_HOST_QUEUE_DEPTH 256u

typedef struct {
    uint32_t session;      /* Matches usb_midi_host_stats_t.session while valid */
    int64_t timestamp_us;  /* esp_timer time when the IN transfer completed */
    uint8_t packet[4];     /* Raw USB-MIDI: [cable<<4 | CIN, MIDI0, MIDI1, MIDI2] */
} usb_midi_packet_t;

typedef struct {
    /* Current state */
    bool present;          /* MIDI-capable device opened and owned */
    bool ready;            /* Interface claimed and IN transfer in flight */
    uint32_t session;      /* Generation advances on connect or input loss */
    uint16_t vid, pid;
    uint8_t speed;         /* usb_speed_t of the current device */
    uint8_t interface_number;
    uint8_t in_ep_address;
    uint16_t in_max_packet;
    uint32_t queue_waiting;
    uint32_t queue_high_water;
    /* Monotonic counters since boot */
    uint32_t devices_seen;
    uint32_t devices_rejected;
    uint32_t connects;
    uint32_t disconnects;
    uint32_t claim_failures;
    uint32_t in_completed;
    uint32_t in_errors;          /* completed with an error status (not disconnect) */
    uint32_t submit_failures;
    uint32_t retry_submits;
    uint32_t packets_rx;
    uint32_t packets_padding;    /* all-zero USB-MIDI padding packets skipped */
    uint32_t partial_bytes;      /* trailing bytes not forming a whole packet */
    uint32_t queue_drops;
    uint32_t stale_drops;        /* dequeued packets from an ended session */
    uint32_t cleanup_attempts_max;
    uint32_t last_desc_result;   /* usb_midi_desc_result_t of the last device */
} usb_midi_host_stats_t;

/* Installs USB host + client and starts their tasks on core 0. Call once. */
esp_err_t usb_midi_host_start(void);

/* Pops one packet of the current session. Never blocks longer than timeout_ms.
 * Packets queued before a disconnect are discarded and counted as stale. */
bool usb_midi_host_receive(usb_midi_packet_t *out, uint32_t timeout_ms);

/* Lock-free snapshot for the audio owner. Zero means no usable input. */
uint32_t usb_midi_host_active_session(void);

void usb_midi_host_get_stats(usb_midi_host_stats_t *out);

#ifdef __cplusplus
}
#endif
