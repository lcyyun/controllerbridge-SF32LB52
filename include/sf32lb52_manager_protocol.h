#ifndef SF32LB52_MANAGER_PROTOCOL_H
#define SF32LB52_MANAGER_PROTOCOL_H

#include "sf32lb52_bridge_protocol.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    sf32lb52_bridge_role_t role;
    sf32lb52_bridge_input_source_t source;
    uint8_t enabled;
    uint8_t active;
    uint8_t latched;
    uint8_t transport_connected;
    uint8_t output_pending;
    uint16_t scale_percent;
    uint16_t hold_ms;
    uint16_t tick_ms;
    uint8_t stop_packets;
    uint32_t output_reports;
    uint32_t output_queued;
    uint32_t output_busy;
    uint32_t output_failures;
    uint32_t source_updates;
    uint32_t source_stops;
    uint32_t source_ignored;
} sf32lb52_manager_rumble_status_t;

const char *sf32lb52_manager_profile_name(
    sf32lb52_bridge_input_source_t source);
const char *sf32lb52_manager_role_name(sf32lb52_bridge_role_t role);
const char *sf32lb52_manager_output_backend_name(
    sf32lb52_bridge_input_source_t source);

/* Returns the JSON byte count, or -1 if the arguments/buffer are invalid. */
int sf32lb52_manager_format_rumble_status(
    const sf32lb52_manager_rumble_status_t *status,
    char *out,
    size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_MANAGER_PROTOCOL_H */
