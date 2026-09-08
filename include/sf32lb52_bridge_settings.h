#ifndef SF32LB52_BRIDGE_SETTINGS_H
#define SF32LB52_BRIDGE_SETTINGS_H

#include "sf32lb52_bridge_protocol.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO = 0,
    SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE,
    SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO,
} sf32lb52_bridge_input_preference_t;

typedef struct {
    sf32lb52_bridge_role_t output_role;
    sf32lb52_bridge_input_preference_t input_preference;
    sf32lb52_bridge_input_source_t last_active_input;
    uint8_t auto_connect;
    uint8_t ds5_address_valid;
    uint8_t ds5_address[6];
    uint8_t ns2_address_valid;
    uint8_t ns2_address_type;
    uint8_t ns2_address[6];
    uint16_t generation;
} sf32lb52_bridge_persisted_config_t;

typedef struct {
    uint8_t backend_available;
    uint8_t initialized;
    uint8_t loaded;
    uint8_t used_defaults;
    uint8_t last_save_ok;
    uint32_t load_failures;
    uint32_t save_failures;
} sf32lb52_bridge_settings_status_t;

void sf32lb52_bridge_settings_defaults(sf32lb52_bridge_persisted_config_t *config);
bool sf32lb52_bridge_settings_load(sf32lb52_bridge_persisted_config_t *config);
bool sf32lb52_bridge_settings_save(sf32lb52_bridge_persisted_config_t *config);
bool sf32lb52_bridge_settings_reset(sf32lb52_bridge_persisted_config_t *config);
void sf32lb52_bridge_settings_get_status(sf32lb52_bridge_settings_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_BRIDGE_SETTINGS_H */
