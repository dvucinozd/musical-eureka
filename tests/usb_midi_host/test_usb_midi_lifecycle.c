#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "usb_midi_desc.h"

static unsigned s_stub_submit_count = 0;
static bool s_stub_fail_submit = false;
static bool s_stub_fail_claim = false;
static bool s_stub_fail_release = false;
static bool s_stub_fail_close = false;

esp_err_t usb_host_install(const usb_host_config_t *config) { (void)config; return ESP_OK; }
esp_err_t usb_host_uninstall(void) { return ESP_OK; }
esp_err_t usb_host_lib_handle_events(uint32_t timeout_ticks, uint32_t *event_flags_ret)
{
    (void)timeout_ticks;
    if (event_flags_ret) *event_flags_ret = 0;
    return ESP_OK;
}
esp_err_t usb_host_device_free_all(void) { return ESP_OK; }

esp_err_t usb_host_client_register(const usb_host_client_config_t *client_config, usb_host_client_handle_t *client_hdl_ret)
{
    (void)client_config;
    *client_hdl_ret = (usb_host_client_handle_t)0x1234;
    return ESP_OK;
}
esp_err_t usb_host_client_deregister(usb_host_client_handle_t client_hdl)
{
    (void)client_hdl;
    return ESP_OK;
}
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl, uint32_t timeout_ticks)
{
    (void)client_hdl; (void)timeout_ticks;
    return ESP_OK;
}
esp_err_t usb_host_client_unblock(usb_host_client_handle_t client_hdl)
{
    (void)client_hdl;
    return ESP_OK;
}
esp_err_t usb_host_device_open(usb_host_client_handle_t client_hdl, uint8_t dev_addr, usb_device_handle_t *dev_hdl_ret)
{
    (void)client_hdl;
    *dev_hdl_ret = (usb_device_handle_t)(uintptr_t)dev_addr;
    return ESP_OK;
}
esp_err_t usb_host_device_close(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl)
{
    (void)client_hdl; (void)dev_hdl;
    return s_stub_fail_close ? ESP_FAIL : ESP_OK;
}
esp_err_t usb_host_interface_claim(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t bInterfaceNumber, uint8_t bAlternateSetting)
{
    (void)client_hdl; (void)dev_hdl; (void)bInterfaceNumber; (void)bAlternateSetting;
    return s_stub_fail_claim ? ESP_FAIL : ESP_OK;
}
esp_err_t usb_host_interface_release(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t bInterfaceNumber)
{
    (void)client_hdl; (void)dev_hdl; (void)bInterfaceNumber;
    return s_stub_fail_release ? ESP_FAIL : ESP_OK;
}

static usb_device_desc_t s_mock_device_desc = {
    .bLength = sizeof(usb_device_desc_t),
    .bDescriptorType = 1,
    .idVendor = 0x1234,
    .idProduct = 0x5678,
    .bMaxPacketSize0 = 64,
};

static uint8_t s_mock_cfg_buf[128];
static usb_config_desc_t *s_mock_cfg_desc = (usb_config_desc_t *)s_mock_cfg_buf;

esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl, const usb_device_desc_t **dev_desc_ret)
{
    (void)dev_hdl;
    *dev_desc_ret = &s_mock_device_desc;
    return ESP_OK;
}
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl, const usb_config_desc_t **config_desc_ret)
{
    (void)dev_hdl;
    *config_desc_ret = s_mock_cfg_desc;
    return ESP_OK;
}
esp_err_t usb_host_device_info(usb_device_handle_t dev_hdl, usb_device_info_t *dev_info)
{
    (void)dev_hdl;
    memset(dev_info, 0, sizeof(*dev_info));
    dev_info->speed = 1; // Full speed
    return ESP_OK;
}

esp_err_t usb_host_transfer_alloc(size_t data_buffer_size, int num_isoc_packets, usb_transfer_t **transfer_ret)
{
    (void)num_isoc_packets;
    usb_transfer_t *xfer = calloc(1, sizeof(usb_transfer_t));
    xfer->data_buffer = calloc(1, data_buffer_size);
    xfer->data_buffer_size = data_buffer_size;
    *transfer_ret = xfer;
    return ESP_OK;
}
esp_err_t usb_host_transfer_free(usb_transfer_t *transfer)
{
    if (transfer) {
        free(transfer->data_buffer);
        free(transfer);
    }
    return ESP_OK;
}
esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer)
{
    (void)transfer;
    s_stub_submit_count++;
    return s_stub_fail_submit ? ESP_FAIL : ESP_OK;
}

// We include usb_midi_host.c directly to access its internal static state and functions
#include "../../components/usb_midi_host/usb_midi_host.c"

static void setup_valid_mock_config(void)
{
    uint8_t cfg[] = {
        // Config descriptor (9 bytes)
        0x09, 0x02, 45, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,
        // Standard AC Interface descriptor (9 bytes)
        0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
        // Standard MS Interface descriptor (9 bytes) - intf 1, alt 0
        0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
        // Standard Bulk IN Endpoint descriptor (9 bytes)
        0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
        // Standard Bulk OUT Endpoint descriptor (9 bytes)
        0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(s_mock_cfg_buf, cfg, sizeof(cfg));
}

static void setup_non_midi_mock_config(void)
{
    uint8_t cfg[] = {
        0x09, 0x02, 27, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x08, 0x06, 0x50, 0x00, // Mass storage
        0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(s_mock_cfg_buf, cfg, sizeof(cfg));
}

static void test_initialization(void)
{
    s_queue = xQueueCreateStatic(USB_MIDI_HOST_QUEUE_DEPTH, sizeof(usb_midi_packet_t),
                                 s_queue_storage, &s_queue_state);
    s_client = (usb_host_client_handle_t)0x1234;

    usb_midi_host_stats_t stats;
    usb_midi_host_get_stats(&stats);
    assert(!stats.present);
    assert(!stats.ready);
    assert(stats.session == 0);
    assert(stats.connects == 0);
}

static void test_non_midi_rejection(void)
{
    setup_non_midi_mock_config();
    usb_host_client_event_msg_t msg = {
        .event = USB_HOST_CLIENT_EVENT_NEW_DEV,
        .new_dev = { .address = 1 }
    };
    client_event_cb(&msg, NULL);

    usb_midi_host_stats_t stats;
    usb_midi_host_get_stats(&stats);
    assert(stats.devices_seen == 1);
    assert(stats.devices_rejected == 1);
    assert(!stats.present);
    assert(!stats.ready);
    assert(stats.last_desc_result == USB_MIDI_DESC_ERR_NO_MIDI_STREAMING);
}

static void test_valid_connect_and_rx(void)
{
    setup_valid_mock_config();
    usb_host_client_event_msg_t msg = {
        .event = USB_HOST_CLIENT_EVENT_NEW_DEV,
        .new_dev = { .address = 2 }
    };
    s_stub_submit_count = 0;
    client_event_cb(&msg, NULL);

    usb_midi_host_stats_t stats;
    usb_midi_host_get_stats(&stats);
    assert(stats.present == true);
    assert(stats.ready == true);
    assert(stats.session == 1);
    assert(stats.connects == 1);
    assert(stats.interface_number == 1);
    assert(stats.in_ep_address == 0x81);
    assert(stats.in_max_packet == 64);
    assert(s_stub_submit_count == 1);

    // Simulate transfer completion with 1 valid MIDI packet + 1 padding packet
    uint8_t test_data[8] = {
        0x09, 0x90, 0x3C, 0x64, // Note On Ch 1, C4, vel 100
        0x00, 0x00, 0x00, 0x00  // Padding
    };
    memcpy(s_in_xfer->data_buffer, test_data, 8);
    s_in_xfer->actual_num_bytes = 8;
    s_in_xfer->status = USB_TRANSFER_STATUS_COMPLETED;

    in_transfer_cb(s_in_xfer);

    usb_midi_host_get_stats(&stats);
    assert(stats.packets_rx == 1);
    assert(stats.packets_padding == 1);
    assert(stats.in_completed == 1);
    assert(s_stub_submit_count == 2); // Resubmitted!

    // Verify receive() pops the packet
    usb_midi_packet_t pkt;
    bool got = usb_midi_host_receive(&pkt, 0);
    assert(got == true);
    assert(pkt.session == 1);
    assert(pkt.packet[0] == 0x09);
    assert(pkt.packet[1] == 0x90);
    assert(pkt.packet[2] == 0x3C);
    assert(pkt.packet[3] == 0x64);
}

static void test_disconnect_and_deferred_cleanup(void)
{
    // Disconnect device
    usb_host_client_event_msg_t msg = {
        .event = USB_HOST_CLIENT_EVENT_DEV_GONE,
        .dev_gone = { .dev_hdl = s_dev }
    };
    client_event_cb(&msg, NULL);

    // In-flight transfer retires with NO_DEVICE / CANCELED
    s_in_xfer->status = USB_TRANSFER_STATUS_NO_DEVICE;
    in_transfer_cb(s_in_xfer);

    usb_midi_host_stats_t stats;
    usb_midi_host_get_stats(&stats);
    assert(!stats.present);
    assert(!stats.ready);
    assert(stats.disconnects == 1);
    assert(stats.session == 2); // Session bumped!
    assert(s_cleanup_pending == true);

    // If new device arrives while cleanup pending, it is saved in s_pending_addr
    usb_host_client_event_msg_t msg_pending = {
        .event = USB_HOST_CLIENT_EVENT_NEW_DEV,
        .new_dev = { .address = 3 }
    };
    client_event_cb(&msg_pending, NULL);
    assert(s_pending_addr == 3);

    // First attempt fails release
    s_stub_fail_release = true;
    TickType_t last_retry = 0;
    client_service(0, &last_retry);
    assert(s_cleanup_pending == true);
    assert(s_cleanup_attempts == 1);

    // Release succeeds, cleanup completes and pending address 3 is opened
    s_stub_fail_release = false;
    client_service(0, &last_retry);
    assert(s_cleanup_pending == false);
    assert(s_pending_addr == 0);

    usb_midi_host_get_stats(&stats);
    assert(stats.present == true);
    assert(stats.session == 3);
    assert(stats.connects == 2);
}

static void test_early_cancel_transfer_stops_resubmit(void)
{
    // When transfer receives CANCELED or NO_DEVICE status, it must not resubmit
    s_in_xfer->status = USB_TRANSFER_STATUS_CANCELED;
    s_stub_submit_count = 0;
    in_transfer_cb(s_in_xfer);

    assert(s_transfers_stopping == true);
    assert(usb_midi_host_active_session() == 0);
    assert(s_stub_submit_count == 0); // No resubmit!
}

static void test_overflow_invalidates_session(void)
{
    xQueueReset(s_queue);
    usb_midi_packet_t packet = {.session = (uint32_t)atomic_load(&s_session)};
    for (unsigned i = 0; i < USB_MIDI_HOST_QUEUE_DEPTH; ++i) assert(xQueueSend(s_queue, &packet, 0) == pdTRUE);
    uint32_t before = (uint32_t)atomic_load(&s_session);
    uint8_t data[4] = {0x09, 0x90, 60, 100};
    forward_packets(data, sizeof(data));
    assert((uint32_t)atomic_load(&s_session) == before + 1);
    assert(usb_midi_host_active_session() == before + 1);
    assert(uxQueueMessagesWaiting(s_queue) == 0);
    assert(CNT_GET(queue_drops) == 1);
}

static void test_input_fault_and_retry_change_session(void)
{
    uint32_t before = usb_midi_host_active_session();
    assert(before != 0);
    usb_midi_packet_t packet = {.session = before};
    assert(xQueueSend(s_queue, &packet, 0) == pdTRUE);
    s_in_xfer->status = USB_TRANSFER_STATUS_ERROR;
    in_transfer_cb(s_in_xfer);
    assert(usb_midi_host_active_session() == 0);
    assert(!usb_midi_host_receive(&packet, 0));
    retry_transfers();
    uint32_t recovered = usb_midi_host_active_session();
    assert(recovered != 0 && recovered != before);

    /* A partial USB-MIDI event could be a missing Note Off. The rearmed
     * transfer must publish a new generation even if it succeeds immediately. */
    s_in_xfer->status = USB_TRANSFER_STATUS_COMPLETED;
    s_in_xfer->actual_num_bytes = 3;
    in_transfer_cb(s_in_xfer);
    assert(usb_midi_host_active_session() != 0);
    assert(usb_midi_host_active_session() != recovered);
    assert(CNT_GET(partial_bytes) == 3);
}

int main(void)
{
    test_initialization();
    test_non_midi_rejection();
    test_valid_connect_and_rx();
    test_disconnect_and_deferred_cleanup();
    test_overflow_invalidates_session();
    test_input_fault_and_retry_change_session();
    test_early_cancel_transfer_stops_resubmit();

    printf("All usb_midi_lifecycle tests passed!\n");
    return 0;
}
