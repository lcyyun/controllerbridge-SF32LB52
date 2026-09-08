#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_USB_XBOX_VID 0x045eu
#define SF32LB52_USB_XBOX_PID 0x028eu
#define SF32LB52_USB_DS5_VID 0x054cu
#define SF32LB52_USB_DS5_PID 0x0ce6u
#define SF32LB52_USB_DSE_PID 0x0df2u
#define SF32LB52_USB_NINTENDO_VID 0x057eu
#define SF32LB52_USB_NINTENDO_PID 0x2069u

/* Wire report shapes for the three host-side personas. */
#define SF32LB52_USB_XBOX_INPUT_REPORT_ID 0x00u
#define SF32LB52_USB_XBOX_OUTPUT_REPORT_ID 0x00u
#define SF32LB52_USB_XBOX_INPUT_REPORT_SIZE 20u
#define SF32LB52_USB_XBOX_OUTPUT_REPORT_SIZE 8u

#define SF32LB52_USB_DS5_INPUT_REPORT_ID 0x01u
#define SF32LB52_USB_DS5_OUTPUT_REPORT_ID 0x02u
#define SF32LB52_USB_DS5_INPUT_REPORT_SIZE 64u
#define SF32LB52_USB_DS5_OUTPUT_REPORT_SIZE 48u
#define SF32LB52_USB_DSE_OUTPUT_REPORT_SIZE 64u

#define SF32LB52_USB_NINTENDO_INPUT_REPORT_ID 0x05u
#define SF32LB52_USB_NINTENDO_OUTPUT_REPORT_ID 0x02u
#define SF32LB52_USB_NINTENDO_INPUT_REPORT_SIZE 64u
#define SF32LB52_USB_NINTENDO_OUTPUT_REPORT_SIZE 64u

#define SF32LB52_USB_HID_FEATURE_REPORT_ID 0x7fu
#define SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID 0xf6u
#define SF32LB52_USB_HID_REPORT_SIZE 64u
#define SF32LB52_USB_HID_PAYLOAD_SIZE 63u
#define SF32LB52_USB_HID_POLL_INTERVAL_MS 1u

/* Backward-compatible Nintendo aliases used by the existing NS2 profile. */
#define SF32LB52_USB_HID_INPUT_REPORT_ID SF32LB52_USB_NINTENDO_INPUT_REPORT_ID
#define SF32LB52_USB_HID_OUTPUT_REPORT_ID SF32LB52_USB_NINTENDO_OUTPUT_REPORT_ID

typedef enum Sf32lb52UsbRole {
    Sf32lb52UsbRoleXbox360 = 0,
    Sf32lb52UsbRoleDualSense,
    Sf32lb52UsbRoleNintendo,
    Sf32lb52UsbRoleDualSenseEdge,
} Sf32lb52UsbRole;

/* Compatibility names retained while app/profile code migrates to UsbRole. */
typedef Sf32lb52UsbRole Sf32lb52UsbIdentity;
#define Sf32lb52UsbIdentityXbox360 Sf32lb52UsbRoleXbox360
#define Sf32lb52UsbIdentityDualSense Sf32lb52UsbRoleDualSense
#define Sf32lb52UsbIdentityNintendo Sf32lb52UsbRoleNintendo
#define Sf32lb52UsbIdentityDualSenseEdge Sf32lb52UsbRoleDualSenseEdge

typedef struct Sf32lb52UsbRoleCapabilities {
    uint16_t vid;
    uint16_t pid;
    uint16_t input_report_size;
    uint16_t output_report_size;
    uint8_t input_report_id;
    uint8_t output_report_id;
    bool gamepad_is_hid;
    bool audio_capable;
} Sf32lb52UsbRoleCapabilities;

typedef struct Sf32lb52UsbDeviceStatus {
    bool mounted;
    bool suspended;
    uint32_t in_reports_sent;
    uint32_t in_reports_failed;
    uint32_t in_report_callbacks;
    uint32_t out_reports_received;
    uint32_t feature_get_reports;
    uint32_t feature_set_reports;
    uint32_t feature_set_dropped;
    uint32_t last_in_busy_ms;
    uint32_t last_in_nbytes;
    uint8_t last_out_report_id;
    uint8_t last_feature_report_id;
    bool in_busy;
    bool host_ready;
    uint16_t last_out_len;
    uint16_t last_feature_len;
    Sf32lb52UsbRole active_role;
    Sf32lb52UsbRole pending_role;
    bool role_switch_pending;
    uint32_t role_switches;
    uint32_t role_switch_failures;
    bool audio_speaker_open;
    bool audio_mic_open;
    uint32_t audio_out_packets;
    uint32_t audio_out_bytes;
    uint32_t audio_out_errors;
    uint32_t audio_in_packets;
    uint32_t audio_in_bytes;
    uint32_t audio_in_errors;
    uint32_t audio_bt_reports;
    uint32_t audio_bt_dropped;
    uint32_t audio_opus_errors;
    uint32_t audio_haptic_blocks;
    uint32_t audio_mic_bt_packets;
    uint32_t audio_mic_bt_dropped;
    uint32_t audio_mic_decode_errors;
    uint32_t audio_mic_underflows;
    uint32_t vendor_out_packets;
    uint32_t vendor_out_bytes;
    uint32_t vendor_in_packets;
    uint32_t vendor_in_callbacks;
    uint32_t vendor_in_bytes;
    uint32_t vendor_errors;
    uint8_t vendor_last_command;
    uint8_t vendor_last_argument;
} Sf32lb52UsbDeviceStatus;

typedef struct Sf32lb52UsbPhyStatus {
    uint8_t power;
    uint8_t devctl;
    uint8_t intrusb;
    uint8_t intrusbe;
    uint8_t usbcfg;
    uint8_t dpbrxdisl;
    uint8_t dpbtxdisl;
    int8_t gamepad_out_arm_result;
    int8_t vendor_out_arm_result;
    uint16_t intrtx;
    uint16_t intrrx;
    uint16_t intrtxe;
    uint16_t intrrxe;
    uint16_t ep1_txmaxp;
    uint16_t ep1_txcsr;
    uint16_t ep1_rxmaxp;
    uint16_t ep1_rxcsr;
    uint16_t ep2_txmaxp;
    uint16_t ep2_txcsr;
    uint16_t ep2_rxmaxp;
    uint16_t ep2_rxcsr;
} Sf32lb52UsbPhyStatus;

/*
 * HID roles pass their report ID separately and payload without the ID.
 * Xbox is not HID: report_id is zero and payload is the complete 20/8-byte
 * XUSB packet, including its 0x00/length header bytes.
 */
typedef void (*Sf32lb52UsbOutputReportCallback)(uint8_t report_id,
                                                const uint8_t *payload,
                                                size_t payload_len);
typedef uint16_t (*Sf32lb52UsbInputGetCallback)(uint8_t *buffer, uint16_t reqlen);
typedef uint16_t (*Sf32lb52UsbFeatureGetCallback)(uint8_t *buffer, uint16_t reqlen);
typedef void (*Sf32lb52UsbFeatureSetCallback)(const uint8_t *payload, uint16_t len);
typedef uint16_t (*Sf32lb52UsbNativeFeatureGetCallback)(uint8_t report_id,
                                                        uint8_t *buffer,
                                                        uint16_t reqlen);
typedef void (*Sf32lb52UsbNativeFeatureSetCallback)(uint8_t report_id,
                                                    const uint8_t *payload,
                                                    uint16_t len);
typedef void (*Sf32lb52UsbAudioHapticsCallback)(
    const int16_t *interleaved_3khz,
    size_t frames);

void sf32lb52_usb_device_init(Sf32lb52UsbIdentity identity);
void sf32lb52_usb_device_task(void);

/* Runtime role changes are applied from device_task using a USB re-enumeration. */
bool sf32lb52_usb_request_role(Sf32lb52UsbRole role);
Sf32lb52UsbRole sf32lb52_usb_get_role(void);
bool sf32lb52_usb_role_switch_pending(void);
uint8_t sf32lb52_usb_manager_feature_report_id(void);
bool sf32lb52_usb_get_role_capabilities(Sf32lb52UsbRole role,
                                        Sf32lb52UsbRoleCapabilities *out);

/* Send a complete, role-native wire report of the exact advertised size. */
bool sf32lb52_usb_send_input_report(const uint8_t *report, size_t len);

/* Queues one controller microphone Opus frame for UAC microphone IN. */
void sf32lb52_usb_ds5_mic_input(const uint8_t *opus_data, size_t len);

/* Legacy HID payload helper; Xbox callers pass a complete 20-byte packet. */
bool sf32lb52_usb_hid_send(uint8_t report_id, const uint8_t *data, size_t len);
void sf32lb52_usb_get_status(Sf32lb52UsbDeviceStatus *out);
void sf32lb52_usb_set_report_callbacks(Sf32lb52UsbOutputReportCallback output_cb,
                                       Sf32lb52UsbInputGetCallback input_get_cb,
                                       Sf32lb52UsbFeatureGetCallback feature_get_cb,
                                       Sf32lb52UsbFeatureSetCallback feature_set_cb);
void sf32lb52_usb_set_native_feature_callbacks(
    Sf32lb52UsbNativeFeatureGetCallback feature_get_cb,
    Sf32lb52UsbNativeFeatureSetCallback feature_set_cb);
void sf32lb52_usb_set_audio_haptics_callback(
    Sf32lb52UsbAudioHapticsCallback callback);
bool sf32lb52_usb_get_phy_status(Sf32lb52UsbPhyStatus *out);
void sf32lb52_usb_force_reconnect(void);

const uint8_t *sf32lb52_usb_device_descriptor(size_t *len);
const uint8_t *sf32lb52_usb_configuration_descriptor(size_t *len);
const uint8_t *sf32lb52_usb_hid_report_descriptor(size_t *len);

void sf32lb52_usb_mount_cb(void);
void sf32lb52_usb_unmount_cb(void);
void sf32lb52_usb_suspend_cb(bool remote_wakeup_enabled);
void sf32lb52_usb_resume_cb(void);
uint16_t sf32lb52_usb_hid_get_report_cb(uint8_t report_id,
                                        uint8_t report_type,
                                        uint8_t *buffer,
                                        uint16_t reqlen);
void sf32lb52_usb_hid_set_report_cb(uint8_t report_id,
                                    uint8_t report_type,
                                    const uint8_t *buffer,
                                    uint16_t len);

/* Legacy adapter declarations kept here for app code that includes only this API. */
int usb_hid_init(void (*output_callback)(const uint8_t *data, size_t len));
int usb_hid_send(uint8_t report_id, const uint8_t *data, size_t len);
int usb_hid_send_input_report(const uint8_t *report, size_t len);
void usb_hid_poll(void);

#ifdef __cplusplus
}
#endif
