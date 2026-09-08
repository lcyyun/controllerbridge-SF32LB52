#ifndef NS2PRO_BRIDGE_NS2_PROFILE_H
#define NS2PRO_BRIDGE_NS2_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "sf32lb52_audio_haptics.h"
#include "sf32lb52_ns2_protocol.h"

#define NS2_USB_NINTENDO_INPUT_REPORT_ID 0x05U
#define NS2_USB_NINTENDO_OUTPUT_REPORT_ID 0x02U
#define NS2_USB_MANAGER_FEATURE_REPORT_ID 0x7fU
#define NS2_USB_REPORT_SIZE 64U

void ns2_profile_init(void);
void ns2_profile_load_tuning(void);
void ns2_profile_poll(void);
void ns2_profile_on_ble_input(uint8_t kind, const uint8_t *data, size_t len);
void ns2_profile_on_usb_output(const uint8_t *data, size_t len);
int ns2_profile_get_snapshot(sf32lb52_ns2_ble_snapshot_t *snapshot);
int ns2_profile_apply_rumble(uint16_t left_motor, uint16_t right_motor);
void ns2_profile_apply_audio_haptics(
    sf32lb52_audio_haptics_event_t event,
    const sf32lb52_audio_haptics_output_t *output);
uint16_t ns2_profile_on_feature_get(uint8_t *buffer, uint16_t len);
void ns2_profile_on_feature_set(const uint8_t *data, uint16_t len);
int ns2_profile_make_usb_report(uint8_t report[NS2_USB_REPORT_SIZE]);
void ns2_profile_stamp_usb_report_timestamp(
    uint8_t report[NS2_USB_REPORT_SIZE]);
void ns2_profile_mark_usb_report_sent(void);
uint16_t ns2_profile_get_report_rate_hz(void);

#endif /* NS2PRO_BRIDGE_NS2_PROFILE_H */
