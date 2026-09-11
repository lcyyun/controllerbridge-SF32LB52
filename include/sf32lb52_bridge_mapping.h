#ifndef SF32LB52_BRIDGE_MAPPING_H
#define SF32LB52_BRIDGE_MAPPING_H

#include "sf32lb52_bridge_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_BRIDGE_MAPPING_NONE UINT8_C(0xff)
#define SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE 40U
#define SF32LB52_BRIDGE_MAPPING_WIRE_SIZE 80U
#define SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE 112U
#define SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE 106U
#define SF32LB52_BRIDGE_MAPPING_SCHEMA 4U

typedef struct {
    uint8_t source_for_target[SF32LB52_BRIDGE_BUTTON_COUNT];
} sf32lb52_bridge_mapping_config_t;

/* Profiles follow the physical input source, never the emulated USB role. */
typedef enum {
    SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED = 0,
    SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
    SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO
} sf32lb52_bridge_mapping_profile_t;

typedef struct {
    sf32lb52_bridge_mapping_config_t ds5;
    sf32lb52_bridge_mapping_config_t ns2pro;
} sf32lb52_bridge_mapping_profiles_t;

typedef struct {
    sf32lb52_bridge_mapping_config_t ds5;
    sf32lb52_bridge_mapping_config_t ns2pro;
    sf32lb52_bridge_mapping_config_t xbox;
} sf32lb52_bridge_mapping_outputs_t;

/* Outer member = physical source; inner member = USB output.
 * The two-profile type and its v2 codec remain available for migration. */
typedef struct {
    sf32lb52_bridge_mapping_outputs_t ds5;
    sf32lb52_bridge_mapping_outputs_t ns2pro;
} sf32lb52_bridge_mapping_routes_t;

typedef enum {
    SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED = 0,
    SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5,
    SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO,
    SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX
} sf32lb52_bridge_mapping_output_t;

typedef enum {
    SF32LB52_BRIDGE_MAPPING_COMMAND_NONE = 0,
    SF32LB52_BRIDGE_MAPPING_COMMAND_GET,
    SF32LB52_BRIDGE_MAPPING_COMMAND_SET,
    SF32LB52_BRIDGE_MAPPING_COMMAND_RESET,
    SF32LB52_BRIDGE_MAPPING_COMMAND_SAVE
} sf32lb52_bridge_mapping_command_kind_t;

typedef struct {
    sf32lb52_bridge_mapping_command_kind_t kind;
    sf32lb52_bridge_mapping_profile_t profile;
    sf32lb52_bridge_mapping_output_t output;
    sf32lb52_bridge_button_t target;
    uint8_t source;
} sf32lb52_bridge_mapping_command_t;

void sf32lb52_bridge_mapping_defaults(
    sf32lb52_bridge_mapping_config_t *config);
void sf32lb52_bridge_mapping_profiles_defaults(
    sf32lb52_bridge_mapping_profiles_t *profiles);
bool sf32lb52_bridge_mapping_validate(
    const sf32lb52_bridge_mapping_config_t *config);
bool sf32lb52_bridge_mapping_profiles_validate(
    const sf32lb52_bridge_mapping_profiles_t *profiles);
bool sf32lb52_bridge_mapping_is_identity(
    const sf32lb52_bridge_mapping_config_t *config);
bool sf32lb52_bridge_mapping_set(
    sf32lb52_bridge_mapping_config_t *config,
    sf32lb52_bridge_button_t target,
    uint8_t source);

/* Copies all non-button state and applies target <- source button bindings. */
bool sf32lb52_bridge_mapping_apply(
    const sf32lb52_bridge_mapping_config_t *config,
    const sf32lb52_bridge_input_state_t *input,
    sf32lb52_bridge_input_state_t *output);

/*
 * Patches only native button/trigger bytes. Touch, motion, timestamps and
 * vendor-specific bytes remain byte-for-byte identical to the source report.
 */
bool sf32lb52_bridge_mapping_patch_native_report(
    const sf32lb52_bridge_mapping_config_t *config,
    sf32lb52_bridge_role_t role,
    const sf32lb52_bridge_input_state_t *input,
    uint8_t *report,
    size_t report_len);

const char *sf32lb52_bridge_button_name(uint8_t button);
bool sf32lb52_bridge_button_parse(const char *name, uint8_t *button);

size_t sf32lb52_bridge_mapping_serialize(
    const sf32lb52_bridge_mapping_config_t *config,
    uint8_t *wire,
    size_t wire_capacity);
bool sf32lb52_bridge_mapping_deserialize(
    const uint8_t *wire,
    size_t wire_len,
    sf32lb52_bridge_mapping_config_t *config);

size_t sf32lb52_bridge_mapping_profiles_serialize(
    const sf32lb52_bridge_mapping_profiles_t *profiles,
    uint8_t *wire,
    size_t wire_capacity);
bool sf32lb52_bridge_mapping_profiles_deserialize(
    const uint8_t *wire,
    size_t wire_len,
    sf32lb52_bridge_mapping_profiles_t *profiles);

void sf32lb52_bridge_mapping_routes_defaults(
    sf32lb52_bridge_mapping_routes_t *routes);
bool sf32lb52_bridge_mapping_routes_validate(
    const sf32lb52_bridge_mapping_routes_t *routes);
bool sf32lb52_bridge_mapping_routes_is_identity(
    const sf32lb52_bridge_mapping_routes_t *routes);
sf32lb52_bridge_mapping_config_t *sf32lb52_bridge_mapping_route(
    sf32lb52_bridge_mapping_routes_t *routes,
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output);
void sf32lb52_bridge_mapping_routes_from_profiles(
    sf32lb52_bridge_mapping_routes_t *routes,
    const sf32lb52_bridge_mapping_profiles_t *profiles);
size_t sf32lb52_bridge_mapping_routes_serialize(
    const sf32lb52_bridge_mapping_routes_t *routes,
    uint8_t *wire, size_t wire_capacity);
bool sf32lb52_bridge_mapping_routes_deserialize(
    const uint8_t *wire, size_t wire_len,
    sf32lb52_bridge_mapping_routes_t *routes);
bool sf32lb52_bridge_mapping_routes_v3_deserialize(
    const uint8_t *wire, size_t wire_len,
    sf32lb52_bridge_mapping_routes_t *routes);

/* Edge shares the DS5 mapping route. Xbox has an independent route. */
sf32lb52_bridge_mapping_output_t sf32lb52_bridge_mapping_output_for_role(
    sf32lb52_bridge_role_t role);
const char *sf32lb52_bridge_mapping_output_name(
    sf32lb52_bridge_mapping_output_t output);
bool sf32lb52_bridge_mapping_output_parse(
    const char *name, sf32lb52_bridge_mapping_output_t *output);

const char *sf32lb52_bridge_mapping_profile_name(
    sf32lb52_bridge_mapping_profile_t profile);
bool sf32lb52_bridge_mapping_profile_parse(
    const char *name,
    sf32lb52_bridge_mapping_profile_t *profile);

/* Returns 1 for a valid mapping command, 0 for another command, and -1 for a
 * malformed command beginning with "mapping". */
int sf32lb52_bridge_mapping_parse_command(
    const char *text,
    sf32lb52_bridge_mapping_command_t *command);

/* Formats the manager response used by `mapping get` and successful edits. */
int sf32lb52_bridge_mapping_format_json(
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty,
    char *json,
    size_t json_len);
int sf32lb52_bridge_mapping_format_profile_json(
    sf32lb52_bridge_mapping_profile_t profile,
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty,
    char *json,
    size_t json_len);
int sf32lb52_bridge_mapping_format_route_json(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty, char *json, size_t json_len);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_BRIDGE_MAPPING_H */
