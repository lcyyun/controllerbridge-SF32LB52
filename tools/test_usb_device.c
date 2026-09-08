#include "sf32lb52_usb_device.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HID_REPORT_TYPE_INPUT 1u
#define HID_REPORT_TYPE_FEATURE 3u
#define REQUIRED_FEATURE_QUEUE_CAPACITY 8u
#define FEATURE_RECORD_CAPACITY 32u

typedef struct FeatureRecord {
    uint16_t len;
    uint8_t payload[SF32LB52_USB_HID_PAYLOAD_SIZE];
} FeatureRecord;

static FeatureRecord feature_records[FEATURE_RECORD_CAPACITY];
static size_t feature_record_count;
static unsigned int cache_miss_calls;
static uint8_t native_set_report_id;
static uint16_t native_set_len;
static uint8_t native_set_payload[SF32LB52_USB_HID_PAYLOAD_SIZE];

static uint16_t cache_miss_input(uint8_t *buffer, uint16_t reqlen)
{
    cache_miss_calls++;
    /* A cache callback is allowed to leave scratch data behind when it
     * returns zero.  The USB layer must discard it before making neutral. */
    memset(buffer, 0xa5, reqlen);
    return 0u;
}

static void record_feature_set(const uint8_t *payload, uint16_t len)
{
    FeatureRecord *record;

    assert(feature_record_count < FEATURE_RECORD_CAPACITY);
    assert(len <= SF32LB52_USB_HID_PAYLOAD_SIZE);
    record = &feature_records[feature_record_count++];
    record->len = len;
    memset(record->payload, 0, sizeof(record->payload));
    if (payload && len > 0u) {
        memcpy(record->payload, payload, len);
    }
}

static uint16_t native_feature_get(uint8_t report_id,
                                   uint8_t *buffer,
                                   uint16_t reqlen)
{
    if ((report_id != 0x05u &&
         report_id != SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID) ||
        reqlen < 4u) {
        return 0u;
    }
    buffer[0] = report_id;
    buffer[1] = 0x11u;
    buffer[2] = 0x22u;
    buffer[3] = 0x33u;
    return 4u;
}

static void native_feature_set(uint8_t report_id,
                               const uint8_t *payload,
                               uint16_t len)
{
    native_set_report_id = report_id;
    native_set_len = len;
    memset(native_set_payload, 0, sizeof(native_set_payload));
    if (payload != 0 && len != 0u) {
        memcpy(native_set_payload, payload, len);
    }
}

static void reset_callbacks_and_records(Sf32lb52UsbRole role)
{
    memset(feature_records, 0, sizeof(feature_records));
    feature_record_count = 0u;
    cache_miss_calls = 0u;
    sf32lb52_usb_set_native_feature_callbacks(0, 0);
    sf32lb52_usb_device_init(role);
    sf32lb52_usb_set_report_callbacks(0, cache_miss_input, 0,
                                       record_feature_set);
}

static int is_ds5_expected_nonzero(size_t offset)
{
    return (offset >= 1u && offset <= 4u) || offset == 8u ||
           offset == 33u || offset == 37u || offset == 53u;
}

static void test_ds5_cache_miss_returns_neutral(void)
{
    uint8_t report[SF32LB52_USB_DS5_INPUT_REPORT_SIZE];
    uint16_t len;
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleDualSense);
    memset(report, 0xcc, sizeof(report));
    len = sf32lb52_usb_hid_get_report_cb(
        SF32LB52_USB_DS5_INPUT_REPORT_ID, HID_REPORT_TYPE_INPUT,
        report, sizeof(report));

    assert(cache_miss_calls == 1u);
    assert(len == SF32LB52_USB_DS5_INPUT_REPORT_SIZE);
    assert(report[0] == SF32LB52_USB_DS5_INPUT_REPORT_ID);
    assert(report[1] == 128u && report[2] == 128u);
    assert(report[3] == 128u && report[4] == 128u);
    assert(report[8] == 8u);       /* D-pad released. */
    assert(report[33] == 0x80u);   /* Touch contact 1 released. */
    assert(report[37] == 0x80u);   /* Touch contact 2 released. */
    assert(report[53] == 0x2au);   /* Unknown/full battery. */
    for (i = 1u; i < sizeof(report); i++) {
        if (!is_ds5_expected_nonzero(i)) {
            assert(report[i] == 0u);
        }
    }
}

static uint16_t unpack_first_12(const uint8_t *packed)
{
    return (uint16_t)packed[0] | (uint16_t)((packed[1] & 0x0fu) << 8);
}

static uint16_t unpack_second_12(const uint8_t *packed)
{
    return (uint16_t)(packed[1] >> 4) | (uint16_t)(packed[2] << 4);
}

static int is_ns2_expected_nonzero(size_t offset)
{
    return offset == 2u || offset == 12u || offset == 13u ||
           offset == 15u || offset == 16u;
}

static void test_ns2_cache_miss_returns_neutral(void)
{
    uint8_t report[SF32LB52_USB_NINTENDO_INPUT_REPORT_SIZE];
    uint16_t len;
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleNintendo);
    memset(report, 0xcc, sizeof(report));
    len = sf32lb52_usb_hid_get_report_cb(
        SF32LB52_USB_NINTENDO_INPUT_REPORT_ID, HID_REPORT_TYPE_INPUT,
        report, sizeof(report));

    assert(cache_miss_calls == 1u);
    assert(len == SF32LB52_USB_NINTENDO_INPUT_REPORT_SIZE);
    assert(report[0] == SF32LB52_USB_NINTENDO_INPUT_REPORT_ID);
    assert(report[2] == 0x20u);
    assert(unpack_first_12(&report[11]) == 0x800u);
    assert(unpack_second_12(&report[11]) == 0x800u);
    assert(unpack_first_12(&report[14]) == 0x800u);
    assert(unpack_second_12(&report[14]) == 0x800u);
    for (i = 1u; i < sizeof(report); i++) {
        if (!is_ns2_expected_nonzero(i)) {
            assert(report[i] == 0u);
        }
    }
}

static void queue_feature_command(uint8_t sequence)
{
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
    uint8_t report_id = sf32lb52_usb_manager_feature_report_id();

    memset(report, 0, sizeof(report));
    report[0] = report_id;
    report[1] = sequence;
    report[2] = (uint8_t)~sequence;
    report[63] = (uint8_t)(sequence ^ 0x5au);
    sf32lb52_usb_hid_set_report_cb(0u, HID_REPORT_TYPE_FEATURE,
                                    report, sizeof(report));
    /* Prove that queued entries own a copy, rather than a caller buffer. */
    memset(report, 0xee, sizeof(report));
}

static void assert_feature_record(size_t index, uint8_t sequence)
{
    assert(index < feature_record_count);
    assert(feature_records[index].len == SF32LB52_USB_HID_PAYLOAD_SIZE);
    assert(feature_records[index].payload[0] == sequence);
    assert(feature_records[index].payload[1] == (uint8_t)~sequence);
    assert(feature_records[index].payload[62] == (uint8_t)(sequence ^ 0x5au));
}

static void test_feature_set_fifo_eight_entries(void)
{
    Sf32lb52UsbDeviceStatus status;
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleNintendo);
    for (i = 0u; i < REQUIRED_FEATURE_QUEUE_CAPACITY; i++) {
        queue_feature_command((uint8_t)(0x10u + i));
    }

    /* SET_REPORT runs in the USB callback context; delivery is deferred. */
    assert(feature_record_count == 0u);
    sf32lb52_usb_device_task();
    sf32lb52_usb_get_status(&status);

    assert(status.feature_set_reports == REQUIRED_FEATURE_QUEUE_CAPACITY);
    assert(status.feature_set_dropped == 0u);
    assert(feature_record_count == REQUIRED_FEATURE_QUEUE_CAPACITY);
    for (i = 0u; i < REQUIRED_FEATURE_QUEUE_CAPACITY; i++) {
        assert_feature_record(i, (uint8_t)(0x10u + i));
    }
}

static void test_feature_set_fifo_wraparound(void)
{
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleNintendo);
    for (i = 0u; i < 5u; i++) {
        queue_feature_command((uint8_t)(0x40u + i));
    }
    sf32lb52_usb_device_task();
    for (i = 0u; i < 6u; i++) {
        queue_feature_command((uint8_t)(0x60u + i));
    }
    sf32lb52_usb_device_task();

    assert(feature_record_count == 11u);
    for (i = 0u; i < 5u; i++) {
        assert_feature_record(i, (uint8_t)(0x40u + i));
    }
    for (i = 0u; i < 6u; i++) {
        assert_feature_record(5u + i, (uint8_t)(0x60u + i));
    }
}

static void test_feature_set_overflow_keeps_oldest_fifo(void)
{
    Sf32lb52UsbDeviceStatus status;
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleNintendo);
    for (i = 0u; i < REQUIRED_FEATURE_QUEUE_CAPACITY + 1u; i++) {
        queue_feature_command((uint8_t)(0x80u + i));
    }
    sf32lb52_usb_device_task();
    sf32lb52_usb_get_status(&status);

    assert(status.feature_set_reports == REQUIRED_FEATURE_QUEUE_CAPACITY + 1u);
    assert(status.feature_set_dropped == 1u);
    assert(feature_record_count == REQUIRED_FEATURE_QUEUE_CAPACITY);
    for (i = 0u; i < REQUIRED_FEATURE_QUEUE_CAPACITY; i++) {
        assert_feature_record(i, (uint8_t)(0x80u + i));
    }
}

static void test_ds5_native_feature_callbacks(void)
{
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
    uint8_t payload[8] = {1u, 2u, 3u, 4u, 0u, 0u, 0u, 0u};
    uint16_t len;

    reset_callbacks_and_records(Sf32lb52UsbRoleDualSense);
    sf32lb52_usb_set_native_feature_callbacks(native_feature_get,
                                               native_feature_set);
    memset(report, 0, sizeof(report));
    len = sf32lb52_usb_hid_get_report_cb(0x05u, HID_REPORT_TYPE_FEATURE,
                                         report, sizeof(report));
    assert(len == 4u);
    assert(report[0] == 0x05u && report[3] == 0x33u);

    native_set_report_id = 0u;
    native_set_len = 0u;
    sf32lb52_usb_hid_set_report_cb(0x60u, HID_REPORT_TYPE_FEATURE,
                                    payload, sizeof(payload));
    assert(native_set_len == 0u);
    sf32lb52_usb_device_task();
    assert(native_set_report_id == 0x60u);
    assert(native_set_len == sizeof(payload));
    assert(memcmp(native_set_payload, payload, sizeof(payload)) == 0);
}

static void test_ds5_manager_magic_preserves_native_f6(void)
{
    uint8_t manager_payload[SF32LB52_USB_HID_PAYLOAD_SIZE] = {
        'Y', '7', 'H', 'I', 'D', '1', 's', 't', 'a', 't', 'u', 's'
    };
    uint8_t native_payload[4] = {0x20u, 0x11u, 0x22u, 0x33u};
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];
    uint16_t len;

    reset_callbacks_and_records(Sf32lb52UsbRoleDualSense);
    sf32lb52_usb_set_native_feature_callbacks(native_feature_get,
                                               native_feature_set);

    memset(report, 0, sizeof(report));
    len = sf32lb52_usb_hid_get_report_cb(
        SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID,
        HID_REPORT_TYPE_FEATURE, report, sizeof(report));
    assert(len == 4u);
    assert(report[0] == SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID);
    assert(report[3] == 0x33u);

    sf32lb52_usb_hid_set_report_cb(
        SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID,
        HID_REPORT_TYPE_FEATURE, manager_payload, sizeof(manager_payload));
    sf32lb52_usb_device_task();
    assert(feature_record_count == 1u);
    assert(feature_records[0].len == sizeof(manager_payload));
    assert(memcmp(feature_records[0].payload, "Y7HID1", 6u) == 0);

    native_set_report_id = 0u;
    native_set_len = 0u;
    sf32lb52_usb_hid_set_report_cb(
        SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID,
        HID_REPORT_TYPE_FEATURE, native_payload, sizeof(native_payload));
    sf32lb52_usb_device_task();
    assert(feature_record_count == 1u);
    assert(native_set_report_id == SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID);
    assert(native_set_len == sizeof(native_payload));
    assert(memcmp(native_set_payload, native_payload, sizeof(native_payload)) == 0);
}

static void test_dse_identity_and_wire_shape(void)
{
    Sf32lb52UsbRoleCapabilities caps;
    const uint8_t *descriptor;
    size_t len;
    size_t i;

    reset_callbacks_and_records(Sf32lb52UsbRoleDualSenseEdge);
    assert(sf32lb52_usb_get_role_capabilities(
        Sf32lb52UsbRoleDualSenseEdge, &caps));
    assert(caps.vid == SF32LB52_USB_DS5_VID);
    assert(caps.pid == SF32LB52_USB_DSE_PID);
    assert(caps.input_report_size == SF32LB52_USB_DS5_INPUT_REPORT_SIZE);
    assert(caps.output_report_size == SF32LB52_USB_DSE_OUTPUT_REPORT_SIZE);
    assert(caps.audio_capable);
    assert(sf32lb52_usb_manager_feature_report_id() ==
           SF32LB52_USB_DS5_MANAGER_FEATURE_REPORT_ID);

    descriptor = sf32lb52_usb_device_descriptor(&len);
    assert(len == 18u);
    assert(descriptor[8] == 0x4cu && descriptor[9] == 0x05u);
    assert(descriptor[10] == 0xf2u && descriptor[11] == 0x0du);

    descriptor = sf32lb52_usb_hid_report_descriptor(&len);
    assert(len == 445u);
    for (i = 0u; i + 7u < len; i++) {
        if (memcmp(descriptor + i,
                   (const uint8_t[]){0x85u, 0x02u, 0x09u, 0x23u,
                                     0x95u, 0x3fu, 0x91u, 0x02u},
                   8u) == 0) {
            break;
        }
    }
    assert(i + 7u < len);

    descriptor = sf32lb52_usb_configuration_descriptor(&len);
    assert(len == 227u);
    for (i = 0u; i + 8u < len; i++) {
        if (descriptor[i] == 9u && descriptor[i + 1u] == 0x21u &&
            descriptor[i + 6u] == 0x22u) {
            assert(descriptor[i + 7u] == (445u & 0xffu));
            assert(descriptor[i + 8u] == (445u >> 8u));
            break;
        }
    }
    assert(i + 8u < len);
}

int main(void)
{
    test_ds5_cache_miss_returns_neutral();
    test_ns2_cache_miss_returns_neutral();
    test_feature_set_fifo_eight_entries();
    test_feature_set_fifo_wraparound();
    test_feature_set_overflow_keeps_oldest_fifo();
    test_ds5_native_feature_callbacks();
    test_ds5_manager_magic_preserves_native_f6();
    test_dse_identity_and_wire_shape();
    puts("sf32lb52 USB device tests passed");
    return 0;
}
