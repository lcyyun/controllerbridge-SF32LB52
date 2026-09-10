/* Exercise the real command handler and feature queue. Hardware stubs abort
 * if a mapping command accidentally reaches Bluetooth, USB I/O or rumble. */
#include "../src/ns2_profile.c"
#include "sf32lb52_bridge_mapping_store.h"

#include <assert.h>

static Sf32lb52UsbRole actual_role = Sf32lb52UsbRoleNintendo;

Sf32lb52UsbRole sf32lb52_usb_get_role(void)
{
    return actual_role;
}

uint32_t platform_millis(void) { assert(!"unexpected clock access"); return 0U; }
uint32_t platform_micros(void) { assert(!"unexpected clock access"); return 0U; }

void ble_gatt_get_status(ble_gatt_status_t *status)
{
    (void)status;
    assert(!"unexpected BLE access");
}

void ds5_classic_get_status(ds5_classic_status_t *status)
{
    (void)status;
    assert(!"unexpected Bluetooth access");
}

void sf32lb52_usb_get_status(Sf32lb52UsbDeviceStatus *status)
{
    (void)status;
    assert(!"unexpected USB status access");
}

bool sf32lb52_usb_get_role_capabilities(Sf32lb52UsbRole role,
                                       Sf32lb52UsbRoleCapabilities *out)
{
    (void)role;
    (void)out;
    assert(!"unexpected USB capability access");
    return false;
}

int ble_gatt_write_handle(uint16_t handle, const uint8_t *data, size_t len)
{
    (void)handle;
    (void)data;
    (void)len;
    assert(!"unexpected BLE write");
    return -1;
}

int sf32lb52_app_test_rumble(uint16_t left, uint16_t right)
{
    (void)left;
    (void)right;
    assert(!"unexpected rumble");
    return -1;
}

int sf32lb52_app_request_input_action(sf32lb52_app_input_action_t action)
{
    (void)action;
    assert(!"unexpected input action");
    return -1;
}

static const char *run_command(const char *text)
{
    static char reply[SF32LB52_NS2_FEATURE_REPLY_CAPACITY + 1U];
    sf32lb52_ns2_feature_reply_t *queued;

    assert(handle_mapping_command(text) == 1);
    queued = &g_feature_replies[g_feature_reply_index];
    assert(queued->length <= SF32LB52_NS2_FEATURE_REPLY_CAPACITY);
    memcpy(reply, queued->bytes, queued->length);
    reply[queued->length] = '\0';
    return reply;
}

static void assert_route_reply(const char *reply, const char *source,
                               const char *output, const char *south)
{
    char field[96];

    assert(strstr(reply, "\"ok\":true") != NULL);
    assert(strstr(reply, "\"mapping_schema\":3") != NULL);
    snprintf(field, sizeof(field), "\"profile\":\"%s\"", source);
    assert(strstr(reply, field) != NULL);
    snprintf(field, sizeof(field), "\"output\":\"%s\"", output);
    assert(strstr(reply, field) != NULL);
    snprintf(field, sizeof(field), "\"south\":\"%s\"", south);
    assert(strstr(reply, field) != NULL);
}

static void reset_routes(void)
{
    sf32lb52_bridge_mapping_routes_t defaults;

    assert(sf32lb52_bridge_mapping_store_reset(&defaults));
    sf32lb52_bridge_runtime_init();
}

static void test_explicit_routes(void)
{
    const char *sources[] = {"ds5", "ns2pro"};
    const char *outputs[] = {"ds5", "ns2pro"};
    const char *buttons[] = {"east", "west", "north", "none"};
    char text[128];
    int s, o;

    reset_routes();
    for (s = 0; s < 2; ++s) {
        for (o = 0; o < 2; ++o) {
            snprintf(text, sizeof(text), "mapping set %s %s south %s",
                     sources[s], outputs[o], buttons[s * 2 + o]);
            assert_route_reply(run_command(text), sources[s], outputs[o],
                               buttons[s * 2 + o]);
        }
    }
    for (s = 0; s < 2; ++s) {
        for (o = 0; o < 2; ++o) {
            snprintf(text, sizeof(text), "mapping get %s %s", sources[s], outputs[o]);
            assert_route_reply(run_command(text), sources[s], outputs[o],
                               buttons[s * 2 + o]);
        }
    }
    assert_route_reply(run_command("mapping save ds5 ns2pro"), "ds5", "ns2pro", "west");
    sf32lb52_bridge_runtime_init();
    assert_route_reply(run_command("mapping get ds5 ns2pro"), "ds5", "ns2pro", "west");
    assert_route_reply(run_command("mapping get ds5 ds5"), "ds5", "ds5", "south");
    assert_route_reply(run_command("mapping get ns2pro ds5"), "ns2pro", "ds5", "south");
    assert_route_reply(run_command("mapping get ns2pro ns2pro"), "ns2pro", "ns2pro", "south");
    assert_route_reply(run_command("mapping reset ds5 ns2pro"), "ds5", "ns2pro", "south");
    sf32lb52_bridge_runtime_init();
    assert_route_reply(run_command("mapping get ds5 ns2pro"), "ds5", "ns2pro", "west");
    run_command("mapping reset ds5 ns2pro");
    run_command("mapping save ds5 ns2pro");
    sf32lb52_bridge_runtime_init();
    assert_route_reply(run_command("mapping get ds5 ns2pro"), "ds5", "ns2pro", "south");
}

static void test_legacy_uses_actual_usb(void)
{
    sf32lb52_bridge_input_state_t input;

    reset_routes();
    actual_role = Sf32lb52UsbRoleNintendo;
    assert(sf32lb52_bridge_runtime_set_role(SF32LB52_BRIDGE_ROLE_DUALSENSE, false));
    assert_route_reply(run_command("mapping set ds5 south east"), "ds5", "ns2pro", "east");
    assert_route_reply(run_command("mapping get ds5"), "ds5", "ns2pro", "east");
    actual_role = Sf32lb52UsbRoleDualSenseEdge;
    assert_route_reply(run_command("mapping get ds5"), "ds5", "ds5", "south");
    assert_route_reply(run_command("mapping set edge dse south north"), "ds5", "ds5", "north");
    actual_role = Sf32lb52UsbRoleXbox360;
    assert_route_reply(run_command("mapping get ds5"), "ds5", "ds5", "north");

    sf32lb52_bridge_input_state_reset(&input);
    input.valid = 1U;
    input.source = SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE;
    assert(sf32lb52_bridge_runtime_accept_input(&input, 100U));
    assert_route_reply(run_command("mapping set south west"), "ns2pro", "ds5", "west");
    assert_route_reply(run_command("mapping get"), "ns2pro", "ds5", "west");
    /* Legacy save still checkpoints every draft, including other outputs. */
    assert_route_reply(run_command("mapping save ns2pro"), "ns2pro", "ds5", "west");
    sf32lb52_bridge_runtime_init();
    assert_route_reply(run_command("mapping get ns2pro ds5"), "ns2pro", "ds5", "west");
    assert_route_reply(run_command("mapping get ds5 ns2pro"), "ds5", "ns2pro", "east");
    assert_route_reply(run_command("mapping reset ns2pro"), "ns2pro", "ds5", "south");
    assert_route_reply(run_command("mapping get ds5 ds5"), "ds5", "ds5", "north");
    assert(strstr(run_command("mapping get ds5 xbox"), "\"ok\":false") != NULL);
    assert(strstr(run_command("mapping set ds5 ns2pro none east"), "\"ok\":false") != NULL);
    assert_route_reply(run_command("mapping get ds5 ns2pro"), "ds5", "ns2pro", "east");
    assert(handle_mapping_command("status") == 0);
}

static void test_route_dirty_replies(void)
{
    sf32lb52_bridge_runtime_status_t status;
    const char *reply;

    reset_routes();
    reply = run_command("mapping set ds5 ds5 south east");
    assert(strstr(reply, "\"dirty\":true") != NULL);
    reply = run_command("mapping set ns2pro ns2pro south west");
    assert(strstr(reply, "\"dirty\":true") != NULL);
    reply = run_command("mapping save ds5 ds5");
    assert_route_reply(reply, "ds5", "ds5", "east");
    assert(strstr(reply, "\"dirty\":false") != NULL);
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_dirty == 1U);
    assert(strstr(run_command("mapping get ds5 ds5"), "\"dirty\":false") != NULL);
    assert(strstr(run_command("mapping get ns2pro ns2pro"), "\"dirty\":true") != NULL);
    assert(strstr(run_command("mapping get ds5 ns2pro"), "\"dirty\":false") != NULL);
    assert(strstr(run_command("mapping save ns2pro ns2pro"), "\"dirty\":false") != NULL);
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_dirty == 0U);
    assert(strstr(run_command("mapping set ds5 ds5 south east"), "\"dirty\":false") != NULL);
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_dirty == 0U);
    fake_nvds_fail_writes(true);
    assert(strstr(run_command("mapping set ds5 ds5 south north"), "\"dirty\":true") != NULL);
    assert(strstr(run_command("mapping save ds5 ds5"), "\"ok\":false") != NULL);
    assert(strstr(run_command("mapping get ds5 ds5"), "\"dirty\":true") != NULL);
    fake_nvds_fail_writes(false);
    assert(strstr(run_command("mapping save ds5 ds5"), "\"dirty\":false") != NULL);
    assert(!sf32lb52_bridge_runtime_get_route_mapping_dirty(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
        SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5, NULL));
}

int main(void)
{
    test_explicit_routes();
    test_legacy_uses_actual_usb();
    test_route_dirty_replies();
    puts("OK real mapping command handler: four routes, legacy role resolution, JSON and saves");
    return 0;
}
