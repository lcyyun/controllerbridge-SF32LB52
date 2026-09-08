#include "ns2_profile.h"
#include "platform.h"
#include "sf32lb52_usb_device.h"
#include "sf32lb52_usb_hid_api.h"

static usb_hid_output_callback_t g_output_callback;

static uint16_t usb_hid_on_input_get(uint8_t *buffer, uint16_t reqlen)
{
    if (buffer == 0 || reqlen < NS2_USB_REPORT_SIZE) {
        return 0U;
    }
    if (ns2_profile_make_usb_report(buffer) != 0) {
        return 0U;
    }
    return NS2_USB_REPORT_SIZE;
}

static void usb_hid_on_output_report(uint8_t report_id, const uint8_t *payload, size_t payload_len)
{
    uint8_t report[SF32LB52_USB_HID_REPORT_SIZE];

    if (g_output_callback == 0 || report_id != SF32LB52_USB_HID_OUTPUT_REPORT_ID) {
        return;
    }

    if (payload_len > SF32LB52_USB_HID_PAYLOAD_SIZE) {
        payload_len = SF32LB52_USB_HID_PAYLOAD_SIZE;
    }

    report[0] = report_id;
    for (size_t i = 0; i < payload_len; ++i) {
        report[1 + i] = payload[i];
    }
    g_output_callback(report, payload_len + 1U);
}

int usb_hid_init(usb_hid_output_callback_t output_callback)
{
    g_output_callback = output_callback;
    sf32lb52_usb_set_report_callbacks(usb_hid_on_output_report,
                                      usb_hid_on_input_get,
                                      ns2_profile_on_feature_get,
                                      ns2_profile_on_feature_set);
    sf32lb52_usb_device_init(Sf32lb52UsbIdentityNintendo);

    /*
     * BLOCKER(CherryUSB): the descriptor/device scaffold is present in
     * usb/sf32lb52_usb_device.c, but the actual SiFli BSP registration calls
     * still need SDK confirmation.
     */
    platform_log("usb", "Nintendo HID descriptor scaffold initialized");
    return 0;
}

int usb_hid_send_input_report(const uint8_t *report, size_t len)
{
    if (report == 0 || len == 0U) {
        return -1;
    }

    if (report[0] != NS2_USB_NINTENDO_INPUT_REPORT_ID) {
        return -1;
    }

    return sf32lb52_usb_hid_send(report[0], report + 1, len - 1U) ? 0 : -1;
}

void usb_hid_poll(void)
{
    sf32lb52_usb_device_task();
}
