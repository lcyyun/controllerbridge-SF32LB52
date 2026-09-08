#include "sf32lb52_ns2_protocol.h"

#include <string.h>

const char SF32LB52_NS2_FEATURE_COMMAND_MAGIC[SF32LB52_NS2_FEATURE_MAGIC_SIZE + 1U] = "Y7HID1";
const char SF32LB52_NS2_FEATURE_REPLY_MAGIC[SF32LB52_NS2_FEATURE_MAGIC_SIZE + 1U] = "Y7HRS1";

const sf32lb52_ns2_settings_t SF32LB52_NS2_DEFAULT_SETTINGS = {
    "ns2",
    250U,
    0U,
    1U,
};

enum {
    REPORT_TIMESTAMP_OFFSET = 0x2b,
    REPORT_MOTION_OFFSET = 0x31,
    FD2_FULL_REPORT_MIN_LEN = 60,
    FD2_FULL_MOTION_OFFSET = 48,
    STICK_CENTER_12BIT = 2048,
    AXIS_DEADZONE = 48,
    AXIS_CALIBRATION_SAMPLES = 20,
    AXIS_PHYSICAL_FULL_SCALE = 1600,
    AXIS_STABLE_MAX_DELTA = 16,
};

typedef struct {
    uint8_t calibrated;
    uint32_t sample_count;
    uint32_t sum_lx;
    uint32_t sum_ly;
    uint32_t sum_rx;
    uint32_t sum_ry;
    uint16_t center_lx;
    uint16_t center_ly;
    uint16_t center_rx;
    uint16_t center_ry;
    uint8_t has_last;
    uint16_t last_lx;
    uint16_t last_ly;
    uint16_t last_rx;
    uint16_t last_ry;
} axis_calibration_t;

static axis_calibration_t fd2_axis;
static axis_calibration_t legacy_axis;

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint16_t clamp_12bit(uint16_t value)
{
    return value > 4095U ? 4095U : value;
}

static uint16_t clamp_12bit_i32(int32_t value)
{
    if (value < 0) {
        return 0U;
    }
    if (value > 4095) {
        return 4095U;
    }
    return (uint16_t)value;
}

static void reset_axis(axis_calibration_t *axis)
{
    if (axis == 0) {
        return;
    }

    memset(axis, 0, sizeof(*axis));
    axis->center_lx = STICK_CENTER_12BIT;
    axis->center_ly = STICK_CENTER_12BIT;
    axis->center_rx = STICK_CENTER_12BIT;
    axis->center_ry = STICK_CENTER_12BIT;
}

static int axis_delta_within(uint16_t a, uint16_t b, uint16_t limit)
{
    int32_t delta = (int32_t)a - (int32_t)b;
    if (delta < 0) {
        delta = -delta;
    }
    return delta <= (int32_t)limit;
}

static int axes_stable(axis_calibration_t *axis,
                       uint16_t lx,
                       uint16_t ly,
                       uint16_t rx,
                       uint16_t ry)
{
    int stable;

    if (axis == 0) {
        return 0;
    }
    if (axis->has_last == 0U) {
        axis->has_last = 1U;
        axis->last_lx = lx;
        axis->last_ly = ly;
        axis->last_rx = rx;
        axis->last_ry = ry;
        return 0;
    }

    stable =
        axis_delta_within(lx, axis->last_lx, AXIS_STABLE_MAX_DELTA) &&
        axis_delta_within(ly, axis->last_ly, AXIS_STABLE_MAX_DELTA) &&
        axis_delta_within(rx, axis->last_rx, AXIS_STABLE_MAX_DELTA) &&
        axis_delta_within(ry, axis->last_ry, AXIS_STABLE_MAX_DELTA);

    axis->last_lx = lx;
    axis->last_ly = ly;
    axis->last_rx = rx;
    axis->last_ry = ry;
    return stable;
}

static void reset_axis_learning(axis_calibration_t *axis)
{
    if (axis == 0) {
        return;
    }
    axis->sample_count = 0U;
    axis->sum_lx = 0U;
    axis->sum_ly = 0U;
    axis->sum_rx = 0U;
    axis->sum_ry = 0U;
}

static uint16_t normalize_axis_12bit(uint16_t value, uint16_t center)
{
    int32_t delta = (int32_t)value - (int32_t)center;
    int negative = delta < 0;
    int32_t magnitude = negative ? -delta : delta;
    int32_t usable;
    int32_t target;
    int32_t scaled;

    if (magnitude <= AXIS_DEADZONE ||
        AXIS_PHYSICAL_FULL_SCALE <= AXIS_DEADZONE) {
        return STICK_CENTER_12BIT;
    }

    usable = AXIS_PHYSICAL_FULL_SCALE - AXIS_DEADZONE;
    target = negative ? STICK_CENTER_12BIT : (4095 - STICK_CENTER_12BIT);
    scaled = ((magnitude - AXIS_DEADZONE) * target + usable / 2) / usable;
    if (scaled > target) {
        scaled = target;
    }

    return clamp_12bit_i32(negative ?
                           (int32_t)STICK_CENTER_12BIT - scaled :
                           (int32_t)STICK_CENTER_12BIT + scaled);
}

static void apply_axes(axis_calibration_t *axis,
                       sf32lb52_ns2_ble_snapshot_t *snapshot,
                       uint16_t lx,
                       uint16_t ly,
                       uint16_t rx,
                       uint16_t ry)
{
    int can_learn_center;

    if (axis == 0 || snapshot == 0) {
        return;
    }

    can_learn_center = snapshot->buttons == 0U &&
                       axes_stable(axis, lx, ly, rx, ry);
    if (axis->calibrated == 0U && can_learn_center) {
        axis->sum_lx += lx;
        axis->sum_ly += ly;
        axis->sum_rx += rx;
        axis->sum_ry += ry;
        axis->sample_count++;
        if (axis->sample_count >= AXIS_CALIBRATION_SAMPLES) {
            axis->center_lx = (uint16_t)(axis->sum_lx / axis->sample_count);
            axis->center_ly = (uint16_t)(axis->sum_ly / axis->sample_count);
            axis->center_rx = (uint16_t)(axis->sum_rx / axis->sample_count);
            axis->center_ry = (uint16_t)(axis->sum_ry / axis->sample_count);
            axis->calibrated = 1U;
        }
    } else if (axis->calibrated == 0U) {
        reset_axis_learning(axis);
    }

    /*
     * Do not suppress live input while the controller is learning its centre.
     * Some NS2Pro firmware revisions keep a status bit set or produce enough
     * idle jitter to restart centre learning indefinitely.  The default
     * centres still give useful input immediately; once learning completes the
     * same path below transparently starts using the measured centres.
     */
    snapshot->lx = normalize_axis_12bit(lx, axis->center_lx);
    snapshot->ly = normalize_axis_12bit(ly, axis->center_ly);
    snapshot->rx = normalize_axis_12bit(rx, axis->center_rx);
    snapshot->ry = normalize_axis_12bit(ry, axis->center_ry);
}

static uint16_t unpack12_x(const uint8_t *data, size_t offset)
{
    return clamp_12bit((uint16_t)((uint16_t)data[offset] |
                                  (((uint16_t)data[offset + 1U] & 0x0fU) << 8U)));
}

static uint16_t unpack12_y(const uint8_t *data, size_t offset)
{
    return clamp_12bit((uint16_t)((((uint16_t)data[offset + 1U] >> 4U) & 0x0fU) |
                                  ((uint16_t)data[offset + 2U] << 4U)));
}

static void set_button(uint32_t *buttons, sf32lb52_ns2_button_t button, int pressed)
{
    uint32_t mask;

    if (buttons == 0 || button >= SF32LB52_NS2_BUTTON_COUNT) {
        return;
    }

    mask = 1UL << (uint8_t)button;
    if (pressed) {
        *buttons |= mask;
    } else {
        *buttons &= ~mask;
    }
}

static uint32_t decode_legacy_buttons(uint8_t b2, uint8_t b3, uint8_t b4)
{
    uint32_t buttons = 0;

    set_button(&buttons, SF32LB52_NS2_BUTTON_B, (b2 & 0x01U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_A, (b2 & 0x02U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_Y, (b2 & 0x04U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_X, (b2 & 0x08U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_R, (b2 & 0x10U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_ZR, (b2 & 0x20U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_PLUS, (b2 & 0x40U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_R_STICK, (b2 & 0x80U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_DOWN, (b3 & 0x01U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_RIGHT, (b3 & 0x02U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_LEFT, (b3 & 0x04U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_UP, (b3 & 0x08U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_L, (b3 & 0x10U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_ZL, (b3 & 0x20U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_MINUS, (b3 & 0x40U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_L_STICK, (b3 & 0x80U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_HOME, (b4 & 0x01U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_CAPTURE, (b4 & 0x02U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_GR, (b4 & 0x04U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_GL, (b4 & 0x08U) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_C, (b4 & 0x10U) != 0U);
    return buttons;
}

static uint32_t decode_fd2_buttons(uint32_t raw)
{
    uint32_t buttons = 0;

    set_button(&buttons, SF32LB52_NS2_BUTTON_Y, (raw & 0x00000001UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_X, (raw & 0x00000002UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_B, (raw & 0x00000004UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_A, (raw & 0x00000008UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_R, (raw & 0x00000040UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_ZR, (raw & 0x00000080UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_MINUS, (raw & 0x00000100UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_PLUS, (raw & 0x00000200UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_R_STICK, (raw & 0x00000400UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_L_STICK, (raw & 0x00000800UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_HOME, (raw & 0x00001000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_CAPTURE, (raw & 0x00002000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_C, (raw & 0x00004000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_DOWN, (raw & 0x00010000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_UP, (raw & 0x00020000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_RIGHT, (raw & 0x00040000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_D_LEFT, (raw & 0x00080000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_L, (raw & 0x00400000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_ZL, (raw & 0x00800000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_GR, (raw & 0x01000000UL) != 0U);
    set_button(&buttons, SF32LB52_NS2_BUTTON_GL, (raw & 0x02000000UL) != 0U);
    return buttons;
}

static int button_pressed(const sf32lb52_ns2_ble_snapshot_t *snapshot,
                          sf32lb52_ns2_button_t button)
{
    return snapshot != 0 &&
        button < SF32LB52_NS2_BUTTON_COUNT &&
        (snapshot->buttons & (1UL << button)) != 0U;
}

static void pack12_pair(uint8_t *out, size_t offset, uint16_t x, uint16_t y)
{
    x = clamp_12bit(x);
    y = clamp_12bit(y);
    out[offset] = (uint8_t)(x & 0xffU);
    out[offset + 1U] = (uint8_t)(((x >> 8U) & 0x0fU) | ((y & 0x0fU) << 4U));
    out[offset + 2U] = (uint8_t)((y >> 4U) & 0xffU);
}

static void write_timestamp(uint8_t report[SF32LB52_NS2_REPORT_SIZE], uint32_t timestamp)
{
    report[REPORT_TIMESTAMP_OFFSET] = (uint8_t)(timestamp & 0xffU);
    report[REPORT_TIMESTAMP_OFFSET + 1U] = (uint8_t)((timestamp >> 8U) & 0xffU);
    report[REPORT_TIMESTAMP_OFFSET + 2U] = (uint8_t)((timestamp >> 16U) & 0xffU);
    report[REPORT_TIMESTAMP_OFFSET + 3U] = (uint8_t)((timestamp >> 24U) & 0xffU);
}

void sf32lb52_ns2_reset_feature_reply(sf32lb52_ns2_feature_reply_t *reply)
{
    if (reply == 0) {
        return;
    }

    reply->length = 0U;
    reply->offset = 0U;
    memset(reply->bytes, 0, sizeof(reply->bytes));
}

int sf32lb52_ns2_queue_feature_reply(sf32lb52_ns2_feature_reply_t *reply, const char *json)
{
    size_t len;

    if (reply == 0 || json == 0) {
        return -1;
    }

    len = strlen(json);
    if (len > SF32LB52_NS2_FEATURE_REPLY_CAPACITY) {
        return -1;
    }

    memcpy(reply->bytes, json, len);
    reply->length = (uint16_t)len;
    reply->offset = 0U;
    return 0;
}

size_t sf32lb52_ns2_build_feature_reply_chunk(sf32lb52_ns2_feature_reply_t *reply,
                                              uint8_t *report,
                                              size_t report_len)
{
    uint16_t total;
    uint16_t offset;
    uint16_t remaining;
    size_t chunk_capacity;
    uint16_t chunk_len;

    if (reply == 0 || report == 0 || report_len == 0U) {
        return 0U;
    }

    memset(report, 0, report_len);
    if (report_len < SF32LB52_NS2_FEATURE_PAYLOAD_OFFSET) {
        return report_len;
    }

    memcpy(report, SF32LB52_NS2_FEATURE_REPLY_MAGIC, SF32LB52_NS2_FEATURE_MAGIC_SIZE);

    if (reply->offset > reply->length) {
        reply->offset = reply->length;
    }

    total = reply->length;
    offset = reply->offset;
    remaining = (uint16_t)(total - offset);
    chunk_capacity = report_len - SF32LB52_NS2_FEATURE_PAYLOAD_OFFSET;
    if (chunk_capacity > 255U) {
        chunk_capacity = 255U;
    }
    chunk_len = remaining < chunk_capacity ? remaining : (uint16_t)chunk_capacity;

    report[6] = (uint8_t)(total & 0xffU);
    report[7] = (uint8_t)((total >> 8U) & 0xffU);
    report[8] = (uint8_t)(offset & 0xffU);
    report[9] = (uint8_t)((offset >> 8U) & 0xffU);
    report[10] = (uint8_t)(chunk_len & 0xffU);

    if (chunk_len > 0U) {
        memcpy(report + SF32LB52_NS2_FEATURE_PAYLOAD_OFFSET, reply->bytes + offset, chunk_len);
        reply->offset = (uint16_t)(offset + chunk_len);
    }

    return report_len;
}

sf32lb52_ns2_feature_command_t sf32lb52_ns2_parse_feature_command(const uint8_t *report,
                                                                  size_t report_len)
{
    sf32lb52_ns2_feature_command_t command = {0U, 0, 0U};

    if (report == 0 || report_len == 0U) {
        return command;
    }

    if (report[0] == SF32LB52_NS2_FEATURE_REPORT_ID) {
        report++;
        report_len--;
    }

    if (report_len < SF32LB52_NS2_FEATURE_MAGIC_SIZE ||
        memcmp(report, SF32LB52_NS2_FEATURE_COMMAND_MAGIC, SF32LB52_NS2_FEATURE_MAGIC_SIZE) != 0) {
        return command;
    }

    report += SF32LB52_NS2_FEATURE_MAGIC_SIZE;
    report_len -= SF32LB52_NS2_FEATURE_MAGIC_SIZE;
    while (report_len > 0U && report[report_len - 1U] == 0U) {
        report_len--;
    }

    command.valid = 1U;
    command.bytes = report;
    command.length = (uint16_t)report_len;
    return command;
}

int sf32lb52_ns2_parse_ble_notify_kind(uint8_t kind,
                                       const uint8_t *data,
                                       size_t len,
                                       sf32lb52_ns2_ble_snapshot_t *snapshot)
{
    uint32_t previous_updates;
    uint32_t previous_errors;
    uint16_t lx;
    uint16_t ly;
    uint16_t rx;
    uint16_t ry;
    uint8_t raw_len;

    if (snapshot == 0) {
        return -1;
    }

    previous_updates = snapshot->updates;
    previous_errors = snapshot->parse_errors;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->updates = previous_updates;
    snapshot->parse_errors = previous_errors;
    snapshot->lx = STICK_CENTER_12BIT;
    snapshot->ly = STICK_CENTER_12BIT;
    snapshot->rx = STICK_CENTER_12BIT;
    snapshot->ry = STICK_CENTER_12BIT;

    if (data == 0 || len == 0U) {
        snapshot->parse_errors++;
        return -1;
    }

    raw_len = (uint8_t)(len > sizeof(snapshot->raw) ? sizeof(snapshot->raw) : len);
    snapshot->raw_len = raw_len;
    memcpy(snapshot->raw, data, raw_len);

    if (kind == SF32LB52_NS2_NOTIFY_KIND_UNKNOWN) {
        kind = len >= 16U ?
            SF32LB52_NS2_NOTIFY_KIND_FD2 :
            SF32LB52_NS2_NOTIFY_KIND_LEGACY;
    }

    if (kind == SF32LB52_NS2_NOTIFY_KIND_FD2) {
        if (len < 16U) {
            snapshot->parse_errors++;
            return -1;
        }
        snapshot->kind = SF32LB52_NS2_NOTIFY_KIND_FD2;
        snapshot->buttons = decode_fd2_buttons(read_le32(data + 4U));
        lx = unpack12_x(data, 10U);
        ly = unpack12_y(data, 10U);
        rx = unpack12_x(data, 13U);
        ry = unpack12_y(data, 13U);
        apply_axes(&fd2_axis, snapshot, lx, ly, rx, ry);
        if (len >= FD2_FULL_REPORT_MIN_LEN) {
            snapshot->motion_valid = 1U;
            memcpy(snapshot->motion, data + FD2_FULL_MOTION_OFFSET, sizeof(snapshot->motion));
        }
    } else if (kind == SF32LB52_NS2_NOTIFY_KIND_LEGACY) {
        if (len < 11U) {
            snapshot->parse_errors++;
            return -1;
        }
        snapshot->kind = SF32LB52_NS2_NOTIFY_KIND_LEGACY;
        snapshot->buttons = decode_legacy_buttons(data[2], data[3], data[4]);
        lx = unpack12_x(data, 5U);
        ly = unpack12_y(data, 5U);
        rx = unpack12_x(data, 8U);
        ry = unpack12_y(data, 8U);
        apply_axes(&legacy_axis, snapshot, lx, ly, rx, ry);
    } else {
        snapshot->parse_errors++;
        return -1;
    }

    snapshot->valid = 1U;
    snapshot->len = (uint16_t)(len > 0xffffU ? 0xffffU : len);
    snapshot->updates++;
    return 0;
}

int sf32lb52_ns2_parse_ble_notify(const uint8_t *data,
                                  size_t len,
                                  sf32lb52_ns2_ble_snapshot_t *snapshot)
{
    return sf32lb52_ns2_parse_ble_notify_kind(SF32LB52_NS2_NOTIFY_KIND_UNKNOWN,
                                             data,
                                             len,
                                             snapshot);
}

void sf32lb52_ns2_reset_axis_calibration(void)
{
    reset_axis(&fd2_axis);
    reset_axis(&legacy_axis);
}

void sf32lb52_ns2_make_neutral_report(uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                      uint8_t sequence,
                                      uint32_t timestamp)
{
    memset(report, 0, SF32LB52_NS2_REPORT_SIZE);
    report[0] = SF32LB52_NS2_INPUT_REPORT_ID;
    report[1] = sequence;
    report[2] = 0x20U;
    pack12_pair(report, 11U, STICK_CENTER_12BIT, STICK_CENTER_12BIT);
    pack12_pair(report, 14U, STICK_CENTER_12BIT, STICK_CENTER_12BIT);
    write_timestamp(report, timestamp);
}

void sf32lb52_ns2_make_report_from_snapshot(const sf32lb52_ns2_ble_snapshot_t *snapshot,
                                            uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                            uint8_t sequence,
                                            uint32_t timestamp)
{
    sf32lb52_ns2_make_neutral_report(report, sequence, timestamp);
    if (snapshot == 0 || snapshot->valid == 0U) {
        return;
    }

    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_Y)) report[5] |= 0x01U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_X)) report[5] |= 0x02U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_B)) report[5] |= 0x04U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_A)) report[5] |= 0x08U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_R)) report[5] |= 0x40U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_ZR)) report[5] |= 0x80U;

    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_MINUS)) report[6] |= 0x01U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_PLUS)) report[6] |= 0x02U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_R_STICK)) report[6] |= 0x04U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_L_STICK)) report[6] |= 0x08U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_HOME)) report[6] |= 0x10U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_CAPTURE)) report[6] |= 0x20U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_C)) report[6] |= 0x40U;

    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_D_DOWN)) report[7] |= 0x01U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_D_UP)) report[7] |= 0x02U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_D_RIGHT)) report[7] |= 0x04U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_D_LEFT)) report[7] |= 0x08U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_L)) report[7] |= 0x40U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_ZL)) report[7] |= 0x80U;

    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_GR)) report[8] |= 0x01U;
    if (button_pressed(snapshot, SF32LB52_NS2_BUTTON_GL)) report[8] |= 0x02U;

    pack12_pair(report, 11U, snapshot->lx, snapshot->ly);
    pack12_pair(report, 14U, snapshot->rx, snapshot->ry);

    if (snapshot->motion_valid != 0U &&
        REPORT_MOTION_OFFSET + sizeof(snapshot->motion) <= SF32LB52_NS2_REPORT_SIZE) {
        memcpy(report + REPORT_MOTION_OFFSET, snapshot->motion, sizeof(snapshot->motion));
    }
}

int sf32lb52_ns2_make_usb_report(const sf32lb52_ns2_ble_snapshot_t *snapshot,
                                 uint8_t raw_passthrough,
                                 uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                 uint8_t sequence,
                                 uint32_t timestamp)
{
    if (snapshot != 0 && snapshot->valid != 0U &&
        raw_passthrough != 0U &&
        snapshot->kind == SF32LB52_NS2_NOTIFY_KIND_FD2 &&
        snapshot->raw_len == SF32LB52_NS2_REPORT_SIZE - 1U) {
        report[0] = SF32LB52_NS2_INPUT_REPORT_ID;
        memcpy(report + 1U, snapshot->raw, snapshot->raw_len);
        return 1;
    }

    sf32lb52_ns2_make_report_from_snapshot(snapshot, report, sequence, timestamp);
    return 0;
}

void sf32lb52_ns2_write_report_timestamp(
    uint8_t report[SF32LB52_NS2_REPORT_SIZE],
    uint32_t timestamp)
{
    if (report != 0) {
        write_timestamp(report, timestamp);
    }
}

int sf32lb52_ns2_make_report_from_hooks(const sf32lb52_ns2_protocol_hooks_t *hooks,
                                        uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                        uint8_t sequence,
                                        uint32_t timestamp)
{
    sf32lb52_ns2_ble_snapshot_t snapshot;
    int have_snapshot = 0;

    memset(&snapshot, 0, sizeof(snapshot));
    if (hooks != 0 && hooks->read_snapshot != 0) {
        have_snapshot = hooks->read_snapshot(&snapshot, hooks->context) == 0;
    }

    sf32lb52_ns2_make_report_from_snapshot(have_snapshot ? &snapshot : 0, report, sequence, timestamp);
    return have_snapshot != 0 && snapshot.valid != 0U ? 0 : -1;
}

int sf32lb52_ns2_forward_output_report(const sf32lb52_ns2_protocol_hooks_t *hooks,
                                       const uint8_t *report,
                                       size_t report_len)
{
    sf32lb52_ns2_rumble_output_t output;

    if (hooks == 0 || hooks->forward_rumble == 0 || report == 0 || report_len == 0U) {
        return -1;
    }

    output.report_id = report[0];
    output.payload = report + 1U;
    output.payload_len = report_len - 1U;
    if (output.report_id != SF32LB52_NS2_OUTPUT_REPORT_ID) {
        output.report_id = SF32LB52_NS2_OUTPUT_REPORT_ID;
        output.payload = report;
        output.payload_len = report_len;
    }

    return hooks->forward_rumble(&output, hooks->context);
}
