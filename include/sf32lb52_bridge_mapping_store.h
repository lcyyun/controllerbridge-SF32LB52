#ifndef SF32LB52_BRIDGE_MAPPING_STORE_H
#define SF32LB52_BRIDGE_MAPPING_STORE_H

#include "sf32lb52_bridge_mapping.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t backend_available;
    uint8_t initialized;
    uint8_t loaded;
    uint8_t used_defaults;
    uint8_t last_save_ok;
    uint32_t load_failures;
    uint32_t save_failures;
} sf32lb52_bridge_mapping_store_status_t;

bool sf32lb52_bridge_mapping_store_load(
    sf32lb52_bridge_mapping_routes_t *routes);
bool sf32lb52_bridge_mapping_store_save(
    const sf32lb52_bridge_mapping_routes_t *routes);
bool sf32lb52_bridge_mapping_store_reset(
    sf32lb52_bridge_mapping_routes_t *routes);
void sf32lb52_bridge_mapping_store_get_status(
    sf32lb52_bridge_mapping_store_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_BRIDGE_MAPPING_STORE_H */
