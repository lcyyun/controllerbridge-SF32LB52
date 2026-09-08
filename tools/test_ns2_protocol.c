#include "sf32lb52_ns2_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#define BIT(button) (1UL << (uint8_t)(button))

static void pack12(uint8_t *data, size_t offset, uint16_t x, uint16_t y)
{
    data[offset] = (uint8_t)(x & 0xffU);
    data[offset + 1U] = (uint8_t)(((x >> 8U) & 0x0fU) | ((y & 0x0fU) << 4U));
    data[offset + 2U] = (uint8_t)((y >> 4U) & 0xffU);
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

static void calibrate_axes(uint8_t kind)
{
    uint8_t notify[60];
    sf32lb52_ns2_ble_snapshot_t snapshot;
    size_t len = kind == SF32LB52_NS2_NOTIFY_KIND_FD2 ? sizeof(notify) : 11U;
    size_t left_offset = kind == SF32LB52_NS2_NOTIFY_KIND_FD2 ? 10U : 5U;
    size_t right_offset = kind == SF32LB52_NS2_NOTIFY_KIND_FD2 ? 13U : 8U;

    memset(notify, 0, sizeof(notify));
    memset(&snapshot, 0, sizeof(snapshot));
    pack12(notify, left_offset, 2048U, 2048U);
    pack12(notify, right_offset, 2048U, 2048U);

    /* The first stable frame establishes history; the next 20 learn center. */
    for (unsigned i = 0; i < 21U; ++i) {
        assert(sf32lb52_ns2_parse_ble_notify_kind(kind, notify, len, &snapshot) == 0);
    }
    assert(snapshot.lx == 2048U);
    assert(snapshot.ly == 2048U);
    assert(snapshot.rx == 2048U);
    assert(snapshot.ry == 2048U);
}

static void test_axes_move_before_calibration(void)
{
    uint8_t notify[60];
    sf32lb52_ns2_ble_snapshot_t snapshot;

    sf32lb52_ns2_reset_axis_calibration();
    memset(notify, 0, sizeof(notify));
    memset(&snapshot, 0, sizeof(snapshot));
    pack12(notify, 10U, 448U, 2048U);
    pack12(notify, 13U, 3648U, 2048U);

    assert(sf32lb52_ns2_parse_ble_notify_kind(SF32LB52_NS2_NOTIFY_KIND_FD2,
                                              notify, sizeof(notify),
                                              &snapshot) == 0);
    assert(snapshot.lx == 0U);
    assert(snapshot.ly == 2048U);
    assert(snapshot.rx == 4095U);
    assert(snapshot.ry == 2048U);
}

static void test_fd2_notify_to_usb_report(void)
{
    uint8_t notify[60];
    sf32lb52_ns2_ble_snapshot_t snapshot;
    uint8_t usb[SF32LB52_NS2_REPORT_SIZE];

    sf32lb52_ns2_reset_axis_calibration();
    calibrate_axes(SF32LB52_NS2_NOTIFY_KIND_FD2);
    memset(notify, 0, sizeof(notify));
    memset(&snapshot, 0, sizeof(snapshot));

    notify[4] = 0x0cU;       /* B + A in FD2 mapping. */
    notify[6] = 0xc0U;       /* L + ZL in FD2 mapping. */
    notify[7] = 0x03U;       /* GR + GL in FD2 mapping. */
    pack12(notify, 10U, 1000U, 2000U);
    pack12(notify, 13U, 3000U, 4000U);
    for (uint8_t i = 0; i < 12U; ++i) {
        notify[48U + i] = (uint8_t)(0xa0U + i);
    }

    assert(sf32lb52_ns2_parse_ble_notify(notify, sizeof(notify), &snapshot) == 0);
    assert(snapshot.valid == 1U);
    assert(snapshot.kind == 1U);
    assert(snapshot.len == sizeof(notify));
    assert(snapshot.updates == 1U);
    assert(snapshot.parse_errors == 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_B)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_A)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_L)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_ZL)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_GR)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_GL)) != 0U);
    assert(snapshot.lx < 2048U);
    assert(snapshot.ly == 2048U); /* Exactly on the configured deadzone edge. */
    assert(snapshot.rx > 2048U);
    assert(snapshot.ry == 4095U);
    assert(snapshot.motion_valid == 1U);
    assert(snapshot.motion[0] == 0xa0U);
    assert(snapshot.motion[11] == 0xabU);

    sf32lb52_ns2_make_report_from_snapshot(&snapshot, usb, 7U, 0x11223344UL);
    assert(usb[0] == SF32LB52_NS2_INPUT_REPORT_ID);
    assert(usb[1] == 7U);
    assert((usb[5] & 0x0cU) == 0x0cU);
    assert((usb[7] & 0xc0U) == 0xc0U);
    assert((usb[8] & 0x03U) == 0x03U);
    assert(unpack12_x(usb, 11U) == snapshot.lx);
    assert(unpack12_y(usb, 11U) == snapshot.ly);
    assert(unpack12_x(usb, 14U) == snapshot.rx);
    assert(unpack12_y(usb, 14U) == snapshot.ry);
    assert(usb[49] == 0xa0U);
    assert(usb[60] == 0xabU);
}

static void test_legacy_notify(void)
{
    uint8_t notify[11];
    sf32lb52_ns2_ble_snapshot_t snapshot;

    sf32lb52_ns2_reset_axis_calibration();
    calibrate_axes(SF32LB52_NS2_NOTIFY_KIND_LEGACY);
    memset(notify, 0, sizeof(notify));
    memset(&snapshot, 0, sizeof(snapshot));

    notify[2] = 0x03U;       /* B + A in legacy mapping. */
    notify[3] = 0x30U;       /* L + ZL in legacy mapping. */
    notify[4] = 0x10U;       /* C in legacy mapping. */
    pack12(notify, 5U, 1234U, 2345U);
    pack12(notify, 8U, 3456U, 456U);

    assert(sf32lb52_ns2_parse_ble_notify(notify, sizeof(notify), &snapshot) == 0);
    assert(snapshot.valid == 1U);
    assert(snapshot.kind == 2U);
    assert(snapshot.len == sizeof(notify));
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_B)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_A)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_L)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_ZL)) != 0U);
    assert((snapshot.buttons & BIT(SF32LB52_NS2_BUTTON_C)) != 0U);
    assert(snapshot.lx < 2048U);
    assert(snapshot.ly > 2048U);
    assert(snapshot.rx > 2048U);
    assert(snapshot.ry < 2048U);
    assert(snapshot.motion_valid == 0U);
}

static void test_bad_notify_increments_errors(void)
{
    uint8_t notify[3] = {0};
    sf32lb52_ns2_ble_snapshot_t snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    assert(sf32lb52_ns2_parse_ble_notify(notify, sizeof(notify), &snapshot) != 0);
    assert(snapshot.valid == 0U);
    assert(snapshot.parse_errors == 1U);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void test_fd2_raw_passthrough_is_exact_and_opt_in(void)
{
    sf32lb52_ns2_ble_snapshot_t snapshot;
    uint8_t usb[SF32LB52_NS2_REPORT_SIZE];
    uint8_t expected[SF32LB52_NS2_REPORT_SIZE - 1U];
    size_t i;

    memset(&snapshot, 0, sizeof(snapshot));
    for (i = 0U; i < sizeof(expected); ++i) {
        expected[i] = (uint8_t)(0x31U + i);
    }
    snapshot.valid = 1U;
    snapshot.kind = SF32LB52_NS2_NOTIFY_KIND_FD2;
    snapshot.raw_len = sizeof(expected);
    memcpy(snapshot.raw, expected, sizeof(expected));
    snapshot.lx = snapshot.ly = snapshot.rx = snapshot.ry = 2048U;

    assert(sf32lb52_ns2_make_usb_report(&snapshot, 0U, usb, 9U,
                                         0x12345678UL) == 0);
    assert(usb[0] == SF32LB52_NS2_INPUT_REPORT_ID);
    assert(usb[1] == 9U);
    assert(read_le32(usb + 0x2bU) == 0x12345678UL);
    assert(memcmp(usb + 1U, expected, sizeof(expected)) != 0);

    assert(sf32lb52_ns2_make_usb_report(&snapshot, 1U, usb, 10U,
                                         0x87654321UL) == 1);
    assert(usb[0] == SF32LB52_NS2_INPUT_REPORT_ID);
    assert(memcmp(usb + 1U, expected, sizeof(expected)) == 0);

    snapshot.raw_len--;
    assert(sf32lb52_ns2_make_usb_report(&snapshot, 1U, usb, 11U,
                                         0x01020304UL) == 0);
    assert(usb[1] == 11U);

    snapshot.raw_len = sizeof(expected);
    snapshot.kind = SF32LB52_NS2_NOTIFY_KIND_LEGACY;
    assert(sf32lb52_ns2_make_usb_report(&snapshot, 1U, usb, 12U,
                                         0x05060708UL) == 0);
    assert(usb[1] == 12U);
}

int main(void)
{
    test_axes_move_before_calibration();
    test_fd2_notify_to_usb_report();
    test_legacy_notify();
    test_bad_notify_increments_errors();
    test_fd2_raw_passthrough_is_exact_and_opt_in();
    return 0;
}
