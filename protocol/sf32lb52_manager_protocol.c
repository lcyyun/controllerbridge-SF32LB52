#include "sf32lb52_manager_protocol.h"

#include <stdio.h>

const char *sf32lb52_manager_profile_name(
    sf32lb52_bridge_input_source_t source)
{
    switch (source) {
    case SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT:
        return "ds5";
    case SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE:
        return "ns2";
    default:
        return "bridge";
    }
}

const char *sf32lb52_manager_role_name(sf32lb52_bridge_role_t role)
{
    switch (role) {
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
        return "xbox";
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
        return "ds5";
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        return "dse";
    case SF32LB52_BRIDGE_ROLE_NS2PRO:
        return "ns2pro";
    default:
        return "unknown";
    }
}

static const char *sf32lb52_manager_source_name(
    sf32lb52_bridge_input_source_t source)
{
    switch (source) {
    case SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT:
        return "ds5";
    case SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE:
        return "ns2pro";
    default:
        return "none";
    }
}

const char *sf32lb52_manager_output_backend_name(
    sf32lb52_bridge_input_source_t source)
{
    switch (source) {
    case SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT:
        return "ds5_classic";
    case SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE:
        return "ns2_ble";
    default:
        return "none";
    }
}

int sf32lb52_manager_format_rumble_status(
    const sf32lb52_manager_rumble_status_t *status,
    char *out,
    size_t out_len)
{
    int written;

    if (status == 0 || out == 0 || out_len == 0U) {
        return -1;
    }

    written = snprintf(
        out,
        out_len,
        "{\"ok\":true,\"profile\":\"%s\",\"role\":\"%s\","
        "\"source\":\"%s\",\"output_backend\":\"%s\","
        "\"rumble_enabled\":%s,\"rumble_active\":%s,"
        "\"rumble_latched\":%s,"
        "\"transport_connected\":%s,\"output_pending\":%s,"
        "\"output_reports\":%lu,\"output_queued\":%lu,"
        "\"output_busy\":%lu,\"output_failures\":%lu,"
        "\"source_updates\":%lu,\"source_stops\":%lu,"
        "\"source_ignored\":%lu,\"scale_percent\":%u,"
        "\"hold_ms\":%u,\"tick_ms\":%u,\"stop_packets\":%u}",
        sf32lb52_manager_profile_name(status->source),
        sf32lb52_manager_role_name(status->role),
        sf32lb52_manager_source_name(status->source),
        sf32lb52_manager_output_backend_name(status->source),
        status->enabled ? "true" : "false",
        status->active ? "true" : "false",
        status->latched ? "true" : "false",
        status->transport_connected ? "true" : "false",
        status->output_pending ? "true" : "false",
        (unsigned long)status->output_reports,
        (unsigned long)status->output_queued,
        (unsigned long)status->output_busy,
        (unsigned long)status->output_failures,
        (unsigned long)status->source_updates,
        (unsigned long)status->source_stops,
        (unsigned long)status->source_ignored,
        (unsigned int)status->scale_percent,
        (unsigned int)status->hold_ms,
        (unsigned int)status->tick_ms,
        (unsigned int)status->stop_packets);
    if (written < 0 || (size_t)written >= out_len) {
        if (out_len != 0U) {
            out[0] = 0;
        }
        return -1;
    }
    return written;
}
