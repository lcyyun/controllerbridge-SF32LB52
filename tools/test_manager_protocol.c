#include "sf32lb52_manager_protocol.h"

#include <assert.h>
#include <string.h>

static void expect_contains(const char *json, const char *field)
{
    assert(strstr(json, field) != 0);
}

static void test_ds5_status(void)
{
    sf32lb52_manager_rumble_status_t status = {0};
    char json[768];

    status.role = SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
    status.source = SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT;
    status.enabled = 1U;
    status.active = 1U;
    status.transport_connected = 1U;
    status.output_pending = 1U;
    status.output_reports = 41U;
    status.output_queued = 3U;
    status.output_busy = 5U;
    status.output_failures = 2U;
    status.scale_percent = 60U;
    status.hold_ms = 140U;
    status.tick_ms = 30U;
    status.stop_packets = 3U;

    assert(sf32lb52_manager_format_rumble_status(
               &status, json, sizeof(json)) > 0);
    expect_contains(json, "\"profile\":\"ds5\"");
    expect_contains(json, "\"role\":\"dse\"");
    expect_contains(json, "\"source\":\"ds5\"");
    expect_contains(json, "\"output_backend\":\"ds5_classic\"");
    expect_contains(json, "\"transport_connected\":true");
    expect_contains(json, "\"output_pending\":true");
    expect_contains(json, "\"output_reports\":41");
    expect_contains(json, "\"output_queued\":3");
    expect_contains(json, "\"output_busy\":5");
    expect_contains(json, "\"output_failures\":2");
    assert(strstr(json, "\"profile\":\"ns2\"") == 0);
}

static void test_ns2_and_idle_status(void)
{
    sf32lb52_manager_rumble_status_t status = {0};
    char json[768];
    char short_buffer[16];

    status.role = SF32LB52_BRIDGE_ROLE_NS2PRO;
    status.source = SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE;
    status.latched = 1U;
    status.output_reports = 17U;
    status.output_failures = 1U;
    status.source_updates = 8U;
    status.source_stops = 4U;
    status.source_ignored = 9U;
    assert(sf32lb52_manager_format_rumble_status(
               &status, json, sizeof(json)) > 0);
    expect_contains(json, "\"profile\":\"ns2\"");
    expect_contains(json, "\"role\":\"ns2pro\"");
    expect_contains(json, "\"output_backend\":\"ns2_ble\"");
    expect_contains(json, "\"rumble_latched\":true");
    expect_contains(json, "\"output_reports\":17");
    expect_contains(json, "\"source_updates\":8");

    status.source = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    assert(sf32lb52_manager_format_rumble_status(
               &status, json, sizeof(json)) > 0);
    expect_contains(json, "\"profile\":\"bridge\"");
    expect_contains(json, "\"output_backend\":\"none\"");

    assert(sf32lb52_manager_format_rumble_status(
               &status, short_buffer, sizeof(short_buffer)) == -1);
    assert(short_buffer[0] == 0);
    assert(sf32lb52_manager_format_rumble_status(0, json, sizeof(json)) == -1);
}

int main(void)
{
    test_ds5_status();
    test_ns2_and_idle_status();
    return 0;
}
