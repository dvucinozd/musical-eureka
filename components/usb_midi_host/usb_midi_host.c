/*
 * SPDX-License-Identifier: MIT
 *
 * NEKTARINA S31 USB-MIDI host adapter, ported from NEKTAR-P4 M2.
 *
 * Client lifecycle adapted from PAJONIIIR-M3 (commit
 * ccebdb96e302d8be8bf2b1269ee8afdad583e430):
 *   firmware/main-deck-p4/components/p4_flx4_host/p4_flx4_host.c
 *   firmware/main-deck-p4/components/usb_storage/usb_storage.c (usb_lib_task)
 * Copyright (c) 2024 The Pajoniiir Contributors, MIT License; see
 * LICENSE-PAJONIIIR in this directory.
 *
 * Kept from the source: single client task owns every submit/claim/release,
 * cancellation latch that prevents reviving a dying session, deferred
 * DEV_GONE cleanup (release interface, then close device), deferred new
 * device address while cleanup is pending, bounded retries.
 * NEKTAR changes: class-based match (no FLX4 VID/PID), no UAC/LED/
 * control_link/MSC, bounds-checked descriptor validation, one fixed IN buffer,
 * no callback resubmission after an error status, bounded packet queue with
 * session identity, HS controller only, no PHY routing or root-port cycling.
 */
#include "usb_midi_host.h"
#include "usb_midi_desc.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb/usb_host.h"

static const char *TAG = "usb_midi";

#define USB_TASK_CORE      0
#define LIB_TASK_STACK     4096
#define LIB_TASK_PRIO      5    /* audio task: priority 20 on core 1 */
#define CLIENT_TASK_STACK  4096
#define CLIENT_TASK_PRIO   6
#define CLIENT_POLL_MS     10u
#define RETRY_PERIOD_MS    100u
#define RETRY_LIMIT        3u
#define DESC_DUMP_MAX      1024u
/* S31 has one HS DWC peripheral, on the CoreBoard-1 J4 Type-A connector. */
#define USB_PERIPHERAL_HS  (1u << 0)

/* ---- Client-task owned state (also touched by callbacks, which run inside
 *      usb_host_client_handle_events() on the same task) ---- */
static usb_host_client_handle_t s_client;
static usb_device_handle_t s_dev;
static usb_midi_endpoint_info_t s_ep;
static usb_transfer_t *s_in_xfer;      /* allocated once, reused across sessions */
static bool s_claimed;
static bool s_in_pending;
static bool s_transfers_stopping;
static unsigned s_in_retries;
static unsigned s_claim_retries;
static usb_device_handle_t s_cleanup_dev;
static bool s_cleanup_pending;
static uint32_t s_cleanup_attempts;
static uint8_t s_pending_addr;

/* ---- Shared state ---- */
static StaticQueue_t s_queue_state;
static uint8_t s_queue_storage[USB_MIDI_HOST_QUEUE_DEPTH * sizeof(usb_midi_packet_t)];
static _Atomic(QueueHandle_t) s_queue;

static atomic_bool s_present;
static _Atomic uint32_t s_session, s_active_session;

typedef struct {
    atomic_uint_fast32_t devices_seen, devices_rejected, connects, disconnects,
        claim_failures, in_completed, in_errors, submit_failures, retry_submits,
        packets_rx, packets_padding, partial_bytes, queue_drops, stale_drops,
        queue_high_water, cleanup_attempts_max, last_desc_result;
} counters_t;
static counters_t s_cnt;
#define CNT_ADD(field, n) atomic_fetch_add_explicit(&s_cnt.field, (n), memory_order_relaxed)
#define CNT_INC(field) CNT_ADD(field, 1u)
#define CNT_GET(field) ((uint32_t)atomic_load_explicit(&s_cnt.field, memory_order_relaxed))
#define CNT_SET(field, v) atomic_store_explicit(&s_cnt.field, (v), memory_order_relaxed)

typedef struct {
    uint16_t vid, pid;
    uint8_t speed, interface_number, in_ep_address;
    uint16_t in_max_packet;
} device_identity_t;
static device_identity_t s_identity;
static portMUX_TYPE s_identity_mux = portMUX_INITIALIZER_UNLOCKED;

static void in_transfer_cb(usb_transfer_t *transfer);

/* All writers run in the same USB client task. Reserve zero for inactive. */
static uint32_t next_session(void)
{
    uint32_t session = atomic_load(&s_session) + 1u;
    if (session == 0) session = 1u;
    atomic_store(&s_session, session);
    return session;
}

static void invalidate_input(void)
{
    if (atomic_exchange(&s_active_session, 0) != 0) {
        (void)next_session();
        xQueueReset(s_queue);
    }
}

static bool status_is_disconnect(usb_transfer_status_t status)
{
    return status == USB_TRANSFER_STATUS_CANCELED ||
           status == USB_TRANSFER_STATUS_NO_DEVICE;
}

static void set_identity(const device_identity_t *identity)
{
    portENTER_CRITICAL(&s_identity_mux);
    s_identity = *identity;
    portEXIT_CRITICAL(&s_identity_mux);
}

static bool submit_in(void)
{
    if (s_transfers_stopping || !s_dev || !s_claimed || s_in_pending) return s_in_pending;
    if (!s_in_xfer &&
        usb_host_transfer_alloc(USB_MIDI_IN_BUFFER_BYTES, 0, &s_in_xfer) != ESP_OK) {
        s_in_xfer = NULL;
        CNT_INC(submit_failures);
        return false;
    }
    s_in_xfer->device_handle = s_dev;
    s_in_xfer->bEndpointAddress = s_ep.in_ep_address;
    s_in_xfer->num_bytes = s_ep.in_max_packet; /* one MPS; validated <= buffer */
    s_in_xfer->callback = in_transfer_cb;
    s_in_xfer->context = NULL;
    const esp_err_t err = usb_host_transfer_submit(s_in_xfer);
    s_in_pending = err == ESP_OK;
    if (s_in_pending) {
        atomic_store(&s_active_session, atomic_load(&s_session));
    } else {
        invalidate_input();
        CNT_INC(submit_failures);
    }
    return s_in_pending;
}

static void forward_packets(const uint8_t *data, int actual)
{
    uint32_t session = usb_midi_host_active_session();
    if (!session) return;
    size_t bytes = actual > 0 ? (size_t)actual : 0u;
    if (bytes > s_ep.in_max_packet) bytes = s_ep.in_max_packet;
    if (bytes % 4u) {
        CNT_ADD(partial_bytes, (uint32_t)(bytes % 4u));
        invalidate_input();
        return;
    }

    usb_midi_packet_t item = {
        .session = session,
        .timestamp_us = esp_timer_get_time(),
    };
    for (size_t i = 0; i + 4u <= bytes; i += 4u) {
        if ((data[i] | data[i + 1u] | data[i + 2u] | data[i + 3u]) == 0u) {
            CNT_INC(packets_padding);
            continue;
        }
        memcpy(item.packet, &data[i], 4u);
        if (xQueueSend(s_queue, &item, 0) == pdTRUE) CNT_INC(packets_rx);
        else {
            CNT_INC(queue_drops);
            /* Losing Note Off requires a new semantic session and synth panic. */
            item.session = next_session();
            atomic_store(&s_active_session, item.session);
            xQueueReset(s_queue);
        }
    }
    const uint32_t waiting = (uint32_t)uxQueueMessagesWaiting(s_queue);
    if (waiting > CNT_GET(queue_high_water)) CNT_SET(queue_high_water, waiting);
}

static void in_transfer_cb(usb_transfer_t *transfer)
{
    if (!transfer) return;
    s_in_pending = false;
    if (status_is_disconnect(transfer->status)) {
        /* Cancellation can precede DEV_GONE: never resubmit this session. */
        s_transfers_stopping = true;
        invalidate_input();
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        /* No resubmit from here: a persistent error would otherwise spin at
         * bus rate. The client task retries at most RETRY_LIMIT times. */
        CNT_INC(in_errors);
        invalidate_input();
        return;
    }
    CNT_INC(in_completed);
    s_in_retries = 0u;
    forward_packets(transfer->data_buffer, transfer->actual_num_bytes);
    (void)submit_in();
}

static void try_claim(void)
{
    if (s_claimed || !s_dev || s_transfers_stopping) return;
    const esp_err_t err = usb_host_interface_claim(s_client, s_dev, s_ep.interface_number,
                                                   s_ep.alternate_setting);
    if (err == ESP_OK) {
        s_claimed = true;
        return;
    }
    CNT_INC(claim_failures);
    ESP_LOGW(TAG, "claim interface %u failed: %s", (unsigned)s_ep.interface_number,
             esp_err_to_name(err));
}

static void log_hex(const char *label, const uint8_t *data, size_t len)
{
    char line[32u * 3u + 1u];
    for (size_t off = 0; off < len; off += 32u) {
        const size_t n = len - off < 32u ? len - off : 32u;
        for (size_t i = 0; i < n; ++i) snprintf(&line[i * 3u], 4u, "%02x ", data[off + i]);
        line[n * 3u - 1u] = '\0';
        ESP_LOGI(TAG, "USBDESC %s %04u: %s", label, (unsigned)off, line);
    }
}

static void string_ascii(const usb_str_desc_t *desc, char *out, size_t out_len)
{
    size_t n = 0;
    if (desc && desc->bLength > 2u) {
        const size_t chars = (desc->bLength - 2u) / 2u;
        for (size_t i = 0; i < chars && n + 1u < out_len; ++i) {
            const uint16_t c = desc->wData[i];
            out[n++] = (c >= 0x20u && c < 0x7fu) ? (char)c : '?';
        }
    }
    out[n] = '\0';
}

static void log_device(uint8_t addr, const usb_device_desc_t *dd, const usb_config_desc_t *cd,
                       const usb_device_info_t *info)
{
    static const char *const speeds[] = {"low", "full", "high"};
    char manufacturer[33], product[33];
    string_ascii(info->str_desc_manufacturer, manufacturer, sizeof(manufacturer));
    string_ascii(info->str_desc_product, product, sizeof(product));
    ESP_LOGI(TAG, "USBDEV addr=%u vid=%04x pid=%04x bcdUSB=%04x bcdDevice=%04x "
             "class=%02x/%02x/%02x mps0=%u speed=%s cfg=%u total=%u",
             (unsigned)addr, dd->idVendor, dd->idProduct, dd->bcdUSB, dd->bcdDevice,
             dd->bDeviceClass, dd->bDeviceSubClass, dd->bDeviceProtocol,
             (unsigned)dd->bMaxPacketSize0,
             (unsigned)info->speed < 3u ? speeds[info->speed] : "?",
             (unsigned)info->bConfigurationValue, (unsigned)cd->wTotalLength);
    ESP_LOGI(TAG, "USBDEV manufacturer=\"%s\" product=\"%s\"", manufacturer, product);
    log_hex("DEV", (const uint8_t *)dd, dd->bLength);
    log_hex("CFG", (const uint8_t *)cd,
            cd->wTotalLength < DESC_DUMP_MAX ? cd->wTotalLength : DESC_DUMP_MAX);
}

static void reject_device(usb_device_handle_t dev)
{
    CNT_INC(devices_rejected);
    (void)usb_host_device_close(s_client, dev);
}

static void handle_new_device(uint8_t addr)
{
    if (s_cleanup_pending) {
        s_pending_addr = addr; /* previous handle not released yet */
        return;
    }
    if (s_dev) {
        ESP_LOGW(TAG, "additional USB device addr=%u ignored; one MIDI device supported",
                 (unsigned)addr);
        CNT_INC(devices_rejected);
        return;
    }
    usb_device_handle_t dev = NULL;
    esp_err_t err = usb_host_device_open(s_client, addr, &dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device_open addr=%u failed: %s", (unsigned)addr, esp_err_to_name(err));
        CNT_INC(devices_rejected);
        return;
    }
    CNT_INC(devices_seen);

    const usb_device_desc_t *dd = NULL;
    const usb_config_desc_t *cd = NULL;
    usb_device_info_t info = {0};
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || !dd ||
        usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK || !cd ||
        usb_host_device_info(dev, &info) != ESP_OK) {
        ESP_LOGW(TAG, "descriptor read failed addr=%u", (unsigned)addr);
        reject_device(dev);
        return;
    }
    log_device(addr, dd, cd, &info);

    usb_midi_endpoint_info_t ep;
    const usb_midi_desc_result_t result = usb_midi_desc_find(
        (const uint8_t *)cd, cd->wTotalLength, USB_MIDI_IN_BUFFER_BYTES, &ep);
    CNT_SET(last_desc_result, (uint32_t)result);
    if (result != USB_MIDI_DESC_OK) {
        ESP_LOGW(TAG, "device addr=%u not used: %s", (unsigned)addr,
                 usb_midi_desc_result_name(result));
        reject_device(dev);
        return;
    }

    s_dev = dev;
    s_ep = ep;
    s_claimed = false;
    s_in_pending = false;
    s_transfers_stopping = false;
    s_in_retries = 0u;
    s_claim_retries = 0u;
    set_identity(&(device_identity_t){
        .vid = dd->idVendor, .pid = dd->idProduct, .speed = (uint8_t)info.speed,
        .interface_number = ep.interface_number, .in_ep_address = ep.in_ep_address,
        .in_max_packet = ep.in_max_packet,
    });
    const uint32_t session = next_session();
    atomic_store(&s_present, true);
    CNT_INC(connects);

    try_claim();
    (void)submit_in();
    ESP_LOGI(TAG, "MIDI session=%" PRIu32 " intf=%u in=0x%02x mps=%u attr=0x%02x out=%s ready=%d",
             session, (unsigned)ep.interface_number, ep.in_ep_address,
             (unsigned)ep.in_max_packet, ep.in_ep_attributes, ep.has_out ? "yes" : "no",
             (int)(usb_midi_host_active_session() != 0));
}

static void handle_dev_gone(usb_device_handle_t dev)
{
    if (!s_dev || dev != s_dev || s_cleanup_pending) return;
    s_transfers_stopping = true;
    invalidate_input();
    atomic_store(&s_present, false);
    const uint32_t session = atomic_load(&s_session);
    CNT_INC(disconnects);
    s_cleanup_dev = s_dev;
    s_cleanup_pending = true;
    s_cleanup_attempts = 0u;
    ESP_LOGI(TAG, "MIDI device gone; session=%" PRIu32 " cleanup pending", session);
}

static void try_cleanup(void)
{
    if (!s_cleanup_pending) return;
    ++s_cleanup_attempts;
    if (s_cleanup_attempts > CNT_GET(cleanup_attempts_max)) {
        CNT_SET(cleanup_attempts_max, s_cleanup_attempts);
    }
    /* The IN transfer must be returned before the interface/handle are
     * released and before the buffer can serve a new session. */
    if (s_in_pending) return;
    const bool log_now = s_cleanup_attempts == 1u || (s_cleanup_attempts % 100u) == 0u;
    if (s_claimed) {
        const esp_err_t err = usb_host_interface_release(s_client, s_cleanup_dev,
                                                         s_ep.interface_number);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            if (log_now) ESP_LOGW(TAG, "interface release pending: %s", esp_err_to_name(err));
            return;
        }
        s_claimed = false;
    }
    const esp_err_t err = usb_host_device_close(s_client, s_cleanup_dev);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        if (log_now) ESP_LOGW(TAG, "device close pending: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "MIDI device released after %" PRIu32 " cleanup attempts", s_cleanup_attempts);
    s_dev = NULL;
    s_cleanup_dev = NULL;
    s_cleanup_pending = false;
    set_identity(&(device_identity_t){0});
}

static void retry_transfers(void)
{
    if (s_transfers_stopping || !s_dev || s_cleanup_pending) return;
    if (!s_claimed) {
        if (s_claim_retries >= RETRY_LIMIT) return;
        ++s_claim_retries;
        try_claim();
        if (!s_claimed) return;
    }
    if (!s_in_pending && s_in_retries < RETRY_LIMIT) {
        ++s_in_retries;
        CNT_INC(retry_submits);
        (void)submit_in();
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (!msg) return;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) handle_new_device(msg->new_dev.address);
    else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) handle_dev_gone(msg->dev_gone.dev_hdl);
}

/* Runs after every client event poll; separated so PC tests can drive it. */
static void client_service(TickType_t now, TickType_t *last_retry)
{
    try_cleanup();
    if (!s_dev && !s_cleanup_pending && s_pending_addr) {
        const uint8_t addr = s_pending_addr;
        s_pending_addr = 0u;
        handle_new_device(addr);
    }
    if ((TickType_t)(now - *last_retry) >= pdMS_TO_TICKS(RETRY_PERIOD_MS)) {
        *last_retry = now;
        retry_transfers();
    }
}

static void client_task(void *arg)
{
    (void)arg;
    TickType_t last_retry = xTaskGetTickCount();
    for (;;) {
        (void)usb_host_client_handle_events(s_client, pdMS_TO_TICKS(CLIENT_POLL_MS));
        client_service(xTaskGetTickCount(), &last_retry);
    }
}

static void lib_task(void *arg)
{
    (void)arg;
    /* Startup can delete this task safely until registration is complete. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
        uint32_t flags = 0u;
        const esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(1000), &flags);
        if (err == ESP_OK && (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
            (void)usb_host_device_free_all();
        }
    }
}

esp_err_t usb_midi_host_start(void)
{
    if (s_queue) return ESP_ERR_INVALID_STATE;
    s_queue = xQueueCreateStatic(USB_MIDI_HOST_QUEUE_DEPTH, sizeof(usb_midi_packet_t),
                                 s_queue_storage, &s_queue_state);
    if (!s_queue) return ESP_ERR_NO_MEM;

    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .root_port_unpowered = false, /* logical port power only; VBUS is not switched */
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .peripheral_map = USB_PERIPHERAL_HS,
    };
    esp_err_t err = usb_host_install(&host_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        goto fail_queue;
    }
    const usb_host_client_config_t client_cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {.client_event_callback = client_event_cb, .callback_arg = NULL},
    };
    err = usb_host_client_register(&client_cfg, &s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
        goto fail_host;
    }
    TaskHandle_t lib_handle = NULL;
    if (xTaskCreatePinnedToCore(lib_task, "usb_lib", LIB_TASK_STACK, NULL, LIB_TASK_PRIO,
                                &lib_handle, USB_TASK_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail_client;
    }
    if (xTaskCreatePinnedToCore(client_task, "usb_midi", CLIENT_TASK_STACK, NULL,
                                CLIENT_TASK_PRIO, NULL, USB_TASK_CORE) != pdPASS) {
        vTaskDelete(lib_handle);
        err = ESP_ERR_NO_MEM;
        goto fail_client;
    }
    xTaskNotifyGive(lib_handle);
    ESP_LOGI(TAG, "USB host on S31 HS controller (J4 Type-A); waiting for a MIDI device");
    return ESP_OK;

fail_client:
    (void)usb_host_client_deregister(s_client);
    s_client = NULL;
fail_host:
    (void)usb_host_uninstall();
fail_queue:
    vQueueDelete(s_queue);
    s_queue = NULL;
    return err;
}

uint32_t usb_midi_host_active_session(void)
{
    return atomic_load(&s_active_session);
}

bool usb_midi_host_receive(usb_midi_packet_t *out, uint32_t timeout_ms)
{
    if (!out || !s_queue) return false;
    TickType_t wait = timeout_ms ? pdMS_TO_TICKS(timeout_ms) : 0;
    if (timeout_ms && wait == 0) wait = 1;
    usb_midi_packet_t item;
    for (unsigned n = 0; n < USB_MIDI_HOST_QUEUE_DEPTH &&
         xQueueReceive(s_queue, &item, wait) == pdTRUE; ++n) {
        if (item.session != 0 && item.session == usb_midi_host_active_session()) {
            *out = item;
            return true;
        }
        CNT_INC(stale_drops);
        wait = 0;
    }
    return false;
}

void usb_midi_host_get_stats(usb_midi_host_stats_t *out)
{
    if (!out) return;
    device_identity_t id;
    portENTER_CRITICAL(&s_identity_mux);
    id = s_identity;
    portEXIT_CRITICAL(&s_identity_mux);
    *out = (usb_midi_host_stats_t){
        .present = atomic_load(&s_present),
        .ready = usb_midi_host_active_session() != 0,
        .session = (uint32_t)atomic_load(&s_session),
        .vid = id.vid, .pid = id.pid, .speed = id.speed,
        .interface_number = id.interface_number,
        .in_ep_address = id.in_ep_address,
        .in_max_packet = id.in_max_packet,
        .queue_waiting = s_queue ? (uint32_t)uxQueueMessagesWaiting(s_queue) : 0u,
        .queue_high_water = CNT_GET(queue_high_water),
        .devices_seen = CNT_GET(devices_seen),
        .devices_rejected = CNT_GET(devices_rejected),
        .connects = CNT_GET(connects),
        .disconnects = CNT_GET(disconnects),
        .claim_failures = CNT_GET(claim_failures),
        .in_completed = CNT_GET(in_completed),
        .in_errors = CNT_GET(in_errors),
        .submit_failures = CNT_GET(submit_failures),
        .retry_submits = CNT_GET(retry_submits),
        .packets_rx = CNT_GET(packets_rx),
        .packets_padding = CNT_GET(packets_padding),
        .partial_bytes = CNT_GET(partial_bytes),
        .queue_drops = CNT_GET(queue_drops),
        .stale_drops = CNT_GET(stale_drops),
        .cleanup_attempts_max = CNT_GET(cleanup_attempts_max),
        .last_desc_result = CNT_GET(last_desc_result),
    };
}
