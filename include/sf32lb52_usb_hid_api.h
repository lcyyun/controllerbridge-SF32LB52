#ifndef SF32LB52_USB_HID_API_H
#define SF32LB52_USB_HID_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sf32lb52_usb_device.h"

#define USB_HID_NINTENDO_VID SF32LB52_USB_NINTENDO_VID
#define USB_HID_NINTENDO_PID SF32LB52_USB_NINTENDO_PID
#define USB_HID_INPUT_REPORT_ID SF32LB52_USB_HID_INPUT_REPORT_ID
#define USB_HID_OUTPUT_REPORT_ID SF32LB52_USB_HID_OUTPUT_REPORT_ID
#define USB_HID_FEATURE_REPORT_ID SF32LB52_USB_HID_FEATURE_REPORT_ID
#define USB_HID_REPORT_SIZE SF32LB52_USB_HID_REPORT_SIZE
#define USB_HID_POLL_INTERVAL_MS SF32LB52_USB_HID_POLL_INTERVAL_MS

typedef void (*usb_hid_output_callback_t)(const uint8_t *data, size_t len);

int usb_hid_init(usb_hid_output_callback_t output_callback);
int usb_hid_send(uint8_t report_id, const uint8_t *data, size_t len);
int usb_hid_send_input_report(const uint8_t *report, size_t len);
void usb_hid_poll(void);

void usb_hid_mount_cb(void);
void usb_hid_unmount_cb(void);
void usb_hid_suspend_cb(bool remote_wakeup_enabled);
void usb_hid_resume_cb(void);
uint16_t usb_hid_get_report_cb(uint8_t report_id,
                               uint8_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen);
void usb_hid_set_report_cb(uint8_t report_id,
                           uint8_t report_type,
                           const uint8_t *buffer,
                           uint16_t len);

#endif
