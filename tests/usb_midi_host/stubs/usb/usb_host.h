#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef void *usb_device_handle_t;
typedef void *usb_host_client_handle_t;

typedef enum {
    USB_TRANSFER_STATUS_COMPLETED = 0,
    USB_TRANSFER_STATUS_ERROR,
    USB_TRANSFER_STATUS_TIMED_OUT,
    USB_TRANSFER_STATUS_CANCELED,
    USB_TRANSFER_STATUS_STALL,
    USB_TRANSFER_STATUS_OVERFLOW,
    USB_TRANSFER_STATUS_SKIPPED,
    USB_TRANSFER_STATUS_NO_DEVICE,
} usb_transfer_status_t;

enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
};

#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS (1u << 0)

typedef struct {
    int event;
    union {
        struct { uint8_t address; } new_dev;
        struct { usb_device_handle_t dev_hdl; } dev_gone;
    };
} usb_host_client_event_msg_t;

typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    struct {
        void (*client_event_callback)(const usb_host_client_event_msg_t *, void *);
        void *callback_arg;
    } async;
} usb_host_client_config_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wData[32];
} __attribute__((packed)) usb_str_desc_t;

typedef struct {
    int speed;
    uint8_t dev_addr;
    uint8_t bMaxPacketSize0;
    uint8_t bConfigurationValue;
    const usb_str_desc_t *str_desc_manufacturer;
    const usb_str_desc_t *str_desc_product;
    const usb_str_desc_t *str_desc_serial_num;
} usb_device_info_t;

typedef struct {
    uint8_t bInterfaceNumber, bAlternateSetting, bInterfaceClass, bInterfaceSubClass, bNumEndpoints;
} usb_intf_desc_t;

typedef struct {
    uint8_t bEndpointAddress, bmAttributes;
    uint16_t wMaxPacketSize;
} usb_ep_desc_t;

typedef struct {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
} usb_setup_packet_t;

typedef struct usb_transfer {
    uint8_t *data_buffer;
    size_t data_buffer_size;
    int num_bytes;
    int actual_num_bytes;
    uint32_t flags;
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    usb_transfer_status_t status;
    uint32_t timeout_ms;
    void (*callback)(struct usb_transfer *);
    void *context;
    int num_isoc_packets;
} usb_transfer_t;

typedef struct {
    bool skip_phy_setup;
    bool root_port_unpowered;
    int intr_flags;
    unsigned peripheral_map;
} usb_host_config_t;

#define USB_EP_DESC_GET_EP_DIR(ep) ((ep)->bEndpointAddress & 0x80)

esp_err_t usb_host_install(const usb_host_config_t *config);
esp_err_t usb_host_uninstall(void);
esp_err_t usb_host_lib_handle_events(uint32_t timeout_ticks, uint32_t *event_flags_ret);
esp_err_t usb_host_device_free_all(void);

esp_err_t usb_host_transfer_alloc(size_t data_buffer_size, int num_isoc_packets, usb_transfer_t **transfer_ret);
esp_err_t usb_host_transfer_free(usb_transfer_t *transfer);
esp_err_t usb_host_transfer_submit(usb_transfer_t *transfer);
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t client_hdl, usb_transfer_t *transfer);

esp_err_t usb_host_device_open(usb_host_client_handle_t client_hdl, uint8_t dev_addr, usb_device_handle_t *dev_hdl_ret);
esp_err_t usb_host_device_close(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl);
esp_err_t usb_host_device_info(usb_device_handle_t dev_hdl, usb_device_info_t *dev_info);

esp_err_t usb_host_interface_claim(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t bInterfaceNumber, uint8_t bAlternateSetting);
esp_err_t usb_host_interface_release(usb_host_client_handle_t client_hdl, usb_device_handle_t dev_hdl, uint8_t bInterfaceNumber);

esp_err_t usb_host_get_device_descriptor(usb_device_handle_t dev_hdl, const usb_device_desc_t **dev_desc_ret);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t dev_hdl, const usb_config_desc_t **config_desc_ret);

esp_err_t usb_host_client_register(const usb_host_client_config_t *client_config, usb_host_client_handle_t *client_hdl_ret);
esp_err_t usb_host_client_deregister(usb_host_client_handle_t client_hdl);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client_hdl, uint32_t timeout_ticks);
esp_err_t usb_host_client_unblock(usb_host_client_handle_t client_hdl);
