#ifndef SF32LB52_BRIDGE_PROTOCOL_H
#define SF32LB52_BRIDGE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "sf32lb52_ns2_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_BRIDGE_XINPUT_REPORT_SIZE 20U
#define SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE 64U
#define SF32LB52_BRIDGE_DS5_OUTPUT_REPORT_SIZE 48U
#define SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE 64U
#define SF32LB52_BRIDGE_BATTERY_UNKNOWN 0xffU

/*
 * USB personalities exposed by the bridge.  These values are protocol-layer
 * identifiers only; they are intentionally independent of USB implementation
 * and persistent-storage layout.
 */
typedef enum {
    SF32LB52_BRIDGE_ROLE_UNKNOWN = 0,
    SF32LB52_BRIDGE_ROLE_XBOX_360,
    SF32LB52_BRIDGE_ROLE_DUALSENSE,
    SF32LB52_BRIDGE_ROLE_NS2PRO,
    SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE
} sf32lb52_bridge_role_t;

typedef enum {
    SF32LB52_BRIDGE_INPUT_SOURCE_NONE = 0,
    SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
    SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE
} sf32lb52_bridge_input_source_t;

/*
 * Face buttons use physical positions, not printed labels:
 *
 *   position  Xbox  DualSense  Nintendo
 *   south     A     Cross      B
 *   east      B     Circle     A
 *   west      X     Square     Y
 *   north     Y     Triangle   X
 *
 * This keeps conversion stable regardless of the selected USB personality.
 * Back maps to Xbox Back / DS5 Create / Nintendo Minus; Start maps to Xbox
 * Start / DS5 Options / Nintendo Plus; Guide maps to Xbox Guide / PS / Home.
 * Capture falls back to Back/Create where a role has no dedicated capture
 * button.  DS5 touchpad falls back to Nintendo Capture.  Edge paddles map to
 * NS2Pro GL/GR and are omitted by XInput.
 */
typedef enum {
    SF32LB52_BRIDGE_BUTTON_SOUTH = 0,
    SF32LB52_BRIDGE_BUTTON_EAST,
    SF32LB52_BRIDGE_BUTTON_WEST,
    SF32LB52_BRIDGE_BUTTON_NORTH,
    SF32LB52_BRIDGE_BUTTON_DPAD_UP,
    SF32LB52_BRIDGE_BUTTON_DPAD_DOWN,
    SF32LB52_BRIDGE_BUTTON_DPAD_LEFT,
    SF32LB52_BRIDGE_BUTTON_DPAD_RIGHT,
    SF32LB52_BRIDGE_BUTTON_LEFT_SHOULDER,
    SF32LB52_BRIDGE_BUTTON_RIGHT_SHOULDER,
    SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER,
    SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER,
    SF32LB52_BRIDGE_BUTTON_BACK,
    SF32LB52_BRIDGE_BUTTON_START,
    SF32LB52_BRIDGE_BUTTON_LEFT_STICK,
    SF32LB52_BRIDGE_BUTTON_RIGHT_STICK,
    SF32LB52_BRIDGE_BUTTON_GUIDE,
    SF32LB52_BRIDGE_BUTTON_TOUCHPAD,
    SF32LB52_BRIDGE_BUTTON_MUTE,
    SF32LB52_BRIDGE_BUTTON_CAPTURE,
    SF32LB52_BRIDGE_BUTTON_LEFT_PADDLE,
    SF32LB52_BRIDGE_BUTTON_RIGHT_PADDLE,
    SF32LB52_BRIDGE_BUTTON_LEFT_FUNCTION,
    SF32LB52_BRIDGE_BUTTON_RIGHT_FUNCTION,
    SF32LB52_BRIDGE_BUTTON_C,
    SF32LB52_BRIDGE_BUTTON_COUNT
} sf32lb52_bridge_button_t;

#define SF32LB52_BRIDGE_BUTTON_MASK(button) \
    (UINT32_C(1) << (uint8_t)(button))

/*
 * Stick coordinates cover the full int16_t range. X grows to the right and Y
 * grows up. Trigger values cover 0..65535. Motion samples retain the raw
 * signed 16-bit sensor units while normalising wire ordering to XYZ. NS2Pro
 * and XInput use the canonical direction directly; DS5's unsigned
 * positive-down Y axis is inverted at its input and output boundaries. The
 * 8-bit and 12-bit codecs saturate both endpoints.
 */
typedef struct {
    uint8_t valid;
    sf32lb52_bridge_input_source_t source;
    uint32_t buttons;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t accel[3];
    int16_t gyro[3];
    uint32_t sensor_timestamp;
    uint8_t battery_percent;
    uint8_t motion_valid;
    uint8_t timestamp_valid;
    uint8_t battery_valid;
} sf32lb52_bridge_input_state_t;

typedef enum {
    SF32LB52_BRIDGE_FEEDBACK_NONE = 0,
    SF32LB52_BRIDGE_FEEDBACK_DUAL_MOTOR,
    SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_RUMBLE,
    SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_HAPTICS,
    SF32LB52_BRIDGE_FEEDBACK_NINTENDO_HD
} sf32lb52_bridge_feedback_type_t;

typedef enum {
    SF32LB52_BRIDGE_OUTPUT_ROUTE_DROP = 0,
    SF32LB52_BRIDGE_OUTPUT_ROUTE_MANAGER,
    SF32LB52_BRIDGE_OUTPUT_ROUTE_DS5_NATIVE,
    SF32LB52_BRIDGE_OUTPUT_ROUTE_NS2_NATIVE,
    SF32LB52_BRIDGE_OUTPUT_ROUTE_TRANSLATED
} sf32lb52_bridge_output_route_t;

/*
 * left_motor/right_motor are normalised 0..65535 amplitudes and are always
 * populated for a valid feedback report.  Nintendo HD reports additionally
 * expose their low/high components so a transport can retain frequency detail.
 */
typedef struct {
    uint8_t valid;
    sf32lb52_bridge_feedback_type_t type;
    uint16_t left_motor;
    uint16_t right_motor;
    uint16_t left_low_amplitude;
    uint16_t left_high_amplitude;
    uint16_t right_low_amplitude;
    uint16_t right_high_amplitude;
    uint16_t left_low_frequency;
    uint16_t left_high_frequency;
    uint16_t right_low_frequency;
    uint16_t right_high_frequency;
} sf32lb52_bridge_feedback_t;

void sf32lb52_bridge_input_state_reset(sf32lb52_bridge_input_state_t *state);
void sf32lb52_bridge_feedback_reset(sf32lb52_bridge_feedback_t *feedback);

sf32lb52_bridge_output_route_t sf32lb52_bridge_select_output_route(
    sf32lb52_bridge_role_t role,
    sf32lb52_bridge_input_source_t source,
    const uint8_t *report,
    size_t report_len);

int sf32lb52_bridge_state_from_ns2_snapshot(
    const sf32lb52_ns2_ble_snapshot_t *snapshot,
    uint32_t sensor_timestamp,
    sf32lb52_bridge_input_state_t *out);

/* Accepts either a 63-byte USB report body or a 64-byte report starting 0x01. */
int sf32lb52_bridge_parse_ds5_usb_input(const uint8_t *report,
                                        size_t report_len,
                                        sf32lb52_bridge_input_state_t *out);

int sf32lb52_bridge_encode_xinput(
    const sf32lb52_bridge_input_state_t *state,
    uint8_t report[SF32LB52_BRIDGE_XINPUT_REPORT_SIZE]);
int sf32lb52_bridge_encode_ds5_input(
    const sf32lb52_bridge_input_state_t *state,
    uint8_t sequence,
    uint8_t report[SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE]);
int sf32lb52_bridge_encode_ns2pro_input(
    const sf32lb52_bridge_input_state_t *state,
    uint8_t sequence,
    uint8_t report[SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE]);

size_t sf32lb52_bridge_input_report_size(sf32lb52_bridge_role_t role);
int sf32lb52_bridge_encode_input(sf32lb52_bridge_role_t role,
                                 const sf32lb52_bridge_input_state_t *state,
                                 uint8_t sequence,
                                 uint8_t *report,
                                 size_t report_capacity);

int sf32lb52_bridge_decode_xbox_output(const uint8_t *report,
                                       size_t report_len,
                                       sf32lb52_bridge_feedback_t *out);
int sf32lb52_bridge_decode_ds5_output(const uint8_t *report,
                                      size_t report_len,
                                      sf32lb52_bridge_feedback_t *out);
int sf32lb52_bridge_decode_ns2pro_output(const uint8_t *report,
                                         size_t report_len,
                                         sf32lb52_bridge_feedback_t *out);

/*
 * Motor wire order is normalised here: Xbox full reports use [3]=left/[4]=right,
 * DS5 bodies use [3]=left-heavy/[2]=right-light, and Nintendo report 0x02 uses
 * five-byte HD frames at full-report offsets 0x02 (left) and 0x12 (right).
 */

/* Build a USB report 0x02 suitable for the DualSense compatibility path. */
int sf32lb52_bridge_encode_ds5_output(
    const sf32lb52_bridge_feedback_t *feedback,
    uint8_t report[SF32LB52_BRIDGE_DS5_OUTPUT_REPORT_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_BRIDGE_PROTOCOL_H */
