#include "sf32lb52_usb_device.h"
#include "sf32lb52_usb_hid_api.h"
#include "sf32lb52_ns2_vendor.h"

#include "platform.h"

#include <string.h>

#if defined(SF32LB52_USB_USE_CHERRYUSB)
#include "bf0_hal.h"
#include "ds5_classic.h"
#include "opus.h"
#include "rthw.h"
#include "rtthread.h"
#include "usbd_core.h"
#include "usbd_audio.h"
#include "usbd_hid.h"
#endif

#ifndef USB_POWER_SOFTCONN
#define USB_POWER_SOFTCONN 0x40u
#endif

#ifndef USB_DEVCTL_SESSION
#define USB_DEVCTL_SESSION 0x01u
#endif

#ifndef USB_USBCFG_AVALID
#define USB_USBCFG_AVALID (1u << 3)
#endif

#ifndef USB_USBCFG_AVALID_DR
#define USB_USBCFG_AVALID_DR (1u << 2)
#endif

#define USB_DESC_TYPE_DEVICE 0x01u
#define USB_DESC_TYPE_CONFIGURATION 0x02u
#define USB_DESC_TYPE_INTERFACE 0x04u
#define USB_DESC_TYPE_ENDPOINT 0x05u
#define USB_DESC_TYPE_DEVICE_QUALIFIER 0x06u
#define USB_DESC_TYPE_HID 0x21u
#define USB_DESC_TYPE_HID_REPORT 0x22u

#define USB_CLASS_PER_INTERFACE 0x00u
#define USB_CLASS_AUDIO 0x01u
#define USB_CLASS_HID 0x03u
#define USB_CLASS_VENDOR_SPECIFIC 0xffu
#define USB_ENDPOINT_ATTR_BULK 0x02u
#define USB_ENDPOINT_ATTR_INTERRUPT 0x03u

#define USB_EP0_SIZE 64u
#define USB_CONFIG_VALUE 1u
#define USB_CONFIG_ATTR_BUS_POWERED 0x80u
#define USB_CONFIG_ATTR_REMOTE_WAKEUP 0xa0u
#define USB_CONFIG_POWER_500MA 250u

#define NINTENDO_HID_ITF_NUM 0u
#define NINTENDO_VENDOR_ITF_NUM 1u
#define NINTENDO_HID_EP_IN 0x81u
#define NINTENDO_HID_EP_OUT 0x02u
#define NINTENDO_VENDOR_EP_IN 0x86u
#define NINTENDO_VENDOR_EP_OUT 0x03u
#define NINTENDO_VENDOR_EP_SIZE 64u

#define DUALSENSE_AUDIO_CONTROL_ITF_NUM 0u
#define DUALSENSE_AUDIO_SPEAKER_ITF_NUM 1u
#define DUALSENSE_AUDIO_MIC_ITF_NUM 2u
#define DUALSENSE_HID_ITF_NUM 3u
/* SF32LB52 assigns independent endpoint numbers by direction.  Keep HID and
 * audio off the same physical endpoint numbers so opening OUT cannot switch an
 * active IN FIFO back to receive mode. */
#define DUALSENSE_HID_EP_IN 0x85u
#define DUALSENSE_HID_EP_OUT 0x03u
#define DUALSENSE_AUDIO_EP_OUT 0x02u
#define DUALSENSE_AUDIO_EP_IN 0x86u
#define DUALSENSE_AUDIO_OUT_PACKET_SIZE 392u
#define DUALSENSE_AUDIO_IN_PACKET_SIZE 196u
#define DUALSENSE_AUDIO_VERSION 0x0100u
#define DUALSENSE_AUDIO_SPEAKER_FU_ID 2u
#define DUALSENSE_AUDIO_MIC_FU_ID 5u
#define DUALSENSE_AUDIO_BT_REPORT_SIZE 398u
#define DUALSENSE_AUDIO_HAPTIC_BYTES 64u
#define DUALSENSE_AUDIO_OPUS_BYTES 200u
#define DUALSENSE_AUDIO_OPUS_FRAMES 480u
#define DUALSENSE_AUDIO_BLOCK_FRAMES 512u
#define DUALSENSE_MIC_OPUS_BYTES 71u
#define DUALSENSE_MIC_PACKET_FRAMES 48u
#define DUALSENSE_MIC_QUEUE_CAPACITY 4u
#define DUALSENSE_MIC_QUEUE_STORAGE (DUALSENSE_MIC_QUEUE_CAPACITY + 1u)
#define DUALSENSE_MIC_RING_FRAMES (DUALSENSE_AUDIO_OPUS_FRAMES * 4u)
#define DUALSENSE_MIC_STATE_RETRY_MS 250u

#define XBOX_GAMEPAD_ITF_NUM 0u
#define XBOX_MANAGER_ITF_NUM 1u
#define XBOX_GAMEPAD_EP_IN 0x81u
#define XBOX_GAMEPAD_EP_OUT 0x02u
#define XBOX_GAMEPAD_EP_SIZE 32u
#define XBOX_MANAGER_EP_IN 0x85u
#define XBOX_MANAGER_EP_SIZE 64u

#define USB_SWITCH2_MS_VENDOR_CODE 0xcdu

#if defined(SF32LB52_USB_SMOKE_MOUSE)
#define NINTENDO_USB_CONFIG_TOTAL_LEN 34u
#define NINTENDO_USB_ITF_TOTAL 1u
#define NINTENDO_HID_EP_COUNT 1u
#if defined(SF32LB52_USB_SMOKE_64) || defined(SF32LB52_USB_SMOKE_NS2)
#define NINTENDO_HID_EP_SIZE 64u
#define NINTENDO_HID_POLL_INTERVAL 1u
#else
#define NINTENDO_HID_EP_SIZE 8u
#define NINTENDO_HID_POLL_INTERVAL 10u
#endif
#else
#define NINTENDO_USB_CONFIG_TOTAL_LEN 64u
#define NINTENDO_USB_ITF_TOTAL 2u
#define NINTENDO_HID_EP_COUNT 2u
#define NINTENDO_HID_EP_SIZE 64u
#define NINTENDO_HID_POLL_INTERVAL 1u
#endif

#define DUALSENSE_USB_CONFIG_TOTAL_LEN 227u
#define DUALSENSE_USB_ITF_TOTAL 4u
#define XBOX_USB_CONFIG_TOTAL_LEN 74u
#define XBOX_USB_ITF_TOTAL 2u

#define HID_REPORT_TYPE_INPUT 1u
#define HID_REPORT_TYPE_OUTPUT 2u
#define HID_REPORT_TYPE_FEATURE 3u

#define USB_FEATURE_SET_QUEUE_CAPACITY 8u
#define USB_FEATURE_SET_QUEUE_STORAGE (USB_FEATURE_SET_QUEUE_CAPACITY + 1u)
#define USB_ROLE_SWITCH_REPLY_GRACE_MS 500u

#define U16_LE(v) (uint8_t)((v) & 0xffu), (uint8_t)(((v) >> 8) & 0xffu)
#define STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]

typedef struct UsbFeatureSetEntry {
    uint8_t report_id;
    uint8_t manager;
    uint16_t len;
    uint8_t payload[SF32LB52_USB_HID_PAYLOAD_SIZE];
} UsbFeatureSetEntry;

typedef struct Sf32lb52UsbRuntime {
    Sf32lb52UsbRole role;
    Sf32lb52UsbRole pending_role;
    Sf32lb52UsbDeviceStatus status;
    Sf32lb52UsbOutputReportCallback output_cb;
    Sf32lb52UsbInputGetCallback input_get_cb;
    Sf32lb52UsbFeatureGetCallback feature_get_cb;
    Sf32lb52UsbFeatureSetCallback feature_set_cb;
    Sf32lb52UsbNativeFeatureGetCallback native_feature_get_cb;
    Sf32lb52UsbNativeFeatureSetCallback native_feature_set_cb;
    Sf32lb52UsbAudioHapticsCallback audio_haptics_cb;
    UsbFeatureSetEntry feature_set_queue[USB_FEATURE_SET_QUEUE_STORAGE];
    volatile uint8_t feature_set_head;
    volatile uint8_t feature_set_tail;
    volatile uint8_t manager_reply_pending;
    volatile uint8_t role_switch_requested;
    uint32_t role_switch_requested_ms;
    bool initialized;
} Sf32lb52UsbRuntime;

static Sf32lb52UsbRuntime usb;

static const Sf32lb52UsbRoleCapabilities role_capabilities[] = {
    {
        .vid = SF32LB52_USB_XBOX_VID,
        .pid = SF32LB52_USB_XBOX_PID,
        .input_report_size = SF32LB52_USB_XBOX_INPUT_REPORT_SIZE,
        .output_report_size = SF32LB52_USB_XBOX_OUTPUT_REPORT_SIZE,
        .input_report_id = SF32LB52_USB_XBOX_INPUT_REPORT_ID,
        .output_report_id = SF32LB52_USB_XBOX_OUTPUT_REPORT_ID,
        .gamepad_is_hid = false,
        .audio_capable = false,
    },
    {
        .vid = SF32LB52_USB_DS5_VID,
        .pid = SF32LB52_USB_DS5_PID,
        .input_report_size = SF32LB52_USB_DS5_INPUT_REPORT_SIZE,
        .output_report_size = SF32LB52_USB_DS5_OUTPUT_REPORT_SIZE,
        .input_report_id = SF32LB52_USB_DS5_INPUT_REPORT_ID,
        .output_report_id = SF32LB52_USB_DS5_OUTPUT_REPORT_ID,
        .gamepad_is_hid = true,
        .audio_capable = true,
    },
    {
        .vid = SF32LB52_USB_NINTENDO_VID,
        .pid = SF32LB52_USB_NINTENDO_PID,
        .input_report_size = SF32LB52_USB_NINTENDO_INPUT_REPORT_SIZE,
        .output_report_size = SF32LB52_USB_NINTENDO_OUTPUT_REPORT_SIZE,
        .input_report_id = SF32LB52_USB_NINTENDO_INPUT_REPORT_ID,
        .output_report_id = SF32LB52_USB_NINTENDO_OUTPUT_REPORT_ID,
        .gamepad_is_hid = true,
        .audio_capable = false,
    },
    {
        .vid = SF32LB52_USB_DS5_VID,
        .pid = SF32LB52_USB_DSE_PID,
        .input_report_size = SF32LB52_USB_DS5_INPUT_REPORT_SIZE,
        .output_report_size = SF32LB52_USB_DSE_OUTPUT_REPORT_SIZE,
        .input_report_id = SF32LB52_USB_DS5_INPUT_REPORT_ID,
        .output_report_id = SF32LB52_USB_DS5_OUTPUT_REPORT_ID,
        .gamepad_is_hid = true,
        .audio_capable = true,
    },
};

static bool usb_role_is_dualsense(Sf32lb52UsbRole role)
{
    return role == Sf32lb52UsbRoleDualSense ||
           role == Sf32lb52UsbRoleDualSenseEdge;
}

static bool usb_role_valid(Sf32lb52UsbRole role)
{
    return role == Sf32lb52UsbRoleXbox360 ||
           role == Sf32lb52UsbRoleDualSense ||
           role == Sf32lb52UsbRoleNintendo ||
           role == Sf32lb52UsbRoleDualSenseEdge;
}

static void usb_memory_barrier(void)
{
#if defined(__GNUC__)
    __sync_synchronize();
#endif
}

static bool is_manager_command(const uint8_t *payload, uint16_t len)
{
    static const uint8_t magic[] = {'Y', '7', 'H', 'I', 'D', '1'};

    return payload != 0 && len >= sizeof(magic) &&
           memcmp(payload, magic, sizeof(magic)) == 0;
}

static const Sf32lb52UsbRoleCapabilities *active_capabilities(void)
{
    return &role_capabilities[(unsigned int)usb.role];
}

static uint16_t fill_neutral_hid_input(uint8_t *buffer, uint16_t reqlen)
{
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();
    uint8_t neutral[SF32LB52_USB_HID_REPORT_SIZE];
    uint16_t len = caps->input_report_size;

    memset(neutral, 0, sizeof(neutral));
    if (usb_role_is_dualsense(usb.role)) {
        neutral[0] = SF32LB52_USB_DS5_INPUT_REPORT_ID;
        neutral[1] = 128u;
        neutral[2] = 128u;
        neutral[3] = 128u;
        neutral[4] = 128u;
        neutral[8] = 8u;   /* D-pad released. */
        neutral[33] = 0x80u; /* Touch contact 1 is not touching. */
        neutral[37] = 0x80u; /* Touch contact 2 is not touching. */
        neutral[53] = 0x2au; /* Unknown/full battery, avoid false warning. */
    } else if (usb.role == Sf32lb52UsbRoleNintendo) {
        neutral[0] = SF32LB52_USB_NINTENDO_INPUT_REPORT_ID;
        neutral[2] = 0x20u;
        /* Two packed 12-bit (0x800, 0x800) stick centres. */
        neutral[11] = 0x00u;
        neutral[12] = 0x08u;
        neutral[13] = 0x80u;
        neutral[14] = 0x00u;
        neutral[15] = 0x08u;
        neutral[16] = 0x80u;
    } else {
        neutral[0] = SF32LB52_USB_XBOX_INPUT_REPORT_ID;
        neutral[1] = SF32LB52_USB_XBOX_INPUT_REPORT_SIZE;
    }

    if (len > reqlen) {
        len = reqlen;
    }
    memcpy(buffer, neutral, len);
    return len;
}

/* -------------------------------------------------------------------------- */
/* Device, configuration and HID report descriptors                           */
/* -------------------------------------------------------------------------- */

static const uint8_t xbox_device_descriptor[] = {
    18, USB_DESC_TYPE_DEVICE,
    U16_LE(0x0200),
    0xff, 0xff, 0xff,
    USB_EP0_SIZE,
    U16_LE(SF32LB52_USB_XBOX_VID),
    U16_LE(SF32LB52_USB_XBOX_PID),
    U16_LE(0x0114),
    1, 2, 3,
    1,
};

static const uint8_t dualsense_device_descriptor[] = {
    18, USB_DESC_TYPE_DEVICE,
    U16_LE(0x0200),
    USB_CLASS_PER_INTERFACE, 0x00, 0x00,
    USB_EP0_SIZE,
    U16_LE(SF32LB52_USB_DS5_VID),
    U16_LE(SF32LB52_USB_DS5_PID),
    U16_LE(0x0100),
    1, 2, 3,
    1,
};

static const uint8_t dualsense_edge_device_descriptor[] = {
    18, USB_DESC_TYPE_DEVICE,
    U16_LE(0x0200),
    USB_CLASS_PER_INTERFACE, 0x00, 0x00,
    USB_EP0_SIZE,
    U16_LE(SF32LB52_USB_DS5_VID),
    U16_LE(SF32LB52_USB_DSE_PID),
    U16_LE(0x0100),
    1, 2, 3,
    1,
};

static const uint8_t nintendo_device_descriptor[] = {
    18, USB_DESC_TYPE_DEVICE,
    U16_LE(0x0200),
    USB_CLASS_PER_INTERFACE, 0x00, 0x00,
    USB_EP0_SIZE,
    U16_LE(SF32LB52_USB_NINTENDO_VID),
    U16_LE(SF32LB52_USB_NINTENDO_PID),
    U16_LE(0x0104),
    1, 2, 3,
    1,
};

static const uint8_t manager_hid_report_descriptor[] = {
    0x06, 0x00, 0xff,
    0x09, 0x01,
    0xa1, 0x01,
    0x85, SF32LB52_USB_HID_FEATURE_REPORT_ID,
    0x09, 0x01,
    0x15, 0x00,
    0x26, 0xff, 0x00,
    0x75, 0x08,
    0x95, SF32LB52_USB_HID_PAYLOAD_SIZE,
    0xb1, 0x02,
    0xc0,
};

static const uint8_t nintendo_hid_report_descriptor[] = {
#if defined(SF32LB52_USB_SMOKE_MOUSE) && !defined(SF32LB52_USB_SMOKE_NS2)
#if defined(SF32LB52_USB_SMOKE_64)
    0x06, 0x00, 0xff,
    0x09, 0x01,
    0xa1, 0x01,
    0x15, 0x00,
    0x26, 0xff, 0x00,
    0x75, 0x08,
    0x95, 0x40,
    0x09, 0x01,
    0x81, 0x02,
    0xc0,
#else
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xc0, 0xc0,
#endif
#else
    0x06, 0x00, 0xff,
    0x09, 0x01,
    0xa1, 0x01,
    0x15, 0x00,
    0x26, 0xff, 0x00,
    0x75, 0x08,
    0x85, SF32LB52_USB_NINTENDO_INPUT_REPORT_ID,
    0x95, SF32LB52_USB_HID_PAYLOAD_SIZE,
    0x09, 0x01,
    0x81, 0x02,
    0x85, SF32LB52_USB_NINTENDO_OUTPUT_REPORT_ID,
    0x95, SF32LB52_USB_HID_PAYLOAD_SIZE,
    0x09, 0x01,
    0x91, 0x02,
    0x85, SF32LB52_USB_HID_FEATURE_REPORT_ID,
    0x95, SF32LB52_USB_HID_PAYLOAD_SIZE,
    0x09, 0x01,
    0xb1, 0x02,
    0xc0,
#endif
};

/*
 * DualSense HID persona captured by the existing Pico/TinyUSB target.  The
 * bridge management channel reuses native feature report 0xf6 so the report
 * descriptor remains byte-for-byte compatible with the 321-byte DS5 layout.
 */
static const uint8_t dualsense_hid_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x09, 0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02, 0x06,
    0x00, 0xff, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07,
    0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00, 0x05,
    0x09, 0x19, 0x01, 0x29, 0x0f, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0f, 0x81, 0x02, 0x06,
    0x00, 0xff, 0x09, 0x21, 0x95, 0x0d, 0x81, 0x02, 0x06, 0x00, 0xff, 0x09, 0x22, 0x15, 0x00, 0x26,
    0xff, 0x00, 0x75, 0x08, 0x95, 0x34, 0x81, 0x02, 0x85, 0x02, 0x09, 0x23, 0x95, 0x2f, 0x91, 0x02,
    0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xb1, 0x02, 0x85, 0x08, 0x09, 0x34, 0x95, 0x2f, 0xb1, 0x02,
    0x85, 0x09, 0x09, 0x24, 0x95, 0x13, 0xb1, 0x02, 0x85, 0x0a, 0x09, 0x25, 0x95, 0x1a, 0xb1, 0x02,
    0x85, 0x0b, 0x09, 0x41, 0x95, 0x29, 0xb1, 0x02, 0x85, 0x0c, 0x09, 0x42, 0x95, 0x29, 0xb1, 0x02,
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0x21, 0x09, 0x27, 0x95, 0x04, 0xb1, 0x02,
    0x85, 0x22, 0x09, 0x40, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0x80, 0x09, 0x28, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x81, 0x09, 0x29, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0x82, 0x09, 0x2a, 0x95, 0x09, 0xb1, 0x02,
    0x85, 0x83, 0x09, 0x2b, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0x84, 0x09, 0x2c, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x85, 0x09, 0x2d, 0x95, 0x02, 0xb1, 0x02, 0x85, 0xa0, 0x09, 0x2e, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xe0, 0x09, 0x2f, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0xf0, 0x09, 0x30, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf1, 0x09, 0x31, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0xf2, 0x09, 0x32, 0x95, 0x0f, 0xb1, 0x02,
    0x85, 0xf4, 0x09, 0x35, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0xf5, 0x09, 0x36, 0x95, 0x03, 0xb1, 0x02,
    0x85, 0xf6, 0x09, 0x37, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0xf7, 0x09, 0x38, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf8, 0x09, 0x39, 0x95, 0x3f, 0xb1, 0x02, 0x85, 0xf9, 0x09, 0x3a, 0x95, 0x3f, 0xb1, 0x02,
    0xc0,
};

/* DualSense Edge extends the DS5 layout with a 63-byte output body and
 * profile/configuration Feature reports. This layout follows the Sony-facing
 * implementation used by the BL618 bridge reference. */
static const uint8_t dualsense_edge_hid_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,
    0x85, 0x01,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x06, 0x81, 0x02,
    0x06, 0x00, 0xff, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39,
    0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x65, 0x00, 0x05, 0x09,
    0x19, 0x01, 0x29, 0x0f, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x0f, 0x81, 0x02,
    0x06, 0x00, 0xff, 0x09, 0x21, 0x95, 0x0d, 0x81, 0x02,
    0x06, 0x00, 0xff, 0x09, 0x22,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x34, 0x81, 0x02,
    0x85, 0x02, 0x09, 0x23, 0x95, 0x3f, 0x91, 0x02,
    0x85, 0x05, 0x09, 0x33, 0x95, 0x28, 0xb1, 0x02,
    0x85, 0x08, 0x09, 0x34, 0x95, 0x2f, 0xb1, 0x02,
    0x85, 0x09, 0x09, 0x24, 0x95, 0x13, 0xb1, 0x02,
    0x85, 0x0a, 0x09, 0x25, 0x95, 0x1a, 0xb1, 0x02,
    0x85, 0x0b, 0x09, 0x41, 0x95, 0x29, 0xb1, 0x02,
    0x85, 0x0c, 0x09, 0x42, 0x95, 0x29, 0xb1, 0x02,
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x21, 0x09, 0x27, 0x95, 0x04, 0xb1, 0x02,
    0x85, 0x22, 0x09, 0x40, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x80, 0x09, 0x28, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x81, 0x09, 0x29, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x82, 0x09, 0x2a, 0x95, 0x09, 0xb1, 0x02,
    0x85, 0x83, 0x09, 0x2b, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x84, 0x09, 0x2c, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x85, 0x09, 0x2d, 0x95, 0x02, 0xb1, 0x02,
    0x85, 0xa0, 0x09, 0x2e, 0x95, 0x01, 0xb1, 0x02,
    0x85, 0xe0, 0x09, 0x2f, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf0, 0x09, 0x30, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf1, 0x09, 0x31, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf2, 0x09, 0x32, 0x95, 0x34, 0xb1, 0x02,
    0x85, 0xf4, 0x09, 0x35, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf5, 0x09, 0x36, 0x95, 0x03, 0xb1, 0x02,
    0x85, 0x60, 0x09, 0x41, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0x61, 0x09, 0x42, 0xb1, 0x02,
    0x85, 0x62, 0x09, 0x43, 0xb1, 0x02,
    0x85, 0x63, 0x09, 0x44, 0xb1, 0x02,
    0x85, 0x64, 0x09, 0x45, 0xb1, 0x02,
    0x85, 0x65, 0x09, 0x46, 0xb1, 0x02,
    0x85, 0x68, 0x09, 0x47, 0xb1, 0x02,
    0x85, 0x70, 0x09, 0x48, 0xb1, 0x02,
    0x85, 0x71, 0x09, 0x49, 0xb1, 0x02,
    0x85, 0x72, 0x09, 0x4a, 0xb1, 0x02,
    0x85, 0x73, 0x09, 0x4b, 0xb1, 0x02,
    0x85, 0x74, 0x09, 0x4c, 0xb1, 0x02,
    0x85, 0x75, 0x09, 0x4d, 0xb1, 0x02,
    0x85, 0x76, 0x09, 0x4e, 0xb1, 0x02,
    0x85, 0x77, 0x09, 0x4f, 0xb1, 0x02,
    0x85, 0x78, 0x09, 0x50, 0xb1, 0x02,
    0x85, 0x79, 0x09, 0x51, 0xb1, 0x02,
    0x85, 0x7a, 0x09, 0x52, 0xb1, 0x02,
    0x85, 0x7b, 0x09, 0x53, 0xb1, 0x02,
    0x85, 0xf6, 0x09, 0x37, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf7, 0x09, 0x38, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf8, 0x09, 0x39, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xf9, 0x09, 0x3a, 0x95, 0x3f, 0xb1, 0x02,
    0x85, 0xfb, 0x09, 0x3c, 0x95, 0x3f, 0xb1, 0x02,
    0xc0,
};

static const uint8_t xbox_configuration_descriptor[] = {
    9, USB_DESC_TYPE_CONFIGURATION,
    U16_LE(XBOX_USB_CONFIG_TOTAL_LEN),
    XBOX_USB_ITF_TOTAL,
    USB_CONFIG_VALUE,
    0,
    USB_CONFIG_ATTR_REMOTE_WAKEUP,
    USB_CONFIG_POWER_500MA,

    /* Primary XUSB gamepad interface (class ff/subclass 5d/protocol 01). */
    9, USB_DESC_TYPE_INTERFACE,
    XBOX_GAMEPAD_ITF_NUM, 0, 2,
    USB_CLASS_VENDOR_SPECIFIC, 0x5d, 0x01, 0,

    /* Microsoft XUSB gamepad class-specific descriptor. */
    0x11, 0x21, U16_LE(0x0100),
    0x01, 0x25,
    XBOX_GAMEPAD_EP_IN, SF32LB52_USB_XBOX_INPUT_REPORT_SIZE,
    0x00, 0x00, 0x00, 0x00, 0x13,
    XBOX_GAMEPAD_EP_OUT, SF32LB52_USB_XBOX_OUTPUT_REPORT_SIZE,
    0x00, 0x00,

    7, USB_DESC_TYPE_ENDPOINT,
    XBOX_GAMEPAD_EP_IN,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(XBOX_GAMEPAD_EP_SIZE),
    1,

    7, USB_DESC_TYPE_ENDPOINT,
    XBOX_GAMEPAD_EP_OUT,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(XBOX_GAMEPAD_EP_SIZE),
    8,

    /* Control/interrupt HID keeps feature report 0x7f in XUSB mode. */
    9, USB_DESC_TYPE_INTERFACE,
    XBOX_MANAGER_ITF_NUM, 0, 1,
    USB_CLASS_HID, 0, 0, 4,

    9, USB_DESC_TYPE_HID,
    U16_LE(0x0111), 0, 1,
    USB_DESC_TYPE_HID_REPORT,
    U16_LE(sizeof(manager_hid_report_descriptor)),

    7, USB_DESC_TYPE_ENDPOINT,
    XBOX_MANAGER_EP_IN,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(XBOX_MANAGER_EP_SIZE),
    10,
};

static const uint8_t dualsense_configuration_descriptor[] = {
    9, USB_DESC_TYPE_CONFIGURATION,
    U16_LE(DUALSENSE_USB_CONFIG_TOTAL_LEN),
    DUALSENSE_USB_ITF_TOTAL,
    USB_CONFIG_VALUE,
    0,
    USB_CONFIG_ATTR_BUS_POWERED,
    USB_CONFIG_POWER_500MA,

    /* UAC1 Audio Control, matching the four-channel DualSense USB speaker. */
    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_AUDIO_CONTROL_ITF_NUM, 0, 0,
    USB_CLASS_AUDIO, 0x01, 0x00, 0,

    10, 0x24, 0x01, U16_LE(0x0100), U16_LE(73), 2,
    DUALSENSE_AUDIO_SPEAKER_ITF_NUM, DUALSENSE_AUDIO_MIC_ITF_NUM,

    /* USB streaming input terminal: four channels (speaker L/R + haptic L/R). */
    12, 0x24, 0x02, 1, U16_LE(0x0101), 6, 4, U16_LE(0x0033), 0, 0,
    12, 0x24, 0x06, DUALSENSE_AUDIO_SPEAKER_FU_ID, 1, 1,
    0x03, 0x00, 0x00, 0x00, 0x00, 0,
    9, 0x24, 0x03, 3, U16_LE(0x0301), 4,
    DUALSENSE_AUDIO_SPEAKER_FU_ID, 0,

    /* Headset microphone input terminal and USB streaming output terminal. */
    12, 0x24, 0x02, 4, U16_LE(0x0402), 3, 2, U16_LE(0x0003), 0, 0,
    9, 0x24, 0x06, DUALSENSE_AUDIO_MIC_FU_ID, 4, 1, 0x03, 0x00, 0,
    9, 0x24, 0x03, 6, U16_LE(0x0101), 1,
    DUALSENSE_AUDIO_MIC_FU_ID, 0,

    /* Speaker/audio-haptics stream, alternate 0 (closed). */
    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_AUDIO_SPEAKER_ITF_NUM, 0, 0,
    USB_CLASS_AUDIO, 0x02, 0x00, 0,

    /* Speaker/audio-haptics stream, alternate 1: 4ch, 16-bit, 48 kHz. */
    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_AUDIO_SPEAKER_ITF_NUM, 1, 1,
    USB_CLASS_AUDIO, 0x02, 0x00, 0,
    7, 0x24, 0x01, 1, 1, U16_LE(0x0001),
    11, 0x24, 0x02, 1, 4, 2, 16, 1, 0x80, 0xbb, 0x00,
    9, USB_DESC_TYPE_ENDPOINT,
    DUALSENSE_AUDIO_EP_OUT, 0x09,
    U16_LE(DUALSENSE_AUDIO_OUT_PACKET_SIZE), 1, 0, 0,
    7, 0x25, 0x01, 0, 0, U16_LE(0),

    /* Microphone stream, alternate 0 (closed). */
    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_AUDIO_MIC_ITF_NUM, 0, 0,
    USB_CLASS_AUDIO, 0x02, 0x00, 0,

    /* Microphone stream, alternate 1: 2ch, 16-bit, 48 kHz. */
    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_AUDIO_MIC_ITF_NUM, 1, 1,
    USB_CLASS_AUDIO, 0x02, 0x00, 0,
    7, 0x24, 0x01, 6, 1, U16_LE(0x0001),
    11, 0x24, 0x02, 1, 2, 2, 16, 1, 0x80, 0xbb, 0x00,
    9, USB_DESC_TYPE_ENDPOINT,
    DUALSENSE_AUDIO_EP_IN, 0x05,
    U16_LE(DUALSENSE_AUDIO_IN_PACKET_SIZE), 1, 0, 0,
    7, 0x25, 0x01, 0, 0, U16_LE(0),

    9, USB_DESC_TYPE_INTERFACE,
    DUALSENSE_HID_ITF_NUM, 0, 2,
    USB_CLASS_HID, 0, 0, 3,

    9, USB_DESC_TYPE_HID,
    U16_LE(0x0111), 0, 1,
    USB_DESC_TYPE_HID_REPORT,
    U16_LE(sizeof(dualsense_hid_report_descriptor)),

    7, USB_DESC_TYPE_ENDPOINT,
    DUALSENSE_HID_EP_IN,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(SF32LB52_USB_DS5_INPUT_REPORT_SIZE),
    SF32LB52_USB_HID_POLL_INTERVAL_MS,

    7, USB_DESC_TYPE_ENDPOINT,
    DUALSENSE_HID_EP_OUT,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(64),
    SF32LB52_USB_HID_POLL_INTERVAL_MS,
};

static uint8_t dualsense_edge_configuration_descriptor[
    sizeof(dualsense_configuration_descriptor)];

static void prepare_dualsense_edge_configuration_descriptor(void)
{
    size_t i;

    memcpy(dualsense_edge_configuration_descriptor,
           dualsense_configuration_descriptor,
           sizeof(dualsense_configuration_descriptor));
    for (i = 0U; i + 8U < sizeof(dualsense_edge_configuration_descriptor);
         i++) {
        if (dualsense_edge_configuration_descriptor[i] == 9U &&
            dualsense_edge_configuration_descriptor[i + 1U] ==
                USB_DESC_TYPE_HID &&
            dualsense_edge_configuration_descriptor[i + 6U] ==
                USB_DESC_TYPE_HID_REPORT) {
            dualsense_edge_configuration_descriptor[i + 7U] =
                (uint8_t)sizeof(dualsense_edge_hid_report_descriptor);
            dualsense_edge_configuration_descriptor[i + 8U] =
                (uint8_t)(sizeof(dualsense_edge_hid_report_descriptor) >> 8U);
            return;
        }
    }
}

static const uint8_t nintendo_configuration_descriptor[] = {
    9, USB_DESC_TYPE_CONFIGURATION,
    U16_LE(NINTENDO_USB_CONFIG_TOTAL_LEN),
    NINTENDO_USB_ITF_TOTAL,
    USB_CONFIG_VALUE,
    0,
    USB_CONFIG_ATTR_BUS_POWERED,
    USB_CONFIG_POWER_500MA,

    9, USB_DESC_TYPE_INTERFACE,
    NINTENDO_HID_ITF_NUM, 0, NINTENDO_HID_EP_COUNT,
    USB_CLASS_HID,
#if defined(SF32LB52_USB_SMOKE_MOUSE)
    1, 2,
#else
    0, 0,
#endif
    4,

    9, USB_DESC_TYPE_HID,
    U16_LE(0x0101), 0, 1,
    USB_DESC_TYPE_HID_REPORT,
    U16_LE(sizeof(nintendo_hid_report_descriptor)),

    7, USB_DESC_TYPE_ENDPOINT,
    NINTENDO_HID_EP_IN,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(NINTENDO_HID_EP_SIZE),
    NINTENDO_HID_POLL_INTERVAL,

#if !defined(SF32LB52_USB_SMOKE_MOUSE)
    7, USB_DESC_TYPE_ENDPOINT,
    NINTENDO_HID_EP_OUT,
    USB_ENDPOINT_ATTR_INTERRUPT,
    U16_LE(NINTENDO_HID_EP_SIZE),
    NINTENDO_HID_POLL_INTERVAL,

    9, USB_DESC_TYPE_INTERFACE,
    NINTENDO_VENDOR_ITF_NUM, 0, 2,
    USB_CLASS_VENDOR_SPECIFIC, 0, 0, 5,

    7, USB_DESC_TYPE_ENDPOINT,
    NINTENDO_VENDOR_EP_IN,
    USB_ENDPOINT_ATTR_BULK,
    U16_LE(NINTENDO_VENDOR_EP_SIZE), 0,

    7, USB_DESC_TYPE_ENDPOINT,
    NINTENDO_VENDOR_EP_OUT,
    USB_ENDPOINT_ATTR_BULK,
    U16_LE(NINTENDO_VENDOR_EP_SIZE), 0,
#endif
};

STATIC_ASSERT(xbox_device_descriptor_is_18_bytes, sizeof(xbox_device_descriptor) == 18);
STATIC_ASSERT(dualsense_device_descriptor_is_18_bytes, sizeof(dualsense_device_descriptor) == 18);
STATIC_ASSERT(dualsense_edge_device_descriptor_is_18_bytes,
              sizeof(dualsense_edge_device_descriptor) == 18);
STATIC_ASSERT(nintendo_device_descriptor_is_18_bytes, sizeof(nintendo_device_descriptor) == 18);
STATIC_ASSERT(xbox_configuration_length_matches,
              sizeof(xbox_configuration_descriptor) == XBOX_USB_CONFIG_TOTAL_LEN);
STATIC_ASSERT(dualsense_configuration_length_matches,
              sizeof(dualsense_configuration_descriptor) == DUALSENSE_USB_CONFIG_TOTAL_LEN);
STATIC_ASSERT(nintendo_configuration_length_matches,
              sizeof(nintendo_configuration_descriptor) == NINTENDO_USB_CONFIG_TOTAL_LEN);
STATIC_ASSERT(dualsense_report_descriptor_is_321_bytes,
              sizeof(dualsense_hid_report_descriptor) == 321);
STATIC_ASSERT(dualsense_edge_report_descriptor_is_445_bytes,
              sizeof(dualsense_edge_hid_report_descriptor) == 445);

/* -------------------------------------------------------------------------- */
/* CherryUSB registration and endpoint callbacks                              */
/* -------------------------------------------------------------------------- */

#if defined(SF32LB52_USB_USE_CHERRYUSB)
static volatile bool gamepad_in_busy;
static volatile bool gamepad_host_ready;
static volatile bool vendor_in_busy;
static volatile uint32_t gamepad_in_started_ms;
static volatile int8_t gamepad_out_arm_result;
static volatile int8_t vendor_out_arm_result;

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t gamepad_in_buffer[SF32LB52_USB_HID_REPORT_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t gamepad_out_buffer[SF32LB52_USB_HID_REPORT_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t hid_feature_buffer[SF32LB52_USB_HID_REPORT_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t xinput_control_buffer[SF32LB52_USB_XBOX_INPUT_REPORT_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t audio_out_buffer[DUALSENSE_AUDIO_OUT_PACKET_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t audio_in_buffer[DUALSENSE_AUDIO_IN_PACKET_SIZE];
static int16_t audio_speaker_accum[DUALSENSE_AUDIO_BLOCK_FRAMES * 2U];
static int16_t audio_speaker_ready_pcm[DUALSENSE_AUDIO_OPUS_FRAMES * 2U];
static int16_t audio_speaker_worker_pcm[DUALSENSE_AUDIO_OPUS_FRAMES * 2U];
static uint8_t audio_haptic_accum[DUALSENSE_AUDIO_HAPTIC_BYTES];
static uint8_t audio_haptic_ready_data[DUALSENSE_AUDIO_HAPTIC_BYTES];
static uint8_t audio_haptic_worker_data[DUALSENSE_AUDIO_HAPTIC_BYTES];
static int16_t audio_haptic_analysis_accum[DUALSENSE_AUDIO_HAPTIC_BYTES];
static int16_t audio_haptic_analysis_ready[DUALSENSE_AUDIO_HAPTIC_BYTES];
static int16_t audio_haptic_analysis_worker[DUALSENSE_AUDIO_HAPTIC_BYTES];
static uint8_t audio_opus_data[DUALSENSE_AUDIO_OPUS_BYTES];
static volatile uint8_t audio_speaker_ready;
static volatile uint8_t audio_haptic_ready;
static volatile uint8_t audio_output_claimed;
static volatile uint8_t audio_haptics_stop_pending;
static uint16_t audio_speaker_samples;
static uint8_t audio_haptic_bytes;
static uint8_t audio_haptic_phase;
static int32_t audio_haptic_left_sum;
static int32_t audio_haptic_right_sum;
static uint8_t audio_report_sequence;
static uint8_t audio_packet_counter;
static struct rt_semaphore audio_work_sem;
static rt_thread_t audio_worker_thread;
static OpusEncoder *audio_opus_encoder;
static OpusDecoder *audio_opus_decoder;
static uint8_t audio_latest_state[DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE];
static uint8_t audio_mic_opus_queue[DUALSENSE_MIC_QUEUE_STORAGE]
                                     [DUALSENSE_MIC_OPUS_BYTES];
static volatile uint8_t audio_mic_queue_head;
static volatile uint8_t audio_mic_queue_tail;
static int16_t audio_mic_ring[DUALSENSE_MIC_RING_FRAMES * 2U];
static uint16_t audio_mic_ring_head;
static uint16_t audio_mic_ring_tail;
static uint16_t audio_mic_ring_count;
static volatile uint8_t audio_mic_state_pending;
static volatile uint8_t audio_mic_state_notified;
static uint32_t audio_mic_state_retry_due_ms;
static uint8_t audio_mic_classic_connected;
static uint8_t audio_mic_status_sequence;
static volatile uint8_t audio_control_state_pending;
static int audio_speaker_volume_db;
static bool audio_speaker_muted;
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
#define NINTENDO_VENDOR_REPLY_QUEUE_CAPACITY 4U
#define NINTENDO_VENDOR_REPLY_QUEUE_STORAGE \
    (NINTENDO_VENDOR_REPLY_QUEUE_CAPACITY + 1U)
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t vendor_out_buffer[NINTENDO_VENDOR_EP_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t vendor_in_buffer[SF32LB52_NS2_VENDOR_REPLY_MAX];
static uint8_t vendor_reply_queue[NINTENDO_VENDOR_REPLY_QUEUE_STORAGE]
                                 [SF32LB52_NS2_VENDOR_REPLY_MAX];
static uint16_t vendor_reply_lengths[NINTENDO_VENDOR_REPLY_QUEUE_STORAGE];
static volatile uint8_t vendor_reply_head;
static volatile uint8_t vendor_reply_tail;
#endif

static const uint8_t standard_device_quality_descriptor[] = {
    10, USB_DESC_TYPE_DEVICE_QUALIFIER,
    U16_LE(0x0200), 0x00, 0x00, 0x00,
    USB_EP0_SIZE, 1, 0,
};

static const uint8_t xbox_device_quality_descriptor[] = {
    10, USB_DESC_TYPE_DEVICE_QUALIFIER,
    U16_LE(0x0200), 0xff, 0xff, 0xff,
    USB_EP0_SIZE, 1, 0,
};

#if !defined(SF32LB52_USB_SMOKE_MOUSE)
static const uint8_t msosv2_wcid_descriptor[] = {
    /* Descriptor set header. */
    U16_LE(0x000a), U16_LE(0x0000),
    0x00, 0x00, 0x03, 0x06, U16_LE(0x00b2),

    /* Configuration subset for configuration value 1. */
    U16_LE(0x0008), U16_LE(0x0001),
    0x00, 0x00, U16_LE(0x00a8),

    /* Function subset for the Nintendo vendor bulk interface. */
    U16_LE(0x0008), U16_LE(0x0002),
    NINTENDO_VENDOR_ITF_NUM, 0x00, U16_LE(0x00a0),

    U16_LE(0x0014), U16_LE(0x0003),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    U16_LE(0x0084), U16_LE(0x0004),
    U16_LE(0x0007), U16_LE(0x002a),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_LE(0x0050),
    '{', 0x00, '6', 0x00, 'F', 0x00, '1', 0x00, '3', 0x00, '7', 0x00,
    '2', 0x00, '5', 0x00, 'E', 0x00, '-', 0x00, 'E', 0x00, 'F', 0x00,
    '0', 0x00, 'E', 0x00, '-', 0x00, '4', 0x00, 'F', 0x00, 'D', 0x00,
    '3', 0x00, '-', 0x00, 'A', 0x00, 'E', 0x00, '5', 0x00, 'F', 0x00,
    '-', 0x00, 'B', 0x00, '2', 0x00, 'D', 0x00, 'E', 0x00, '9', 0x00,
    '8', 0x00, '9', 0x00, 'E', 0x00, 'C', 0x00, '8', 0x00, '2', 0x00,
    '5', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t bos_winusb_descriptor[] = {
    USB_BOS_HEADER_DESCRIPTOR_INIT(5 + USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_LEN, 1),
    USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_INIT(
        USB_SWITCH2_MS_VENDOR_CODE, sizeof(msosv2_wcid_descriptor)),
};

static struct usb_msosv2_descriptor msosv2_desc = {
    .vendor_code = USB_SWITCH2_MS_VENDOR_CODE,
    .compat_id = msosv2_wcid_descriptor,
    .compat_id_len = sizeof(msosv2_wcid_descriptor),
};

static struct usb_bos_descriptor bos_desc = {
    .string = bos_winusb_descriptor,
    .string_len = sizeof(bos_winusb_descriptor),
};
#endif

static const char *const xbox_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Microsoft",
    "Controller",
    "SF32LB52X360",
    "Bridge Management",
};

static const char *const dualsense_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Sony Interactive Entertainment",
    "DualSense Wireless Controller",
    "DualSense HID",
};

static const char *const dualsense_edge_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Sony Interactive Entertainment",
    "DualSense Edge Wireless Controller",
    "DualSense Edge HID",
};

static const char *const nintendo_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Nintendo Co., Ltd.",
#if defined(SF32LB52_USB_SMOKE_MOUSE)
#if defined(SF32LB52_USB_SMOKE_NS2)
    "SF32LB52 NS2 HID Smoke",
    "SF32LB52SMOKENS2",
#elif defined(SF32LB52_USB_SMOKE_64)
    "SF32LB52 HID 64-byte Smoke",
    "SF32LB52SMOKE64",
#else
    "SF32LB52 HID Mouse Smoke",
    "SF32LB52SMOKE",
#endif
#else
    "Nintendo Switch Pro Controller",
    "HA2F83JI",
#endif
    "HID Interface",
    "Nintendo Switch 2 bulk",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return sf32lb52_usb_device_descriptor(NULL);
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return sf32lb52_usb_configuration_descriptor(NULL);
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return usb.role == Sf32lb52UsbRoleXbox360 ?
        xbox_device_quality_descriptor : standard_device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    const char *const *strings;
    size_t count;

    (void)speed;
    if (usb.role == Sf32lb52UsbRoleXbox360) {
        strings = xbox_string_descriptors;
        count = sizeof(xbox_string_descriptors) / sizeof(xbox_string_descriptors[0]);
    } else if (usb.role == Sf32lb52UsbRoleDualSense) {
        strings = dualsense_string_descriptors;
        count = sizeof(dualsense_string_descriptors) / sizeof(dualsense_string_descriptors[0]);
    } else if (usb.role == Sf32lb52UsbRoleDualSenseEdge) {
        strings = dualsense_edge_string_descriptors;
        count = sizeof(dualsense_edge_string_descriptors) /
                sizeof(dualsense_edge_string_descriptors[0]);
    } else {
        strings = nintendo_string_descriptors;
        count = sizeof(nintendo_string_descriptors) / sizeof(nintendo_string_descriptors[0]);
    }
    return index < count ? strings[index] : NULL;
}

static const struct usb_descriptor standard_usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

static const struct usb_descriptor nintendo_usb_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
    .msosv2_descriptor = &msosv2_desc,
    .bos_descriptor = &bos_desc,
#endif
};

static struct usbd_interface gamepad_hid_interface;
static struct usbd_interface xinput_interface;
static struct usbd_interface manager_hid_interface;
static struct usbd_interface audio_control_interface;
static struct usbd_interface audio_speaker_interface;
static struct usbd_interface audio_mic_interface;

static struct audio_entity_info dualsense_audio_entities[] = {
    {
        .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
        .bEntityId = DUALSENSE_AUDIO_SPEAKER_FU_ID,
        .ep = DUALSENSE_AUDIO_EP_OUT,
    },
    {
        .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT,
        .bEntityId = DUALSENSE_AUDIO_MIC_FU_ID,
        .ep = DUALSENSE_AUDIO_EP_IN,
    },
};

static int16_t audio_haptic_average(int32_t sum)
{
    int32_t value = sum / 16;

    if (value < INT16_MIN) {
        value = INT16_MIN;
    } else if (value > INT16_MAX) {
        value = INT16_MAX;
    }
    return (int16_t)value;
}

static int8_t audio_haptic_to_s8(int16_t average)
{
    int32_t value = (int32_t)average / 256;

    if (value < -128) value = -128;
    if (value > 127) value = 127;
    return (int8_t)value;
}

static void audio_latest_state_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    memset(audio_latest_state, 0, sizeof(audio_latest_state));
    rt_hw_interrupt_enable(level);
}

static void audio_latest_state_store(const uint8_t *state, size_t len)
{
    rt_base_t level;

    if (len > sizeof(audio_latest_state)) {
        len = sizeof(audio_latest_state);
    }
    level = rt_hw_interrupt_disable();
    memset(audio_latest_state, 0, sizeof(audio_latest_state));
    if (state != 0 && len != 0U) {
        memcpy(audio_latest_state, state, len);
    }
    rt_hw_interrupt_enable(level);
}

static void audio_latest_state_snapshot(uint8_t state[DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE])
{
    rt_base_t level = rt_hw_interrupt_disable();

    memcpy(state, audio_latest_state, sizeof(audio_latest_state));
    rt_hw_interrupt_enable(level);
}

static void audio_bridge_reset(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    audio_speaker_samples = 0U;
    audio_haptic_bytes = 0U;
    audio_haptic_phase = 0U;
    audio_haptic_left_sum = 0;
    audio_haptic_right_sum = 0;
    if (!audio_output_claimed) {
        audio_speaker_ready = 0U;
        audio_haptic_ready = 0U;
    }
    audio_haptics_stop_pending = 1U;
    rt_hw_interrupt_enable(level);
    if (audio_worker_thread != RT_NULL) {
        (void)rt_sem_release(&audio_work_sem);
    }
}

static bool audio_bridge_claim_output(void)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (!audio_speaker_ready || !audio_haptic_ready || audio_output_claimed) {
        rt_hw_interrupt_enable(level);
        return false;
    }
    audio_output_claimed = 1U;
    rt_hw_interrupt_enable(level);

    memcpy(audio_speaker_worker_pcm, audio_speaker_ready_pcm,
           sizeof(audio_speaker_worker_pcm));
    memcpy(audio_haptic_worker_data, audio_haptic_ready_data,
           sizeof(audio_haptic_worker_data));
    memcpy(audio_haptic_analysis_worker, audio_haptic_analysis_ready,
           sizeof(audio_haptic_analysis_worker));

    level = rt_hw_interrupt_disable();
    audio_speaker_ready = 0U;
    audio_haptic_ready = 0U;
    audio_output_claimed = 0U;
    rt_hw_interrupt_enable(level);
    return true;
}

static void audio_resample_speaker_block(void)
{
    uint16_t out_frame;

    for (out_frame = 0U; out_frame < DUALSENSE_AUDIO_OPUS_FRAMES;
         out_frame++) {
        uint16_t source_frame = (uint16_t)(
            ((uint32_t)out_frame * DUALSENSE_AUDIO_BLOCK_FRAMES) /
            DUALSENSE_AUDIO_OPUS_FRAMES);

        audio_speaker_ready_pcm[out_frame * 2U] =
            audio_speaker_accum[source_frame * 2U];
        audio_speaker_ready_pcm[out_frame * 2U + 1U] =
            audio_speaker_accum[source_frame * 2U + 1U];
    }
}

static void audio_bridge_accept_pcm(const uint8_t *data, uint32_t nbytes)
{
    const int16_t *pcm = (const int16_t *)data;
    uint32_t frames = nbytes / (4U * sizeof(int16_t));
    uint32_t frame;

    for (frame = 0U; frame < frames; frame++) {
        if (audio_speaker_samples + 2U <=
            (uint16_t)(DUALSENSE_AUDIO_BLOCK_FRAMES * 2U)) {
            audio_speaker_accum[audio_speaker_samples++] = pcm[frame * 4U];
            audio_speaker_accum[audio_speaker_samples++] = pcm[frame * 4U + 1U];
        }

        audio_haptic_left_sum += pcm[frame * 4U + 2U];
        audio_haptic_right_sum += pcm[frame * 4U + 3U];
        audio_haptic_phase++;
        if (audio_haptic_phase == 16U) {
            int16_t left_average = audio_haptic_average(audio_haptic_left_sum);
            int16_t right_average = audio_haptic_average(audio_haptic_right_sum);

            audio_haptic_analysis_accum[audio_haptic_bytes] = left_average;
            audio_haptic_accum[audio_haptic_bytes++] =
                (uint8_t)audio_haptic_to_s8(left_average);
            audio_haptic_analysis_accum[audio_haptic_bytes] = right_average;
            audio_haptic_accum[audio_haptic_bytes++] =
                (uint8_t)audio_haptic_to_s8(right_average);
            audio_haptic_phase = 0U;
            audio_haptic_left_sum = 0;
            audio_haptic_right_sum = 0;
            if (audio_haptic_bytes == DUALSENSE_AUDIO_HAPTIC_BYTES) {
                if (!audio_output_claimed &&
                    !audio_haptic_ready && !audio_speaker_ready &&
                    audio_speaker_samples ==
                        DUALSENSE_AUDIO_BLOCK_FRAMES * 2U) {
                    audio_resample_speaker_block();
                    memcpy(audio_haptic_ready_data, audio_haptic_accum,
                           sizeof(audio_haptic_ready_data));
                    memcpy(audio_haptic_analysis_ready,
                           audio_haptic_analysis_accum,
                           sizeof(audio_haptic_analysis_ready));
                    usb_memory_barrier();
                    audio_speaker_ready = 1U;
                    audio_haptic_ready = 1U;
                } else {
                    usb.status.audio_bt_dropped++;
                }
                audio_speaker_samples = 0U;
                audio_haptic_bytes = 0U;
                (void)rt_sem_release(&audio_work_sem);
            }
        }
    }
}

static bool audio_mic_take_opus(uint8_t frame[DUALSENSE_MIC_OPUS_BYTES])
{
    uint8_t tail;

    rt_enter_critical();
    tail = audio_mic_queue_tail;
    if (tail == audio_mic_queue_head) {
        rt_exit_critical();
        return false;
    }
    memcpy(frame, audio_mic_opus_queue[tail], DUALSENSE_MIC_OPUS_BYTES);
    audio_mic_queue_tail =
        (uint8_t)((tail + 1U) % DUALSENSE_MIC_QUEUE_STORAGE);
    rt_exit_critical();
    return true;
}

static void audio_mic_write_pcm(const int16_t *mono, uint16_t frames)
{
    uint16_t frame;

    rt_enter_critical();
    for (frame = 0U; frame < frames; frame++) {
        uint16_t offset;

        if (audio_mic_ring_count == DUALSENSE_MIC_RING_FRAMES) {
            audio_mic_ring_tail =
                (uint16_t)((audio_mic_ring_tail + 1U) %
                           DUALSENSE_MIC_RING_FRAMES);
            audio_mic_ring_count--;
            usb.status.audio_mic_bt_dropped++;
        }
        offset = (uint16_t)(audio_mic_ring_head * 2U);
        audio_mic_ring[offset] = mono[frame];
        audio_mic_ring[offset + 1U] = mono[frame];
        audio_mic_ring_head =
            (uint16_t)((audio_mic_ring_head + 1U) %
                       DUALSENSE_MIC_RING_FRAMES);
        audio_mic_ring_count++;
    }
    rt_exit_critical();
}

static uint32_t audio_mic_fill_packet(void)
{
    int16_t *packet = (int16_t *)audio_in_buffer;
    uint16_t available;
    uint16_t frame;

    memset(audio_in_buffer, 0,
           DUALSENSE_MIC_PACKET_FRAMES * 2U * sizeof(int16_t));
    rt_enter_critical();
    available = audio_mic_ring_count < DUALSENSE_MIC_PACKET_FRAMES ?
        audio_mic_ring_count : DUALSENSE_MIC_PACKET_FRAMES;
    for (frame = 0U; frame < available; frame++) {
        uint16_t offset = (uint16_t)(audio_mic_ring_tail * 2U);

        packet[frame * 2U] = audio_mic_ring[offset];
        packet[frame * 2U + 1U] = audio_mic_ring[offset + 1U];
        audio_mic_ring_tail =
            (uint16_t)((audio_mic_ring_tail + 1U) %
                       DUALSENSE_MIC_RING_FRAMES);
    }
    audio_mic_ring_count = (uint16_t)(audio_mic_ring_count - available);
    if (available < DUALSENSE_MIC_PACKET_FRAMES) {
        usb.status.audio_mic_underflows++;
    }
    rt_exit_critical();
    return DUALSENSE_MIC_PACKET_FRAMES * 2U * sizeof(int16_t);
}

static void audio_mic_wake_worker_if_due(void)
{
    uint8_t wake = 0U;
    uint32_t now = platform_millis();
    rt_base_t level;

    if (audio_worker_thread == RT_NULL) {
        return;
    }
    level = rt_hw_interrupt_disable();
    if (audio_mic_state_pending && !audio_mic_state_notified &&
        (int32_t)(now - audio_mic_state_retry_due_ms) >= 0) {
        audio_mic_state_notified = 1U;
        wake = 1U;
    }
    rt_hw_interrupt_enable(level);
    if (wake) {
        (void)rt_sem_release(&audio_work_sem);
    }
}

static void audio_mic_process_state(void)
{
    uint8_t report[142] = {0};
    uint8_t pending;
    uint8_t enabled;
    uint32_t now = platform_millis();
    int result;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    audio_mic_state_notified = 0U;
    pending = audio_mic_state_pending;
    if (pending &&
        (int32_t)(now - audio_mic_state_retry_due_ms) >= 0) {
        audio_mic_state_pending = 0U;
    } else {
        pending = 0U;
    }
    enabled = usb.status.audio_mic_open ? 1U : 0U;
    rt_hw_interrupt_enable(level);
    if (!pending) {
        return;
    }
    report[0] = 0x32U;
    report[1] = (uint8_t)((audio_mic_status_sequence++ & 0x0fU) << 4U);
    report[2] = 0x90U;
    report[3] = 1U;
    report[4] = enabled ? 0x03U : 0x02U;
    result = ds5_classic_send_raw_output_report(report, sizeof(report));
    if (result != DS5_CLASSIC_OK) {
        usb.status.audio_mic_bt_dropped++;
        level = rt_hw_interrupt_disable();
        if ((usb.status.audio_mic_open ? 1U : 0U) == enabled) {
            audio_mic_state_pending = 1U;
            audio_mic_state_retry_due_ms = now +
                DUALSENSE_MIC_STATE_RETRY_MS;
        }
        rt_hw_interrupt_enable(level);
    }
}

static void audio_mic_process_queue(void)
{
    uint8_t opus_frame[DUALSENSE_MIC_OPUS_BYTES];
    int16_t mono[DUALSENSE_AUDIO_OPUS_FRAMES];

    while (audio_mic_take_opus(opus_frame)) {
        int decoded;

        if (!usb.status.audio_mic_open || audio_opus_decoder == 0) {
            usb.status.audio_mic_bt_dropped++;
            continue;
        }
        decoded = opus_decode(audio_opus_decoder, opus_frame,
                              DUALSENSE_MIC_OPUS_BYTES, mono,
                              DUALSENSE_AUDIO_OPUS_FRAMES, 0);
        if (decoded <= 0) {
            usb.status.audio_mic_decode_errors++;
            continue;
        }
        audio_mic_write_pcm(mono, (uint16_t)decoded);
    }
}

static void audio_control_process_state(void)
{
    uint8_t state[DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE];
    uint8_t pending;
    size_t state_len;
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    pending = audio_control_state_pending;
    audio_control_state_pending = 0U;
    memcpy(state, audio_latest_state, sizeof(state));
    state_len = usb.role == Sf32lb52UsbRoleDualSenseEdge ?
        DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE :
        DS5_CLASSIC_USB_OUTPUT_BODY_SIZE;
    rt_hw_interrupt_enable(level);
    if (pending && ds5_classic_send_output_report(state, state_len) !=
                       DS5_CLASSIC_OK) {
        usb.status.audio_bt_dropped++;
    }
}

static void audio_bridge_process_output(void)
{
    uint8_t report[DUALSENSE_AUDIO_BT_REPORT_SIZE];
    uint8_t latest_state[DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE];
    ds5_classic_status_t classic_status;

    if (!audio_bridge_claim_output()) {
        return;
    }
    if (usb.audio_haptics_cb != 0) {
        usb.audio_haptics_cb(audio_haptic_analysis_worker,
                             DUALSENSE_AUDIO_HAPTIC_BYTES / 2U);
        usb.status.audio_haptic_blocks++;
    }

    ds5_classic_get_status(&classic_status);
    if (!classic_status.connected) {
        return;
    }
    {
        int encoded = -1;

        if (audio_opus_encoder != 0) {
            encoded = opus_encode(audio_opus_encoder,
                                  (const opus_int16 *)audio_speaker_worker_pcm,
                                  DUALSENSE_AUDIO_OPUS_FRAMES,
                                  audio_opus_data,
                                  sizeof(audio_opus_data));
        }
        if (encoded < 0) {
            memset(audio_opus_data, 0, sizeof(audio_opus_data));
            usb.status.audio_opus_errors++;
        } else if ((size_t)encoded < sizeof(audio_opus_data)) {
            memset(audio_opus_data + encoded, 0,
                   sizeof(audio_opus_data) - (size_t)encoded);
        }
    }

    memset(report, 0, sizeof(report));
    report[0] = 0x36U;
    report[1] = (uint8_t)((audio_report_sequence++ & 0x0fU) << 4);
    report[2] = 0x91U;
    report[3] = 7U;
    report[4] = 0xfeU;
    report[5] = 64U;
    report[6] = 64U;
    report[7] = 64U;
    report[8] = 64U;
    report[9] = 64U;
    report[10] = audio_packet_counter++;
    report[11] = 0x90U;
    report[12] = 63U;
    audio_latest_state_snapshot(latest_state);
    memcpy(report + 13U, latest_state, sizeof(latest_state));
    report[76] = 0x92U;
    report[77] = DUALSENSE_AUDIO_HAPTIC_BYTES;
    memcpy(report + 78U, audio_haptic_worker_data,
           sizeof(audio_haptic_worker_data));
    report[142] = 0x93U;
    report[143] = DUALSENSE_AUDIO_OPUS_BYTES;
    memcpy(report + 144U, audio_opus_data, sizeof(audio_opus_data));
    if (ds5_classic_send_raw_output_report(report, sizeof(report)) ==
        DS5_CLASSIC_OK) {
        usb.status.audio_bt_reports++;
    } else {
        usb.status.audio_bt_dropped++;
    }
}

static void audio_bridge_worker(void *parameter)
{
    (void)parameter;
    audio_opus_encoder = opus_encoder_create(48000, 2,
                                              OPUS_APPLICATION_AUDIO, 0);
    if (audio_opus_encoder != 0) {
        (void)opus_encoder_ctl(audio_opus_encoder,
                               OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
        (void)opus_encoder_ctl(audio_opus_encoder, OPUS_SET_BITRATE(160000));
        (void)opus_encoder_ctl(audio_opus_encoder, OPUS_SET_VBR(0));
        (void)opus_encoder_ctl(audio_opus_encoder, OPUS_SET_COMPLEXITY(0));
    } else {
        usb.status.audio_opus_errors++;
    }
    audio_opus_decoder = opus_decoder_create(48000, 1, 0);
    if (audio_opus_decoder == 0) {
        usb.status.audio_mic_decode_errors++;
    }

    for (;;) {
        if (audio_haptics_stop_pending != 0U) {
            audio_haptics_stop_pending = 0U;
            if (usb.audio_haptics_cb != 0) {
                usb.audio_haptics_cb(0, 0U);
            }
        }
        (void)rt_sem_take(&audio_work_sem, RT_WAITING_FOREVER);
        audio_mic_process_state();
        audio_mic_process_queue();
        audio_control_process_state();
        audio_bridge_process_output();
    }
}

static void audio_bridge_init(void)
{
    audio_bridge_reset();
    memset(audio_opus_data, 0, sizeof(audio_opus_data));
    audio_latest_state_reset();
    rt_enter_critical();
    audio_mic_queue_head = 0U;
    audio_mic_queue_tail = 0U;
    audio_mic_ring_head = 0U;
    audio_mic_ring_tail = 0U;
    audio_mic_ring_count = 0U;
    audio_mic_state_pending = 0U;
    audio_mic_state_notified = 0U;
    audio_mic_state_retry_due_ms = 0U;
    audio_mic_classic_connected = 0U;
    audio_control_state_pending = 0U;
    audio_speaker_volume_db = 0;
    audio_speaker_muted = false;
    rt_exit_critical();
    if (audio_worker_thread != RT_NULL) {
        return;
    }
    (void)rt_sem_init(&audio_work_sem, "ds5aud", 0U, RT_IPC_FLAG_FIFO);
    audio_worker_thread = rt_thread_create("ds5_audio", audio_bridge_worker,
                                           RT_NULL, 32768U, 20U, 10U);
    if (audio_worker_thread != RT_NULL) {
        (void)rt_thread_startup(audio_worker_thread);
    } else {
        usb.status.audio_opus_errors++;
    }
}

static void audio_mic_request_state(bool open, bool force)
{
    bool changed;
    uint32_t now = platform_millis();
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    changed = usb.status.audio_mic_open != open;
    usb.status.audio_mic_open = open;
    if (changed || force) {
        audio_mic_queue_head = 0U;
        audio_mic_queue_tail = 0U;
        audio_mic_ring_head = 0U;
        audio_mic_ring_tail = 0U;
        audio_mic_ring_count = 0U;
        audio_mic_state_pending = 1U;
        audio_mic_state_retry_due_ms = now;
    }
    rt_hw_interrupt_enable(level);

    if (changed || force) {
        audio_mic_wake_worker_if_due();
    }
}

static void audio_mic_set_open(bool open)
{
    audio_mic_request_state(open, false);
}

static void audio_mic_force_closed(void)
{
    audio_mic_request_state(false, true);
}

static void audio_mic_close_before_usb_disconnect(void)
{
    audio_mic_force_closed();
    audio_mic_process_state();
}

static void audio_mic_sync_classic_state(void)
{
    ds5_classic_status_t classic_status;
    uint8_t connected;
    uint8_t reconnected = 0U;
    rt_base_t level;

    ds5_classic_get_status(&classic_status);
    connected = classic_status.connected ? 1U : 0U;
    level = rt_hw_interrupt_disable();
    if (connected && !audio_mic_classic_connected) {
        reconnected = 1U;
    }
    audio_mic_classic_connected = connected;
    rt_hw_interrupt_enable(level);

    if (reconnected) {
        audio_mic_request_state(
            usb_role_is_dualsense(usb.role) && usb.status.audio_mic_open,
            true);
    }
    audio_mic_wake_worker_if_due();
}

static void gamepad_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    usb.status.in_report_callbacks++;
    usb.status.last_in_nbytes = nbytes;
    usb.status.last_in_busy_ms = 0;
    usb.status.in_busy = false;
    gamepad_in_busy = false;
}

static void gamepad_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();
    uint32_t read_len = caps->output_report_size;

    (void)ep;
    if (nbytes > caps->output_report_size) {
        nbytes = caps->output_report_size;
    }
    sf32lb52_usb_hid_set_report_cb(0, HID_REPORT_TYPE_OUTPUT,
                                   gamepad_out_buffer, (uint16_t)nbytes);
    (void)usbd_ep_start_read(busid, ep, gamepad_out_buffer, read_len);
}

static void manager_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;
}

static void audio_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    usb.status.audio_out_packets++;
    usb.status.audio_out_bytes += nbytes;
    if (nbytes >= 8U) {
        audio_bridge_accept_pcm(audio_out_buffer, nbytes);
    }
    if (usb.status.audio_speaker_open) {
        if (usbd_ep_start_read(busid, ep, audio_out_buffer,
                               sizeof(audio_out_buffer)) != 0) {
            usb.status.audio_out_errors++;
        }
    }
}

static void audio_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    uint32_t packet_len;

    usb.status.audio_in_packets++;
    usb.status.audio_in_bytes += nbytes;
    if (usb.status.audio_mic_open) {
        packet_len = audio_mic_fill_packet();
        if (usbd_ep_start_write(busid, ep, audio_in_buffer,
                                packet_len) != 0) {
            usb.status.audio_in_errors++;
        }
    }
}

#if !defined(SF32LB52_USB_SMOKE_MOUSE)
static void vendor_start_next_reply(uint8_t busid)
{
    uint8_t tail;
    uint16_t len;

    if (vendor_in_busy) {
        return;
    }
    tail = vendor_reply_tail;
    if (tail == vendor_reply_head) {
        return;
    }

    len = vendor_reply_lengths[tail];
    memcpy(vendor_in_buffer, vendor_reply_queue[tail], len);
    vendor_in_busy = true;
    if (usbd_ep_start_write(busid, NINTENDO_VENDOR_EP_IN,
                            vendor_in_buffer, len) != 0) {
        vendor_in_busy = false;
        usb.status.vendor_errors++;
    } else {
        usb.status.vendor_in_packets++;
    }
    vendor_reply_tail = (uint8_t)((tail + 1U) %
                                  NINTENDO_VENDOR_REPLY_QUEUE_STORAGE);
}

static void vendor_in_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;
    vendor_in_busy = false;
    usb.status.vendor_in_callbacks++;
    usb.status.vendor_in_bytes += nbytes;
    vendor_start_next_reply(busid);
}

static void vendor_out_callback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    size_t reply_len;
    uint8_t head;
    uint8_t next;

    if (nbytes > sizeof(vendor_out_buffer)) {
        nbytes = sizeof(vendor_out_buffer);
    }
    usb.status.vendor_out_packets++;
    usb.status.vendor_out_bytes += nbytes;
    usb.status.vendor_last_command = nbytes > 0U ? vendor_out_buffer[0] : 0U;
    usb.status.vendor_last_argument = nbytes > 3U ? vendor_out_buffer[3] : 0U;
    head = vendor_reply_head;
    next = (uint8_t)((head + 1U) % NINTENDO_VENDOR_REPLY_QUEUE_STORAGE);
    if (next == vendor_reply_tail) {
        usb.status.vendor_errors++;
        reply_len = 0U;
    } else {
        reply_len = sf32lb52_ns2_vendor_build_reply(
            vendor_out_buffer, nbytes, vendor_reply_queue[head],
            sizeof(vendor_reply_queue[head]));
    }
    if (reply_len != 0U) {
        vendor_reply_lengths[head] = (uint16_t)reply_len;
        usb_memory_barrier();
        vendor_reply_head = next;
        vendor_start_next_reply(busid);
    }
    (void)usbd_ep_start_read(busid, ep, vendor_out_buffer, sizeof(vendor_out_buffer));
}
#endif

static struct usbd_endpoint gamepad_in_ep = {
    .ep_cb = gamepad_in_callback,
};

static struct usbd_endpoint gamepad_out_ep = {
    .ep_cb = gamepad_out_callback,
};

static struct usbd_endpoint manager_in_ep = {
    .ep_cb = manager_in_callback,
    .ep_addr = XBOX_MANAGER_EP_IN,
};

static struct usbd_endpoint audio_out_ep = {
    .ep_cb = audio_out_callback,
    .ep_addr = DUALSENSE_AUDIO_EP_OUT,
};

static struct usbd_endpoint audio_in_ep = {
    .ep_cb = audio_in_callback,
    .ep_addr = DUALSENSE_AUDIO_EP_IN,
};

void usbd_audio_set_volume(uint8_t busid, uint8_t ep, uint8_t ch, int volume_db)
{
    int volume;
    rt_base_t level;

    (void)busid;
    (void)ch;
    if (ep == DUALSENSE_AUDIO_EP_IN) {
        return;
    }
    if (volume_db < -100) {
        volume_db = -100;
    } else if (volume_db > 0) {
        volume_db = 0;
    }
    volume = ((volume_db + 100) * 127 + 50) / 100;
    level = rt_hw_interrupt_disable();
    audio_speaker_volume_db = volume_db;
    audio_latest_state[0] |= 0x30U;
    audio_latest_state[4] = (uint8_t)volume;
    audio_latest_state[5] = (uint8_t)volume;
    audio_control_state_pending = 1U;
    rt_hw_interrupt_enable(level);
    if (audio_worker_thread != RT_NULL) {
        (void)rt_sem_release(&audio_work_sem);
    }
}

int usbd_audio_get_volume(uint8_t busid, uint8_t ep, uint8_t ch)
{
    (void)busid;
    (void)ch;
    return ep == DUALSENSE_AUDIO_EP_IN ? 0 : audio_speaker_volume_db;
}

void usbd_audio_set_mute(uint8_t busid, uint8_t ep, uint8_t ch, bool mute)
{
    rt_base_t level;

    (void)busid;
    (void)ch;
    if (ep == DUALSENSE_AUDIO_EP_IN) {
        return;
    }
    level = rt_hw_interrupt_disable();
    audio_speaker_muted = mute;
    audio_latest_state[1] |= 0x02U;
    if (mute) {
        audio_latest_state[9] |= 0x60U;
    } else {
        audio_latest_state[9] &= (uint8_t)~0x60U;
    }
    audio_control_state_pending = 1U;
    rt_hw_interrupt_enable(level);
    if (audio_worker_thread != RT_NULL) {
        (void)rt_sem_release(&audio_work_sem);
    }
}

bool usbd_audio_get_mute(uint8_t busid, uint8_t ep, uint8_t ch)
{
    (void)busid;
    (void)ch;
    return ep == DUALSENSE_AUDIO_EP_IN ? false : audio_speaker_muted;
}

void usbd_audio_set_sampling_freq(uint8_t busid, uint8_t ep, uint32_t sampling_freq)
{
    (void)busid;
    (void)ep;
    (void)sampling_freq;
}

uint32_t usbd_audio_get_sampling_freq(uint8_t busid, uint8_t ep)
{
    (void)busid;
    (void)ep;
    return 48000u;
}

void usbd_audio_open(uint8_t busid, uint8_t intf)
{
    if (!usb_role_is_dualsense(usb.role)) {
        return;
    }
    if (intf == DUALSENSE_AUDIO_SPEAKER_ITF_NUM) {
        usb.status.audio_speaker_open = true;
        audio_bridge_reset();
        if (usbd_ep_start_read(busid, DUALSENSE_AUDIO_EP_OUT,
                               audio_out_buffer, sizeof(audio_out_buffer)) != 0) {
            usb.status.audio_out_errors++;
        }
    } else if (intf == DUALSENSE_AUDIO_MIC_ITF_NUM) {
        uint32_t packet_len;

        audio_mic_set_open(true);
        packet_len = audio_mic_fill_packet();
        if (usbd_ep_start_write(busid, DUALSENSE_AUDIO_EP_IN,
                                audio_in_buffer, packet_len) != 0) {
            usb.status.audio_in_errors++;
        }
    }
}

void usbd_audio_close(uint8_t busid, uint8_t intf)
{
    (void)busid;
    if (intf == DUALSENSE_AUDIO_SPEAKER_ITF_NUM) {
        usb.status.audio_speaker_open = false;
        audio_bridge_reset();
    } else if (intf == DUALSENSE_AUDIO_MIC_ITF_NUM) {
        audio_mic_force_closed();
    }
}

#if !defined(SF32LB52_USB_SMOKE_MOUSE)
static struct usbd_endpoint vendor_in_ep = {
    .ep_cb = vendor_in_callback,
    .ep_addr = NINTENDO_VENDOR_EP_IN,
};

static struct usbd_endpoint vendor_out_ep = {
    .ep_cb = vendor_out_callback,
    .ep_addr = NINTENDO_VENDOR_EP_OUT,
};
#endif

static uint8_t active_gamepad_in_ep(void)
{
    if (usb_role_is_dualsense(usb.role)) {
        return DUALSENSE_HID_EP_IN;
    }
    if (usb.role == Sf32lb52UsbRoleNintendo) {
        return NINTENDO_HID_EP_IN;
    }
    return XBOX_GAMEPAD_EP_IN;
}

static uint8_t active_gamepad_out_ep(void)
{
    if (usb_role_is_dualsense(usb.role)) {
        return DUALSENSE_HID_EP_OUT;
    }
    if (usb.role == Sf32lb52UsbRoleNintendo) {
        return NINTENDO_HID_EP_OUT;
    }
    return XBOX_GAMEPAD_EP_OUT;
}

static int xinput_vendor_request_handler(uint8_t busid,
                                         struct usb_setup_packet *setup,
                                         uint8_t **data,
                                         uint32_t *len)
{
    uint32_t reply_len;

    (void)busid;
    if (!setup || !data || !*data || !len) {
        return -1;
    }

    if ((setup->bmRequestType & USB_REQUEST_DIR_MASK) == USB_REQUEST_DIR_OUT) {
        *len = 0;
        return 0;
    }

    memset(xinput_control_buffer, 0, sizeof(xinput_control_buffer));
    xinput_control_buffer[0] = 0x00;
    xinput_control_buffer[1] = SF32LB52_USB_XBOX_INPUT_REPORT_SIZE;
    reply_len = *len < sizeof(xinput_control_buffer) ? *len : sizeof(xinput_control_buffer);
    memcpy(*data, xinput_control_buffer, reply_len);
    *len = reply_len;
    return 0;
}

static const uint8_t *active_hid_report_descriptor(size_t *len)
{
    const uint8_t *descriptor;
    size_t descriptor_len;

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        descriptor = manager_hid_report_descriptor;
        descriptor_len = sizeof(manager_hid_report_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSense) {
        descriptor = dualsense_hid_report_descriptor;
        descriptor_len = sizeof(dualsense_hid_report_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSenseEdge) {
        descriptor = dualsense_edge_hid_report_descriptor;
        descriptor_len = sizeof(dualsense_edge_hid_report_descriptor);
    } else {
        descriptor = nintendo_hid_report_descriptor;
        descriptor_len = sizeof(nintendo_hid_report_descriptor);
    }
    if (len) {
        *len = descriptor_len;
    }
    return descriptor;
}

static int usb_register_current_role(void);

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_RESET:
        platform_log("usb", "event reset");
        gamepad_in_busy = false;
        gamepad_host_ready = false;
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
        vendor_in_busy = false;
        vendor_reply_head = 0U;
        vendor_reply_tail = 0U;
#endif
        usb.status.audio_speaker_open = false;
        audio_bridge_reset();
        audio_mic_force_closed();
        break;
    case USBD_EVENT_DISCONNECTED:
        platform_log("usb", "event disconnected");
        gamepad_in_busy = false;
        gamepad_host_ready = false;
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
        vendor_in_busy = false;
        vendor_reply_head = 0U;
        vendor_reply_tail = 0U;
#endif
        usb.status.audio_speaker_open = false;
        audio_bridge_reset();
        audio_mic_force_closed();
        sf32lb52_usb_unmount_cb();
        break;
    case USBD_EVENT_SUSPEND:
        sf32lb52_usb_suspend_cb(false);
        break;
    case USBD_EVENT_RESUME:
        sf32lb52_usb_resume_cb();
        break;
    case USBD_EVENT_CONFIGURED:
        gamepad_in_busy = false;
        gamepad_host_ready = true;
        sf32lb52_usb_mount_cb();
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
        gamepad_out_arm_result = (int8_t)usbd_ep_start_read(
            busid, active_gamepad_out_ep(), gamepad_out_buffer,
            active_capabilities()->output_report_size);
        if (usb.role == Sf32lb52UsbRoleNintendo) {
            vendor_out_arm_result = (int8_t)usbd_ep_start_read(
                busid, NINTENDO_VENDOR_EP_OUT, vendor_out_buffer,
                sizeof(vendor_out_buffer));
        }
#endif
        break;
    default:
        break;
    }
}

static int usb_register_current_role(void)
{
    const uint8_t *report_descriptor;
    size_t report_descriptor_len;

    memset(&gamepad_hid_interface, 0, sizeof(gamepad_hid_interface));
    memset(&xinput_interface, 0, sizeof(xinput_interface));
    memset(&manager_hid_interface, 0, sizeof(manager_hid_interface));
    memset(&audio_control_interface, 0, sizeof(audio_control_interface));
    memset(&audio_speaker_interface, 0, sizeof(audio_speaker_interface));
    memset(&audio_mic_interface, 0, sizeof(audio_mic_interface));

    gamepad_in_ep.ep_addr = active_gamepad_in_ep();
    gamepad_out_ep.ep_addr = active_gamepad_out_ep();

    usbd_desc_register(0, usb.role == Sf32lb52UsbRoleNintendo ?
                          &nintendo_usb_descriptor : &standard_usb_descriptor);

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        xinput_interface.vendor_handler = xinput_vendor_request_handler;
        usbd_add_interface(0, &xinput_interface);
        usbd_add_interface(0,
            usbd_hid_init_intf(0, &manager_hid_interface,
                               manager_hid_report_descriptor,
                               sizeof(manager_hid_report_descriptor)));
        usbd_add_endpoint(0, &gamepad_in_ep);
        usbd_add_endpoint(0, &gamepad_out_ep);
        usbd_add_endpoint(0, &manager_in_ep);
    } else {
        report_descriptor = active_hid_report_descriptor(&report_descriptor_len);
        if (usb_role_is_dualsense(usb.role)) {
            usbd_add_interface(0,
                usbd_audio_init_intf(0, &audio_control_interface,
                                     DUALSENSE_AUDIO_VERSION,
                                     dualsense_audio_entities,
                                     sizeof(dualsense_audio_entities) /
                                         sizeof(dualsense_audio_entities[0])));
            usbd_add_interface(0,
                usbd_audio_init_intf(0, &audio_speaker_interface,
                                     DUALSENSE_AUDIO_VERSION,
                                     dualsense_audio_entities,
                                     sizeof(dualsense_audio_entities) /
                                         sizeof(dualsense_audio_entities[0])));
            usbd_add_interface(0,
                usbd_audio_init_intf(0, &audio_mic_interface,
                                     DUALSENSE_AUDIO_VERSION,
                                     dualsense_audio_entities,
                                     sizeof(dualsense_audio_entities) /
                                         sizeof(dualsense_audio_entities[0])));
        }
        usbd_add_interface(0,
            usbd_hid_init_intf(0, &gamepad_hid_interface,
                               report_descriptor, report_descriptor_len));
        usbd_add_endpoint(0, &gamepad_in_ep);
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
        usbd_add_endpoint(0, &gamepad_out_ep);
        if (usb_role_is_dualsense(usb.role)) {
            usbd_add_endpoint(0, &audio_out_ep);
            usbd_add_endpoint(0, &audio_in_ep);
        }
        if (usb.role == Sf32lb52UsbRoleNintendo) {
            usbd_add_endpoint(0, &vendor_in_ep);
            usbd_add_endpoint(0, &vendor_out_ep);
        }
#endif
    }

    return usbd_initialize(0, (uintptr_t)USBC_BASE, usbd_event_handler);
}

static void usb_phy_disconnect(void)
{
    hwp_usbc->power &= (uint8_t)~USB_POWER_SOFTCONN;
}

static void usb_phy_connect(void)
{
    hwp_usbc->usbcfg |= (USB_USBCFG_AVALID | USB_USBCFG_AVALID_DR);
    hwp_usbc->devctl |= USB_DEVCTL_SESSION;
    hwp_usbc->power |= USB_POWER_SOFTCONN;
}

static bool usb_apply_pending_role(void)
{
    const Sf32lb52UsbRole previous_role = usb.role;
    const Sf32lb52UsbRole requested_role = usb.pending_role;
    int result;

    usb.role_switch_requested = 0;
    usb.status.role_switch_pending = false;
    if (requested_role == previous_role) {
        usb.status.pending_role = previous_role;
        return true;
    }

    if (usb_role_is_dualsense(previous_role)) {
        usb.status.audio_speaker_open = false;
        audio_bridge_reset();
        audio_mic_close_before_usb_disconnect();
    }
    usb_phy_disconnect();
    HAL_Delay(20);
    sf32lb52_usb_unmount_cb();
    (void)usbd_deinitialize(0);
    usb.initialized = false;

    usb.role = requested_role;
    usb.status.active_role = requested_role;
    usb.status.pending_role = requested_role;
    usb.feature_set_head = 0U;
    usb.feature_set_tail = 0U;
    usb.manager_reply_pending = 0U;
    gamepad_in_busy = false;
    gamepad_host_ready = false;
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
    vendor_in_busy = false;
    vendor_reply_head = 0U;
    vendor_reply_tail = 0U;
#endif
    audio_latest_state_reset();
    result = usb_register_current_role();
    if (result < 0) {
        int rollback_result;

        usb.status.role_switch_failures++;
        usb.role = previous_role;
        usb.status.active_role = previous_role;
        usb.status.pending_role = previous_role;
        rollback_result = usb_register_current_role();
        if (rollback_result >= 0) {
            usb.initialized = true;
            usb_phy_connect();
        } else {
            /* Remain disconnected and report the stack as unavailable.  A
             * later force-reconnect can retry registration safely. */
            usb.status.role_switch_failures++;
            usb.initialized = false;
        }
        return false;
    }

    usb.initialized = true;
    usb.status.role_switches++;
    usb_phy_connect();
    return true;
}
#endif

/* -------------------------------------------------------------------------- */
/* Public callbacks and role control                                           */
/* -------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define SF32_WEAK __attribute__((weak))
#else
#define SF32_WEAK
#endif

SF32_WEAK void sf32lb52_usb_mount_cb(void)
{
    usb.status.mounted = true;
    usb.status.suspended = false;
    usb.status.in_busy = false;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    gamepad_host_ready = true;
#endif
}

SF32_WEAK void sf32lb52_usb_unmount_cb(void)
{
    usb.status.mounted = false;
    usb.status.suspended = false;
    usb.status.in_busy = false;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    gamepad_host_ready = false;
#endif
}

SF32_WEAK void sf32lb52_usb_suspend_cb(bool remote_wakeup_enabled)
{
    (void)remote_wakeup_enabled;
    usb.status.suspended = true;
}

SF32_WEAK void sf32lb52_usb_resume_cb(void)
{
    usb.status.suspended = false;
}

SF32_WEAK uint16_t sf32lb52_usb_hid_get_report_cb(uint8_t report_id,
                                                  uint8_t report_type,
                                                  uint8_t *buffer,
                                                  uint16_t reqlen)
{
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();

    if (!buffer || reqlen == 0) {
        return 0;
    }

    if (report_type == HID_REPORT_TYPE_INPUT && caps->gamepad_is_hid &&
        (report_id == 0 || report_id == caps->input_report_id)) {
        uint16_t actual_len = 0;

#if defined(SF32LB52_USB_USE_CHERRYUSB)
        gamepad_host_ready = true;
#endif
        memset(buffer, 0, reqlen);
        if (usb.input_get_cb) {
            actual_len = usb.input_get_cb(buffer, reqlen);
            if (actual_len > reqlen) {
                actual_len = reqlen;
            }
            if (actual_len > 0 && buffer[0] == caps->input_report_id) {
                return actual_len;
            }
            /* A callback belonging to the previous role must not leak a
             * differently shaped report during/after re-enumeration. */
            memset(buffer, 0, reqlen);
        }
        return fill_neutral_hid_input(buffer, reqlen);
    }

    if (report_type == HID_REPORT_TYPE_FEATURE) {
        usb.status.feature_get_reports++;
        usb.status.last_feature_report_id = report_id;
        usb.status.last_feature_len = reqlen;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
        gamepad_host_ready = true;
#endif
        memset(buffer, 0, reqlen);
        if (report_id == sf32lb52_usb_manager_feature_report_id() &&
            (!usb_role_is_dualsense(usb.role) ||
             usb.manager_reply_pending != 0U) &&
            reqlen > 1) {
            uint16_t actual_len = 0;

            buffer[0] = report_id;
            if (usb.feature_get_cb) {
                actual_len = usb.feature_get_cb(buffer + 1, (uint16_t)(reqlen - 1));
                if (actual_len > (uint16_t)(reqlen - 1)) {
                    actual_len = (uint16_t)(reqlen - 1);
                }
                if (actual_len >= 11U &&
                    memcmp(buffer + 1, "Y7HRS1", 6U) == 0) {
                    uint16_t total = (uint16_t)buffer[7] |
                        ((uint16_t)buffer[8] << 8U);
                    uint16_t offset = (uint16_t)buffer[9] |
                        ((uint16_t)buffer[10] << 8U);
                    uint16_t chunk_len = buffer[11];

                    if ((uint32_t)offset + chunk_len >= total) {
                        usb.manager_reply_pending = 0U;
                    }
                }
                return (uint16_t)(actual_len + 1);
            }
            return reqlen;
        }
        if (usb_role_is_dualsense(usb.role) &&
            usb.native_feature_get_cb) {
            return usb.native_feature_get_cb(report_id, buffer, reqlen);
        }
        if (usb.feature_get_cb && report_id == 0) {
            return usb.feature_get_cb(buffer, reqlen);
        }
        return reqlen;
    }

    memset(buffer, 0, reqlen);
    return reqlen;
}

SF32_WEAK void sf32lb52_usb_hid_set_report_cb(uint8_t report_id,
                                              uint8_t report_type,
                                              const uint8_t *buffer,
                                              uint16_t len)
{
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();
    uint8_t effective_report_id = report_id;

    if (report_type == HID_REPORT_TYPE_FEATURE) {
        const uint8_t *payload = buffer;
        uint16_t payload_len = len;
        bool includes_report_id;
        bool manager_command;

        if (effective_report_id == 0 && buffer && len > 0) {
            effective_report_id = buffer[0];
        }
        includes_report_id = buffer && len > 0 &&
            buffer[0] == effective_report_id &&
            (report_id == 0 || len == SF32LB52_USB_HID_REPORT_SIZE);
        if (includes_report_id) {
            payload = buffer + 1;
            payload_len = (uint16_t)(len - 1);
        }
        manager_command =
            effective_report_id == sf32lb52_usb_manager_feature_report_id() &&
            (!usb_role_is_dualsense(usb.role) ||
             is_manager_command(payload, payload_len));
        if (manager_command ||
            (usb_role_is_dualsense(usb.role) &&
             usb.native_feature_set_cb != 0)) {
            uint8_t head;
            uint8_t next;
            UsbFeatureSetEntry *entry;

            usb.status.feature_set_reports++;
            usb.status.last_feature_report_id = effective_report_id;
            usb.status.last_feature_len = len;
            if (payload_len > SF32LB52_USB_HID_PAYLOAD_SIZE) {
                payload_len = SF32LB52_USB_HID_PAYLOAD_SIZE;
            }
            head = usb.feature_set_head;
            next = (uint8_t)((head + 1u) % USB_FEATURE_SET_QUEUE_STORAGE);
            if (next == usb.feature_set_tail) {
                usb.status.feature_set_dropped++;
                return;
            }
            entry = &usb.feature_set_queue[head];
            entry->report_id = effective_report_id;
            entry->manager = manager_command ? 1U : 0U;
            if (payload && payload_len > 0) {
                memcpy(entry->payload, payload, payload_len);
            }
            entry->len = payload_len;
            usb_memory_barrier();
            usb.feature_set_head = next;
            if (manager_command) {
                usb.manager_reply_pending = 1U;
            }
        }
        return;
    }

    if (report_type != HID_REPORT_TYPE_OUTPUT || !buffer) {
        return;
    }

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        uint16_t payload_len = len;

        if (payload_len > caps->output_report_size) {
            payload_len = caps->output_report_size;
        }
        usb.status.out_reports_received++;
        usb.status.last_out_report_id = 0;
        usb.status.last_out_len = payload_len;
        if (usb.output_cb) {
            usb.output_cb(0, buffer, payload_len);
        }
        return;
    }

    if (effective_report_id == 0 && len > 0) {
        effective_report_id = buffer[0];
    }
    if (effective_report_id == caps->output_report_id) {
        const uint8_t *payload = buffer;
        uint16_t payload_len = len;
        const bool includes_report_id = len > 0 &&
            buffer[0] == effective_report_id &&
            (report_id == 0 || len == caps->output_report_size);

        if (includes_report_id) {
            payload = buffer + 1;
            payload_len = (uint16_t)(len - 1);
        }
        if (payload_len > (uint16_t)(caps->output_report_size - 1)) {
            payload_len = (uint16_t)(caps->output_report_size - 1);
        }
        usb.status.out_reports_received++;
        usb.status.last_out_report_id = effective_report_id;
        usb.status.last_out_len = len;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
        if (usb_role_is_dualsense(usb.role)) {
            audio_latest_state_store(payload, payload_len);
        }
#endif
        if (usb.output_cb) {
            usb.output_cb(effective_report_id, payload, payload_len);
        }
    }
}

bool sf32lb52_usb_get_role_capabilities(Sf32lb52UsbRole role,
                                        Sf32lb52UsbRoleCapabilities *out)
{
    if (!out || !usb_role_valid(role)) {
        return false;
    }
    *out = role_capabilities[(unsigned int)role];
    return true;
}

Sf32lb52UsbRole sf32lb52_usb_get_role(void)
{
    return usb.role;
}

uint8_t sf32lb52_usb_manager_feature_report_id(void)
{
    return usb_role_is_dualsense(usb.role) ?
        SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID :
        SF32LB52_USB_HID_FEATURE_REPORT_ID;
}

bool sf32lb52_usb_role_switch_pending(void)
{
    return usb.role_switch_requested != 0;
}

bool sf32lb52_usb_request_role(Sf32lb52UsbRole role)
{
    if (!usb_role_valid(role)) {
        return false;
    }
    usb.pending_role = role;
    usb.status.pending_role = role;
    if (role == usb.role) {
        usb.role_switch_requested = 0;
        usb.status.role_switch_pending = false;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
        if (!usb.initialized) {
            return false;
        }
#endif
        return true;
    }
    usb.role_switch_requested = 1;
    usb.status.role_switch_pending = true;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    if (!usb.initialized) {
        usb.role_switch_requested = 0;
        usb.status.role_switch_pending = false;
        usb.status.pending_role = usb.role;
        return false;
    }
    /* The manager command reply is fetched by a subsequent control transfer.
     * Give the host time to read that ACK before disconnecting this persona. */
    usb.role_switch_requested_ms = platform_millis();
    return true;
#else
    usb.role = role;
    usb.status.active_role = role;
    usb.status.pending_role = role;
    usb.status.role_switches++;
    usb.role_switch_requested = 0;
    usb.status.role_switch_pending = false;
    return true;
#endif
}

const uint8_t *sf32lb52_usb_device_descriptor(size_t *len)
{
    const uint8_t *descriptor;

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        descriptor = xbox_device_descriptor;
    } else if (usb.role == Sf32lb52UsbRoleDualSense) {
        descriptor = dualsense_device_descriptor;
    } else if (usb.role == Sf32lb52UsbRoleDualSenseEdge) {
        descriptor = dualsense_edge_device_descriptor;
    } else {
        descriptor = nintendo_device_descriptor;
    }
    if (len) {
        *len = 18;
    }
    return descriptor;
}

const uint8_t *sf32lb52_usb_configuration_descriptor(size_t *len)
{
    const uint8_t *descriptor;
    size_t descriptor_len;

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        descriptor = xbox_configuration_descriptor;
        descriptor_len = sizeof(xbox_configuration_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSense) {
        descriptor = dualsense_configuration_descriptor;
        descriptor_len = sizeof(dualsense_configuration_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSenseEdge) {
        prepare_dualsense_edge_configuration_descriptor();
        descriptor = dualsense_edge_configuration_descriptor;
        descriptor_len = sizeof(dualsense_edge_configuration_descriptor);
    } else {
        descriptor = nintendo_configuration_descriptor;
        descriptor_len = sizeof(nintendo_configuration_descriptor);
    }
    if (len) {
        *len = descriptor_len;
    }
    return descriptor;
}

const uint8_t *sf32lb52_usb_hid_report_descriptor(size_t *len)
{
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    return active_hid_report_descriptor(len);
#else
    const uint8_t *descriptor;
    size_t descriptor_len;

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        descriptor = manager_hid_report_descriptor;
        descriptor_len = sizeof(manager_hid_report_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSense) {
        descriptor = dualsense_hid_report_descriptor;
        descriptor_len = sizeof(dualsense_hid_report_descriptor);
    } else if (usb.role == Sf32lb52UsbRoleDualSenseEdge) {
        descriptor = dualsense_edge_hid_report_descriptor;
        descriptor_len = sizeof(dualsense_edge_hid_report_descriptor);
    } else {
        descriptor = nintendo_hid_report_descriptor;
        descriptor_len = sizeof(nintendo_hid_report_descriptor);
    }
    if (len) {
        *len = descriptor_len;
    }
    return descriptor;
#endif
}

void sf32lb52_usb_get_status(Sf32lb52UsbDeviceStatus *out)
{
    if (out) {
        *out = usb.status;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
        out->in_busy = gamepad_in_busy;
        out->host_ready = gamepad_host_ready;
        out->last_in_busy_ms = gamepad_in_busy ?
            (uint32_t)(platform_millis() - gamepad_in_started_ms) : 0;
#endif
    }
}

bool sf32lb52_usb_get_phy_status(Sf32lb52UsbPhyStatus *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    out->power = hwp_usbc->power;
    out->devctl = hwp_usbc->devctl;
    out->intrusb = hwp_usbc->intrusb;
    out->intrusbe = hwp_usbc->intrusbe;
    out->usbcfg = hwp_usbc->usbcfg;
    out->dpbrxdisl = hwp_usbc->dpbrxdisl;
    out->dpbtxdisl = hwp_usbc->dpbtxdisl;
    out->gamepad_out_arm_result = gamepad_out_arm_result;
    out->vendor_out_arm_result = vendor_out_arm_result;
    out->intrtx = hwp_usbc->intrtx;
    out->intrrx = hwp_usbc->intrrx;
    out->intrtxe = hwp_usbc->intrtxe;
    out->intrrxe = hwp_usbc->intrrxe;
    out->ep1_txmaxp = hwp_usbc->ep[1].epN.txmaxp;
    out->ep1_txcsr = hwp_usbc->ep[1].epN.txcsr;
    out->ep1_rxmaxp = hwp_usbc->ep[1].epN.rxmaxp;
    out->ep1_rxcsr = hwp_usbc->ep[1].epN.rxcsr;
    out->ep2_txmaxp = hwp_usbc->ep[2].epN.txmaxp;
    out->ep2_txcsr = hwp_usbc->ep[2].epN.txcsr;
    out->ep2_rxmaxp = hwp_usbc->ep[2].epN.rxmaxp;
    out->ep2_rxcsr = hwp_usbc->ep[2].epN.rxcsr;
    return true;
#else
    return false;
#endif
}

void sf32lb52_usb_force_reconnect(void)
{
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    if (!usb.initialized) {
        if (usb_register_current_role() < 0) {
            usb.status.role_switch_failures++;
            return;
        }
        usb.initialized = true;
    }
    if (usb_role_is_dualsense(usb.role)) {
        audio_mic_close_before_usb_disconnect();
    }
    usb_phy_disconnect();
    HAL_Delay(20);
    usb_phy_connect();
#endif
}

void sf32lb52_usb_set_report_callbacks(Sf32lb52UsbOutputReportCallback output_cb,
                                       Sf32lb52UsbInputGetCallback input_get_cb,
                                       Sf32lb52UsbFeatureGetCallback feature_get_cb,
                                       Sf32lb52UsbFeatureSetCallback feature_set_cb)
{
    usb.output_cb = output_cb;
    usb.input_get_cb = input_get_cb;
    usb.feature_get_cb = feature_get_cb;
    usb.feature_set_cb = feature_set_cb;
}

void sf32lb52_usb_set_native_feature_callbacks(
    Sf32lb52UsbNativeFeatureGetCallback feature_get_cb,
    Sf32lb52UsbNativeFeatureSetCallback feature_set_cb)
{
    usb.native_feature_get_cb = feature_get_cb;
    usb.native_feature_set_cb = feature_set_cb;
}

void sf32lb52_usb_set_audio_haptics_callback(
    Sf32lb52UsbAudioHapticsCallback callback)
{
    usb.audio_haptics_cb = callback;
}

void sf32lb52_usb_device_init(Sf32lb52UsbIdentity identity)
{
    Sf32lb52UsbOutputReportCallback output_cb = usb.output_cb;
    Sf32lb52UsbInputGetCallback input_get_cb = usb.input_get_cb;
    Sf32lb52UsbFeatureGetCallback feature_get_cb = usb.feature_get_cb;
    Sf32lb52UsbFeatureSetCallback feature_set_cb = usb.feature_set_cb;
    Sf32lb52UsbNativeFeatureGetCallback native_feature_get_cb =
        usb.native_feature_get_cb;
    Sf32lb52UsbNativeFeatureSetCallback native_feature_set_cb =
        usb.native_feature_set_cb;
    Sf32lb52UsbAudioHapticsCallback audio_haptics_cb =
        usb.audio_haptics_cb;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    const bool was_initialized = usb.initialized;
#endif

    if (!usb_role_valid(identity)) {
        identity = Sf32lb52UsbRoleNintendo;
    }

#if defined(SF32LB52_USB_USE_CHERRYUSB)
    if (was_initialized) {
        if (usb_role_is_dualsense(usb.role)) {
            audio_mic_close_before_usb_disconnect();
        }
        usb_phy_disconnect();
        HAL_Delay(20);
        (void)usbd_deinitialize(0);
    }
#endif

    memset(&usb, 0, sizeof(usb));
    usb.role = identity;
    usb.pending_role = identity;
    usb.status.active_role = identity;
    usb.status.pending_role = identity;
    usb.output_cb = output_cb;
    usb.input_get_cb = input_get_cb;
    usb.feature_get_cb = feature_get_cb;
    usb.feature_set_cb = feature_set_cb;
    usb.native_feature_get_cb = native_feature_get_cb;
    usb.native_feature_set_cb = native_feature_set_cb;
    usb.audio_haptics_cb = audio_haptics_cb;

#if defined(SF32LB52_USB_USE_CHERRYUSB)
    audio_bridge_init();
    gamepad_in_busy = false;
    gamepad_host_ready = false;
    vendor_in_busy = false;
#if !defined(SF32LB52_USB_SMOKE_MOUSE)
    vendor_reply_head = 0U;
    vendor_reply_tail = 0U;
#endif
    if (usb_register_current_role() < 0) {
        usb.status.role_switch_failures++;
        usb.initialized = false;
    } else {
        usb.initialized = true;
    }
#endif
}

void sf32lb52_usb_device_task(void)
{
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    audio_mic_sync_classic_state();
#endif

    if (usb.feature_set_cb || usb.native_feature_set_cb) {
        uint8_t processed;

        for (processed = 0u; processed < USB_FEATURE_SET_QUEUE_CAPACITY;
             processed++) {
            UsbFeatureSetEntry entry;
            uint8_t tail = usb.feature_set_tail;

            if (tail == usb.feature_set_head) {
                break;
            }
            usb_memory_barrier();
            entry = usb.feature_set_queue[tail];
            usb_memory_barrier();
            usb.feature_set_tail =
                (uint8_t)((tail + 1u) % USB_FEATURE_SET_QUEUE_STORAGE);
            if (entry.manager != 0U) {
                if (usb.feature_set_cb) {
                    usb.feature_set_cb(entry.payload, entry.len);
                }
            } else if (usb.native_feature_set_cb) {
                usb.native_feature_set_cb(entry.report_id,
                                          entry.payload,
                                          entry.len);
            }
        }
    }

    if (usb.role_switch_requested) {
#if defined(SF32LB52_USB_USE_CHERRYUSB)
        if (usb.initialized &&
            (uint32_t)(platform_millis() - usb.role_switch_requested_ms) >=
                USB_ROLE_SWITCH_REPLY_GRACE_MS) {
            (void)usb_apply_pending_role();
        } else if (!usb.initialized) {
            usb.status.role_switch_failures++;
            usb.status.pending_role = usb.role;
            usb.status.role_switch_pending = false;
            usb.role_switch_requested = 0;
        }
#else
        usb.role = usb.pending_role;
        usb.status.active_role = usb.role;
        usb.status.role_switch_pending = false;
        usb.status.role_switches++;
        usb.role_switch_requested = 0;
#endif
    }
}

bool sf32lb52_usb_send_input_report(const uint8_t *report, size_t len)
{
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();

    if (!report || len != caps->input_report_size ||
        !usb.status.mounted || usb.status.suspended) {
        usb.status.in_reports_failed++;
        return false;
    }
    if (caps->gamepad_is_hid && report[0] != caps->input_report_id) {
        usb.status.in_reports_failed++;
        return false;
    }
    if (usb.role == Sf32lb52UsbRoleXbox360 &&
        (report[0] != 0x00 || report[1] != SF32LB52_USB_XBOX_INPUT_REPORT_SIZE)) {
        usb.status.in_reports_failed++;
        return false;
    }

#if defined(SF32LB52_USB_USE_CHERRYUSB)
    if (!gamepad_host_ready || gamepad_in_busy) {
        if (gamepad_in_busy) {
            usb.status.last_in_busy_ms =
                (uint32_t)(platform_millis() - gamepad_in_started_ms);
        }
        usb.status.in_reports_failed++;
        return false;
    }

    memcpy(gamepad_in_buffer, report, len);
    gamepad_in_busy = true;
    usb.status.in_busy = true;
    gamepad_in_started_ms = platform_millis();
    if (usbd_ep_start_write(0, active_gamepad_in_ep(), gamepad_in_buffer, len) < 0) {
        gamepad_in_busy = false;
        usb.status.in_busy = false;
        usb.status.in_reports_failed++;
        return false;
    }
    usb.status.in_reports_sent++;
    return true;
#else
    usb.status.in_reports_failed++;
    return false;
#endif
}

void sf32lb52_usb_ds5_mic_input(const uint8_t *opus_data, size_t len)
{
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    uint8_t head;
    uint8_t next;

    if (opus_data == 0 || len < DUALSENSE_MIC_OPUS_BYTES) {
        usb.status.audio_mic_bt_dropped++;
        return;
    }
    usb.status.audio_mic_bt_packets++;
    rt_enter_critical();
    if (!usb_role_is_dualsense(usb.role) ||
        !usb.status.audio_mic_open || audio_worker_thread == RT_NULL) {
        usb.status.audio_mic_bt_dropped++;
        rt_exit_critical();
        return;
    }
    head = audio_mic_queue_head;
    next = (uint8_t)((head + 1U) % DUALSENSE_MIC_QUEUE_STORAGE);
    if (next == audio_mic_queue_tail) {
        audio_mic_queue_tail =
            (uint8_t)((audio_mic_queue_tail + 1U) %
                      DUALSENSE_MIC_QUEUE_STORAGE);
        usb.status.audio_mic_bt_dropped++;
    }
    memcpy(audio_mic_opus_queue[head], opus_data,
           DUALSENSE_MIC_OPUS_BYTES);
    usb_memory_barrier();
    audio_mic_queue_head = next;
    rt_exit_critical();
    (void)rt_sem_release(&audio_work_sem);
#else
    (void)opus_data;
    (void)len;
#endif
}

bool sf32lb52_usb_hid_send(uint8_t report_id, const uint8_t *data, size_t len)
{
#if defined(SF32LB52_USB_SMOKE_MOUSE)
#if defined(SF32LB52_USB_SMOKE_64) || defined(SF32LB52_USB_SMOKE_NS2)
    uint8_t smoke_report[SF32LB52_USB_HID_REPORT_SIZE] = {0};
#else
    uint8_t mouse_report[4] = {0, 0, 0, 0};
#endif

    (void)report_id;
    (void)data;
    (void)len;
#if defined(SF32LB52_USB_USE_CHERRYUSB)
    if (!usb.status.mounted || usb.status.suspended || gamepad_in_busy) {
        usb.status.in_reports_failed++;
        return false;
    }
#if defined(SF32LB52_USB_SMOKE_NS2)
    smoke_report[0] = SF32LB52_USB_NINTENDO_INPUT_REPORT_ID;
    memcpy(gamepad_in_buffer, smoke_report, sizeof(smoke_report));
#elif defined(SF32LB52_USB_SMOKE_64)
    memcpy(gamepad_in_buffer, smoke_report, sizeof(smoke_report));
#else
    memcpy(gamepad_in_buffer, mouse_report, sizeof(mouse_report));
#endif
    gamepad_in_busy = true;
    if (usbd_ep_start_write(0, NINTENDO_HID_EP_IN,
                            gamepad_in_buffer,
#if defined(SF32LB52_USB_SMOKE_64) || defined(SF32LB52_USB_SMOKE_NS2)
                            sizeof(smoke_report)
#else
                            sizeof(mouse_report)
#endif
                            ) < 0) {
        gamepad_in_busy = false;
        usb.status.in_reports_failed++;
        return false;
    }
    usb.status.in_reports_sent++;
    return true;
#else
    usb.status.in_reports_failed++;
    return false;
#endif
#else
    const Sf32lb52UsbRoleCapabilities *caps = active_capabilities();
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];

    if (usb.role == Sf32lb52UsbRoleXbox360) {
        if (report_id != 0 || len != caps->input_report_size) {
            usb.status.in_reports_failed++;
            return false;
        }
        return sf32lb52_usb_send_input_report(data, len);
    }

    if (report_id != caps->input_report_id || !data ||
        len > (size_t)(caps->input_report_size - 1)) {
        usb.status.in_reports_failed++;
        return false;
    }
    memset(report, 0, caps->input_report_size);
    report[0] = report_id;
    memcpy(report + 1, data, len);
    return sf32lb52_usb_send_input_report(report, caps->input_report_size);
#endif
}

#if defined(SF32LB52_USB_USE_CHERRYUSB)
void usbd_hid_get_report(uint8_t busid,
                         uint8_t intf,
                         uint8_t report_id,
                         uint8_t report_type,
                         uint8_t **data,
                         uint32_t *len)
{
    uint16_t requested_len;
    uint16_t actual_len;

    (void)busid;
    (void)intf;
    if (!data || !len) {
        return;
    }
    requested_len = *len > sizeof(hid_feature_buffer) ?
        sizeof(hid_feature_buffer) : (uint16_t)*len;
    memset(hid_feature_buffer, 0, sizeof(hid_feature_buffer));
    actual_len = sf32lb52_usb_hid_get_report_cb(report_id, report_type,
                                                 hid_feature_buffer,
                                                 requested_len);
    *data = hid_feature_buffer;
    *len = actual_len;
}

void usbd_hid_set_report(uint8_t busid,
                         uint8_t intf,
                         uint8_t report_id,
                         uint8_t report_type,
                         uint8_t *report,
                         uint32_t report_len)
{
    (void)busid;
    (void)intf;
    sf32lb52_usb_hid_set_report_cb(report_id, report_type, report,
                                   report_len > UINT16_MAX ? UINT16_MAX :
                                   (uint16_t)report_len);
}
#endif

/* Legacy adapter expected by src/usb_hid_cherryusb.c. */
int usb_hid_send(uint8_t report_id, const uint8_t *data, size_t len)
{
    return sf32lb52_usb_hid_send(report_id, data, len) ? 0 : -1;
}

void usb_hid_mount_cb(void)
{
    sf32lb52_usb_mount_cb();
}

void usb_hid_unmount_cb(void)
{
    sf32lb52_usb_unmount_cb();
}

void usb_hid_suspend_cb(bool remote_wakeup_enabled)
{
    sf32lb52_usb_suspend_cb(remote_wakeup_enabled);
}

void usb_hid_resume_cb(void)
{
    sf32lb52_usb_resume_cb();
}

uint16_t usb_hid_get_report_cb(uint8_t report_id,
                               uint8_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    return sf32lb52_usb_hid_get_report_cb(report_id, report_type, buffer, reqlen);
}

void usb_hid_set_report_cb(uint8_t report_id,
                           uint8_t report_type,
                           const uint8_t *buffer,
                           uint16_t len)
{
    sf32lb52_usb_hid_set_report_cb(report_id, report_type, buffer, len);
}
