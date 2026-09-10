#ifndef SF32LB52_BRIDGE_RUNTIME_H
#define SF32LB52_BRIDGE_RUNTIME_H

#include "sf32lb52_bridge_mapping.h"
#include "sf32lb52_bridge_protocol.h"
#include "sf32lb52_bridge_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_BRIDGE_INPUT_STALE_MS 250U
#define SF32LB52_BRIDGE_INPUT_TAKEOVER_MS 1500U

typedef int (*sf32lb52_bridge_role_change_callback_t)(
    sf32lb52_bridge_role_t role,
    void *context);
typedef int (*sf32lb52_bridge_feedback_callback_t)(
    sf32lb52_bridge_input_source_t source,
    const sf32lb52_bridge_feedback_t *feedback,
    void *context);

typedef struct {
    sf32lb52_bridge_role_t output_role;
    sf32lb52_bridge_input_preference_t input_preference;
    sf32lb52_bridge_input_source_t active_input;
    uint8_t input_valid;
    uint8_t input_stale;
    uint8_t settings_dirty;
    uint8_t last_persist_ok;
    uint8_t mapping_custom;
    uint8_t mapping_dirty;
    uint8_t mapping_last_persist_ok;
    uint32_t active_since_ms;
    uint32_t last_input_ms;
    uint32_t accepted_reports;
    uint32_t ignored_reports;
    uint32_t source_switches;
    uint32_t neutral_reports;
    uint32_t usb_output_reports;
    uint32_t feedback_forwarded;
    uint32_t feedback_failed;
    uint32_t role_switches;
} sf32lb52_bridge_runtime_status_t;

void sf32lb52_bridge_runtime_init(void);
void sf32lb52_bridge_runtime_set_callbacks(
    sf32lb52_bridge_role_change_callback_t role_change,
    sf32lb52_bridge_feedback_callback_t feedback,
    void *context);
void sf32lb52_bridge_runtime_poll(uint32_t now_ms);

sf32lb52_bridge_role_t sf32lb52_bridge_runtime_role(void);
bool sf32lb52_bridge_runtime_set_role(sf32lb52_bridge_role_t role,
                                      bool persist);
/* Align runtime/Flash with the persona that USB actually kept after a delayed
 * re-enumeration. This deliberately bypasses the role-change callback. */
bool sf32lb52_bridge_runtime_sync_role(sf32lb52_bridge_role_t role,
                                       bool persist);
bool sf32lb52_bridge_runtime_cycle_role(bool persist);
bool sf32lb52_bridge_runtime_set_input_preference(
    sf32lb52_bridge_input_preference_t preference,
    bool persist);
bool sf32lb52_bridge_runtime_set_auto_connect(bool enabled, bool persist);

bool sf32lb52_bridge_runtime_accept_input(
    const sf32lb52_bridge_input_state_t *state,
    uint32_t now_ms);
void sf32lb52_bridge_runtime_release_input(
    sf32lb52_bridge_input_source_t source,
    uint32_t now_ms);
size_t sf32lb52_bridge_runtime_make_input_report(uint32_t now_ms,
                                                 uint8_t *report,
                                                 size_t report_capacity);
int sf32lb52_bridge_runtime_on_usb_output(const uint8_t *report,
                                          size_t report_len);

bool sf32lb52_bridge_runtime_remember_ds5(const uint8_t address[6]);
bool sf32lb52_bridge_runtime_remember_ns2(uint8_t address_type,
                                         const uint8_t address[6]);
bool sf32lb52_bridge_runtime_forget_input(
    sf32lb52_bridge_input_source_t source,
    bool persist);
bool sf32lb52_bridge_runtime_save_settings(void);
bool sf32lb52_bridge_runtime_reset_settings(void);
void sf32lb52_bridge_runtime_get_config(
    sf32lb52_bridge_persisted_config_t *config);
void sf32lb52_bridge_runtime_get_status(
    sf32lb52_bridge_runtime_status_t *status);
bool sf32lb52_bridge_runtime_get_input_state(
    sf32lb52_bridge_input_state_t *state);

sf32lb52_bridge_mapping_profile_t sf32lb52_bridge_runtime_active_mapping_profile(void);
sf32lb52_bridge_mapping_output_t sf32lb52_bridge_runtime_active_mapping_output(void);
bool sf32lb52_bridge_runtime_set_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    sf32lb52_bridge_button_t target, uint8_t source);
bool sf32lb52_bridge_runtime_reset_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output);
/* Explicit save persists only this route, retaining other saved routes and
 * leaving other unsaved edits in RAM. mapping_dirty remains aggregate. */
bool sf32lb52_bridge_runtime_save_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output);
bool sf32lb52_bridge_runtime_get_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    sf32lb52_bridge_mapping_config_t *config);
bool sf32lb52_bridge_runtime_get_route_mapping_dirty(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    bool *dirty);
/* Legacy profile APIs resolve output from the actual USB role, not a pending
 * runtime role request. */
bool sf32lb52_bridge_runtime_set_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_button_t target,
    uint8_t source);
bool sf32lb52_bridge_runtime_reset_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile);
/* Legacy save retains the existing aggregate-save policy, now in v3 format. */
bool sf32lb52_bridge_runtime_save_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile);
bool sf32lb52_bridge_runtime_get_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_config_t *config);

bool sf32lb52_bridge_runtime_set_button_mapping(
    sf32lb52_bridge_button_t target,
    uint8_t source);
bool sf32lb52_bridge_runtime_reset_button_mapping(void);
bool sf32lb52_bridge_runtime_save_button_mapping(void);
bool sf32lb52_bridge_runtime_get_button_mapping(
    sf32lb52_bridge_mapping_config_t *config);
bool sf32lb52_bridge_runtime_mapping_is_identity(void);

/* Applies the active mapping to a same-role native report without replacing
 * touch, motion, timestamp or vendor-specific bytes. */
bool sf32lb52_bridge_runtime_patch_native_input_report(
    uint32_t now_ms,
    uint8_t *report,
    size_t report_len);

const char *sf32lb52_bridge_role_name(sf32lb52_bridge_role_t role);
const char *sf32lb52_bridge_source_name(sf32lb52_bridge_input_source_t source);
const char *sf32lb52_bridge_preference_name(
    sf32lb52_bridge_input_preference_t preference);
bool sf32lb52_bridge_parse_role(const char *text,
                                sf32lb52_bridge_role_t *role);
bool sf32lb52_bridge_parse_input_preference(
    const char *text,
    sf32lb52_bridge_input_preference_t *preference);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_BRIDGE_RUNTIME_H */
