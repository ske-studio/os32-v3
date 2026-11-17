#ifndef USB_DESC_H
#define USB_DESC_H
#include "endian_le.h"
/* USB 2.0 chapter 9: wire records are byte arrays; multibyte fields use
 * endian_le.h, independently of DMA word access and CPU alignment. */
#define USB_DESC_DEVICE 1u
#define USB_DESC_CONFIG 2u
#define USB_DESC_STRING 3u
#define USB_DESC_INTERFACE 4u
#define USB_DESC_ENDPOINT 5u
#define USB_DESC_HID 0x21u
#define USB_DESC_HUB 0x29u
#define USB_DEVICE_DESC_LEN 18u
#define USB_CONFIG_DESC_LEN 9u
#define USB_INTERFACE_DESC_LEN 9u
#define USB_ENDPOINT_DESC_LEN 7u
#define USB_HID_DESC_MIN_LEN 6u
#define USB_HUB_DESC_MIN_LEN 7u
#define USB_DIR_IN 0x80u
#define USB_EP_NUM_MASK 0x0fu
#define USB_EP_TYPE_MASK 3u
#define USB_EP_CONTROL 0u
#define USB_EP_ISO 1u
#define USB_EP_BULK 2u
#define USB_EP_INTERRUPT 3u
#define USB_REQ_GET_STATUS 0u
#define USB_REQ_CLEAR_FEATURE 1u
#define USB_REQ_SET_FEATURE 3u
#define USB_REQ_SET_ADDRESS 5u
#define USB_REQ_GET_DESCRIPTOR 6u
#define USB_REQ_GET_CONFIGURATION 8u
#define USB_REQ_SET_CONFIGURATION 9u
#define USB_REQ_SET_INTERFACE 11u
#define USB_REQ_CLASS 0x20u
#define USB_RECIP_DEVICE 0u
#define USB_RECIP_INTERFACE 1u
#define USB_RECIP_ENDPOINT 2u
#define USB_RECIP_OTHER 3u
#define USB_CLASS_HID 3u
#define USB_CLASS_HUB 9u
#define USB_CLASS_MASS_STORAGE 8u
#define USB_HID_SUBCLASS_BOOT 1u
#define USB_HID_PROTOCOL_KBD 1u
#define USB_HID_PROTOCOL_MOUSE 2u
#define USB_HID_SET_IDLE 10u
#define USB_HID_SET_PROTOCOL 11u
#define USB_PORT_CONNECTION 0u
#define USB_PORT_ENABLE 1u
#define USB_PORT_SUSPEND 2u
#define USB_PORT_OVER_CURRENT 3u
#define USB_PORT_RESET 4u
#define USB_PORT_POWER 8u
#define USB_C_PORT_CONNECTION 16u
#define USB_C_PORT_ENABLE 17u
#define USB_C_PORT_SUSPEND 18u
#define USB_C_PORT_OVER_CURRENT 19u
#define USB_C_PORT_RESET 20u
struct usb_setup {
    u8 type, request;
    u8 value[2], index[2], length[2];
};
struct usb_device_desc {
    u8 length, type, usb[2], dev_class, subclass, protocol, ep0_mps;
    u8 vendor[2], product[2], release[2], manufacturer, product_string;
    u8 serial_string, configurations;
};
struct usb_config_desc {
    u8 length, type, total_length[2], interfaces, value, string, attributes, power;
};
struct usb_interface_desc {
    u8 length, type, number, alternate, endpoints, iface_class, subclass, protocol, string;
};
struct usb_endpoint_desc {
    u8 length, type, address, attributes, max_packet[2], interval;
};
STATIC_ASSERT(sizeof(struct usb_setup) == 8, usb_setup_wire_size);
STATIC_ASSERT(sizeof(struct usb_device_desc) == USB_DEVICE_DESC_LEN, usb_device_wire_size);
STATIC_ASSERT(sizeof(struct usb_config_desc) == USB_CONFIG_DESC_LEN, usb_config_wire_size);
STATIC_ASSERT(sizeof(struct usb_interface_desc) == USB_INTERFACE_DESC_LEN, usb_interface_wire_size);
STATIC_ASSERT(sizeof(struct usb_endpoint_desc) == USB_ENDPOINT_DESC_LEN, usb_endpoint_wire_size);
#endif
