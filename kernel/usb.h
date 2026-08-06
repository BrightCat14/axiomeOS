#ifndef AXIOME_USB_H
#define AXIOME_USB_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ *
 * Standard USB Device Request (control transfer setup packet)
 * ------------------------------------------------------------------ */
struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

/* Request types */
#define USB_REQ_GET_STATUS        0
#define USB_REQ_CLEAR_FEATURE     1
#define USB_REQ_SET_FEATURE       3
#define USB_REQ_SET_ADDRESS       5
#define USB_REQ_GET_DESCRIPTOR    6
#define USB_REQ_SET_DESCRIPTOR    7
#define USB_REQ_GET_CONFIGURATION 8
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_INTERFACE     10
#define USB_REQ_SET_INTERFACE     11
#define USB_REQ_SYNCH_FRAME       12

/* Standard descriptor types */
#define USB_DESC_DEVICE               1
#define USB_DESC_CONFIGURATION        2
#define USB_DESC_STRING               3
#define USB_DESC_INTERFACE            4
#define USB_DESC_ENDPOINT             5
#define USB_DESC_DEVICE_QUALIFIER     6
#define USB_DESC_OTHER_SPEED          7
#define USB_DESC_BOS                 15

/* ------------------------------------------------------------------ *
 * Standard Device Descriptor (18 bytes)
 * ------------------------------------------------------------------ */
struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

/* ------------------------------------------------------------------ *
 * Standard Configuration Descriptor (9 bytes)
 * ------------------------------------------------------------------ */
struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

/* ------------------------------------------------------------------ *
 * Standard Interface Descriptor (9 bytes)
 * ------------------------------------------------------------------ */
struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

/* ------------------------------------------------------------------ *
 * Standard Endpoint Descriptor (7 bytes)
 * ------------------------------------------------------------------ */
struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

/* Endpoint address bits */
#define EP_ADDR_DIR_IN    0x80
#define EP_ADDR_NUM(x)    ((x) & 0x0F)

/* Endpoint attributes (bmAttributes) */
#define EP_ATTR_TYPE(x)   ((x) & 3)
#define  EP_TYPE_CONTROL   0
#define  EP_TYPE_ISOCH     1
#define  EP_TYPE_BULK      2
#define  EP_TYPE_INTERRUPT 3

/* ------------------------------------------------------------------ *
 * USB Device States
 * ------------------------------------------------------------------ */
enum usb_device_state {
    USB_STATE_DISABLED,
    USB_STATE_DEFAULT,
    USB_STATE_ADDRESS,
    USB_STATE_CONFIGURED,
};

/* ------------------------------------------------------------------ *
 * HCD (Host Controller Driver) device structure
 * ------------------------------------------------------------------ */
struct usb_device {
    int slot_id;
    int port;
    int speed;
    uint8_t address;
    enum usb_device_state state;

    struct usb_device_descriptor desc;
    struct usb_config_descriptor *config;

    struct hcd_ops *ops;
    void *hc_priv;
};

/* ------------------------------------------------------------------ *
 * HCD operations — implemented by the host controller driver (xHCI)
 * ------------------------------------------------------------------ */
struct hcd_ops {
    int (*control_transfer)(struct usb_device *dev,
                            struct usb_setup_packet *setup,
                            void *data, size_t data_len);
    int (*bulk_transfer)(struct usb_device *dev, int ep_addr,
                         void *data, size_t len);
    int (*interrupt_transfer)(struct usb_device *dev, int ep_addr,
                              void *data, size_t len);
    int (*reset_device)(struct usb_device *dev);
};

/* ------------------------------------------------------------------ *
 * Hub / root hub constants for device requests
 * ------------------------------------------------------------------ */
#define USB_DIR_IN   0x80
#define USB_DIR_OUT  0x00
#define USB_TYPE_STANDARD  (0 << 5)
#define USB_TYPE_CLASS     (1 << 5)
#define USB_TYPE_VENDOR    (2 << 5)
#define USB_RECIP_DEVICE   0
#define USB_RECIP_INTERFACE 1
#define USB_RECIP_ENDPOINT  2
#define USB_RECIP_OTHER     3

#endif
