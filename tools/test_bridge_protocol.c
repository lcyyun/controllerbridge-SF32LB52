#include "sf32lb52_bridge_protocol.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define BUTTON(button) SF32LB52_BRIDGE_BUTTON_MASK(button)
#define NS2_BUTTON(button) (UINT32_C(1) << (uint8_t)(button))

static void put_s16(uint8_t *data, int16_t value)
{
    uint16_t wire = (uint16_t)value;
    data[0] = (uint8_t)(wire & 0xffU);
    data[1] = (uint8_t)((wire >> 8U) & 0xffU);
}

static int16_t get_s16(const uint8_t *data)
{
    uint16_t wire = (uint16_t)((uint16_t)data[0] |
                               ((uint16_t)data[1] << 8U));
    return wire <= 0x7fffU ? (int16_t)wire :
           (int16_t)((int32_t)wire - INT32_C(65536));
}

static uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static uint16_t unpack12_x(const uint8_t *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] |
                      (((uint16_t)data[offset + 1U] & 0x0fU) << 8U));
}

static uint16_t unpack12_y(const uint8_t *data, size_t offset)
{
    return (uint16_t)((((uint16_t)data[offset + 1U] >> 4U) & 0x0fU) |
                      ((uint16_t)data[offset + 2U] << 4U));
}

static void assert_button(const sf32lb52_bridge_input_state_t *state,
                          sf32lb52_bridge_button_t button)
{
    if ((state->buttons & BUTTON(button)) == 0U) {
        (void)fprintf(stderr, "missing button %d in mask 0x%08lx\n",
                      (int)button, (unsigned long)state->buttons);
    }
    assert((state->buttons & BUTTON(button)) != 0U);
}

static void test_reset_and_invalid_inputs(void)
{
    sf32lb52_bridge_input_state_t state;
    sf32lb52_bridge_feedback_t feedback;
    uint8_t report[64];

    memset(&state, 0xff, sizeof(state));
    sf32lb52_bridge_input_state_reset(&state);
    assert(state.valid == 0U);
    assert(state.source == SF32LB52_BRIDGE_INPUT_SOURCE_NONE);
    assert(state.left_x == 0);
    assert(state.battery_percent == SF32LB52_BRIDGE_BATTERY_UNKNOWN);

    memset(&feedback, 0xff, sizeof(feedback));
    sf32lb52_bridge_feedback_reset(&feedback);
    assert(feedback.valid == 0U);
    assert(feedback.type == SF32LB52_BRIDGE_FEEDBACK_NONE);

    memset(report, 0, sizeof(report));
    assert(sf32lb52_bridge_parse_ds5_usb_input(report, 62U, &state) == -1);
    assert(state.valid == 0U);
    assert(state.source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT);
    assert(sf32lb52_bridge_parse_ds5_usb_input(report, 64U, &state) == -1);
    assert(sf32lb52_bridge_state_from_ns2_snapshot(0, 1U, &state) == -1);
    assert(state.source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_UNKNOWN,
                                        &state, 0U, report, sizeof(report)) == -1);
}

static void test_ns2_snapshot_conversion(void)
{
    sf32lb52_ns2_ble_snapshot_t snapshot;
    sf32lb52_bridge_input_state_t state;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1U;
    snapshot.buttons =
        NS2_BUTTON(SF32LB52_NS2_BUTTON_B) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_A) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_Y) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_X) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_D_UP) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_D_RIGHT) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_L) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_R) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_ZL) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_ZR) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_MINUS) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_PLUS) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_L_STICK) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_R_STICK) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_HOME) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_CAPTURE) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_GL) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_GR) |
        NS2_BUTTON(SF32LB52_NS2_BUTTON_C);
    snapshot.lx = 0U;
    snapshot.ly = 2048U;
    snapshot.rx = 4095U;
    snapshot.ry = 5000U; /* The converter must saturate corrupt input. */
    snapshot.motion_valid = 1U;
    put_s16(snapshot.motion + 0U, -1);
    put_s16(snapshot.motion + 2U, 2);
    put_s16(snapshot.motion + 4U, INT16_MIN);
    put_s16(snapshot.motion + 6U, INT16_MAX);
    put_s16(snapshot.motion + 8U, -1234);
    put_s16(snapshot.motion + 10U, 42);

    assert(sf32lb52_bridge_state_from_ns2_snapshot(
               &snapshot, UINT32_C(0x12345678), &state) == 0);
    assert(state.valid == 1U);
    assert(state.source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE);
    assert(state.left_x == INT16_MIN);
    assert(state.left_y == 0);
    assert(state.right_x == INT16_MAX);
    assert(state.right_y == INT16_MAX);
    assert(state.left_trigger == UINT16_MAX);
    assert(state.right_trigger == UINT16_MAX);
    assert(state.motion_valid == 1U);
    assert(state.accel[0] == -1 && state.accel[1] == 2 &&
           state.accel[2] == INT16_MIN);
    assert(state.gyro[0] == INT16_MAX && state.gyro[1] == -1234 &&
           state.gyro[2] == 42);
    assert(state.timestamp_valid == 1U);
    assert(state.sensor_timestamp == UINT32_C(0x12345678));
    assert(state.battery_valid == 0U);
    assert(state.battery_percent == SF32LB52_BRIDGE_BATTERY_UNKNOWN);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_SOUTH);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_EAST);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_WEST);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_NORTH);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_DPAD_UP);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_DPAD_RIGHT);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_LEFT_PADDLE);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_RIGHT_PADDLE);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_C);
}

static void make_ds5_body(uint8_t body[63])
{
    memset(body, 0, 63U);
    body[0] = 0U;
    body[1] = 128U;
    body[2] = 255U;
    body[3] = 128U;
    body[4] = 0U;
    body[5] = 255U;
    body[7] = 0x91U; /* NE + Square(west) + Triangle(north). */
    body[8] = 0xffU;
    body[9] = 0xf7U;
    put_s16(body + 15U, 101);
    put_s16(body + 17U, 303); /* DS5 wire order is gyro X,Z,Y. */
    put_s16(body + 19U, 202);
    put_s16(body + 21U, -11);
    put_s16(body + 23U, -22);
    put_s16(body + 25U, -33);
    body[27] = 0x44U;
    body[28] = 0x33U;
    body[29] = 0x22U;
    body[30] = 0x11U;
    body[52] = 0x07U;
}

static void test_ds5_input_parsing(void)
{
    uint8_t body[63];
    uint8_t full[64];
    sf32lb52_bridge_input_state_t state;
    sf32lb52_bridge_input_state_t full_state;

    make_ds5_body(body);
    assert(sf32lb52_bridge_parse_ds5_usb_input(body, sizeof(body), &state) == 0);
    assert(state.valid == 1U);
    assert(state.source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT);
    assert(state.left_x == INT16_MIN);
    assert(state.left_y == 0);
    assert(state.right_x == INT16_MAX);
    assert(state.right_y == 0);
    assert(state.left_trigger == 0U);
    assert(state.right_trigger == UINT16_MAX);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_DPAD_UP);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_DPAD_RIGHT);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_WEST);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_NORTH);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_LEFT_SHOULDER);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_BACK);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_GUIDE);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_TOUCHPAD);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_MUTE);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_LEFT_FUNCTION);
    assert_button(&state, SF32LB52_BRIDGE_BUTTON_RIGHT_PADDLE);
    assert(state.gyro[0] == 101 && state.gyro[1] == 202 &&
           state.gyro[2] == 303);
    assert(state.accel[0] == -11 && state.accel[1] == -22 &&
           state.accel[2] == -33);
    assert(state.sensor_timestamp == UINT32_C(0x11223344));
    assert(state.battery_valid == 1U && state.battery_percent == 70U);

    full[0] = 0x01U;
    memcpy(full + 1U, body, sizeof(body));
    assert(sf32lb52_bridge_parse_ds5_usb_input(full, sizeof(full),
                                               &full_state) == 0);
    assert(full_state.buttons == state.buttons);
    assert(full_state.left_x == state.left_x);
    assert(full_state.sensor_timestamp == state.sensor_timestamp);
}

static sf32lb52_bridge_input_state_t make_output_state(void)
{
    sf32lb52_bridge_input_state_t state;

    sf32lb52_bridge_input_state_reset(&state);
    state.valid = 1U;
    state.buttons =
        BUTTON(SF32LB52_BRIDGE_BUTTON_SOUTH) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_EAST) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_WEST) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_NORTH) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_DPAD_UP) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_DPAD_RIGHT) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_SHOULDER) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_SHOULDER) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_BACK) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_START) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_STICK) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_STICK) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_GUIDE) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_TOUCHPAD) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_MUTE) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_CAPTURE) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_PADDLE) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_PADDLE) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_FUNCTION) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_FUNCTION) |
        BUTTON(SF32LB52_BRIDGE_BUTTON_C);
    state.left_x = INT16_MIN;
    state.left_y = INT16_MIN;
    state.right_x = INT16_MAX;
    state.right_y = INT16_MAX;
    state.left_trigger = UINT16_MAX;
    state.right_trigger = UINT16_C(0x8080);
    state.accel[0] = -1;
    state.accel[1] = -2;
    state.accel[2] = -3;
    state.gyro[0] = 11;
    state.gyro[1] = 22;
    state.gyro[2] = 33;
    state.motion_valid = 1U;
    state.sensor_timestamp = UINT32_C(0xa1b2c3d4);
    state.timestamp_valid = 1U;
    state.battery_percent = 94U;
    state.battery_valid = 1U;
    return state;
}

static void test_xinput_encoding(void)
{
    sf32lb52_bridge_input_state_t state = make_output_state();
    uint8_t report[SF32LB52_BRIDGE_XINPUT_REPORT_SIZE];

    assert(sf32lb52_bridge_encode_xinput(&state, report) == 0);
    assert(report[0] == 0x00U && report[1] == 0x14U);
    assert(report[2] == 0xf9U);
    assert(report[3] == 0xf7U);
    assert(report[4] == 0xffU && report[5] == 0x80U);
    assert(get_s16(report + 6U) == INT16_MIN);
    assert(get_s16(report + 8U) == INT16_MIN);
    assert(get_s16(report + 10U) == INT16_MAX);
    assert(get_s16(report + 12U) == INT16_MAX);
    assert(report[14] == 0U && report[19] == 0U);
}

static void test_ds5_encoding_and_round_trip(void)
{
    sf32lb52_bridge_input_state_t state = make_output_state();
    sf32lb52_bridge_input_state_t parsed;
    uint8_t report[SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE];

    assert(sf32lb52_bridge_encode_ds5_input(&state, 0x5aU, report) == 0);
    assert(report[0] == 0x01U);
    assert(report[1] == 0U && report[2] == 255U);
    assert(report[3] == 255U && report[4] == 0U);
    assert(report[5] == 255U && report[6] == 128U);
    assert(report[7] == 0x5aU);
    assert(report[8] == 0xf1U);
    assert(report[9] == 0xffU);
    assert(report[10] == 0xf7U);
    assert(get_s16(report + 16U) == 11);
    assert(get_s16(report + 18U) == 33);
    assert(get_s16(report + 20U) == 22);
    assert(get_s16(report + 22U) == -1);
    assert(get_u32(report + 28U) == UINT32_C(0xa1b2c3d4));
    assert(report[33] == 0x80U && report[37] == 0x80U);
    assert(report[53] == 9U);

    assert(sf32lb52_bridge_parse_ds5_usb_input(report, sizeof(report),
                                               &parsed) == 0);
    assert(parsed.left_x == INT16_MIN && parsed.right_x == INT16_MAX);
    assert(parsed.left_y == INT16_MIN && parsed.right_y == INT16_MAX);
    assert_button(&parsed, SF32LB52_BRIDGE_BUTTON_SOUTH);
    assert_button(&parsed, SF32LB52_BRIDGE_BUTTON_DPAD_UP);
    assert_button(&parsed, SF32LB52_BRIDGE_BUTTON_DPAD_RIGHT);
    assert(parsed.gyro[1] == 22 && parsed.gyro[2] == 33);
    assert(parsed.battery_percent == 90U);
}

static void test_cross_role_y_axis_polarity(void)
{
    sf32lb52_ns2_ble_snapshot_t snapshot;
    sf32lb52_bridge_input_state_t state;
    uint8_t xinput[SF32LB52_BRIDGE_XINPUT_REPORT_SIZE];
    uint8_t ds5[SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE];
    uint8_t ns2[SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE];

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.valid = 1U;
    snapshot.lx = 2048U;
    snapshot.ly = 4095U;
    snapshot.rx = 2048U;
    snapshot.ry = 0U;

    assert(sf32lb52_bridge_state_from_ns2_snapshot(&snapshot, 1U, &state) == 0);
    assert(state.left_y == INT16_MAX);
    assert(state.right_y == INT16_MIN);

    assert(sf32lb52_bridge_encode_xinput(&state, xinput) == 0);
    assert(get_s16(xinput + 8U) == INT16_MAX);
    assert(get_s16(xinput + 12U) == INT16_MIN);

    assert(sf32lb52_bridge_encode_ds5_input(&state, 0U, ds5) == 0);
    assert(ds5[2] == 0U);
    assert(ds5[4] == 255U);

    assert(sf32lb52_bridge_encode_ns2pro_input(&state, 0U, ns2) == 0);
    assert(unpack12_y(ns2, 11U) == 4095U);
    assert(unpack12_y(ns2, 14U) == 0U);
}

static void test_ns2pro_encoding(void)
{
    sf32lb52_bridge_input_state_t state = make_output_state();
    uint8_t report[SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE];

    assert(sf32lb52_bridge_encode_ns2pro_input(&state, 7U, report) == 0);
    assert(report[0] == 0x05U && report[1] == 7U && report[2] == 0x20U);
    assert(report[5] == 0xcfU);
    assert(report[6] == 0x7fU);
    assert(report[7] == 0xc6U);
    assert(report[8] == 0x03U);
    assert(unpack12_x(report, 11U) == 0U);
    assert(unpack12_y(report, 11U) == 0U);
    assert(unpack12_x(report, 14U) == 4095U);
    assert(unpack12_y(report, 14U) == 4095U);
    assert(get_u32(report + 0x2bU) == UINT32_C(0xa1b2c3d4));
    assert(get_s16(report + 0x31U) == -1);
    assert(get_s16(report + 0x33U) == -2);
    assert(get_s16(report + 0x37U) == 11);
    assert(get_s16(report + 0x3bU) == 33);
}

static void test_generic_encoder(void)
{
    sf32lb52_bridge_input_state_t state = make_output_state();
    uint8_t report[64];

    assert(sf32lb52_bridge_input_report_size(SF32LB52_BRIDGE_ROLE_XBOX_360) == 20U);
    assert(sf32lb52_bridge_input_report_size(SF32LB52_BRIDGE_ROLE_DUALSENSE) == 64U);
    assert(sf32lb52_bridge_input_report_size(SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE) == 64U);
    assert(sf32lb52_bridge_input_report_size(SF32LB52_BRIDGE_ROLE_NS2PRO) == 64U);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_XBOX_360,
                                        &state, 0U, report, 19U) == -1);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_XBOX_360,
                                        &state, 0U, report, sizeof(report)) == 0);
    assert(report[1] == 20U);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_DUALSENSE,
                                        &state, 9U, report, sizeof(report)) == 0);
    assert(report[0] == 1U && report[7] == 9U);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE,
                                        &state, 10U, report, sizeof(report)) == 0);
    assert(report[0] == 1U && report[7] == 10U);
    assert(sf32lb52_bridge_encode_input(SF32LB52_BRIDGE_ROLE_NS2PRO,
                                        &state, 10U, report, sizeof(report)) == 0);
    assert(report[0] == 5U && report[1] == 10U);
}

static void test_feedback(void)
{
    sf32lb52_bridge_feedback_t feedback;
    uint8_t xbox[8] = {0x00U, 0x08U, 0x00U, 0x20U, 0x80U, 0U, 0U, 0U};
    uint8_t xbox_body[7] = {0x08U, 0x00U, 0x10U, 0x40U, 0U, 0U, 0U};
    uint8_t ds5[48];
    uint8_t ds5_encoded[SF32LB52_BRIDGE_DS5_OUTPUT_REPORT_SIZE];
    uint8_t ns2[64];

    assert(sf32lb52_bridge_decode_xbox_output(xbox, sizeof(xbox), &feedback) == 0);
    assert(feedback.valid == 1U);
    assert(feedback.type == SF32LB52_BRIDGE_FEEDBACK_DUAL_MOTOR);
    assert(feedback.left_motor == UINT16_C(0x2020));
    assert(feedback.right_motor == UINT16_C(0x8080));
    assert(sf32lb52_bridge_decode_xbox_output(xbox_body, sizeof(xbox_body),
                                              &feedback) == 0);
    assert(feedback.left_motor == UINT16_C(0x1010));
    assert(feedback.right_motor == UINT16_C(0x4040));

    memset(ds5, 0, sizeof(ds5));
    ds5[0] = 0x02U;
    ds5[1] = 0x03U;
    ds5[3] = 0x22U; /* body[2] right */
    ds5[4] = 0x44U; /* body[3] left */
    assert(sf32lb52_bridge_decode_ds5_output(ds5, sizeof(ds5), &feedback) == 0);
    assert(feedback.type == SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_RUMBLE);
    assert(feedback.left_motor == UINT16_C(0x4444));
    assert(feedback.right_motor == UINT16_C(0x2222));
    assert(sf32lb52_bridge_encode_ds5_output(&feedback, ds5_encoded) == 0);
    assert(ds5_encoded[0] == 0x02U && ds5_encoded[1] == 0x03U);
    assert(ds5_encoded[3] == 0x22U && ds5_encoded[4] == 0x44U);

    feedback.type = SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_HAPTICS;
    assert(sf32lb52_bridge_encode_ds5_output(&feedback, ds5_encoded) == 0);
    assert(ds5_encoded[1] == 0x01U);
    ds5[1] = 0x01U;
    assert(sf32lb52_bridge_decode_ds5_output(ds5, sizeof(ds5), &feedback) == 0);
    assert(feedback.type == SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_HAPTICS);

    memset(ns2, 0, sizeof(ns2));
    ns2[0] = 0x02U;
    ns2[1] = 0x5aU;
    ns2[2] = 0x34U;
    ns2[3] = 0xa2U;
    ns2[4] = 0x05U;
    ns2[5] = 0xc3U;
    ns2[6] = 0x12U;
    ns2[0x12U] = 0x78U;
    ns2[0x13U] = 0x44U;
    ns2[0x14U] = 0x09U;
    ns2[0x15U] = 0x82U;
    ns2[0x16U] = 0x56U;
    assert(sf32lb52_bridge_decode_ns2pro_output(ns2, sizeof(ns2),
                                                &feedback) == 0);
    assert(feedback.type == SF32LB52_BRIDGE_FEEDBACK_NINTENDO_HD);
    assert(feedback.left_high_frequency == 0x234U);
    assert(feedback.left_high_amplitude == 0x5a00U);
    assert(feedback.left_low_frequency == 0x30U);
    assert(feedback.left_low_amplitude == 0x12c0U);
    assert(feedback.left_motor == 0x5a00U);
    assert(feedback.right_high_frequency == 0x078U);
    assert(feedback.right_high_amplitude == 0x9440U);
    assert(feedback.right_low_frequency == 0x20U);
    assert(feedback.right_low_amplitude == 0x5680U);
    assert(feedback.right_motor == 0x9440U);

    ns2[0] = 0U;
    assert(sf32lb52_bridge_decode_ns2pro_output(ns2, sizeof(ns2),
                                                &feedback) == -1);
    assert(feedback.valid == 0U);
}

static void test_output_routing(void)
{
    static const uint8_t manager_magic[SF32LB52_NS2_FEATURE_MAGIC_SIZE] = {
        'Y', '7', 'H', 'I', 'D', '1'
    };
    uint8_t report[64] = {0x02U};

    memcpy(report + 1U, manager_magic,
           SF32LB52_NS2_FEATURE_MAGIC_SIZE);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_NS2PRO,
               SF32LB52_BRIDGE_INPUT_SOURCE_NONE,
               report, sizeof(report)) ==
           SF32LB52_BRIDGE_OUTPUT_ROUTE_MANAGER);

    memset(report + 1U, 0, sizeof(report) - 1U);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_DUALSENSE,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               report, 48U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_DS5_NATIVE);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               report, 64U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_DS5_NATIVE);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_NS2PRO,
               SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
               report, 64U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_NS2_NATIVE);

    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_XBOX_360,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               report, 8U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_TRANSLATED);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_DUALSENSE,
               SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
               report, 48U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_TRANSLATED);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_NS2PRO,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               report, 64U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_TRANSLATED);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_DUALSENSE,
               SF32LB52_BRIDGE_INPUT_SOURCE_NONE,
               report, 48U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_DROP);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_UNKNOWN,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               report, 48U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_DROP);
    assert(sf32lb52_bridge_select_output_route(
               SF32LB52_BRIDGE_ROLE_DUALSENSE,
               SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
               0, 0U) == SF32LB52_BRIDGE_OUTPUT_ROUTE_DROP);
}

int main(void)
{
    test_reset_and_invalid_inputs();
    test_ns2_snapshot_conversion();
    test_ds5_input_parsing();
    test_xinput_encoding();
    test_ds5_encoding_and_round_trip();
    test_cross_role_y_axis_polarity();
    test_ns2pro_encoding();
    test_generic_encoder();
    test_feedback();
    test_output_routing();
    return 0;
}
