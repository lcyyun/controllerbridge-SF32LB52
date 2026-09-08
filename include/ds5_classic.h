#ifndef NS2PRO_BRIDGE_DS5_CLASSIC_H
#define NS2PRO_BRIDGE_DS5_CLASSIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS5_CLASSIC_HID_CONTROL_PSM 0x0011U
#define DS5_CLASSIC_HID_INTERRUPT_PSM 0x0013U
#define DS5_CLASSIC_USB_INPUT_BODY_SIZE 63U
#define DS5_CLASSIC_USB_OUTPUT_BODY_SIZE 47U
#define DS5_CLASSIC_USB_OUTPUT_REPORT_SIZE 48U
#define DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE 63U
#define DS5_CLASSIC_DSE_USB_OUTPUT_REPORT_SIZE 64U
#define DS5_CLASSIC_BT_INPUT_PACKET_SIZE 79U
#define DS5_CLASSIC_BT_OUTPUT_PACKET_SIZE 79U
#define DS5_CLASSIC_AUDIO_REPORT_SIZE 398U
#define DS5_CLASSIC_BT_MAX_OUTPUT_PACKET_SIZE (DS5_CLASSIC_AUDIO_REPORT_SIZE + 1U)

typedef enum ds5_classic_result {
    DS5_CLASSIC_OK = 0,
    DS5_CLASSIC_ERROR_INVALID_ARGUMENT = -1,
    DS5_CLASSIC_ERROR_UNSUPPORTED = -2,
    DS5_CLASSIC_ERROR_NOT_READY = -3,
    DS5_CLASSIC_ERROR_BUSY = -4,
    DS5_CLASSIC_ERROR_NO_SAVED_DEVICE = -5,
    DS5_CLASSIC_ERROR_NOT_CONNECTED = -6,
    DS5_CLASSIC_ERROR_SDK = -7,
} ds5_classic_result_t;

typedef enum ds5_classic_state {
    DS5_CLASSIC_STATE_UNAVAILABLE = 0,
    DS5_CLASSIC_STATE_IDLE,
    DS5_CLASSIC_STATE_STARTING,
    DS5_CLASSIC_STATE_PAIRING,
    DS5_CLASSIC_STATE_CONNECTING,
    DS5_CLASSIC_STATE_CONNECTED,
    DS5_CLASSIC_STATE_DISCONNECTING,
    DS5_CLASSIC_STATE_ERROR,
} ds5_classic_state_t;

/*
 * The callback receives the 63-byte body used by a USB DualSense input report
 * (USB report ID 0x01 is deliberately omitted). The buffer is only valid for
 * the duration of the callback.
 */
typedef void (*ds5_classic_input_callback_t)(const uint8_t *usb_body,
                                             size_t len,
                                             void *context);
typedef void (*ds5_classic_audio_input_callback_t)(const uint8_t *opus_data,
                                                   size_t len,
                                                   void *context);

typedef struct ds5_classic_status {
    uint8_t sdk_available;
    uint8_t event_bridge_available;
    uint8_t initialized;
    uint8_t stack_ready;
    uint8_t control_registered;
    uint8_t interrupt_registered;
    uint8_t discovering;
    uint8_t pairing;
    uint8_t connecting;
    uint8_t connected;
    uint8_t disconnecting;
    uint8_t auto_reconnect_enabled;
    uint8_t reconnect_pending;
    uint8_t output_pending;
    uint8_t feature_request_pending;
    uint8_t feature_response_pending;
    uint8_t edge_known;
    uint8_t is_edge;
    uint8_t dse_unlock_phase;
    uint8_t dse_profiles_ready;
    uint8_t saved_address_valid;
    uint8_t active_address_valid;
    /* SDK/general byte order: LAP little-endian, then UAP, then NAP little-endian. */
    uint8_t saved_address[6];
    uint8_t active_address[6];
    uint16_t control_cid;
    uint16_t interrupt_cid;
    uint16_t control_remote_mtu;
    uint16_t interrupt_remote_mtu;
    int16_t last_error;
    uint16_t last_sdk_result;
    ds5_classic_state_t state;
    uint32_t discovery_reports;
    uint32_t candidates_found;
    uint32_t input_reports;
    uint32_t input_crc_errors;
    uint32_t short_input_reports;
    uint32_t audio_input_reports;
    uint32_t audio_input_bytes;
    uint32_t output_reports;
    uint32_t primer_reports;
    uint32_t output_busy;
    uint32_t output_queued;
    uint32_t output_failures;
    uint32_t feature_requests;
    uint32_t feature_responses;
    uint32_t feature_cache_hits;
    uint32_t feature_cache_misses;
    uint32_t feature_set_reports;
    uint32_t feature_failures;
    uint32_t feature_timeouts;
    uint32_t feature_retries;
    uint32_t dse_unlocks;
    uint32_t dse_profile_reports;
    uint32_t audio_output_reports;
    uint32_t audio_output_dropped;
    uint32_t reconnect_attempts;
    uint32_t registration_failures;
    uint32_t registration_retries;
    uint32_t connects;
    uint32_t disconnects;
    uint32_t key_missing_events;
} ds5_classic_status_t;

int ds5_classic_init(ds5_classic_input_callback_t input_callback,
                     void *callback_context);
void ds5_classic_set_audio_input_callback(
    ds5_classic_audio_input_callback_t audio_callback,
    void *callback_context);

/*
 * Injects the persisted target address in the SDK/general byte order used by
 * ds5_classic_status_t. The address takes priority over bonded-device lookup.
 * Passing NULL clears the injected/saved target.
 */
int ds5_classic_set_saved_address(const uint8_t address[6]);

/*
 * Enables or disables adapter-initiated reconnect. It is disabled by default.
 * Enabling it while idle schedules an immediate attempt when a saved target is
 * available. Explicit disconnect/forget suppresses reconnect until the app
 * enables it again or calls ds5_classic_connect_saved().
 */
int ds5_classic_set_auto_reconnect(int enabled);

int ds5_classic_start_pairing(void);
int ds5_classic_connect_saved(void);
int ds5_classic_disconnect(void);
int ds5_classic_forget(void);
void ds5_classic_poll(void);
void ds5_classic_get_status(ds5_classic_status_t *status);

/*
 * Accepts either the 47-byte USB output body or a complete 48-byte USB report
 * beginning with report ID 0x02. It emits a Bluetooth HIDP 0xA2/0x31 packet,
 * adds the sequence/tag bytes, zero padding, and the Sony CRC-32. If another
 * packet is in flight, the newest body replaces the deferred packet and this
 * function still returns DS5_CLASSIC_OK because the update was accepted.
 */
int ds5_classic_send_output_report(const uint8_t *usb_report, size_t len);

/* Sends a complete DS5 Bluetooth report body such as report 0x36 audio. */
int ds5_classic_send_raw_output_report(const uint8_t *report, size_t len);

/* Returns a complete USB Feature report, including the report ID. */
size_t ds5_classic_get_feature_report(uint8_t report_id,
                                      uint8_t *report,
                                      size_t report_capacity);

/* data excludes the report ID. DSE profile writes are forwarded with CRC. */
int ds5_classic_set_feature_report(uint8_t report_id,
                                   const uint8_t *data,
                                   size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NS2PRO_BRIDGE_DS5_CLASSIC_H */
