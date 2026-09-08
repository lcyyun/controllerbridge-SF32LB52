#ifndef SF32LB52_NS2_PROTOCOL_H
#define SF32LB52_NS2_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_NS2_INPUT_REPORT_ID 0x05U
#define SF32LB52_NS2_OUTPUT_REPORT_ID 0x02U
#define SF32LB52_NS2_FEATURE_REPORT_ID 0x7fU
#define SF32LB52_NS2_REPORT_SIZE 64U
#define SF32LB52_NS2_FEATURE_PAYLOAD_OFFSET 11U
#define SF32LB52_NS2_FEATURE_REPLY_CAPACITY 2048U
#define SF32LB52_NS2_FEATURE_MAGIC_SIZE 6U
#define SF32LB52_NS2_NOTIFY_KIND_UNKNOWN 0U
#define SF32LB52_NS2_NOTIFY_KIND_FD2 1U
#define SF32LB52_NS2_NOTIFY_KIND_LEGACY 2U

extern const char SF32LB52_NS2_FEATURE_COMMAND_MAGIC[SF32LB52_NS2_FEATURE_MAGIC_SIZE + 1U];
extern const char SF32LB52_NS2_FEATURE_REPLY_MAGIC[SF32LB52_NS2_FEATURE_MAGIC_SIZE + 1U];

typedef struct {
    const char *profile;
    uint16_t report_rate_hz;
    uint8_t raw_passthrough;
    uint8_t rumble_enabled;
} sf32lb52_ns2_settings_t;

extern const sf32lb52_ns2_settings_t SF32LB52_NS2_DEFAULT_SETTINGS;

typedef enum {
    SF32LB52_NS2_BUTTON_B = 0,
    SF32LB52_NS2_BUTTON_A,
    SF32LB52_NS2_BUTTON_Y,
    SF32LB52_NS2_BUTTON_X,
    SF32LB52_NS2_BUTTON_R,
    SF32LB52_NS2_BUTTON_ZR,
    SF32LB52_NS2_BUTTON_PLUS,
    SF32LB52_NS2_BUTTON_R_STICK,
    SF32LB52_NS2_BUTTON_D_DOWN,
    SF32LB52_NS2_BUTTON_D_RIGHT,
    SF32LB52_NS2_BUTTON_D_LEFT,
    SF32LB52_NS2_BUTTON_D_UP,
    SF32LB52_NS2_BUTTON_L,
    SF32LB52_NS2_BUTTON_ZL,
    SF32LB52_NS2_BUTTON_MINUS,
    SF32LB52_NS2_BUTTON_L_STICK,
    SF32LB52_NS2_BUTTON_HOME,
    SF32LB52_NS2_BUTTON_CAPTURE,
    SF32LB52_NS2_BUTTON_GR,
    SF32LB52_NS2_BUTTON_GL,
    SF32LB52_NS2_BUTTON_C,
    SF32LB52_NS2_BUTTON_COUNT
} sf32lb52_ns2_button_t;

typedef struct {
    uint8_t valid;
    uint8_t kind;
    uint16_t len;
    uint32_t updates;
    uint32_t parse_errors;
    uint32_t buttons;
    uint16_t lx;
    uint16_t ly;
    uint16_t rx;
    uint16_t ry;
    uint8_t motion_valid;
    uint8_t motion[12];
    uint8_t raw_len;
    uint8_t raw[64];
} sf32lb52_ns2_ble_snapshot_t;

typedef struct {
    uint8_t report_id;
    const uint8_t *payload;
    size_t payload_len;
} sf32lb52_ns2_rumble_output_t;

typedef int (*sf32lb52_ns2_snapshot_provider_t)(sf32lb52_ns2_ble_snapshot_t *out,
                                                void *context);
typedef int (*sf32lb52_ns2_rumble_forwarder_t)(const sf32lb52_ns2_rumble_output_t *output,
                                               void *context);

typedef struct {
    sf32lb52_ns2_snapshot_provider_t read_snapshot;
    sf32lb52_ns2_rumble_forwarder_t forward_rumble;
    void *context;
} sf32lb52_ns2_protocol_hooks_t;

typedef struct {
    uint8_t bytes[SF32LB52_NS2_FEATURE_REPLY_CAPACITY];
    uint16_t length;
    uint16_t offset;
} sf32lb52_ns2_feature_reply_t;

typedef struct {
    uint8_t valid;
    const uint8_t *bytes;
    uint16_t length;
} sf32lb52_ns2_feature_command_t;

void sf32lb52_ns2_reset_feature_reply(sf32lb52_ns2_feature_reply_t *reply);
int sf32lb52_ns2_queue_feature_reply(sf32lb52_ns2_feature_reply_t *reply, const char *json);
size_t sf32lb52_ns2_build_feature_reply_chunk(sf32lb52_ns2_feature_reply_t *reply,
                                              uint8_t *report,
                                              size_t report_len);
sf32lb52_ns2_feature_command_t sf32lb52_ns2_parse_feature_command(const uint8_t *report,
                                                                  size_t report_len);
int sf32lb52_ns2_parse_ble_notify(const uint8_t *data,
                                  size_t len,
                                  sf32lb52_ns2_ble_snapshot_t *snapshot);
int sf32lb52_ns2_parse_ble_notify_kind(uint8_t kind,
                                       const uint8_t *data,
                                       size_t len,
                                       sf32lb52_ns2_ble_snapshot_t *snapshot);
void sf32lb52_ns2_reset_axis_calibration(void);

void sf32lb52_ns2_make_neutral_report(uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                      uint8_t sequence,
                                      uint32_t timestamp);
void sf32lb52_ns2_make_report_from_snapshot(const sf32lb52_ns2_ble_snapshot_t *snapshot,
                                            uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                            uint8_t sequence,
                                            uint32_t timestamp);
int sf32lb52_ns2_make_usb_report(const sf32lb52_ns2_ble_snapshot_t *snapshot,
                                 uint8_t raw_passthrough,
                                 uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                 uint8_t sequence,
                                 uint32_t timestamp);
void sf32lb52_ns2_write_report_timestamp(
    uint8_t report[SF32LB52_NS2_REPORT_SIZE],
    uint32_t timestamp);
int sf32lb52_ns2_make_report_from_hooks(const sf32lb52_ns2_protocol_hooks_t *hooks,
                                        uint8_t report[SF32LB52_NS2_REPORT_SIZE],
                                        uint8_t sequence,
                                        uint32_t timestamp);
int sf32lb52_ns2_forward_output_report(const sf32lb52_ns2_protocol_hooks_t *hooks,
                                       const uint8_t *report,
                                       size_t report_len);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_NS2_PROTOCOL_H */
