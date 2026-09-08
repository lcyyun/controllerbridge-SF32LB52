#include "ble_gatt.h"
#include "ds5_classic.h"
#include "ns2_profile.h"
#include "platform.h"
#include "sf32lb52_app.h"
#include "sf32lb52_audio_haptics.h"
#include "sf32lb52_bridge_protocol.h"
#include "sf32lb52_bridge_runtime.h"
#include "sf32lb52_ns2_protocol.h"
#include "sf32lb52_usb_device.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("rtthread.h")
#include "rtthread.h"
#define SF32LB52_APP_HAS_RTTHREAD 1
#endif
#if __has_include("rtdevice.h")
#include "rtdevice.h"
#define SF32LB52_APP_HAS_RTDEVICE 1
#endif
#endif

static uint32_t heartbeat_count;
static uint8_t ble_started;
static uint8_t ds5_started;
static uint8_t ds5_auto_reconnect_applied;
static uint32_t boot_ms;
static uint32_t ble_start_retry_ms;
static uint32_t ds5_start_retry_ms;
static uint8_t ds5_was_connected;
static uint8_t ns2_was_connected;
static uint32_t observed_ds5_key_missing_events;
static uint8_t observed_ds5_address_valid;
static uint8_t observed_ds5_address[6];

typedef struct {
    sf32lb52_bridge_role_t role;
    size_t len;
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
} pending_usb_output_t;

typedef struct {
    uint8_t kind;
    uint8_t len;
    uint32_t timestamp_ms;
    uint32_t sensor_timestamp_us;
    uint8_t data[SF32LB52_USB_HID_REPORT_SIZE];
} pending_ns2_input_t;

typedef struct {
    uint8_t len;
    uint32_t timestamp_ms;
    uint8_t body[DS5_CLASSIC_USB_INPUT_BODY_SIZE];
} pending_ds5_input_t;

typedef struct {
    Sf32lb52UsbRole role;
    uint16_t len;
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
} cached_usb_input_t;

typedef struct {
    sf32lb52_audio_haptics_event_t event;
    sf32lb52_audio_haptics_output_t output;
} pending_audio_haptics_t;

#define BRIDGE_USB_OUTPUT_QUEUE_CAPACITY 8U
#define BRIDGE_USB_OUTPUT_QUEUE_STORAGE \
    (BRIDGE_USB_OUTPUT_QUEUE_CAPACITY + 1U)
static pending_usb_output_t
    pending_usb_outputs[BRIDGE_USB_OUTPUT_QUEUE_STORAGE];
static volatile uint8_t pending_usb_output_head;
static volatile uint8_t pending_usb_output_tail;
static volatile uint32_t pending_usb_output_drops;
static pending_ns2_input_t pending_ns2_input;
static volatile uint32_t pending_ns2_input_sequence;
static uint32_t processed_ns2_input_sequence;
static pending_ds5_input_t pending_ds5_input;
static volatile uint32_t pending_ds5_input_sequence;
static uint32_t processed_ds5_input_sequence;
static pending_ds5_input_t latest_ds5_passthrough;
static uint8_t latest_ds5_passthrough_valid;
static cached_usb_input_t cached_usb_inputs[2];
static volatile uint8_t cached_usb_input_index;
static sf32lb52_audio_haptics_processor_t audio_haptics_processor;
static pending_audio_haptics_t pending_audio_haptics;
static volatile uint32_t pending_audio_haptics_sequence;
static uint32_t processed_audio_haptics_sequence;

#if defined(SF32LB52_APP_HAS_RTTHREAD)
typedef enum {
    BRIDGE_CONTROL_USB_RECONNECT = 0,
    BRIDGE_CONTROL_ROLE_SET,
    BRIDGE_CONTROL_ROLE_NEXT,
    BRIDGE_CONTROL_INPUT_SET,
    BRIDGE_CONTROL_PAIR_DS5,
    BRIDGE_CONTROL_CONNECT_DS5,
    BRIDGE_CONTROL_DISCONNECT_DS5,
    BRIDGE_CONTROL_FORGET_DS5,
    BRIDGE_CONTROL_PAIR_NS2,
    BRIDGE_CONTROL_CONNECT_NS2,
    BRIDGE_CONTROL_DISCONNECT_NS2,
    BRIDGE_CONTROL_FORGET_NS2,
} bridge_control_command_kind_t;

typedef struct {
    bridge_control_command_kind_t kind;
    int32_t argument;
} bridge_control_command_t;

#define BRIDGE_CONTROL_QUEUE_CAPACITY 8U
#define BRIDGE_CONTROL_QUEUE_STORAGE (BRIDGE_CONTROL_QUEUE_CAPACITY + 1U)
static bridge_control_command_t
    bridge_control_queue[BRIDGE_CONTROL_QUEUE_STORAGE];
static volatile uint8_t bridge_control_head;
static volatile uint8_t bridge_control_tail;
static sf32lb52_app_input_action_t pending_input_action;
static uint32_t pending_input_retry_ms;
static uint32_t input_exclusion_retry_ms;
#endif

#ifndef SF32LB52_BLE_START_DELAY_MS
#define SF32LB52_BLE_START_DELAY_MS 5000U
#endif

static void bridge_memory_barrier(void)
{
#if defined(__GNUC__)
    __sync_synchronize();
#endif
}

static Sf32lb52UsbRole usb_role_from_bridge(sf32lb52_bridge_role_t role)
{
    switch (role) {
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
        return Sf32lb52UsbRoleDualSense;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        return Sf32lb52UsbRoleDualSenseEdge;
    case SF32LB52_BRIDGE_ROLE_NS2PRO:
        return Sf32lb52UsbRoleNintendo;
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
    default:
        return Sf32lb52UsbRoleXbox360;
    }
}

static sf32lb52_bridge_role_t bridge_role_from_usb(Sf32lb52UsbRole role)
{
    switch (role) {
    case Sf32lb52UsbRoleDualSense:
        return SF32LB52_BRIDGE_ROLE_DUALSENSE;
    case Sf32lb52UsbRoleDualSenseEdge:
        return SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
    case Sf32lb52UsbRoleNintendo:
        return SF32LB52_BRIDGE_ROLE_NS2PRO;
    case Sf32lb52UsbRoleXbox360:
    default:
        return SF32LB52_BRIDGE_ROLE_XBOX_360;
    }
}

static void bridge_cache_usb_input(Sf32lb52UsbRole role,
                                   const uint8_t *report,
                                   size_t len)
{
    uint8_t next;

    if (report == 0 || len == 0U ||
        len > sizeof(cached_usb_inputs[0].report)) {
        return;
    }
    next = (uint8_t)(cached_usb_input_index ^ 1U);
    cached_usb_inputs[next].role = role;
    cached_usb_inputs[next].len = (uint16_t)len;
    memcpy(cached_usb_inputs[next].report, report, len);
    bridge_memory_barrier();
    cached_usb_input_index = next;
}

static void bridge_cache_neutral_input(sf32lb52_bridge_role_t role)
{
    sf32lb52_bridge_input_state_t neutral;
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
    size_t len = sf32lb52_bridge_input_report_size(role);

    sf32lb52_bridge_input_state_reset(&neutral);
    neutral.valid = 1U;
    neutral.source = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    if (len != 0U && len <= sizeof(report) &&
        sf32lb52_bridge_encode_input(role, &neutral, 0U,
                                     report, sizeof(report)) == 0) {
        bridge_cache_usb_input(usb_role_from_bridge(role), report, len);
    }
}

static int bridge_on_role_change(sf32lb52_bridge_role_t role, void *context)
{
    sf32lb52_bridge_role_t old_role = sf32lb52_bridge_runtime_role();

    (void)context;
    bridge_cache_neutral_input(role);
    if (!sf32lb52_usb_request_role(usb_role_from_bridge(role))) {
        bridge_cache_neutral_input(old_role);
        return -1;
    }
    return 0;
}

static int bridge_on_feedback(sf32lb52_bridge_input_source_t source,
                              const sf32lb52_bridge_feedback_t *feedback,
                              void *context)
{
    (void)context;
    if (feedback == 0 || !feedback->valid) {
        return -1;
    }
    if (source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE) {
        return ns2_profile_apply_rumble(feedback->left_motor,
                                        feedback->right_motor);
    }
    if (source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT) {
        uint8_t report[SF32LB52_BRIDGE_DS5_OUTPUT_REPORT_SIZE];

        if (sf32lb52_bridge_encode_ds5_output(feedback, report) != 0) {
            return -1;
        }
        return ds5_classic_send_output_report(report, sizeof(report));
    }
    return -1;
}

int sf32lb52_app_test_rumble(uint16_t left_motor, uint16_t right_motor)
{
    sf32lb52_bridge_feedback_t feedback;
    sf32lb52_bridge_runtime_status_t status;

    sf32lb52_bridge_feedback_reset(&feedback);
    memset(&status, 0, sizeof(status));
    sf32lb52_bridge_runtime_get_status(&status);
    feedback.valid = 1U;
    feedback.type = SF32LB52_BRIDGE_FEEDBACK_DUALSENSE_RUMBLE;
    feedback.left_motor = left_motor;
    feedback.right_motor = right_motor;
    return bridge_on_feedback(status.active_input, &feedback, 0);
}

static void bridge_on_ns2_input(uint8_t kind, const uint8_t *data, size_t len)
{
    if (data == 0 || len == 0U || len > sizeof(pending_ns2_input.data)) {
        return;
    }

    pending_ns2_input_sequence++;
    bridge_memory_barrier();
    pending_ns2_input.kind = kind;
    pending_ns2_input.len = (uint8_t)len;
    pending_ns2_input.timestamp_ms = platform_millis();
    pending_ns2_input.sensor_timestamp_us = platform_micros();
    memcpy(pending_ns2_input.data, data, len);
    bridge_memory_barrier();
    pending_ns2_input_sequence++;
}

static void bridge_on_ds5_input(const uint8_t *usb_body,
                                size_t len,
                                void *context)
{
    (void)context;
    if (usb_body == 0 || len != DS5_CLASSIC_USB_INPUT_BODY_SIZE) {
        return;
    }

    pending_ds5_input_sequence++;
    bridge_memory_barrier();
    pending_ds5_input.len = (uint8_t)len;
    pending_ds5_input.timestamp_ms = platform_millis();
    memcpy(pending_ds5_input.body, usb_body, len);
    bridge_memory_barrier();
    pending_ds5_input_sequence++;
}

static void bridge_on_ds5_audio_input(const uint8_t *opus_data,
                                      size_t len,
                                      void *context)
{
    (void)context;
    sf32lb52_usb_ds5_mic_input(opus_data, len);
}

static void bridge_on_usb_audio_haptics(const int16_t *interleaved_3khz,
                                        size_t frames)
{
    sf32lb52_audio_haptics_output_t output;
    sf32lb52_audio_haptics_event_t event;

    memset(&output, 0, sizeof(output));
    event = sf32lb52_audio_haptics_process_s16(&audio_haptics_processor,
                                                interleaved_3khz,
                                                frames,
                                                &output);
    if (event == SF32LB52_AUDIO_HAPTICS_NO_OUTPUT) {
        return;
    }

    pending_audio_haptics_sequence++;
    bridge_memory_barrier();
    pending_audio_haptics.event = event;
    pending_audio_haptics.output = output;
    bridge_memory_barrier();
    pending_audio_haptics_sequence++;
}

static void bridge_process_audio_haptics(void)
{
    pending_audio_haptics_t pending;
    uint32_t before;
    uint32_t after;
    uint8_t attempts = 0U;

    do {
        before = pending_audio_haptics_sequence;
        if (before == processed_audio_haptics_sequence || (before & 1U) != 0U) {
            return;
        }
        bridge_memory_barrier();
        pending = pending_audio_haptics;
        bridge_memory_barrier();
        after = pending_audio_haptics_sequence;
        attempts++;
    } while ((before != after || (after & 1U) != 0U) && attempts < 2U);

    if (before == after && (after & 1U) == 0U) {
        processed_audio_haptics_sequence = after;
        ns2_profile_apply_audio_haptics(pending.event, &pending.output);
    }
}

static uint16_t bridge_usb_input_get(uint8_t *buffer, uint16_t reqlen)
{
    cached_usb_input_t cached;
    uint8_t before;
    uint8_t after;
    uint8_t attempts = 0U;

    if (buffer == 0) {
        return 0U;
    }
    do {
        before = cached_usb_input_index;
        bridge_memory_barrier();
        cached = cached_usb_inputs[before];
        bridge_memory_barrier();
        after = cached_usb_input_index;
        attempts++;
    } while (before != after && attempts < 2U);

    if (before != after || cached.len == 0U || cached.len > reqlen ||
        cached.role != sf32lb52_usb_get_role()) {
        return 0U;
    }
    memcpy(buffer, cached.report, cached.len);
    return cached.len;
}

static uint16_t bridge_usb_native_feature_get(uint8_t report_id,
                                              uint8_t *buffer,
                                              uint16_t reqlen)
{
    size_t len = ds5_classic_get_feature_report(report_id, buffer, reqlen);

    return len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
}

static void bridge_usb_native_feature_set(uint8_t report_id,
                                          const uint8_t *payload,
                                          uint16_t len)
{
    (void)ds5_classic_set_feature_report(report_id, payload, len);
}

static void bridge_usb_output(uint8_t report_id,
                              const uint8_t *payload,
                              size_t payload_len)
{
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
    pending_usb_output_t *slot;
    uint8_t head;
    uint8_t next;
    size_t report_len;
    /* Classify bytes by the persona that actually received them.  During a
     * transactional role change USB is switched before Flash persistence and
     * the runtime role commit, so consulting runtime here can mis-tag the new
     * persona's enumeration/output packets with the old role. */
    sf32lb52_bridge_role_t role =
        bridge_role_from_usb(sf32lb52_usb_get_role());

    if (payload == 0 || payload_len == 0U) {
        return;
    }
    if (role == SF32LB52_BRIDGE_ROLE_XBOX_360) {
        if (payload_len > sizeof(report)) {
            return;
        }
        memcpy(report, payload, payload_len);
        report_len = payload_len;
    } else {
        if (payload_len + 1U > sizeof(report)) {
            return;
        }
        report[0] = report_id;
        memcpy(report + 1U, payload, payload_len);
        report_len = payload_len + 1U;
    }

    head = pending_usb_output_head;
    next = (uint8_t)((head + 1U) % BRIDGE_USB_OUTPUT_QUEUE_STORAGE);
    if (next == pending_usb_output_tail) {
        pending_usb_output_drops++;
        return;
    }
    slot = &pending_usb_outputs[head];
    slot->role = role;
    slot->len = report_len;
    memcpy(slot->report, report, report_len);
    bridge_memory_barrier();
    pending_usb_output_head = next;
}

static void bridge_process_usb_output(void)
{
    uint8_t processed;

    for (processed = 0U; processed < BRIDGE_USB_OUTPUT_QUEUE_CAPACITY;
         processed++) {
        pending_usb_output_t output;
        sf32lb52_bridge_runtime_status_t status;
        sf32lb52_bridge_output_route_t route;
        uint8_t tail = pending_usb_output_tail;

        if (tail == pending_usb_output_head) {
            return;
        }
        bridge_memory_barrier();
        output = pending_usb_outputs[tail];
        bridge_memory_barrier();
        pending_usb_output_tail =
            (uint8_t)((tail + 1U) % BRIDGE_USB_OUTPUT_QUEUE_STORAGE);

        if (output.len == 0U || output.len > sizeof(output.report) ||
            output.role != sf32lb52_bridge_runtime_role()) {
            continue;
        }

        memset(&status, 0, sizeof(status));
        sf32lb52_bridge_runtime_get_status(&status);
        route = sf32lb52_bridge_select_output_route(
            output.role, status.active_input, output.report, output.len);
        if (route == SF32LB52_BRIDGE_OUTPUT_ROUTE_MANAGER) {
            ns2_profile_on_usb_output(output.report, output.len);
        } else if (route == SF32LB52_BRIDGE_OUTPUT_ROUTE_NS2_NATIVE) {
            ns2_profile_on_usb_output(output.report, output.len);
        } else if (route == SF32LB52_BRIDGE_OUTPUT_ROUTE_DS5_NATIVE) {
            (void)ds5_classic_send_output_report(output.report, output.len);
        } else if (route == SF32LB52_BRIDGE_OUTPUT_ROUTE_TRANSLATED) {
            (void)sf32lb52_bridge_runtime_on_usb_output(output.report,
                                                        output.len);
        }
    }
}

static bool bridge_take_ns2_input(pending_ns2_input_t *out)
{
    uint32_t before;
    uint32_t after;
    uint8_t attempts = 0U;

    if (out == 0) {
        return false;
    }
    do {
        before = pending_ns2_input_sequence;
        if (before == processed_ns2_input_sequence || (before & 1U) != 0U) {
            return false;
        }
        bridge_memory_barrier();
        *out = pending_ns2_input;
        bridge_memory_barrier();
        after = pending_ns2_input_sequence;
        attempts++;
    } while (before != after && attempts < 8U);
    if (before != after || (after & 1U) != 0U) {
        return false;
    }
    processed_ns2_input_sequence = after;
    return true;
}

static bool bridge_take_ds5_input(pending_ds5_input_t *out)
{
    uint32_t before;
    uint32_t after;
    uint8_t attempts = 0U;

    if (out == 0) {
        return false;
    }
    do {
        before = pending_ds5_input_sequence;
        if (before == processed_ds5_input_sequence || (before & 1U) != 0U) {
            return false;
        }
        bridge_memory_barrier();
        *out = pending_ds5_input;
        bridge_memory_barrier();
        after = pending_ds5_input_sequence;
        attempts++;
    } while (before != after && attempts < 8U);
    if (before != after || (after & 1U) != 0U) {
        return false;
    }
    processed_ds5_input_sequence = after;
    return true;
}

static void bridge_apply_ns2_input(const pending_ns2_input_t *input)
{
    sf32lb52_ns2_ble_snapshot_t snapshot;
    sf32lb52_bridge_input_state_t state;

    if (input == 0 || input->len == 0U) {
        return;
    }
    ns2_profile_on_ble_input(input->kind, input->data, input->len);
    if (ns2_profile_get_snapshot(&snapshot) == 0 &&
        sf32lb52_bridge_state_from_ns2_snapshot(&snapshot,
                                                input->sensor_timestamp_us,
                                                &state) == 0) {
        (void)sf32lb52_bridge_runtime_accept_input(&state,
                                                   input->timestamp_ms);
    }
}

static void bridge_apply_ds5_input(const pending_ds5_input_t *input)
{
    sf32lb52_bridge_input_state_t state;

    if (input != 0 && input->len == DS5_CLASSIC_USB_INPUT_BODY_SIZE &&
        sf32lb52_bridge_parse_ds5_usb_input(input->body,
                                            input->len,
                                            &state) == 0) {
        if (sf32lb52_bridge_runtime_accept_input(&state,
                                                 input->timestamp_ms)) {
            latest_ds5_passthrough = *input;
            latest_ds5_passthrough_valid = 1U;
        }
    }
}

static void bridge_process_wireless_inputs(void)
{
    pending_ns2_input_t ns2;
    pending_ds5_input_t ds5;
    sf32lb52_bridge_persisted_config_t config;
    bool have_ns2 = bridge_take_ns2_input(&ns2);
    bool have_ds5 = bridge_take_ds5_input(&ds5);
    bool ds5_first;

    if (!have_ns2 && !have_ds5) {
        return;
    }
    if (!have_ns2) {
        bridge_apply_ds5_input(&ds5);
        return;
    }
    if (!have_ds5) {
        bridge_apply_ns2_input(&ns2);
        return;
    }

    sf32lb52_bridge_runtime_get_config(&config);
    ds5_first = (int32_t)(ds5.timestamp_ms - ns2.timestamp_ms) < 0 ||
        (ds5.timestamp_ms == ns2.timestamp_ms &&
         config.last_active_input ==
             SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT);
    if (ds5_first) {
        bridge_apply_ds5_input(&ds5);
        bridge_apply_ns2_input(&ns2);
    } else {
        bridge_apply_ns2_input(&ns2);
        bridge_apply_ds5_input(&ds5);
    }
}

static size_t bridge_make_input_report(uint32_t now_ms,
                                       uint8_t *report,
                                       size_t report_capacity)
{
    sf32lb52_bridge_runtime_status_t status;
    size_t report_len;

    memset(&status, 0, sizeof(status));
    sf32lb52_bridge_runtime_get_status(&status);
    if (sf32lb52_bridge_runtime_role() ==
            SF32LB52_BRIDGE_ROLE_NS2PRO &&
        status.active_input ==
            SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE &&
        status.input_valid && !status.input_stale &&
        report_capacity >= SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE &&
        ns2_profile_make_usb_report(report) == 0 &&
        sf32lb52_bridge_runtime_patch_native_input_report(
            now_ms, report, SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE)) {
        return SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE;
    }
    if ((sf32lb52_bridge_runtime_role() ==
             SF32LB52_BRIDGE_ROLE_DUALSENSE ||
         sf32lb52_bridge_runtime_role() ==
             SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE) &&
        status.active_input ==
            SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT &&
        status.input_valid && !status.input_stale &&
        latest_ds5_passthrough_valid &&
        (uint32_t)(now_ms - latest_ds5_passthrough.timestamp_ms) <=
            SF32LB52_BRIDGE_INPUT_STALE_MS &&
        report_capacity >= SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE) {
        report[0] = SF32LB52_USB_DS5_INPUT_REPORT_ID;
        memcpy(report + 1U,
               latest_ds5_passthrough.body,
               DS5_CLASSIC_USB_INPUT_BODY_SIZE);
        if (sf32lb52_bridge_runtime_patch_native_input_report(
                now_ms, report, SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE)) {
            return SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE;
        }
    }
    report_len = sf32lb52_bridge_runtime_make_input_report(now_ms,
                                                            report,
                                                            report_capacity);
    if (sf32lb52_bridge_runtime_role() ==
            SF32LB52_BRIDGE_ROLE_NS2PRO &&
        report_len == SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE) {
        ns2_profile_stamp_usb_report_timestamp(report);
    }
    return report_len;
}

static void sf32lb52_heartbeat_init(void)
{
#if defined(SF32LB52_APP_HAS_RTDEVICE) && defined(BSP_LED1_PIN)
    rt_pin_mode(BSP_LED1_PIN, PIN_MODE_OUTPUT);
#endif
}

static void sf32lb52_heartbeat_tick(void)
{
    static uint32_t last_ms;
    static uint8_t led_on;
    const uint32_t now_ms = platform_millis();

    if ((uint32_t)(now_ms - last_ms) < 1000U) {
        return;
    }

    last_ms = now_ms;
    heartbeat_count++;
    led_on = (uint8_t)!led_on;

#if defined(SF32LB52_APP_HAS_RTDEVICE) && defined(BSP_LED1_PIN)
#if defined(BSP_LED1_ACTIVE_HIGH)
    rt_pin_write(BSP_LED1_PIN, led_on ? PIN_HIGH : PIN_LOW);
#else
    rt_pin_write(BSP_LED1_PIN, led_on ? PIN_LOW : PIN_HIGH);
#endif
#endif

    platform_log("sf32lb52", led_on ? "heartbeat led=on" : "heartbeat led=off");
}

#if defined(SF32LB52_APP_HAS_RTTHREAD)
static bool bridge_queue_control_command(bridge_control_command_kind_t kind,
                                         int32_t argument)
{
    uint8_t head = bridge_control_head;
    uint8_t next =
        (uint8_t)((head + 1U) % BRIDGE_CONTROL_QUEUE_STORAGE);

    if (next == bridge_control_tail) {
        return false;
    }
    bridge_control_queue[head].kind = kind;
    bridge_control_queue[head].argument = argument;
    bridge_memory_barrier();
    bridge_control_head = next;
    return true;
}

int sf32lb52_app_request_input_action(sf32lb52_app_input_action_t action)
{
    bridge_control_command_kind_t kind;

    switch (action) {
    case SF32LB52_APP_INPUT_ACTION_PAIR_DS5:
        kind = BRIDGE_CONTROL_PAIR_DS5;
        break;
    case SF32LB52_APP_INPUT_ACTION_CONNECT_DS5:
        kind = BRIDGE_CONTROL_CONNECT_DS5;
        break;
    case SF32LB52_APP_INPUT_ACTION_DISCONNECT_DS5:
        kind = BRIDGE_CONTROL_DISCONNECT_DS5;
        break;
    case SF32LB52_APP_INPUT_ACTION_FORGET_DS5:
        kind = BRIDGE_CONTROL_FORGET_DS5;
        break;
    case SF32LB52_APP_INPUT_ACTION_PAIR_NS2:
        kind = BRIDGE_CONTROL_PAIR_NS2;
        break;
    case SF32LB52_APP_INPUT_ACTION_CONNECT_NS2:
        kind = BRIDGE_CONTROL_CONNECT_NS2;
        break;
    case SF32LB52_APP_INPUT_ACTION_DISCONNECT_NS2:
        kind = BRIDGE_CONTROL_DISCONNECT_NS2;
        break;
    case SF32LB52_APP_INPUT_ACTION_FORGET_NS2:
        kind = BRIDGE_CONTROL_FORGET_NS2;
        break;
    default:
        return -1;
    }
    return bridge_queue_control_command(kind, 0) ? 0 : -1;
}

static void bridge_process_control_commands(void)
{
    uint8_t processed;

    for (processed = 0U; processed < BRIDGE_CONTROL_QUEUE_CAPACITY;
         processed++) {
        bridge_control_command_t command;
        uint8_t tail = bridge_control_tail;
        int result = -1;

        if (tail == bridge_control_head) {
            return;
        }
        bridge_memory_barrier();
        command = bridge_control_queue[tail];
        bridge_memory_barrier();
        bridge_control_tail =
            (uint8_t)((tail + 1U) % BRIDGE_CONTROL_QUEUE_STORAGE);

        switch (command.kind) {
        case BRIDGE_CONTROL_USB_RECONNECT:
            sf32lb52_usb_force_reconnect();
            result = 0;
            break;
        case BRIDGE_CONTROL_ROLE_SET:
            result = sf32lb52_bridge_runtime_set_role(
                         (sf32lb52_bridge_role_t)command.argument,
                         true) ? 0 : -1;
            break;
        case BRIDGE_CONTROL_ROLE_NEXT:
            result = sf32lb52_bridge_runtime_cycle_role(true) ? 0 : -1;
            break;
        case BRIDGE_CONTROL_INPUT_SET:
            result = sf32lb52_bridge_runtime_set_input_preference(
                         (sf32lb52_bridge_input_preference_t)command.argument,
                         true) ? 0 : -1;
            break;
        case BRIDGE_CONTROL_PAIR_DS5:
            (void)sf32lb52_bridge_runtime_set_input_preference(
                SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE, true);
            pending_input_action = SF32LB52_APP_INPUT_ACTION_PAIR_DS5;
            pending_input_retry_ms = 0U;
            result = 0;
            break;
        case BRIDGE_CONTROL_CONNECT_DS5:
            (void)sf32lb52_bridge_runtime_set_input_preference(
                SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE, true);
            pending_input_action = SF32LB52_APP_INPUT_ACTION_CONNECT_DS5;
            pending_input_retry_ms = 0U;
            result = 0;
            break;
        case BRIDGE_CONTROL_DISCONNECT_DS5:
            result = ds5_classic_disconnect();
            break;
        case BRIDGE_CONTROL_FORGET_DS5:
            result = ds5_classic_forget();
            if (result == DS5_CLASSIC_OK) {
                result = sf32lb52_bridge_runtime_forget_input(
                             SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
                             true) ? 0 : -1;
            }
            break;
        case BRIDGE_CONTROL_PAIR_NS2:
            (void)sf32lb52_bridge_runtime_set_input_preference(
                SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO, true);
            pending_input_action = SF32LB52_APP_INPUT_ACTION_PAIR_NS2;
            pending_input_retry_ms = 0U;
            result = 0;
            break;
        case BRIDGE_CONTROL_CONNECT_NS2:
            (void)sf32lb52_bridge_runtime_set_input_preference(
                SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO, true);
            pending_input_action = SF32LB52_APP_INPUT_ACTION_CONNECT_NS2;
            pending_input_retry_ms = 0U;
            result = 0;
            break;
        case BRIDGE_CONTROL_DISCONNECT_NS2:
            result = ble_gatt_disconnect(0U);
            break;
        case BRIDGE_CONTROL_FORGET_NS2:
            (void)ble_gatt_disconnect(0U);
            ble_gatt_clear_target();
            result = sf32lb52_bridge_runtime_forget_input(
                         SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
                         true) ? 0 : -1;
            break;
        default:
            break;
        }
        rt_kprintf("sf32 control command=%u result=%d\n",
                   (unsigned int)command.kind,
                   result);
    }
}

static void bridge_process_pending_input_action(
    const ds5_classic_status_t *ds5_status,
    const ble_gatt_status_t *ns2_status,
    uint32_t now_ms)
{
    sf32lb52_app_input_action_t action = pending_input_action;
    int result;

    if (action == SF32LB52_APP_INPUT_ACTION_NONE ||
        ds5_status == 0 || ns2_status == 0 ||
        (int32_t)(now_ms - pending_input_retry_ms) < 0) {
        return;
    }

    if (action == SF32LB52_APP_INPUT_ACTION_PAIR_DS5 ||
        action == SF32LB52_APP_INPUT_ACTION_CONNECT_DS5) {
        if (ns2_status->scanning || ns2_status->connecting ||
            ns2_status->connected) {
            result = ble_gatt_disconnect(0U);
            pending_input_retry_ms = now_ms + 250U;
            if (result != 0) {
                platform_log("ble", "retrying NS2 stop before DS5 action");
            }
            return;
        }
        result = action == SF32LB52_APP_INPUT_ACTION_PAIR_DS5 ?
            ds5_classic_start_pairing() : ds5_classic_connect_saved();
        if (result == DS5_CLASSIC_OK) {
            ds5_started = 1U;
            pending_input_action = SF32LB52_APP_INPUT_ACTION_NONE;
            platform_log("bt", "DS5 action started after NS2 stopped");
        } else if (result == DS5_CLASSIC_ERROR_BUSY ||
                   result == DS5_CLASSIC_ERROR_NOT_READY) {
            pending_input_retry_ms = now_ms + 250U;
        } else {
            pending_input_action = SF32LB52_APP_INPUT_ACTION_NONE;
            platform_log("bt", "DS5 action failed result=%d", result);
        }
        return;
    }

    if (action == SF32LB52_APP_INPUT_ACTION_PAIR_NS2 ||
        action == SF32LB52_APP_INPUT_ACTION_CONNECT_NS2) {
        if (ds5_status->connected || ds5_status->connecting ||
            ds5_status->disconnecting || ds5_status->discovering ||
            ds5_status->pairing ||
            ds5_status->state == DS5_CLASSIC_STATE_PAIRING) {
            if (ds5_status->connected || ds5_status->connecting) {
                result = ds5_classic_disconnect();
                if (result != DS5_CLASSIC_OK &&
                    result != DS5_CLASSIC_ERROR_NOT_CONNECTED) {
                    platform_log("bt",
                                 "retrying DS5 stop before NS2 action result=%d",
                                 result);
                }
            }
            pending_input_retry_ms = now_ms + 250U;
            return;
        }

        if (action == SF32LB52_APP_INPUT_ACTION_PAIR_NS2) {
            ble_gatt_clear_target();
            if (!sf32lb52_bridge_runtime_forget_input(
                    SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE, true)) {
                result = -1;
            } else {
                result = ble_gatt_disconnect(1U);
            }
        } else {
            sf32lb52_bridge_persisted_config_t config;

            sf32lb52_bridge_runtime_get_config(&config);
            if (config.ns2_address_valid) {
                (void)ble_gatt_set_target(config.ns2_address_type,
                                          config.ns2_address);
            }
            result = ble_gatt_start_scan();
        }
        if (result == 0) {
            ble_started = 1U;
            pending_input_action = SF32LB52_APP_INPUT_ACTION_NONE;
            platform_log("ble", "NS2 action started after DS5 stopped");
        } else {
            pending_input_retry_ms = now_ms + 250U;
        }
    }
}

static void sf32_status(int argc, char **argv)
{
    Sf32lb52UsbDeviceStatus usb_status;
    Sf32lb52UsbPhyStatus phy_status;
    sf32lb52_bridge_runtime_status_t bridge_status;
    ds5_classic_status_t ds5_status;
    ble_gatt_status_t ns2_status;

    (void)argc;
    (void)argv;
    memset(&bridge_status, 0, sizeof(bridge_status));
    memset(&ds5_status, 0, sizeof(ds5_status));
    memset(&ns2_status, 0, sizeof(ns2_status));
    sf32lb52_usb_get_status(&usb_status);
    sf32lb52_bridge_runtime_get_status(&bridge_status);
    ds5_classic_get_status(&ds5_status);
    ble_gatt_get_status(&ns2_status);
    rt_kprintf("sf32lb52 bridge role=%s input=%s preference=%s heartbeat=%u millis=%u\n",
               sf32lb52_bridge_role_name(bridge_status.output_role),
               sf32lb52_bridge_source_name(bridge_status.active_input),
               sf32lb52_bridge_preference_name(bridge_status.input_preference),
               heartbeat_count,
               platform_millis());
    rt_kprintf("usb mounted=%u suspended=%u in=%u fail=%u out=%u fget=%u fset=%u fdrop=%u\n",
               usb_status.mounted ? 1U : 0U,
               usb_status.suspended ? 1U : 0U,
               usb_status.in_reports_sent,
               usb_status.in_reports_failed,
               usb_status.out_reports_received,
               usb_status.feature_get_reports,
               usb_status.feature_set_reports,
               usb_status.feature_set_dropped);
    rt_kprintf("ds5 connected=%u pairing=%u reports=%u ctrl=%u intr=%u\n",
               ds5_status.connected ? 1U : 0U,
               ds5_status.pairing ? 1U : 0U,
               ds5_status.input_reports,
               ds5_status.control_registered,
               ds5_status.interrupt_registered);
    rt_kprintf("ds5 regfail=%u retry=%u error=%d sdk=%u keymissing=%u\n",
               ds5_status.registration_failures,
               ds5_status.registration_retries,
               ds5_status.last_error,
               ds5_status.last_sdk_result,
               ds5_status.key_missing_events);
    rt_kprintf("ns2 connected=%u scanning=%u notify=%u accepted=%u ignored=%u switches=%u\n",
               ns2_status.connected ? 1U : 0U,
               ns2_status.scanning ? 1U : 0U,
               ns2_status.notifications,
               bridge_status.accepted_reports,
               bridge_status.ignored_reports,
               bridge_status.source_switches);
    if (sf32lb52_usb_get_phy_status(&phy_status)) {
        rt_kprintf("usb phy power=0x%02x devctl=0x%02x intrusb=0x%02x intrusbe=0x%02x usbcfg=0x%02x dpbrxdisl=0x%02x dpbtxdisl=0x%02x\n",
                   phy_status.power,
                   phy_status.devctl,
                   phy_status.intrusb,
                   phy_status.intrusbe,
                   phy_status.usbcfg,
                   phy_status.dpbrxdisl,
                   phy_status.dpbtxdisl);
        rt_kprintf("usb ep irq tx=0x%04x/0x%04x rx=0x%04x/0x%04x arm=%d/%d\n",
                   phy_status.intrtx,
                   phy_status.intrtxe,
                   phy_status.intrrx,
                   phy_status.intrrxe,
                   phy_status.gamepad_out_arm_result,
                   phy_status.vendor_out_arm_result);
        rt_kprintf("usb ep1 tx=%u/0x%04x rx=%u/0x%04x ep2 tx=%u/0x%04x rx=%u/0x%04x\n",
                   phy_status.ep1_txmaxp,
                   phy_status.ep1_txcsr,
                   phy_status.ep1_rxmaxp,
                   phy_status.ep1_rxcsr,
                   phy_status.ep2_txmaxp,
                   phy_status.ep2_txcsr,
                   phy_status.ep2_rxmaxp,
                   phy_status.ep2_rxcsr);
    }
}
MSH_CMD_EXPORT(sf32_status, show sf32lb52 ns2 bridge status);

static void sf32_mgr(int argc, char **argv)
{
    uint8_t command[NS2_USB_REPORT_SIZE] = {0};
    uint8_t report[NS2_USB_REPORT_SIZE];
    char line[112];
    size_t command_len = SF32LB52_NS2_FEATURE_MAGIC_SIZE;
    uint16_t total = 0U;
    uint16_t received = 0U;
    int arg;

    if (argc < 2) {
        rt_kprintf("usage: sf32_mgr <manager command>\n");
        return;
    }
    memcpy(command, SF32LB52_NS2_FEATURE_COMMAND_MAGIC,
           SF32LB52_NS2_FEATURE_MAGIC_SIZE);
    for (arg = 1; arg < argc; arg++) {
        size_t arg_len = strlen(argv[arg]);
        if (arg > 1 && command_len < sizeof(command)) {
            command[command_len++] = ' ';
        }
        if (arg_len > sizeof(command) - command_len) {
            arg_len = sizeof(command) - command_len;
        }
        memcpy(command + command_len, argv[arg], arg_len);
        command_len += arg_len;
    }

    ns2_profile_on_feature_set(command, (uint16_t)command_len);
    do {
        uint16_t offset;
        uint8_t chunk_len;
        uint8_t chunk_offset = 0U;

        memset(report, 0, sizeof(report));
        (void)ns2_profile_on_feature_get(report, sizeof(report));
        if (memcmp(report, SF32LB52_NS2_FEATURE_REPLY_MAGIC,
                   SF32LB52_NS2_FEATURE_MAGIC_SIZE) != 0) {
            rt_kprintf("SF32MGR:0:0:7b226f6b223a66616c73657d\n");
            return;
        }
        total = (uint16_t)(report[6] | ((uint16_t)report[7] << 8U));
        offset = (uint16_t)(report[8] | ((uint16_t)report[9] << 8U));
        chunk_len = report[10];
        while (chunk_offset < chunk_len) {
            uint8_t part_len = (uint8_t)(chunk_len - chunk_offset);
            size_t used;
            uint8_t index;

            if (part_len > 40U) {
                part_len = 40U;
            }
            used = (size_t)rt_snprintf(line, sizeof(line),
                                       "SF32MGR:%u:%u:", total,
                                       (uint16_t)(offset + chunk_offset));
            for (index = 0U; index < part_len && used + 2U < sizeof(line);
                 index++) {
                used += (size_t)rt_snprintf(line + used, sizeof(line) - used,
                                            "%02x",
                                            report[SF32LB52_NS2_FEATURE_PAYLOAD_OFFSET +
                                                   chunk_offset + index]);
            }
            rt_kprintf("%s\n", line);
            chunk_offset = (uint8_t)(chunk_offset + part_len);
        }
        received = (uint16_t)(offset + chunk_len);
    } while (received < total);
}
MSH_CMD_EXPORT(sf32_mgr, run bridge manager command over serial);

static void sf32_usb_reconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("sf32lb52 usb reconnect %s\n",
               bridge_queue_control_command(BRIDGE_CONTROL_USB_RECONNECT, 0) ?
                   "queued" : "queue full");
}
MSH_CMD_EXPORT(sf32_usb_reconnect, force sf32lb52 usb soft reconnect);

static void sf32_role(int argc, char **argv)
{
    sf32lb52_bridge_role_t role;

    if (argc < 2) {
        rt_kprintf("role=%s (xbox|ds5|ns2pro|next)\n",
                   sf32lb52_bridge_role_name(sf32lb52_bridge_runtime_role()));
        return;
    }
    if (strcmp(argv[1], "next") == 0) {
        rt_kprintf("role switch %s\n",
                   bridge_queue_control_command(BRIDGE_CONTROL_ROLE_NEXT, 0) ?
                       "queued" : "queue full");
    } else if (sf32lb52_bridge_parse_role(argv[1], &role)) {
        rt_kprintf("role switch %s\n",
                   bridge_queue_control_command(BRIDGE_CONTROL_ROLE_SET,
                                                (int32_t)role) ?
                       "queued" : "queue full");
    } else {
        rt_kprintf("usage: sf32_role xbox|ds5|ns2pro|next\n");
    }
}
MSH_CMD_EXPORT(sf32_role, select xbox ds5 or ns2pro USB role);

static void sf32_input(int argc, char **argv)
{
    sf32lb52_bridge_input_preference_t preference;

    if (argc < 2 ||
        !sf32lb52_bridge_parse_input_preference(argv[1], &preference)) {
        rt_kprintf("usage: sf32_input auto|ds5|ns2pro\n");
        return;
    }
    rt_kprintf("input preference %s\n",
               bridge_queue_control_command(BRIDGE_CONTROL_INPUT_SET,
                                            (int32_t)preference) ?
                   "queued" : "queue full");
}
MSH_CMD_EXPORT(sf32_input, select automatic ds5 or ns2pro input);

static void sf32_pair(int argc, char **argv)
{
    if (argc < 2) {
        rt_kprintf("usage: sf32_pair ds5|ns2pro\n");
        return;
    }
    if (strcmp(argv[1], "ds5") == 0) {
        rt_kprintf("pair %s\n",
                   bridge_queue_control_command(BRIDGE_CONTROL_PAIR_DS5, 0) ?
                       "queued" : "queue full");
    } else if (strcmp(argv[1], "ns2") == 0 ||
               strcmp(argv[1], "ns2pro") == 0) {
        rt_kprintf("pair %s\n",
                   bridge_queue_control_command(BRIDGE_CONTROL_PAIR_NS2, 0) ?
                       "queued" : "queue full");
    } else {
        rt_kprintf("usage: sf32_pair ds5|ns2pro\n");
        return;
    }
}
MSH_CMD_EXPORT(sf32_pair, pair a DualSense or NS2Pro input);
#endif

#if !defined(SF32LB52_APP_HAS_RTTHREAD)
int sf32lb52_app_request_input_action(sf32lb52_app_input_action_t action)
{
    (void)action;
    return -1;
}
#endif

int sf32lb52_app_main(void)
{
#if defined(SF32LB52_USB_ONLY_SMOKE)
    platform_init();
    sf32lb52_usb_device_init(Sf32lb52UsbRoleNintendo);

    for (;;) {
        static uint32_t last_smoke_ms;
        uint32_t now_ms = platform_millis();

        sf32lb52_usb_device_task();
        if ((uint32_t)(now_ms - last_smoke_ms) >= 50U) {
            last_smoke_ms = now_ms;
            (void)sf32lb52_usb_hid_send(0U, 0, 0U);
        }
        platform_sleep_ms(10U);
    }
#else
    sf32lb52_bridge_persisted_config_t config;
    sf32lb52_bridge_input_preference_t observed_input_preference;
    uint8_t observed_auto_connect;
    uint8_t prefer_ns2_at_boot;
    int bluetooth_start_result;
    int ds5_init_result;
    int start_result;

    platform_init();
    platform_log("sf32lb52", "boot board=SF32LB52_DEVKIT_NANO profile=bridge");
    sf32lb52_heartbeat_init();
    boot_ms = platform_millis();

    /* SiFli's BLE power-on path initializes the SDK-owned NVDS FlashDB mutex.
     * Register callbacks first, start Bluetooth, and wait for its power event
     * before the bridge settings layer touches that shared database. */
    ns2_profile_init();
    sf32lb52_audio_haptics_init(&audio_haptics_processor);
    if (ble_gatt_init(bridge_on_ns2_input) != 0) {
        platform_log("ble", "SiFli BLE GATT central unavailable");
    }
    ds5_init_result = ds5_classic_init(bridge_on_ds5_input, 0);
    if (ds5_init_result != DS5_CLASSIC_OK) {
        platform_log("bt", "SiFli Bluetooth Classic L2CAP host unavailable");
    }
    bluetooth_start_result = platform_bluetooth_start_once();
    if (bluetooth_start_result == 0) {
        ble_gatt_status_t boot_ble_status;
        uint32_t bluetooth_deadline_ms = boot_ms + 5000U;

        memset(&boot_ble_status, 0, sizeof(boot_ble_status));
        do {
            ble_gatt_poll();
            ble_gatt_get_status(&boot_ble_status);
            if (boot_ble_status.powered) {
                break;
            }
            platform_sleep_ms(10U);
        } while ((int32_t)(platform_millis() - bluetooth_deadline_ms) < 0);
        if (!boot_ble_status.powered) {
            platform_log("bt", "Bluetooth/NVDS startup timed out");
        }
    } else {
        platform_log("bt", "SiFli Bluetooth core start failed");
    }

    sf32lb52_bridge_runtime_init();
    ns2_profile_load_tuning();
    sf32lb52_bridge_runtime_set_callbacks(bridge_on_role_change,
                                          bridge_on_feedback,
                                          0);
    sf32lb52_bridge_runtime_get_config(&config);
    observed_input_preference = config.input_preference;
    observed_auto_connect = config.auto_connect;
    prefer_ns2_at_boot =
        config.input_preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO ||
        (config.input_preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO &&
         config.last_active_input ==
             SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE);
    if (config.ns2_address_valid) {
        (void)ble_gatt_set_target(config.ns2_address_type,
                                  config.ns2_address);
    }
    if (ds5_init_result == DS5_CLASSIC_OK) {
        int ds5_result;

        if (config.ds5_address_valid) {
            ds5_result = ds5_classic_set_saved_address(config.ds5_address);
            if (ds5_result == DS5_CLASSIC_OK) {
                observed_ds5_address_valid = 1U;
                memcpy(observed_ds5_address,
                       config.ds5_address,
                       sizeof(observed_ds5_address));
            }
        }
        ds5_auto_reconnect_applied =
            (uint8_t)(config.auto_connect && !prefer_ns2_at_boot);
        (void)ds5_classic_set_auto_reconnect(
            ds5_auto_reconnect_applied != 0U);
    }
    if (bluetooth_start_result == 0 &&
        config.auto_connect && prefer_ns2_at_boot) {
        if (ble_gatt_start_scan() == 0) {
            ble_started = 1U;
        } else {
            ble_start_retry_ms = boot_ms + 1000U;
        }
    } else if (bluetooth_start_result == 0 && config.auto_connect) {
        start_result = ds5_classic_connect_saved();
        if (start_result == DS5_CLASSIC_OK) {
            ds5_started = 1U;
        } else {
            ds5_start_retry_ms = boot_ms + 1000U;
        }
    }
    sf32lb52_usb_set_report_callbacks(bridge_usb_output,
                                       bridge_usb_input_get,
                                       ns2_profile_on_feature_get,
                                       ns2_profile_on_feature_set);
    sf32lb52_usb_set_native_feature_callbacks(
        bridge_usb_native_feature_get,
        bridge_usb_native_feature_set);
    sf32lb52_usb_set_audio_haptics_callback(bridge_on_usb_audio_haptics);
    ds5_classic_set_audio_input_callback(bridge_on_ds5_audio_input, 0);
    bridge_cache_neutral_input(sf32lb52_bridge_runtime_role());
    sf32lb52_usb_device_init(
        usb_role_from_bridge(sf32lb52_bridge_runtime_role()));

    for (;;) {
        uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
        Sf32lb52UsbDeviceStatus usb_status;
        ds5_classic_status_t ds5_status;
        ble_gatt_status_t ns2_status;
        static uint32_t last_usb_report_ms;
        uint32_t now_ms;
        uint32_t report_interval_ms;
        uint8_t want_ds5_auto_reconnect;
        size_t report_len;

        bridge_process_audio_haptics();
        ns2_profile_poll();
        ds5_classic_poll();
        ble_gatt_poll();
        sf32lb52_usb_device_task();
#if defined(SF32LB52_APP_HAS_RTTHREAD)
        bridge_process_control_commands();
#endif
        bridge_process_wireless_inputs();
        bridge_process_usb_output();
        sf32lb52_heartbeat_tick();
        now_ms = platform_millis();
        sf32lb52_bridge_runtime_poll(now_ms);
        sf32lb52_bridge_runtime_get_config(&config);

        if (config.ds5_address_valid != observed_ds5_address_valid ||
            (config.ds5_address_valid &&
             memcmp(config.ds5_address,
                    observed_ds5_address,
                    sizeof(observed_ds5_address)) != 0)) {
            if (ds5_classic_set_saved_address(
                    config.ds5_address_valid ? config.ds5_address : 0) ==
                DS5_CLASSIC_OK) {
                observed_ds5_address_valid = config.ds5_address_valid;
                if (config.ds5_address_valid) {
                    memcpy(observed_ds5_address,
                           config.ds5_address,
                           sizeof(observed_ds5_address));
                } else {
                    memset(observed_ds5_address,
                           0,
                           sizeof(observed_ds5_address));
                }
            }
        }

        want_ds5_auto_reconnect =
            (uint8_t)(config.auto_connect &&
                      config.input_preference !=
                          SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO &&
                      (config.input_preference ==
                           SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE ||
                       ds5_started ||
                       (uint32_t)(now_ms - boot_ms) >=
                           SF32LB52_BLE_START_DELAY_MS));
        if (want_ds5_auto_reconnect != ds5_auto_reconnect_applied) {
            if (ds5_classic_set_auto_reconnect(
                    want_ds5_auto_reconnect != 0U) == DS5_CLASSIC_OK) {
                ds5_auto_reconnect_applied = want_ds5_auto_reconnect;
            }
        }

        sf32lb52_usb_get_status(&usb_status);
        if (!usb_status.role_switch_pending) {
            sf32lb52_bridge_role_t actual_role =
                bridge_role_from_usb(usb_status.active_role);

            if (actual_role != sf32lb52_bridge_runtime_role()) {
                platform_log("usb",
                             "role switch rolled back to %s",
                             sf32lb52_bridge_role_name(actual_role));
                (void)sf32lb52_bridge_runtime_sync_role(actual_role, true);
                bridge_cache_neutral_input(actual_role);
                sf32lb52_bridge_runtime_get_config(&config);
            }
        }
        memset(&ds5_status, 0, sizeof(ds5_status));
        memset(&ns2_status, 0, sizeof(ns2_status));
        ds5_classic_get_status(&ds5_status);
        ble_gatt_get_status(&ns2_status);

        if (ds5_status.key_missing_events !=
            observed_ds5_key_missing_events) {
            observed_ds5_key_missing_events =
                ds5_status.key_missing_events;
            /* The SDK no longer owns a usable link key. Remove the injected
             * target transactionally so reboot cannot enter a permanent
             * reconnect loop with a stale address. Pairing remains explicit. */
            if (config.ds5_address_valid &&
                !sf32lb52_bridge_runtime_forget_input(
                    SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT, true)) {
                platform_log("bt", "failed to forget DS5 after missing key");
            }
        }

        if (ds5_status.connected || ds5_status.connecting) {
            ds5_started = 1U;
        }
        if (ns2_status.scanning || ns2_status.connecting ||
            ns2_status.connected) {
            ble_started = 1U;
        }
        if (config.input_preference != observed_input_preference ||
            config.auto_connect != observed_auto_connect) {
            if (config.auto_connect &&
                config.input_preference !=
                    SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO &&
                !ds5_status.connected && !ds5_status.connecting) {
                ds5_started = 0U;
                ds5_start_retry_ms = now_ms;
            }
            if (config.auto_connect &&
                config.input_preference !=
                    SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE &&
                !ns2_status.connected && !ns2_status.connecting &&
                !ns2_status.scanning) {
                ble_started = 0U;
                ble_start_retry_ms = now_ms;
            }
            observed_input_preference = config.input_preference;
            observed_auto_connect = config.auto_connect;
        }

#if defined(SF32LB52_APP_HAS_RTTHREAD)
        if (pending_input_action == SF32LB52_APP_INPUT_ACTION_NONE &&
            (int32_t)(now_ms - input_exclusion_retry_ms) >= 0) {
            if (config.input_preference ==
                    SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE &&
                (ns2_status.scanning || ns2_status.connecting ||
                 ns2_status.connected)) {
                if (ble_gatt_disconnect(0U) != 0) {
                    platform_log("ble",
                                 "failed to stop NS2 transport for DS5 preference");
                }
                input_exclusion_retry_ms = now_ms + 250U;
            } else if (config.input_preference ==
                           SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO &&
                       (ds5_status.connected || ds5_status.connecting)) {
                int disconnect_result = ds5_classic_disconnect();

                if (disconnect_result != DS5_CLASSIC_OK &&
                    disconnect_result != DS5_CLASSIC_ERROR_NOT_CONNECTED) {
                    platform_log("bt",
                                 "failed to stop DS5 transport for NS2 preference result=%d",
                                 disconnect_result);
                }
                input_exclusion_retry_ms = now_ms + 250U;
            }
        }
#endif
        if (config.input_preference ==
                SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE &&
            !ns2_status.scanning && !ns2_status.connecting &&
            !ns2_status.connected) {
            ble_started = 0U;
        } else if (config.input_preference ==
                       SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO &&
                   !ds5_status.connected && !ds5_status.connecting &&
                   !ds5_status.disconnecting && !ds5_status.discovering &&
                   !ds5_status.pairing &&
                   ds5_status.state != DS5_CLASSIC_STATE_PAIRING) {
            ds5_started = 0U;
        }
#if defined(SF32LB52_APP_HAS_RTTHREAD)
        bridge_process_pending_input_action(&ds5_status, &ns2_status, now_ms);
#endif

        if (ds5_status.saved_address_valid &&
            (!config.ds5_address_valid ||
             memcmp(config.ds5_address,
                    ds5_status.saved_address,
                    sizeof(config.ds5_address)) != 0)) {
            /* Persist a successful pairing as soon as the Classic security
             * manager publishes it. Waiting for both HID PSMs to connect can
             * lose the new target if L2CAP fails or power is removed. */
            if (sf32lb52_bridge_runtime_remember_ds5(
                    ds5_status.saved_address)) {
                (void)sf32lb52_bridge_runtime_save_settings();
            }
        } else if (ds5_status.connected &&
                   ds5_status.active_address_valid) {
            (void)sf32lb52_bridge_runtime_remember_ds5(
                ds5_status.active_address);
        } else if (ds5_was_connected) {
            sf32lb52_bridge_runtime_release_input(
                SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT, now_ms);
        }
        if (ns2_status.connected && ns2_status.last_candidate_valid) {
            (void)sf32lb52_bridge_runtime_remember_ns2(
                ns2_status.last_candidate_addr_type,
                ns2_status.last_candidate_addr);
            if (!ns2_status.target_valid ||
                ns2_status.target_addr_type !=
                    ns2_status.last_candidate_addr_type ||
                memcmp(ns2_status.target_addr,
                       ns2_status.last_candidate_addr,
                       sizeof(ns2_status.target_addr)) != 0) {
                (void)ble_gatt_set_target(
                    ns2_status.last_candidate_addr_type,
                    ns2_status.last_candidate_addr);
            }
        } else if (ns2_was_connected) {
            sf32lb52_bridge_runtime_release_input(
                SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE, now_ms);
        }
        ds5_was_connected = ds5_status.connected;
        ns2_was_connected = ns2_status.connected;

        if (!ds5_started &&
#if defined(SF32LB52_APP_HAS_RTTHREAD)
            pending_input_action == SF32LB52_APP_INPUT_ACTION_NONE &&
#endif
            config.auto_connect &&
            config.input_preference !=
                SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO &&
            (config.input_preference ==
                 SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE ||
             (uint32_t)(now_ms - boot_ms) >=
                 SF32LB52_BLE_START_DELAY_MS) &&
            (int32_t)(now_ms - ds5_start_retry_ms) >= 0 &&
            !ds5_status.connected && !ds5_status.connecting &&
            !ds5_status.disconnecting && !ds5_status.discovering &&
            !ds5_status.pairing &&
            ds5_status.state != DS5_CLASSIC_STATE_PAIRING &&
            !ns2_status.scanning && !ns2_status.connecting &&
            !ns2_status.connected) {
            start_result = ds5_classic_connect_saved();
            if (start_result == DS5_CLASSIC_OK) {
                ds5_started = 1U;
            } else {
                ds5_start_retry_ms = now_ms + 1000U;
            }
        }

        if (!ble_started &&
#if defined(SF32LB52_APP_HAS_RTTHREAD)
            pending_input_action == SF32LB52_APP_INPUT_ACTION_NONE &&
#endif
            config.auto_connect &&
            config.input_preference != SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE &&
            (config.input_preference ==
                 SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO ||
             (uint32_t)(now_ms - boot_ms) >=
                  SF32LB52_BLE_START_DELAY_MS) &&
            (int32_t)(now_ms - ble_start_retry_ms) >= 0 &&
            !ds5_status.connected && !ds5_status.connecting &&
            !ds5_status.disconnecting && !ds5_status.discovering &&
            !ds5_status.pairing &&
            ds5_status.state != DS5_CLASSIC_STATE_PAIRING) {
            if (ble_gatt_start_scan() == 0) {
                ble_started = 1U;
            } else {
                ble_start_retry_ms = now_ms + 1000U;
                platform_log("ble", "SiFli BLE scan start failed");
            }
        }

        switch (sf32lb52_bridge_runtime_role()) {
        case SF32LB52_BRIDGE_ROLE_DUALSENSE:
        case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
            report_interval_ms = 1U;
            break;
        case SF32LB52_BRIDGE_ROLE_XBOX_360:
            report_interval_ms = 4U;
            break;
        case SF32LB52_BRIDGE_ROLE_NS2PRO:
        default:
            report_interval_ms = 1000U / ns2_profile_get_report_rate_hz();
            if (report_interval_ms == 0U) {
                report_interval_ms = 1U;
            }
            break;
        }
        if (usb_status.mounted &&
            (uint32_t)(now_ms - last_usb_report_ms) >= report_interval_ms) {
            report_len = bridge_make_input_report(now_ms,
                                                   report,
                                                   sizeof(report));
            if (report_len != 0U) {
                bridge_cache_usb_input(
                    usb_role_from_bridge(sf32lb52_bridge_runtime_role()),
                    report,
                    report_len);
            }
            if (report_len != 0U &&
                sf32lb52_usb_send_input_report(report, report_len)) {
                last_usb_report_ms = now_ms;
                if (sf32lb52_bridge_runtime_role() ==
                    SF32LB52_BRIDGE_ROLE_NS2PRO) {
                    ns2_profile_mark_usb_report_sent();
                }
            }
        }

        platform_sleep_ms(1U);
    }
#endif
}
