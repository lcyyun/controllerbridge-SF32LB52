#include "ble_gatt.h"
#include "ds5_classic.h"
#include "ns2_profile.h"
#include "platform.h"
#include "sf32lb52_bridge_runtime.h"
#include "sf32lb52_app.h"
#include "sf32lb52_manager_protocol.h"
#include "sf32lb52_ns2_protocol.h"
#include "sf32lb52_usb_device.h"

#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("bf0_sibles_nvds.h")
#include "bf0_sibles_nvds.h"
#define SF32LB52_NS2_HAS_NVDS 1
#endif
#endif

#define HD_SCALE_DEFAULT_PERCENT 60U
#define HD_SCALE_MIN_PERCENT 5U
#define HD_SCALE_MAX_PERCENT 250U
#define HD_HOLD_DEFAULT_MS 140U
#define HD_HOLD_MIN_MS 20U
#define HD_HOLD_MAX_MS 3000U
#define HD_TICK_DEFAULT_MS 30U
#define HD_TICK_MIN_MS 5U
#define HD_TICK_MAX_MS 100U
#define HD_STOP_DEFAULT_PACKETS 3U
#define HD_STOP_MIN_PACKETS 1U
#define HD_STOP_MAX_PACKETS 12U
#define AUDIO_HAPTICS_WATCHDOG_MS 150U
#define AUDIO_HAPTICS_TICK_MS 17U
#define NS2_TUNING_KEY "sf32_ns2_tune_v1"
#define NS2_TUNING_WIRE_SIZE 20U

static uint8_t g_last_report[NS2_USB_REPORT_SIZE];
static uint8_t g_sequence;
static sf32lb52_ns2_feature_reply_t g_feature_replies[2];
static volatile uint8_t g_feature_reply_index;
static sf32lb52_ns2_ble_snapshot_t g_snapshot;
static uint16_t g_report_rate_hz;
static uint8_t g_raw_passthrough;
static uint32_t g_last_sensor_timestamp_us;
static uint8_t g_web_parse_reports = 1U;
static uint16_t g_rumble_target_handle;
static uint32_t g_ds5_test_rumble_until_ms;

static void queue_json(const char *json);
static void queue_settings_json(void);
static uint16_t clamp_report_rate(uint32_t rate_hz);

static uint32_t ns2_tuning_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xffffffffU;
    size_t i;

    for (i = 0U; i < len; ++i) {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static void ns2_tuning_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)(value >> 8U);
}

static uint16_t ns2_tuning_get_u16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8U));
}

static void ns2_tuning_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)((value >> 8U) & 0xffU);
    out[2] = (uint8_t)((value >> 16U) & 0xffU);
    out[3] = (uint8_t)(value >> 24U);
}

static uint32_t ns2_tuning_get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8U) |
           ((uint32_t)in[2] << 16U) | ((uint32_t)in[3] << 24U);
}

static uint32_t next_sensor_timestamp_us(void)
{
    uint32_t timestamp = platform_micros();

    if ((int32_t)(timestamp - g_last_sensor_timestamp_us) <= 0) {
        timestamp = g_last_sensor_timestamp_us + 1U;
    }
    g_last_sensor_timestamp_us = timestamp;
    return timestamp;
}

static void profile_memory_barrier(void)
{
#if defined(__GNUC__)
    __sync_synchronize();
#endif
}

typedef struct {
    uint8_t active;
    uint8_t latched;
    uint32_t until_ms;
    uint32_t next_tick_ms;
    uint8_t left_vibration[5];
    uint8_t right_vibration[5];
    uint8_t packet_id;
    uint8_t stop_packets_pending;
    uint8_t enabled;
    uint16_t scale_percent;
    uint16_t hold_ms;
    uint16_t tick_ms;
    uint8_t stop_packets;
    uint32_t updates;
    uint32_t writes;
    uint32_t stops;
    uint32_t errors;
    uint32_t hid_out_reports;
    uint32_t hid_out_rumble_reports;
    uint32_t hid_out_ignored;
} ns2_rumble_state_t;

static ns2_rumble_state_t g_rumble;

typedef struct {
    uint8_t active;
    uint32_t until_ms;
    uint32_t left_until_ms;
    uint32_t right_until_ms;
    uint8_t left_vibration[5];
    uint8_t right_vibration[5];
    uint32_t updates;
    uint32_t stops;
    uint32_t mixed_ticks;
} ns2_audio_haptics_state_t;

typedef struct {
    uint16_t low_frequency;
    uint16_t low_amplitude;
    uint16_t high_frequency;
    uint16_t high_amplitude;
} ns2_vibration_fields_t;

static ns2_audio_haptics_state_t g_audio_haptics;

static const char *input_kind_name(uint8_t kind)
{
    switch (kind) {
    case 1U:
        return "FD2";
    case 2U:
        return "LEG";
    default:
        return "UNK";
    }
}

static const char *usb_role_name(Sf32lb52UsbRole role)
{
    switch (role) {
    case Sf32lb52UsbRoleXbox360:
        return "xbox";
    case Sf32lb52UsbRoleDualSense:
        return "ds5";
    case Sf32lb52UsbRoleDualSenseEdge:
        return "dse";
    case Sf32lb52UsbRoleNintendo:
        return "ns2pro";
    default:
        return "unknown";
    }
}

static int command_equals(const sf32lb52_ns2_feature_command_t *command, const char *literal)
{
    size_t literal_len;

    if (command == 0 || literal == 0 || command->valid == 0U) {
        return 0;
    }

    literal_len = strlen(literal);
    return command->length == literal_len &&
           memcmp(command->bytes, literal, literal_len) == 0;
}

static int command_has_prefix(const sf32lb52_ns2_feature_command_t *command, const char *prefix)
{
    size_t prefix_len;

    if (command == 0 || prefix == 0 || command->valid == 0U) {
        return 0;
    }

    prefix_len = strlen(prefix);
    return command->length >= prefix_len &&
           memcmp(command->bytes, prefix, prefix_len) == 0;
}

static void command_to_cstr(const sf32lb52_ns2_feature_command_t *command,
                            char *out,
                            size_t out_len)
{
    size_t n;

    if (out == 0 || out_len == 0U) {
        return;
    }
    out[0] = 0;
    if (command == 0 || command->valid == 0U || command->bytes == 0) {
        return;
    }

    n = command->length;
    if (n >= out_len) {
        n = out_len - 1U;
    }
    memcpy(out, command->bytes, n);
    out[n] = 0;
}

static uint32_t clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static const char *skip_spaces(const char *p)
{
    while (p != 0 && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

static int parse_next_uint(const char **cursor, uint32_t *out)
{
    const char *p;
    uint32_t value = 0U;
    int have_digit = 0;

    if (cursor == 0 || *cursor == 0 || out == 0) {
        return 0;
    }

    p = skip_spaces(*cursor);
    while (*p >= '0' && *p <= '9') {
        have_digit = 1;
        value = (value * 10U) + (uint32_t)(*p - '0');
        p++;
    }

    if (!have_digit) {
        return 0;
    }

    *out = value;
    *cursor = p;
    return 1;
}

static int map_switch_amp_to_ble(int value)
{
    int64_t scaled = (int64_t)value * 1023LL * (int64_t)g_rumble.scale_percent;
    int64_t mapped = (scaled + 1450000LL) / 2900000LL;
    return clamp_int((int)mapped, 0, 1023);
}

static void build_ble_vibration_data(uint16_t lf_freq,
                                     uint8_t lf_tone,
                                     uint16_t lf_amp,
                                     uint16_t hf_freq,
                                     uint8_t hf_tone,
                                     uint16_t hf_amp,
                                     uint8_t out[5])
{
    uint64_t value = 0U;
    size_t i;

    value |= (uint64_t)(lf_freq & 0x01ffU);
    value |= (uint64_t)(lf_tone ? 1U : 0U) << 9;
    value |= (uint64_t)(lf_amp & 0x03ffU) << 10;
    value |= (uint64_t)(hf_freq & 0x01ffU) << 20;
    value |= (uint64_t)(hf_tone ? 1U : 0U) << 29;
    value |= (uint64_t)(hf_amp & 0x03ffU) << 30;

    for (i = 0U; i < 5U; i++) {
        out[i] = (uint8_t)((value >> (8U * i)) & 0xffU);
    }
}

static void build_zero_ble_vibration(uint8_t out[5])
{
    build_ble_vibration_data(0x0e1U, 0U, 0U, 0x1e1U, 0U, 0U, out);
}

static void decode_ble_vibration_data(const uint8_t data[5],
                                      ns2_vibration_fields_t *out)
{
    uint64_t value = 0U;
    size_t index;

    for (index = 0U; index < 5U; index++) {
        value |= (uint64_t)data[index] << (8U * index);
    }
    out->low_frequency = (uint16_t)(value & UINT64_C(0x01ff));
    out->low_amplitude = (uint16_t)((value >> 10U) & UINT64_C(0x03ff));
    out->high_frequency = (uint16_t)((value >> 20U) & UINT64_C(0x01ff));
    out->high_amplitude = (uint16_t)((value >> 30U) & UINT64_C(0x03ff));
}

static uint16_t soft_mix_amplitude(uint16_t first, uint16_t second)
{
    uint32_t overlap = ((uint32_t)first * (uint32_t)second + 511U) / 1023U;
    uint32_t mixed = (uint32_t)first + (uint32_t)second - overlap;

    return (uint16_t)(mixed > 1023U ? 1023U : mixed);
}

static void mix_ble_vibration_data(const uint8_t ordinary[5],
                                   const uint8_t audio[5],
                                   uint8_t out[5])
{
    ns2_vibration_fields_t base;
    ns2_vibration_fields_t overlay;

    decode_ble_vibration_data(ordinary, &base);
    decode_ble_vibration_data(audio, &overlay);
    build_ble_vibration_data(
        base.low_amplitude != 0U ? base.low_frequency : overlay.low_frequency,
        0U,
        soft_mix_amplitude(base.low_amplitude, overlay.low_amplitude),
        base.high_amplitude != 0U ? base.high_frequency : overlay.high_frequency,
        0U,
        soft_mix_amplitude(base.high_amplitude, overlay.high_amplitude),
        out);
}

static void encode_ble_vibration_from_switch_frame(const uint8_t *report,
                                                   uint16_t len,
                                                   uint16_t offset,
                                                   uint8_t out[5])
{
    int b0;
    int b1;
    int b2;
    int b3;
    int b4;
    int high_freq;
    int high_amp;
    int low_freq;
    int low_amp;

    if (report == 0 || len < (uint16_t)(offset + 5U)) {
        build_zero_ble_vibration(out);
        return;
    }

    b0 = report[offset];
    b1 = report[offset + 1U];
    b2 = report[offset + 2U];
    b3 = report[offset + 3U];
    b4 = report[offset + 4U];

    high_freq = b0 | ((b1 & 0x03) << 8);
    high_amp = ((b1 & 0xfc) << 4) | ((b2 & 0x0f) << 12);
    low_freq = ((b2 & 0xf0) >> 4) | ((b3 & 0x3f) << 4);
    low_amp = (b3 & 0xc0) | (b4 << 8);

    build_ble_vibration_data((uint16_t)low_freq,
                             0U,
                             (uint16_t)map_switch_amp_to_ble(low_amp),
                             (uint16_t)high_freq,
                             0U,
                             (uint16_t)map_switch_amp_to_ble(high_amp),
                             out);
}

static void write_motor_block(uint8_t *out,
                              uint16_t offset,
                              uint8_t packet_id,
                              const uint8_t first[5],
                              const uint8_t zero[5])
{
    out[offset] = (uint8_t)(0x50U | (packet_id & 0x0fU));
    memcpy(out + offset + 1U, first, 5U);
    memcpy(out + offset + 6U, zero, 5U);
    memcpy(out + offset + 11U, zero, 5U);
}

static void build_pro2_hd_packet(uint8_t packet_id,
                                 const uint8_t left[5],
                                 const uint8_t right[5],
                                 uint8_t out[33])
{
    uint8_t zero[5];

    build_zero_ble_vibration(zero);
    memset(out, 0, 33U);
    out[0] = 0x00U;
    write_motor_block(out, 1U, packet_id, left, zero);
    write_motor_block(out, 17U, packet_id, right, zero);
}

static int has_non_zero_payload(const uint8_t *data, uint16_t len, uint16_t offset)
{
    uint16_t i;

    if (data == 0 || offset >= len) {
        return 0;
    }

    for (i = offset; i < len; i++) {
        if (data[i] != 0U) {
            return 1;
        }
    }
    return 0;
}

static int has_neutral_rumble_frame(const uint8_t *data, uint16_t len, uint16_t offset)
{
    return data != 0 &&
           len >= (uint16_t)(offset + 5U) &&
           data[offset] == 0x87U &&
           data[offset + 1U] == 0x01U &&
           data[offset + 2U] == 0x20U &&
           data[offset + 3U] == 0x11U &&
           data[offset + 4U] == 0x00U;
}

static int is_neutral_switch_rumble(const uint8_t *data, uint16_t len)
{
    return has_neutral_rumble_frame(data, len, 2U) &&
           has_neutral_rumble_frame(data, len, 0x12U);
}

static int is_switch2_hid_rumble_report(const uint8_t *data, uint16_t len)
{
    return data != 0 &&
           len >= 7U &&
           data[0] == NS2_USB_NINTENDO_OUTPUT_REPORT_ID &&
           (data[1] & 0xf0U) == 0x50U;
}

static void stop_regular_rumble(void)
{
    build_zero_ble_vibration(g_rumble.left_vibration);
    build_zero_ble_vibration(g_rumble.right_vibration);
    g_rumble.until_ms = 0U;
    g_rumble.active = 0U;
    g_rumble.latched = 0U;
    g_rumble.stop_packets_pending = g_rumble.stop_packets;
    g_rumble.stops++;
    g_rumble.next_tick_ms = 0U;
}

static void stop_audio_haptics(void)
{
    if (g_audio_haptics.active != 0U) {
        g_audio_haptics.stops++;
    }
    g_audio_haptics.active = 0U;
    g_audio_haptics.until_ms = 0U;
    g_audio_haptics.left_until_ms = 0U;
    g_audio_haptics.right_until_ms = 0U;
    build_zero_ble_vibration(g_audio_haptics.left_vibration);
    build_zero_ble_vibration(g_audio_haptics.right_vibration);
    g_rumble.stop_packets_pending = g_rumble.stop_packets;
    g_rumble.next_tick_ms = 0U;
}

static void stop_hd_rumble(void)
{
    stop_regular_rumble();
    stop_audio_haptics();
}

static void update_hd_rumble_stream(const uint8_t left[5],
                                    const uint8_t right[5],
                                    uint32_t hold_ms,
                                    uint8_t latched)
{
    uint32_t now;

    if (!g_rumble.enabled) {
        stop_hd_rumble();
        return;
    }

    now = platform_millis();
    memcpy(g_rumble.left_vibration, left, 5U);
    memcpy(g_rumble.right_vibration, right, 5U);
    g_rumble.until_ms = latched ? 0U : now + hold_ms;
    g_rumble.active = 1U;
    g_rumble.latched = latched ? 1U : 0U;
    g_rumble.updates++;
    g_rumble.next_tick_ms = 0U;
}

static void bridge_hid_output_to_ble(const uint8_t *data, uint16_t len)
{
    uint8_t left[5];
    uint8_t right[5];
    int active;

    if (data == 0 || len < 2U) {
        return;
    }

    g_rumble.hid_out_reports++;
    if (!is_switch2_hid_rumble_report(data, len)) {
        g_rumble.hid_out_ignored++;
        return;
    }

    g_rumble.hid_out_rumble_reports++;
    active = has_non_zero_payload(data, len, 2U) &&
             !is_neutral_switch_rumble(data, len);
    if (active) {
        encode_ble_vibration_from_switch_frame(data, len, 2U, left);
        encode_ble_vibration_from_switch_frame(data, len, 0x12U, right);
        update_hd_rumble_stream(left, right, g_rumble.hold_ms, 0U);
    } else {
        stop_hd_rumble();
    }
}

static void hd_rumble_task(void)
{
    uint32_t now = platform_millis();
    uint8_t left[5];
    uint8_t right[5];
    uint8_t send_stop = 0U;
    uint8_t regular_active;
    uint8_t audio_active;
    uint8_t audio_left_active;
    uint8_t audio_right_active;
    uint8_t packet[33];
    uint16_t output_tick_ms;
    uint16_t target_handle;
    ble_gatt_status_t ble_status;
    sf32lb52_bridge_runtime_status_t bridge_status;
    Sf32lb52UsbDeviceStatus usb_status;

    if (g_rumble.next_tick_ms != 0U &&
        (int32_t)(now - g_rumble.next_tick_ms) < 0) {
        return;
    }

    memset(&ble_status, 0, sizeof(ble_status));
    memset(&bridge_status, 0, sizeof(bridge_status));
    memset(&usb_status, 0, sizeof(usb_status));
    ble_gatt_get_status(&ble_status);
    sf32lb52_bridge_runtime_get_status(&bridge_status);
    sf32lb52_usb_get_status(&usb_status);

    regular_active = (uint8_t)(g_rumble.active &&
                               (g_rumble.latched ||
                                (int32_t)(g_rumble.until_ms - now) >= 0));
    if (regular_active && g_rumble.latched &&
        (!ble_status.connected || !usb_status.mounted || usb_status.suspended ||
         bridge_status.active_input !=
             SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE)) {
        regular_active = 0U;
    }

    if (g_rumble.active && !regular_active) {
        g_rumble.active = 0U;
        g_rumble.latched = 0U;
        g_rumble.stop_packets_pending = g_rumble.stop_packets;
        g_rumble.stops++;
    }

    audio_active = (uint8_t)(
        g_audio_haptics.active &&
        (int32_t)(g_audio_haptics.until_ms - now) >= 0 &&
        ble_status.connected && usb_status.mounted && !usb_status.suspended &&
        usb_status.audio_speaker_open &&
        bridge_status.active_input ==
            SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE);
    if (g_audio_haptics.active && !audio_active) {
        stop_audio_haptics();
    }
    audio_left_active = (uint8_t)(audio_active &&
        (int32_t)(g_audio_haptics.left_until_ms - now) >= 0);
    audio_right_active = (uint8_t)(audio_active &&
        (int32_t)(g_audio_haptics.right_until_ms - now) >= 0);
    output_tick_ms = g_rumble.tick_ms;
    if ((audio_left_active || audio_right_active) &&
        output_tick_ms > AUDIO_HAPTICS_TICK_MS) {
        output_tick_ms = AUDIO_HAPTICS_TICK_MS;
    }

    if (regular_active || audio_left_active || audio_right_active) {
        build_zero_ble_vibration(left);
        build_zero_ble_vibration(right);
        if (regular_active) {
            memcpy(left, g_rumble.left_vibration, sizeof(left));
            memcpy(right, g_rumble.right_vibration, sizeof(right));
        }
        if (audio_left_active) {
            if (regular_active) {
                mix_ble_vibration_data(g_rumble.left_vibration,
                                        g_audio_haptics.left_vibration,
                                        left);
            } else {
                memcpy(left, g_audio_haptics.left_vibration, sizeof(left));
            }
        }
        if (audio_right_active) {
            if (regular_active) {
                mix_ble_vibration_data(g_rumble.right_vibration,
                                        g_audio_haptics.right_vibration,
                                        right);
            } else {
                memcpy(right, g_audio_haptics.right_vibration, sizeof(right));
            }
        }
        if (regular_active && (audio_left_active || audio_right_active)) {
            g_audio_haptics.mixed_ticks++;
        }
    } else if (g_rumble.stop_packets_pending > 0U) {
        g_rumble.stop_packets_pending--;
        send_stop = 1U;
        build_zero_ble_vibration(left);
        build_zero_ble_vibration(right);
    } else {
        return;
    }

    build_pro2_hd_packet((uint8_t)(g_rumble.packet_id++ & 0x0fU),
                         left,
                         right,
                         packet);

    target_handle = g_rumble_target_handle != 0U ?
        g_rumble_target_handle : ble_status.rumble_handle;

    if (target_handle != 0U &&
        ble_gatt_write_handle(target_handle, packet, sizeof(packet)) == 0) {
        g_rumble.writes++;
    } else {
        g_rumble.errors++;
    }

    g_rumble.next_tick_ms = now + output_tick_ms;
    if (send_stop && g_rumble.stop_packets_pending == 0U) {
        g_rumble.next_tick_ms = now + output_tick_ms;
    }
}

static void set_rumble_tune(uint32_t scale_percent,
                            uint32_t hold_ms,
                            uint32_t tick_ms,
                            uint32_t stop_packets)
{
    g_rumble.scale_percent =
        (uint16_t)clamp_u32(scale_percent, HD_SCALE_MIN_PERCENT, HD_SCALE_MAX_PERCENT);
    g_rumble.hold_ms =
        (uint16_t)clamp_u32(hold_ms, HD_HOLD_MIN_MS, HD_HOLD_MAX_MS);
    g_rumble.tick_ms =
        (uint16_t)clamp_u32(tick_ms, HD_TICK_MIN_MS, HD_TICK_MAX_MS);
    g_rumble.stop_packets =
        (uint8_t)clamp_u32(stop_packets, HD_STOP_MIN_PACKETS, HD_STOP_MAX_PACKETS);
}

static uint16_t clamp_report_rate(uint32_t rate_hz)
{
    return (uint16_t)clamp_u32(rate_hz, 60U, 1000U);
}

static int ns2_tuning_save(void)
{
#if defined(SF32LB52_NS2_HAS_NVDS)
    uint8_t wire[NS2_TUNING_WIRE_SIZE];

    memset(wire, 0, sizeof(wire));
    memcpy(wire, "SFT1", 4U);
    wire[4] = 1U;
    wire[5] = g_raw_passthrough ? 1U : 0U;
    wire[6] = g_web_parse_reports ? 1U : 0U;
    wire[7] = g_rumble.enabled ? 1U : 0U;
    ns2_tuning_put_u16(wire + 8U, g_report_rate_hz);
    ns2_tuning_put_u16(wire + 10U, g_rumble.scale_percent);
    ns2_tuning_put_u16(wire + 12U, g_rumble.hold_ms);
    wire[14] = (uint8_t)g_rumble.tick_ms;
    wire[15] = g_rumble.stop_packets;
    ns2_tuning_put_u32(wire + 16U, ns2_tuning_crc32(wire, 16U));
    return sifli_nvds_flash_adaptor_init() == NVDS_OK &&
           sifli_nvds_flash_write(NS2_TUNING_KEY, wire, sizeof(wire)) == NVDS_OK;
#else
    return 0;
#endif
}

static int ns2_tuning_load(void)
{
#if defined(SF32LB52_NS2_HAS_NVDS)
    uint8_t wire[NS2_TUNING_WIRE_SIZE];

    if (sifli_nvds_flash_adaptor_init() != NVDS_OK ||
        sifli_nvds_flash_read(NS2_TUNING_KEY, wire, sizeof(wire)) !=
            sizeof(wire) ||
        memcmp(wire, "SFT1", 4U) != 0 || wire[4] != 1U ||
        ns2_tuning_get_u32(wire + 16U) != ns2_tuning_crc32(wire, 16U)) {
        return 0;
    }

    g_report_rate_hz = clamp_report_rate(ns2_tuning_get_u16(wire + 8U));
    g_raw_passthrough = wire[5] ? 1U : 0U;
    g_web_parse_reports = wire[6] ? 1U : 0U;
    g_rumble.enabled = wire[7] ? 1U : 0U;
    set_rumble_tune(ns2_tuning_get_u16(wire + 10U),
                    ns2_tuning_get_u16(wire + 12U),
                    wire[14], wire[15]);
    return 1;
#else
    return 0;
#endif
}

static void format_usb_config_json(char *out, size_t out_len)
{
    uint32_t interval_us = 1000000UL / (g_report_rate_hz == 0U ? 250U : g_report_rate_hz);

    snprintf(out,
             out_len,
             "{\"ok\":true,\"profile\":\"ns2\",\"report_rate_hz\":%u,"
             "\"report_interval_us\":%lu}",
             (unsigned int)g_report_rate_hz,
             (unsigned long)interval_us);
}

static void handle_usb_command(const char *command)
{
    char json[384];

    if (strcmp(command, "usb config") == 0 ||
        strcmp(command, "report config") == 0) {
        format_usb_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (strcmp(command, "usb raw on") == 0) {
        g_raw_passthrough = 1U;
        queue_settings_json();
        return;
    }
    if (strcmp(command, "usb raw off") == 0) {
        g_raw_passthrough = 0U;
        queue_settings_json();
        return;
    }
    if (memcmp(command, "usb rate", strlen("usb rate")) == 0 ||
        memcmp(command, "report rate", strlen("report rate")) == 0) {
        const char *cursor = memcmp(command, "usb rate", strlen("usb rate")) == 0 ?
            command + strlen("usb rate") :
            command + strlen("report rate");
        uint32_t rate = 0U;
        if (!parse_next_uint(&cursor, &rate)) {
            queue_json("{\"ok\":false,\"error\":\"usage: usb rate hz\"}");
            return;
        }
        g_report_rate_hz = clamp_report_rate(rate);
        format_usb_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }

    queue_json("{\"ok\":false,\"error\":\"unknown_usb_command\"}");
}

static void handle_settings_command(const char *command)
{
    if (strcmp(command, "settings") == 0 ||
        strcmp(command, "settings status") == 0 ||
        strcmp(command, "config") == 0 ||
        strcmp(command, "config status") == 0) {
        queue_settings_json();
        return;
    }
    if (strcmp(command, "settings save") == 0 ||
        strcmp(command, "config save") == 0) {
        char json[384];
        int bridge_saved = sf32lb52_bridge_runtime_save_settings() ? 1 : 0;
        int tuning_saved = ns2_tuning_save();
        int saved = bridge_saved && tuning_saved;

        snprintf(json,
                 sizeof(json),
                 "{\"ok\":%s,\"profile\":\"bridge\",\"saved\":%s,"
                 "\"volatile\":false,\"profile_tuning_volatile\":false,"
                 "\"report_rate_hz\":%u,\"usb_raw_passthrough\":%s,"
                 "\"web_parse_reports\":%s,\"rumble_enabled\":%s,"
                 "\"rumble_scale_percent\":%u,\"rumble_hold_ms\":%u,"
                 "\"rumble_tick_ms\":%u,\"rumble_stop_packets\":%u}",
                 saved ? "true" : "false",
                 saved ? "true" : "false",
                 (unsigned int)g_report_rate_hz,
                 g_raw_passthrough ? "true" : "false",
                 g_web_parse_reports ? "true" : "false",
                 g_rumble.enabled ? "true" : "false",
                 (unsigned int)g_rumble.scale_percent,
                 (unsigned int)g_rumble.hold_ms,
                 (unsigned int)g_rumble.tick_ms,
                 (unsigned int)g_rumble.stop_packets);
        queue_json(json);
        return;
    }
    if (strcmp(command, "web parse on") == 0 ||
        strcmp(command, "webui parse on") == 0) {
        g_web_parse_reports = 1U;
        queue_settings_json();
        return;
    }
    if (strcmp(command, "web parse off") == 0 ||
        strcmp(command, "webui parse off") == 0) {
        g_web_parse_reports = 0U;
        queue_settings_json();
        return;
    }

    queue_json("{\"ok\":false,\"error\":\"unknown_settings_command\"}");
}

static void queue_bridge_status_json(void)
{
    char json[2048];
    sf32lb52_bridge_runtime_status_t bridge;
    sf32lb52_bridge_persisted_config_t config;
    sf32lb52_bridge_settings_status_t settings;
    ds5_classic_status_t ds5;
    ble_gatt_status_t ns2;
    Sf32lb52UsbDeviceStatus usb;
    Sf32lb52UsbRoleCapabilities usb_caps;
    uint32_t output_reports = 0U;
    uint32_t output_queued = 0U;
    uint32_t output_busy = 0U;
    uint32_t output_failures = 0U;

    memset(&bridge, 0, sizeof(bridge));
    memset(&config, 0, sizeof(config));
    memset(&settings, 0, sizeof(settings));
    memset(&ds5, 0, sizeof(ds5));
    memset(&ns2, 0, sizeof(ns2));
    memset(&usb, 0, sizeof(usb));
    memset(&usb_caps, 0, sizeof(usb_caps));
    sf32lb52_bridge_runtime_get_status(&bridge);
    sf32lb52_bridge_runtime_get_config(&config);
    sf32lb52_bridge_settings_get_status(&settings);
    ds5_classic_get_status(&ds5);
    ble_gatt_get_status(&ns2);
    sf32lb52_usb_get_status(&usb);
    (void)sf32lb52_usb_get_role_capabilities(sf32lb52_usb_get_role(), &usb_caps);
    if (bridge.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT) {
        output_reports = ds5.output_reports;
        output_queued = ds5.output_queued;
        output_busy = ds5.output_busy;
        output_failures = ds5.output_failures;
    } else if (bridge.active_input ==
               SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE) {
        output_reports = g_rumble.writes;
        output_failures = g_rumble.errors;
    }

    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"profile\":\"%s\",\"manager\":\"bridge\","
             "\"output_backend\":\"%s\",\"output_reports\":%lu,"
             "\"output_queued\":%lu,\"output_busy\":%lu,"
             "\"output_failures\":%lu,"
             "\"role\":\"%s\",\"usb_role\":\"%s\","
             "\"usb_pending_role\":\"%s\",\"role_switch_pending\":%s,"
             "\"input_preference\":\"%s\",\"auto_connect\":%s,"
             "\"active_input\":\"%s\",\"input_valid\":%s,"
             "\"input_stale\":%s,\"imu_exposed\":%s,"
             "\"audio_capable\":%s,\"single_active\":true,"
             "\"accepted_reports\":%lu,\"ignored_reports\":%lu,"
             "\"source_switches\":%lu,\"role_switches\":%lu,"
             "\"usb_role_switches\":%lu,\"usb_role_switch_failures\":%lu,"
             "\"usb_output_reports\":%lu,"
             "\"feedback_forwarded\":%lu,\"feedback_failed\":%lu,"
             "\"settings_loaded\":%s,\"settings_dirty\":%s,"
             "\"settings_generation\":%u,"
             "\"ds5_saved\":%s,\"ds5_connected\":%s,"
             "\"ds5_pairing\":%s,\"ds5_inputs\":%lu,"
             "\"ds5_output_reports\":%lu,\"ds5_output_queued\":%lu,"
             "\"ds5_output_busy\":%lu,\"ds5_output_failures\":%lu,"
             "\"ds5_output_pending\":%s,"
             "\"ds5_audio_inputs\":%lu,\"ds5_audio_input_bytes\":%lu,"
             "\"ds5_reg_failures\":%lu,\"ds5_key_missing\":%lu,"
             "\"ds5_primer_reports\":%lu,\"ds5_feature_requests\":%lu,"
             "\"ds5_feature_responses\":%lu,\"ds5_feature_hits\":%lu,"
             "\"ds5_feature_misses\":%lu,\"ds5_feature_failures\":%lu,"
             "\"ds5_edge_known\":%s,\"ds5_is_edge\":%s,"
             "\"dse_unlock_phase\":%u,\"dse_profiles_ready\":%s,"
             "\"dse_unlocks\":%lu,\"dse_profile_reports\":%lu,"
             "\"ns2_saved\":%s,\"ns2_connected\":%s,"
             "\"ns2_scanning\":%s,\"ns2_notifications\":%lu}",
             sf32lb52_manager_profile_name(bridge.active_input),
             sf32lb52_manager_output_backend_name(bridge.active_input),
             (unsigned long)output_reports,
             (unsigned long)output_queued,
             (unsigned long)output_busy,
             (unsigned long)output_failures,
             sf32lb52_bridge_role_name(bridge.output_role),
             usb_role_name(usb.active_role),
             usb_role_name(usb.pending_role),
             usb.role_switch_pending ? "true" : "false",
             sf32lb52_bridge_preference_name(bridge.input_preference),
             config.auto_connect ? "true" : "false",
             sf32lb52_bridge_source_name(bridge.active_input),
             bridge.input_valid ? "true" : "false",
             bridge.input_stale ? "true" : "false",
             bridge.output_role == SF32LB52_BRIDGE_ROLE_XBOX_360 ?
                 "false" : "true",
             usb_caps.audio_capable ? "true" : "false",
             (unsigned long)bridge.accepted_reports,
             (unsigned long)bridge.ignored_reports,
             (unsigned long)bridge.source_switches,
             (unsigned long)bridge.role_switches,
             (unsigned long)usb.role_switches,
             (unsigned long)usb.role_switch_failures,
             (unsigned long)bridge.usb_output_reports,
             (unsigned long)bridge.feedback_forwarded,
             (unsigned long)bridge.feedback_failed,
             settings.loaded ? "true" : "false",
             bridge.settings_dirty ? "true" : "false",
             (unsigned int)config.generation,
             (ds5.saved_address_valid || config.ds5_address_valid) ?
                 "true" : "false",
             ds5.connected ? "true" : "false",
             ds5.pairing ? "true" : "false",
             (unsigned long)ds5.input_reports,
             (unsigned long)ds5.output_reports,
             (unsigned long)ds5.output_queued,
             (unsigned long)ds5.output_busy,
             (unsigned long)ds5.output_failures,
             ds5.output_pending ? "true" : "false",
             (unsigned long)ds5.audio_input_reports,
             (unsigned long)ds5.audio_input_bytes,
             (unsigned long)ds5.registration_failures,
             (unsigned long)ds5.key_missing_events,
             (unsigned long)ds5.primer_reports,
             (unsigned long)ds5.feature_requests,
             (unsigned long)ds5.feature_responses,
             (unsigned long)ds5.feature_cache_hits,
             (unsigned long)ds5.feature_cache_misses,
             (unsigned long)ds5.feature_failures,
             ds5.edge_known ? "true" : "false",
             ds5.is_edge ? "true" : "false",
             (unsigned int)ds5.dse_unlock_phase,
             ds5.dse_profiles_ready ? "true" : "false",
             (unsigned long)ds5.dse_unlocks,
             (unsigned long)ds5.dse_profile_reports,
             config.ns2_address_valid ? "true" : "false",
             ns2.connected ? "true" : "false",
             ns2.scanning ? "true" : "false",
             (unsigned long)ns2.notifications);
    queue_json(json);
}

static void queue_bridge_input_json(void)
{
    char json[640];
    sf32lb52_bridge_runtime_status_t status;
    sf32lb52_bridge_input_state_t input;
    int snapshot_ok;

    memset(&status, 0, sizeof(status));
    memset(&input, 0, sizeof(input));
    sf32lb52_bridge_runtime_get_status(&status);
    snapshot_ok = sf32lb52_bridge_runtime_get_input_state(&input) ? 1 : 0;
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"profile\":\"bridge_input\"," 
             "\"snapshot_ok\":%s,\"source\":\"%s\",\"valid\":%s,"
             "\"stale\":%s,\"buttons\":%lu,"
             "\"left_x\":%d,\"left_y\":%d,\"right_x\":%d,\"right_y\":%d,"
             "\"left_trigger\":%u,\"right_trigger\":%u,"
             "\"motion_valid\":%s,\"accel_x\":%d,\"accel_y\":%d,\"accel_z\":%d,"
             "\"gyro_x\":%d,\"gyro_y\":%d,\"gyro_z\":%d,"
             "\"timestamp_valid\":%s,\"sensor_timestamp\":%lu,"
             "\"battery_valid\":%s,\"battery_percent\":%u}",
             snapshot_ok ? "true" : "false",
             sf32lb52_bridge_source_name(input.source),
             input.valid && status.input_valid ? "true" : "false",
             status.input_stale ? "true" : "false",
             (unsigned long)input.buttons,
             (int)input.left_x,
             (int)input.left_y,
             (int)input.right_x,
             (int)input.right_y,
             (unsigned int)input.left_trigger,
             (unsigned int)input.right_trigger,
             input.motion_valid ? "true" : "false",
             (int)input.accel[0],
             (int)input.accel[1],
             (int)input.accel[2],
             (int)input.gyro[0],
             (int)input.gyro[1],
             (int)input.gyro[2],
             input.timestamp_valid ? "true" : "false",
             (unsigned long)input.sensor_timestamp,
             input.battery_valid ? "true" : "false",
             (unsigned int)input.battery_percent);
    queue_json(json);
}

static void queue_bridge_operation(const char *operation, int result)
{
    char json[256];

    snprintf(json,
             sizeof(json),
             "{\"ok\":%s,\"operation\":\"%s\",\"result\":%d,"
             "\"role\":\"%s\"}",
             result == 0 ? "true" : "false",
             operation ? operation : "unknown",
             result,
             sf32lb52_bridge_role_name(sf32lb52_bridge_runtime_role()));
    queue_json(json);
}

static void queue_mapping_json(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output)
{
    sf32lb52_bridge_mapping_config_t mapping;
    bool dirty;
    char json[SF32LB52_NS2_FEATURE_REPLY_CAPACITY + 1U];

    if (!sf32lb52_bridge_runtime_get_route_button_mapping(
            profile, output, &mapping) ||
        !sf32lb52_bridge_runtime_get_route_mapping_dirty(
            profile, output, &dirty)) {
        queue_json("{\"ok\":false,\"error\":\"mapping_read_failed\"}");
        return;
    }
    if (sf32lb52_bridge_mapping_format_route_json(
            profile, output, &mapping, dirty,
            json, sizeof(json)) < 0) {
        queue_json("{\"ok\":false,\"error\":\"mapping_reply_failed\"}");
        return;
    }
    queue_json(json);
}

static int handle_mapping_command(const char *text)
{
    sf32lb52_bridge_mapping_command_t command;
    int parsed = sf32lb52_bridge_mapping_parse_command(text, &command);
    bool ok = false;
    bool explicit_output;

    if (parsed == 0) {
        return 0;
    }
    if (parsed < 0) {
        queue_json("{\"ok\":false,\"error\":\"usage: mapping get|reset|save [source [output]]; mapping set [source [output]] target button|none; source/output: ds5|ns2pro\"}");
        return 1;
    }
    explicit_output =
        command.output != SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED;
    if (command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED) {
        command.profile = sf32lb52_bridge_runtime_active_mapping_profile();
    }
    if (!explicit_output) {
        command.output = sf32lb52_bridge_runtime_active_mapping_output();
    }

    switch (command.kind) {
    case SF32LB52_BRIDGE_MAPPING_COMMAND_GET:
        queue_mapping_json(command.profile, command.output);
        return 1;
    case SF32LB52_BRIDGE_MAPPING_COMMAND_SET:
        ok = sf32lb52_bridge_runtime_set_route_button_mapping(
            command.profile, command.output, command.target, command.source);
        break;
    case SF32LB52_BRIDGE_MAPPING_COMMAND_RESET:
        ok = sf32lb52_bridge_runtime_reset_route_button_mapping(
            command.profile, command.output);
        break;
    case SF32LB52_BRIDGE_MAPPING_COMMAND_SAVE:
        ok = explicit_output
            ? sf32lb52_bridge_runtime_save_route_button_mapping(
                command.profile, command.output)
            : sf32lb52_bridge_runtime_save_profile_button_mapping(
                command.profile);
        break;
    case SF32LB52_BRIDGE_MAPPING_COMMAND_NONE:
    default:
        break;
    }
    if (!ok) {
        queue_json("{\"ok\":false,\"error\":\"mapping_operation_failed\"}");
    } else {
        queue_mapping_json(command.profile, command.output);
    }
    return 1;
}

static int handle_bridge_command(const char *command)
{
    sf32lb52_bridge_role_t role;
    sf32lb52_bridge_input_preference_t preference;
    const char *argument;
    int result;

    if (handle_mapping_command(command)) {
        return 1;
    }

    if (strcmp(command, "status") == 0 ||
        strcmp(command, "bridge status") == 0 ||
        strcmp(command, "role") == 0 ||
        strcmp(command, "role status") == 0 ||
        strcmp(command, "input status") == 0) {
        queue_bridge_status_json();
        return 1;
    }
    if (strcmp(command, "bridge input") == 0 ||
        strcmp(command, "input live") == 0 ||
        strcmp(command, "controller status") == 0) {
        queue_bridge_input_json();
        return 1;
    }
    if (strcmp(command, "role next") == 0) {
        result = sf32lb52_bridge_runtime_cycle_role(true) ? 0 : -1;
        queue_bridge_operation("role next", result);
        return 1;
    }
    if (strncmp(command, "role set ", 9U) == 0) {
        argument = skip_spaces(command + 9U);
        result = sf32lb52_bridge_parse_role(argument, &role) &&
                 sf32lb52_bridge_runtime_set_role(role, true) ? 0 : -1;
        queue_bridge_operation("role set", result);
        return 1;
    }
    if (strncmp(command, "input set ", 10U) == 0) {
        argument = skip_spaces(command + 10U);
        result = sf32lb52_bridge_parse_input_preference(argument, &preference) &&
                 sf32lb52_bridge_runtime_set_input_preference(preference, true) ?
                 0 : -1;
        queue_bridge_operation("input set", result);
        return 1;
    }
    if (strcmp(command, "auto connect on") == 0 ||
        strcmp(command, "autoconnect on") == 0) {
        result = sf32lb52_bridge_runtime_set_auto_connect(true, true) ? 0 : -1;
        queue_bridge_operation("auto connect on", result);
        return 1;
    }
    if (strcmp(command, "auto connect off") == 0 ||
        strcmp(command, "autoconnect off") == 0) {
        result = sf32lb52_bridge_runtime_set_auto_connect(false, true) ? 0 : -1;
        queue_bridge_operation("auto connect off", result);
        return 1;
    }
    if (strcmp(command, "pair ds5") == 0) {
        result = sf32lb52_bridge_runtime_set_input_preference(
                     SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE, true) ? 0 : -1;
        if (result == 0) {
            result = sf32lb52_app_request_input_action(
                SF32LB52_APP_INPUT_ACTION_PAIR_DS5);
        }
        queue_bridge_operation("pair ds5", result);
        return 1;
    }
    if (strcmp(command, "connect ds5") == 0) {
        result = sf32lb52_bridge_runtime_set_input_preference(
                     SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE, true) ? 0 : -1;
        if (result == 0) {
            result = sf32lb52_app_request_input_action(
                SF32LB52_APP_INPUT_ACTION_CONNECT_DS5);
        }
        queue_bridge_operation("connect ds5", result);
        return 1;
    }
    if (strcmp(command, "disconnect ds5") == 0) {
        result = sf32lb52_app_request_input_action(
            SF32LB52_APP_INPUT_ACTION_DISCONNECT_DS5);
        queue_bridge_operation("disconnect ds5", result);
        return 1;
    }
    if (strcmp(command, "forget ds5") == 0) {
        result = sf32lb52_app_request_input_action(
            SF32LB52_APP_INPUT_ACTION_FORGET_DS5);
        queue_bridge_operation("forget ds5", result);
        return 1;
    }
    if (strcmp(command, "pair ns2") == 0) {
        result = sf32lb52_bridge_runtime_set_input_preference(
                     SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO, true) ? 0 : -1;
        if (result == 0) {
            result = sf32lb52_app_request_input_action(
                SF32LB52_APP_INPUT_ACTION_PAIR_NS2);
        }
        queue_bridge_operation("pair ns2", result);
        return 1;
    }
    if (strcmp(command, "connect ns2") == 0) {
        result = sf32lb52_bridge_runtime_set_input_preference(
                     SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO, true) ? 0 : -1;
        if (result == 0) {
            result = sf32lb52_app_request_input_action(
                SF32LB52_APP_INPUT_ACTION_CONNECT_NS2);
        }
        queue_bridge_operation("connect ns2", result);
        return 1;
    }
    if (strcmp(command, "disconnect ns2") == 0) {
        result = sf32lb52_app_request_input_action(
            SF32LB52_APP_INPUT_ACTION_DISCONNECT_NS2);
        queue_bridge_operation("disconnect ns2", result);
        return 1;
    }
    if (strcmp(command, "forget ns2") == 0) {
        result = sf32lb52_app_request_input_action(
            SF32LB52_APP_INPUT_ACTION_FORGET_NS2);
        queue_bridge_operation("forget ns2", result);
        return 1;
    }
    return 0;
}

static void format_rumble_config_json(char *out, size_t out_len)
{
    sf32lb52_manager_rumble_status_t diagnostic;
    sf32lb52_bridge_runtime_status_t bridge;
    ds5_classic_status_t ds5;
    ble_gatt_status_t ble_status;

    memset(&diagnostic, 0, sizeof(diagnostic));
    memset(&bridge, 0, sizeof(bridge));
    memset(&ds5, 0, sizeof(ds5));
    memset(&ble_status, 0, sizeof(ble_status));
    sf32lb52_bridge_runtime_get_status(&bridge);
    ds5_classic_get_status(&ds5);
    ble_gatt_get_status(&ble_status);

    diagnostic.role = bridge.output_role;
    diagnostic.source = bridge.active_input;
    diagnostic.enabled = g_rumble.enabled;
    diagnostic.scale_percent = g_rumble.scale_percent;
    diagnostic.hold_ms = g_rumble.hold_ms;
    diagnostic.tick_ms = g_rumble.tick_ms;
    diagnostic.stop_packets = g_rumble.stop_packets;
    if (bridge.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT) {
        diagnostic.active = g_ds5_test_rumble_until_ms != 0U;
        diagnostic.transport_connected = ds5.connected;
        diagnostic.output_pending = ds5.output_pending;
        diagnostic.output_reports = ds5.output_reports;
        diagnostic.output_queued = ds5.output_queued;
        diagnostic.output_busy = ds5.output_busy;
        diagnostic.output_failures = ds5.output_failures;
    } else if (bridge.active_input ==
               SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE) {
        diagnostic.active = g_rumble.active || g_audio_haptics.active;
        diagnostic.latched = g_rumble.latched;
        diagnostic.transport_connected = ble_status.connected;
        diagnostic.output_pending =
            g_rumble.active || g_audio_haptics.active ||
            g_rumble.stop_packets_pending != 0U;
        diagnostic.output_reports = g_rumble.writes;
        diagnostic.output_failures = g_rumble.errors;
        diagnostic.source_updates = g_rumble.updates + g_audio_haptics.updates;
        diagnostic.source_stops = g_rumble.stops + g_audio_haptics.stops;
        diagnostic.source_ignored = g_rumble.hid_out_ignored;
    }
    if (sf32lb52_manager_format_rumble_status(
            &diagnostic, out, out_len) < 0 && out_len != 0U) {
        (void)snprintf(out, out_len,
                       "{\"ok\":false,\"error\":\"reply_too_large\"}");
    }
}

static int start_test_rumble(uint8_t left_on,
                             uint8_t right_on,
                             uint16_t hold_ms,
                             uint16_t amp)
{
    uint8_t left[5];
    uint8_t right[5];
    int scaled_amp;
    uint16_t clamped_amp;
    sf32lb52_bridge_runtime_status_t bridge;

    memset(&bridge, 0, sizeof(bridge));
    sf32lb52_bridge_runtime_get_status(&bridge);
    if (bridge.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT) {
        uint16_t motor = (uint16_t)(((uint32_t)amp * 65535U) / 1023U);
        int result = sf32lb52_app_test_rumble(left_on ? motor : 0U,
                                              right_on ? motor : 0U);
        if (result == 0) {
            g_ds5_test_rumble_until_ms = platform_millis() +
                clamp_u32(hold_ms, HD_HOLD_MIN_MS, HD_HOLD_MAX_MS);
        }
        return result;
    }
    if (bridge.active_input != SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE) {
        return -3;
    }
    {
        ble_gatt_status_t ble;

        memset(&ble, 0, sizeof(ble));
        ble_gatt_get_status(&ble);
        if (!ble.connected || ble.rumble_handle == 0U) {
            return -6;
        }
    }

    build_zero_ble_vibration(left);
    build_zero_ble_vibration(right);

    scaled_amp = (int)amp * (int)g_rumble.scale_percent /
                 (int)HD_SCALE_DEFAULT_PERCENT;
    clamped_amp = (uint16_t)clamp_int(scaled_amp, 0, 1023);
    if (left_on) {
        build_ble_vibration_data(0x0e1U, 0U, clamped_amp,
                                 0x1e1U, 0U, clamped_amp, left);
    }
    if (right_on) {
        build_ble_vibration_data(0x0e1U, 0U, clamped_amp,
                                 0x1e1U, 0U, clamped_amp, right);
    }

    update_hd_rumble_stream(left,
                            right,
                            clamp_u32(hold_ms, HD_HOLD_MIN_MS, HD_HOLD_MAX_MS),
                            0U);
    return 0;
}

static int queue_rumble_test_result(int result, char *json, size_t json_len)
{
    if (result == 0) {
        format_rumble_config_json(json, json_len);
    } else {
        snprintf(json, json_len,
                 "{\"ok\":false,\"error\":\"rumble_target_not_ready\"," 
                 "\"result\":%d}", result);
    }
    queue_json(json);
    return result;
}

static void stop_test_rumble(void)
{
    stop_hd_rumble();
    if (g_ds5_test_rumble_until_ms != 0U) {
        (void)sf32lb52_app_test_rumble(0U, 0U);
        g_ds5_test_rumble_until_ms = 0U;
    }
}

static void handle_rumble_command(const char *command)
{
    char json[512];

    if (strcmp(command, "rumble") == 0 ||
        strcmp(command, "rumble config") == 0) {
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (strcmp(command, "rumble stop") == 0) {
        stop_test_rumble();
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (strcmp(command, "rumble on") == 0) {
        g_rumble.enabled = 1U;
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (strcmp(command, "rumble off") == 0) {
        g_rumble.enabled = 0U;
        stop_hd_rumble();
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (strcmp(command, "rumble handle") == 0 ||
        strcmp(command, "rumble handle auto") == 0) {
        g_rumble_target_handle = 0U;
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (memcmp(command, "rumble handle", strlen("rumble handle")) == 0) {
        const char *cursor = command + strlen("rumble handle");
        uint32_t handle = 0U;
        if (!parse_next_uint(&cursor, &handle) || handle > 0xffffU) {
            queue_json("{\"ok\":false,\"error\":\"usage: rumble handle value|auto\"}");
            return;
        }
        g_rumble_target_handle = (uint16_t)handle;
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (memcmp(command, "rumble tune", strlen("rumble tune")) == 0) {
        const char *cursor = command + strlen("rumble tune");
        uint32_t scale = 0U;
        uint32_t hold = 0U;
        uint32_t tick = 0U;
        uint32_t stops = 0U;
        if (!parse_next_uint(&cursor, &scale) ||
            !parse_next_uint(&cursor, &hold) ||
            !parse_next_uint(&cursor, &tick) ||
            !parse_next_uint(&cursor, &stops)) {
            queue_json("{\"ok\":false,\"error\":\"usage: rumble tune scale hold_ms tick_ms stop_packets\"}");
            return;
        }
        set_rumble_tune(scale, hold, tick, stops);
        format_rumble_config_json(json, sizeof(json));
        queue_json(json);
        return;
    }
    if (memcmp(command, "rumble hold", strlen("rumble hold")) == 0) {
        const char *cursor = command + strlen("rumble hold");
        uint32_t hold = g_rumble.hold_ms;
        (void)parse_next_uint(&cursor, &hold);
        (void)queue_rumble_test_result(
            start_test_rumble(1U, 1U, (uint16_t)hold, 480U),
            json, sizeof(json));
        return;
    }
    if (strcmp(command, "rumble hdtest") == 0 ||
        strcmp(command, "rumble test both") == 0) {
        (void)queue_rumble_test_result(
            start_test_rumble(1U, 1U, 240U, 520U), json, sizeof(json));
        return;
    }
    if (strcmp(command, "rumble test left") == 0) {
        (void)queue_rumble_test_result(
            start_test_rumble(1U, 0U, 240U, 520U), json, sizeof(json));
        return;
    }
    if (strcmp(command, "rumble test right") == 0) {
        (void)queue_rumble_test_result(
            start_test_rumble(0U, 1U, 240U, 520U), json, sizeof(json));
        return;
    }
    if (strcmp(command, "rumble test click") == 0) {
        (void)queue_rumble_test_result(
            start_test_rumble(1U, 1U, 70U, 760U), json, sizeof(json));
        return;
    }

    queue_json("{\"ok\":false,\"error\":\"unknown_rumble_command\"}");
}

static void queue_json(const char *json)
{
    uint8_t next = (uint8_t)(g_feature_reply_index ^ 1U);
    sf32lb52_ns2_feature_reply_t *reply = &g_feature_replies[next];

    if (sf32lb52_ns2_queue_feature_reply(reply, json) != 0) {
        (void)sf32lb52_ns2_queue_feature_reply(reply,
                                               "{\"ok\":false,\"error\":\"reply_too_large\"}");
    }
    profile_memory_barrier();
    g_feature_reply_index = next;
}

static void queue_status_json(void)
{
    char json[2048];
    ble_gatt_status_t ble_status;

    memset(&ble_status, 0, sizeof(ble_status));
    ble_gatt_get_status(&ble_status);

    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"profile\":\"ns2\",\"board\":\"SF32LB52_DEVKIT_NANO\","
             "\"ble\":{\"sdk\":%s,\"powered\":%s,\"scanning\":%s,"
             "\"connecting\":%s,\"connected\":%s,\"conn_idx\":%u,"
             "\"local_addr\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
             "\"connect_attempts\":%lu,\"connect_failures\":%lu,"
             "\"last_connect_status\":%u,\"disconnects\":%lu,"
             "\"last_disconnect_reason\":%u,"
             "\"last_candidate\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
             "\"last_candidate_type\":%u,\"adv_reports\":%lu,"
             "\"ns2_candidates\":%lu,\"notifications\":%lu,"
             "\"writes_ok\":%lu,\"writes_failed\":%lu,\"last_write_ret\":%d,"
             "\"remote_handle\":%u,\"service_start\":%u,\"service_end\":%u,"
             "\"command_handle\":%u,\"rumble_handle\":%u,"
             "\"ack_cccd\":%u,\"input_cccd\":%u,"
             "\"input_notify_count\":%u,\"last_notify_handle\":%u,"
             "\"last_notify_kind\":%u,\"last_write_handle\":%u,\"mtu\":%u,"
             "\"last_rssi\":%d},"
             "\"input\":{\"valid\":%s,\"kind\":\"%s\",\"len\":%u,"
             "\"updates\":%lu,\"parse_errors\":%lu,\"buttons\":%lu,"
             "\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,"
             "\"motion\":%s},"
             "\"rumble\":{\"enabled\":%s,\"active\":%s,"
             "\"updates\":%lu,\"writes\":%lu,\"stops\":%lu,"
              "\"errors\":%lu,\"hid_out\":%lu,\"hid_rumble\":%lu,"
              "\"hid_ignored\":%lu,\"scale_percent\":%u,"
              "\"audio_active\":%s,\"audio_updates\":%lu,"
              "\"audio_stops\":%lu,\"audio_mixed_ticks\":%lu,"
              "\"target_handle\":%u,\"override_handle\":%u,"
             "\"hold_ms\":%u,\"tick_ms\":%u,\"stop_packets\":%u},"
             "\"usb\":\"cherryusb\",\"report_rate_hz\":%u,\"uptime_ms\":%lu}",
             ble_status.sdk_available ? "true" : "false",
             ble_status.powered ? "true" : "false",
             ble_status.scanning ? "true" : "false",
             ble_status.connecting ? "true" : "false",
             ble_status.connected ? "true" : "false",
             (unsigned int)ble_status.conn_idx,
             (unsigned int)ble_status.local_addr[5],
             (unsigned int)ble_status.local_addr[4],
             (unsigned int)ble_status.local_addr[3],
             (unsigned int)ble_status.local_addr[2],
             (unsigned int)ble_status.local_addr[1],
             (unsigned int)ble_status.local_addr[0],
             (unsigned long)ble_status.connect_attempts,
             (unsigned long)ble_status.connect_failures,
             (unsigned int)ble_status.last_connect_status,
             (unsigned long)ble_status.disconnects,
             (unsigned int)ble_status.last_disconnect_reason,
             (unsigned int)ble_status.last_candidate_addr[5],
             (unsigned int)ble_status.last_candidate_addr[4],
             (unsigned int)ble_status.last_candidate_addr[3],
             (unsigned int)ble_status.last_candidate_addr[2],
             (unsigned int)ble_status.last_candidate_addr[1],
             (unsigned int)ble_status.last_candidate_addr[0],
             ble_status.last_candidate_valid ?
                (unsigned int)ble_status.last_candidate_addr_type : 255U,
             (unsigned long)ble_status.adv_reports,
             (unsigned long)ble_status.ns2_candidates,
             (unsigned long)ble_status.notifications,
             (unsigned long)ble_status.writes_ok,
             (unsigned long)ble_status.writes_failed,
             (int)ble_status.last_write_ret,
             (unsigned int)ble_status.remote_handle,
             (unsigned int)ble_status.service_start,
             (unsigned int)ble_status.service_end,
             (unsigned int)ble_status.command_handle,
             (unsigned int)ble_status.rumble_handle,
             (unsigned int)ble_status.ack_cccd_handle,
             (unsigned int)ble_status.input_cccd_handle,
             (unsigned int)ble_status.input_notify_count,
             (unsigned int)ble_status.last_notify_handle,
             (unsigned int)ble_status.last_notify_kind,
             (unsigned int)ble_status.last_write_handle,
             (unsigned int)ble_status.mtu,
             (int)ble_status.last_rssi,
             g_snapshot.valid ? "true" : "false",
             input_kind_name(g_snapshot.kind),
             (unsigned int)g_snapshot.len,
             (unsigned long)g_snapshot.updates,
             (unsigned long)g_snapshot.parse_errors,
             (unsigned long)g_snapshot.buttons,
             (unsigned int)g_snapshot.lx,
             (unsigned int)g_snapshot.ly,
             (unsigned int)g_snapshot.rx,
             (unsigned int)g_snapshot.ry,
             g_snapshot.motion_valid ? "true" : "false",
             g_rumble.enabled ? "true" : "false",
              (g_rumble.active || g_audio_haptics.active) ? "true" : "false",
             (unsigned long)g_rumble.updates,
             (unsigned long)g_rumble.writes,
             (unsigned long)g_rumble.stops,
             (unsigned long)g_rumble.errors,
             (unsigned long)g_rumble.hid_out_reports,
              (unsigned long)g_rumble.hid_out_rumble_reports,
              (unsigned long)g_rumble.hid_out_ignored,
              (unsigned int)g_rumble.scale_percent,
              g_audio_haptics.active ? "true" : "false",
              (unsigned long)g_audio_haptics.updates,
              (unsigned long)g_audio_haptics.stops,
              (unsigned long)g_audio_haptics.mixed_ticks,
             (unsigned int)(g_rumble_target_handle != 0U ?
                g_rumble_target_handle : ble_status.rumble_handle),
             (unsigned int)g_rumble_target_handle,
             (unsigned int)g_rumble.hold_ms,
             (unsigned int)g_rumble.tick_ms,
             (unsigned int)g_rumble.stop_packets,
             (unsigned int)g_report_rate_hz,
             (unsigned long)platform_millis());
    queue_json(json);
}

static void queue_usb_status_json(void)
{
    char json[1664];
    Sf32lb52UsbDeviceStatus status;

    memset(&status, 0, sizeof(status));
    sf32lb52_usb_get_status(&status);
    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"mounted\":%s,\"suspended\":%s,"
             "\"in_sent\":%lu,\"in_failed\":%lu,\"out_reports\":%lu,"
             "\"feature_get\":%lu,\"feature_set\":%lu,"
             "\"feature_drop\":%lu,"
             "\"in_cb\":%lu,\"in_busy\":%s,\"host_ready\":%s,\"in_busy_ms\":%lu,"
             "\"last_in_nbytes\":%lu,"
             "\"audio_speaker_open\":%s,\"audio_mic_open\":%s,"
             "\"audio_out_packets\":%lu,\"audio_out_bytes\":%lu,"
             "\"audio_out_errors\":%lu,\"audio_in_packets\":%lu,"
             "\"audio_in_bytes\":%lu,\"audio_in_errors\":%lu,"
              "\"audio_bt_reports\":%lu,\"audio_bt_dropped\":%lu,"
              "\"audio_opus_errors\":%lu,\"audio_haptic_blocks\":%lu,"
              "\"audio_ns2_active\":%s,\"audio_ns2_updates\":%lu,"
              "\"audio_ns2_stops\":%lu,\"audio_ns2_mixed_ticks\":%lu,"
             "\"audio_mic_bt_packets\":%lu,\"audio_mic_bt_dropped\":%lu,"
             "\"audio_mic_decode_errors\":%lu,\"audio_mic_underflows\":%lu,"
             "\"vendor_out_packets\":%lu,\"vendor_out_bytes\":%lu,"
             "\"vendor_in_packets\":%lu,\"vendor_in_callbacks\":%lu,"
             "\"vendor_in_bytes\":%lu,\"vendor_errors\":%lu,"
             "\"vendor_last_cmd\":%u,\"vendor_last_arg\":%u,"
             "\"rumble_enabled\":%s,\"rumble_active\":%s,"
             "\"rumble_updates\":%lu,\"rumble_writes\":%lu,"
             "\"rumble_stops\":%lu,\"rumble_errors\":%lu,"
             "\"hid_out\":%lu,\"hid_rumble\":%lu,\"hid_ignored\":%lu}",
             status.mounted ? "true" : "false",
             status.suspended ? "true" : "false",
             (unsigned long)status.in_reports_sent,
             (unsigned long)status.in_reports_failed,
             (unsigned long)status.out_reports_received,
             (unsigned long)status.feature_get_reports,
             (unsigned long)status.feature_set_reports,
             (unsigned long)status.feature_set_dropped,
             (unsigned long)status.in_report_callbacks,
             status.in_busy ? "true" : "false",
             status.host_ready ? "true" : "false",
             (unsigned long)status.last_in_busy_ms,
             (unsigned long)status.last_in_nbytes,
             status.audio_speaker_open ? "true" : "false",
             status.audio_mic_open ? "true" : "false",
             (unsigned long)status.audio_out_packets,
             (unsigned long)status.audio_out_bytes,
             (unsigned long)status.audio_out_errors,
             (unsigned long)status.audio_in_packets,
             (unsigned long)status.audio_in_bytes,
             (unsigned long)status.audio_in_errors,
             (unsigned long)status.audio_bt_reports,
              (unsigned long)status.audio_bt_dropped,
              (unsigned long)status.audio_opus_errors,
              (unsigned long)status.audio_haptic_blocks,
              g_audio_haptics.active ? "true" : "false",
              (unsigned long)g_audio_haptics.updates,
              (unsigned long)g_audio_haptics.stops,
              (unsigned long)g_audio_haptics.mixed_ticks,
             (unsigned long)status.audio_mic_bt_packets,
             (unsigned long)status.audio_mic_bt_dropped,
             (unsigned long)status.audio_mic_decode_errors,
             (unsigned long)status.audio_mic_underflows,
             (unsigned long)status.vendor_out_packets,
             (unsigned long)status.vendor_out_bytes,
             (unsigned long)status.vendor_in_packets,
             (unsigned long)status.vendor_in_callbacks,
             (unsigned long)status.vendor_in_bytes,
             (unsigned long)status.vendor_errors,
             (unsigned int)status.vendor_last_command,
             (unsigned int)status.vendor_last_argument,
             g_rumble.enabled ? "true" : "false",
              (g_rumble.active || g_audio_haptics.active) ? "true" : "false",
             (unsigned long)g_rumble.updates,
             (unsigned long)g_rumble.writes,
             (unsigned long)g_rumble.stops,
             (unsigned long)g_rumble.errors,
             (unsigned long)g_rumble.hid_out_reports,
             (unsigned long)g_rumble.hid_out_rumble_reports,
             (unsigned long)g_rumble.hid_out_ignored);
    queue_json(json);
}

static void queue_settings_json(void)
{
    char json[320];

    snprintf(json,
             sizeof(json),
             "{\"ok\":true,\"profile\":\"%s\",\"report_rate_hz\":%u,"
             "\"usb_raw_passthrough\":%s,\"rumble_enabled\":%s,"
             "\"web_parse_reports\":%s,"
             "\"rumble_scale_percent\":%u,\"rumble_hold_ms\":%u,"
             "\"rumble_tick_ms\":%u,\"rumble_stop_packets\":%u}",
             SF32LB52_NS2_DEFAULT_SETTINGS.profile,
             (unsigned int)g_report_rate_hz,
             g_raw_passthrough ? "true" : "false",
             g_rumble.enabled ? "true" : "false",
             g_web_parse_reports ? "true" : "false",
             (unsigned int)g_rumble.scale_percent,
             (unsigned int)g_rumble.hold_ms,
             (unsigned int)g_rumble.tick_ms,
             (unsigned int)g_rumble.stop_packets);
    queue_json(json);
}

void ns2_profile_init(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    memset(&g_rumble, 0, sizeof(g_rumble));
    memset(&g_audio_haptics, 0, sizeof(g_audio_haptics));
    g_report_rate_hz = SF32LB52_NS2_DEFAULT_SETTINGS.report_rate_hz;
    g_raw_passthrough = SF32LB52_NS2_DEFAULT_SETTINGS.raw_passthrough;
    g_last_sensor_timestamp_us = platform_micros();
    g_web_parse_reports = 1U;
    g_rumble_target_handle = 0U;
    g_ds5_test_rumble_until_ms = 0U;
    g_snapshot.lx = 2048U;
    g_snapshot.ly = 2048U;
    g_snapshot.rx = 2048U;
    g_snapshot.ry = 2048U;
    g_rumble.enabled = SF32LB52_NS2_DEFAULT_SETTINGS.rumble_enabled;
    g_rumble.scale_percent = HD_SCALE_DEFAULT_PERCENT;
    g_rumble.hold_ms = HD_HOLD_DEFAULT_MS;
    g_rumble.tick_ms = HD_TICK_DEFAULT_MS;
    g_rumble.stop_packets = HD_STOP_DEFAULT_PACKETS;
    build_zero_ble_vibration(g_rumble.left_vibration);
    build_zero_ble_vibration(g_rumble.right_vibration);
    build_zero_ble_vibration(g_audio_haptics.left_vibration);
    build_zero_ble_vibration(g_audio_haptics.right_vibration);
    sf32lb52_ns2_reset_axis_calibration();
    sf32lb52_ns2_make_neutral_report(g_last_report, g_sequence++,
                                     g_last_sensor_timestamp_us);
    g_feature_reply_index = 0U;
    sf32lb52_ns2_reset_feature_reply(&g_feature_replies[0]);
    sf32lb52_ns2_reset_feature_reply(&g_feature_replies[1]);
    queue_json("{\"ok\":true,\"profile\":\"bridge\",\"status\":\"ready\"}");
}

void ns2_profile_load_tuning(void)
{
    (void)ns2_tuning_load();
}

void ns2_profile_poll(void)
{
    hd_rumble_task();
    if (g_ds5_test_rumble_until_ms != 0U &&
        (int32_t)(platform_millis() - g_ds5_test_rumble_until_ms) >= 0) {
        (void)sf32lb52_app_test_rumble(0U, 0U);
        g_ds5_test_rumble_until_ms = 0U;
    }
}

void ns2_profile_on_ble_input(uint8_t kind, const uint8_t *data, size_t len)
{
    if (sf32lb52_ns2_parse_ble_notify_kind(kind, data, len, &g_snapshot) != 0) {
        return;
    }
}

int ns2_profile_get_snapshot(sf32lb52_ns2_ble_snapshot_t *snapshot)
{
    if (snapshot == 0 || !g_snapshot.valid) {
        return -1;
    }
    *snapshot = g_snapshot;
    return 0;
}

int ns2_profile_apply_rumble(uint16_t left_motor, uint16_t right_motor)
{
    uint8_t left[5];
    uint8_t right[5];
    uint16_t left_amp;
    uint16_t right_amp;

    if (left_motor == 0U && right_motor == 0U) {
        stop_regular_rumble();
        return 0;
    }

    left_amp = (uint16_t)(((uint32_t)left_motor * 1023U + 32767U) / 65535U);
    right_amp = (uint16_t)(((uint32_t)right_motor * 1023U + 32767U) / 65535U);
    left_amp = (uint16_t)clamp_int(
        (int)((uint32_t)left_amp * g_rumble.scale_percent / 100U), 0, 1023);
    right_amp = (uint16_t)clamp_int(
        (int)((uint32_t)right_amp * g_rumble.scale_percent / 100U), 0, 1023);

    build_zero_ble_vibration(left);
    build_zero_ble_vibration(right);
    if (left_amp != 0U) {
        build_ble_vibration_data(0x0e1U, 0U, left_amp,
                                 0x1e1U, 0U, left_amp, left);
    }
    if (right_amp != 0U) {
        build_ble_vibration_data(0x0e1U, 0U, right_amp,
                                 0x1e1U, 0U, right_amp, right);
    }
    /* DS5/XInput motor state is latched until the host sends an explicit zero. */
    update_hd_rumble_stream(left, right, g_rumble.hold_ms, 1U);
    return 0;
}

void ns2_profile_apply_audio_haptics(
    sf32lb52_audio_haptics_event_t event,
    const sf32lb52_audio_haptics_output_t *output)
{
    uint32_t now;
    uint16_t left_low;
    uint16_t left_high;
    uint16_t right_low;
    uint16_t right_high;
    uint8_t any_active;
    uint8_t was_active;

    if (event == SF32LB52_AUDIO_HAPTICS_NO_OUTPUT) {
        return;
    }
    if (event == SF32LB52_AUDIO_HAPTICS_STOP || output == 0 ||
        !g_rumble.enabled) {
        stop_audio_haptics();
        return;
    }

    now = platform_millis();
    left_low = (uint16_t)map_switch_amp_to_ble(output->left.low_amplitude);
    left_high = (uint16_t)map_switch_amp_to_ble(output->left.high_amplitude);
    right_low = (uint16_t)map_switch_amp_to_ble(output->right.low_amplitude);
    right_high = (uint16_t)map_switch_amp_to_ble(output->right.high_amplitude);
    any_active = (uint8_t)(left_low != 0U || left_high != 0U ||
                           right_low != 0U || right_high != 0U);
    if (!any_active) {
        return;
    }

    was_active = (uint8_t)(g_audio_haptics.active &&
        (int32_t)(g_audio_haptics.until_ms - now) >= 0);
    if (left_low != 0U || left_high != 0U) {
        build_ble_vibration_data(output->left.low_frequency, 0U, left_low,
                                 output->left.high_frequency, 0U, left_high,
                                 g_audio_haptics.left_vibration);
        g_audio_haptics.left_until_ms = now + AUDIO_HAPTICS_WATCHDOG_MS;
    }
    if (right_low != 0U || right_high != 0U) {
        build_ble_vibration_data(output->right.low_frequency, 0U, right_low,
                                 output->right.high_frequency, 0U, right_high,
                                 g_audio_haptics.right_vibration);
        g_audio_haptics.right_until_ms = now + AUDIO_HAPTICS_WATCHDOG_MS;
    }
    g_audio_haptics.active = 1U;
    g_audio_haptics.until_ms = now + AUDIO_HAPTICS_WATCHDOG_MS;
    g_audio_haptics.updates++;
    if (!was_active) {
        g_rumble.next_tick_ms = 0U;
    }
}

void ns2_profile_on_usb_output(const uint8_t *data, size_t len)
{
    if (data == 0 || len == 0U) {
        return;
    }

    if (data[0] == NS2_USB_NINTENDO_OUTPUT_REPORT_ID &&
        len > 7U &&
        memcmp(data + 1, SF32LB52_NS2_FEATURE_COMMAND_MAGIC,
               SF32LB52_NS2_FEATURE_MAGIC_SIZE) == 0) {
        ns2_profile_on_feature_set(data + 1, (uint16_t)(len - 1U));
        return;
    }

    if (data[0] == NS2_USB_NINTENDO_OUTPUT_REPORT_ID) {
        bridge_hid_output_to_ble(data, (uint16_t)(len > 0xffffU ? 0xffffU : len));
    }
}

uint16_t ns2_profile_on_feature_get(uint8_t *buffer, uint16_t len)
{
    uint8_t index = g_feature_reply_index;

    profile_memory_barrier();
    return (uint16_t)sf32lb52_ns2_build_feature_reply_chunk(
        &g_feature_replies[index], buffer, len);
}

void ns2_profile_on_feature_set(const uint8_t *data, uint16_t len)
{
    sf32lb52_ns2_feature_command_t command =
        sf32lb52_ns2_parse_feature_command(data, len);
    char bridge_command[80];

    if (command.valid == 0U) {
        queue_json("{\"ok\":false,\"error\":\"bad_magic\"}");
        return;
    }

    command_to_cstr(&command, bridge_command, sizeof(bridge_command));
    if (handle_bridge_command(skip_spaces(bridge_command))) {
        return;
    }

    if (command_equals(&command, "ns2 status")) {
        queue_status_json();
        return;
    }

    if (command_equals(&command, "usb status")) {
        queue_usb_status_json();
        return;
    }

    if (command_has_prefix(&command, "usb ") ||
        command_has_prefix(&command, "report rate") ||
        command_equals(&command, "usb config") ||
        command_equals(&command, "report config")) {
        char text[80];

        command_to_cstr(&command, text, sizeof(text));
        handle_usb_command(skip_spaces(text));
        return;
    }

    if (command_has_prefix(&command, "settings") ||
        command_has_prefix(&command, "config") ||
        command_has_prefix(&command, "web parse") ||
        command_has_prefix(&command, "webui parse")) {
        char text[80];

        command_to_cstr(&command, text, sizeof(text));
        handle_settings_command(skip_spaces(text));
        return;
    }

    if (command_equals(&command, "motion status") ||
        command_equals(&command, "imu status")) {
        char json[640];
        char raw_hex[65];
        uint8_t raw_count;
        uint8_t i;

        raw_count = g_snapshot.raw_len > 32U ? 32U : g_snapshot.raw_len;
        for (i = 0U; i < raw_count; i++) {
            (void)snprintf(raw_hex + (i * 2U),
                           sizeof(raw_hex) - (i * 2U),
                           "%02x",
                           (unsigned int)g_snapshot.raw[i]);
        }
        raw_hex[raw_count * 2U] = 0;

        snprintf(json,
                 sizeof(json),
                 "{\"ok\":true,\"motion_valid\":%s,\"input_kind\":\"%s\","
                 "\"input_len\":%u,\"updates\":%lu,\"parse_errors\":%lu,"
                 "\"valid\":%s,\"buttons\":%lu,"
                 "\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,"
                 "\"motion_raw\":\"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\","
                 "\"raw_len\":%u,\"raw32\":\"%s\"}",
                 g_snapshot.motion_valid ? "true" : "false",
                 input_kind_name(g_snapshot.kind),
                 (unsigned int)g_snapshot.len,
                 (unsigned long)g_snapshot.updates,
                 (unsigned long)g_snapshot.parse_errors,
                 g_snapshot.valid ? "true" : "false",
                 (unsigned long)g_snapshot.buttons,
                 (unsigned int)g_snapshot.lx,
                 (unsigned int)g_snapshot.ly,
                 (unsigned int)g_snapshot.rx,
                 (unsigned int)g_snapshot.ry,
                 (unsigned int)g_snapshot.motion[0],
                 (unsigned int)g_snapshot.motion[1],
                 (unsigned int)g_snapshot.motion[2],
                 (unsigned int)g_snapshot.motion[3],
                 (unsigned int)g_snapshot.motion[4],
                 (unsigned int)g_snapshot.motion[5],
                 (unsigned int)g_snapshot.motion[6],
                 (unsigned int)g_snapshot.motion[7],
                 (unsigned int)g_snapshot.motion[8],
                 (unsigned int)g_snapshot.motion[9],
                 (unsigned int)g_snapshot.motion[10],
                 (unsigned int)g_snapshot.motion[11],
                 (unsigned int)g_snapshot.raw_len,
                 raw_hex);
        queue_json(json);
        return;
    }

    if (command_equals(&command, "stick recalibrate") ||
        command_equals(&command, "axis recalibrate")) {
        g_snapshot.lx = 2048U;
        g_snapshot.ly = 2048U;
        g_snapshot.rx = 2048U;
        g_snapshot.ry = 2048U;
        sf32lb52_ns2_reset_axis_calibration();
        queue_json("{\"ok\":true,\"profile\":\"ns2\",\"axis_calibration\":\"reset\",\"note\":\"keep_sticks_centered\"}");
        return;
    }

    if (command_equals(&command, "rumble") ||
        command_has_prefix(&command, "rumble ")) {
        char text[80];

        command_to_cstr(&command, text, sizeof(text));
        handle_rumble_command(skip_spaces(text));
        return;
    }

    queue_json("{\"ok\":false,\"error\":\"unknown_command\"}");
}

int ns2_profile_make_usb_report(uint8_t report[NS2_USB_REPORT_SIZE])
{
    if (report == 0) {
        return -1;
    }

    (void)sf32lb52_ns2_make_usb_report(g_snapshot.valid ? &g_snapshot : 0,
                                       g_raw_passthrough,
                                       g_last_report,
                                       g_sequence++,
                                       next_sensor_timestamp_us());
    memcpy(report, g_last_report, NS2_USB_REPORT_SIZE);
    return 0;
}

void ns2_profile_stamp_usb_report_timestamp(
    uint8_t report[NS2_USB_REPORT_SIZE])
{
    sf32lb52_ns2_write_report_timestamp(report, next_sensor_timestamp_us());
}

void ns2_profile_mark_usb_report_sent(void)
{
}

uint16_t ns2_profile_get_report_rate_hz(void)
{
    if (g_report_rate_hz == 0U) {
        return SF32LB52_NS2_DEFAULT_SETTINGS.report_rate_hz;
    }
    return g_report_rate_hz;
}
