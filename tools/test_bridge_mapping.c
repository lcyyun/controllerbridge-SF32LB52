#include "sf32lb52_bridge_mapping.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define BUTTON(button) SF32LB52_BRIDGE_BUTTON_MASK(button)

static sf32lb52_bridge_input_state_t make_input(void)
{
    sf32lb52_bridge_input_state_t input;

    sf32lb52_bridge_input_state_reset(&input);
    input.valid = 1U;
    input.source = SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT;
    input.buttons = BUTTON(SF32LB52_BRIDGE_BUTTON_EAST);
    input.left_x = 1234;
    input.left_y = -2345;
    input.right_x = 3000;
    input.right_y = -4000;
    input.accel[0] = 11;
    input.gyro[2] = -22;
    input.motion_valid = 1U;
    input.sensor_timestamp = UINT32_C(0x12345678);
    input.timestamp_valid = 1U;
    return input;
}

static void test_identity_and_remap(void)
{
    sf32lb52_bridge_mapping_config_t mapping;
    sf32lb52_bridge_input_state_t input = make_input();
    sf32lb52_bridge_input_state_t output;

    sf32lb52_bridge_mapping_defaults(&mapping);
    assert(sf32lb52_bridge_mapping_validate(&mapping));
    assert(sf32lb52_bridge_mapping_is_identity(&mapping));
    assert(sf32lb52_bridge_mapping_apply(&mapping, &input, &output));
    assert(memcmp(&input, &output, sizeof(input)) == 0);

    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_SOUTH,
        SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_EAST,
        SF32LB52_BRIDGE_MAPPING_NONE));
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_NORTH,
        SF32LB52_BRIDGE_BUTTON_EAST));
    assert(!sf32lb52_bridge_mapping_is_identity(&mapping));
    assert(sf32lb52_bridge_mapping_apply(&mapping, &input, &output));
    assert((output.buttons & BUTTON(SF32LB52_BRIDGE_BUTTON_SOUTH)) != 0U);
    assert((output.buttons & BUTTON(SF32LB52_BRIDGE_BUTTON_NORTH)) != 0U);
    assert((output.buttons & BUTTON(SF32LB52_BRIDGE_BUTTON_EAST)) == 0U);
    assert(output.left_x == input.left_x && output.gyro[2] == input.gyro[2]);
    input.buttons = 0U;
    assert(sf32lb52_bridge_mapping_apply(&mapping, &input, &output));
    assert(output.buttons == 0U);
}

static void test_trigger_targets(void)
{
    sf32lb52_bridge_mapping_config_t mapping;
    sf32lb52_bridge_input_state_t input = make_input();
    sf32lb52_bridge_input_state_t output;

    sf32lb52_bridge_mapping_defaults(&mapping);
    input.buttons = BUTTON(SF32LB52_BRIDGE_BUTTON_SOUTH);
    input.left_trigger = 123U;
    input.right_trigger = 456U;
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER,
        SF32LB52_BRIDGE_BUTTON_SOUTH));
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER,
        SF32LB52_BRIDGE_MAPPING_NONE));
    assert(sf32lb52_bridge_mapping_apply(&mapping, &input, &output));
    assert(output.left_trigger == UINT16_MAX);
    assert(output.right_trigger == 0U);
    assert((output.buttons & BUTTON(SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER)) != 0U);
    assert((output.buttons & BUTTON(SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER)) == 0U);
    input.buttons = 0U;
    assert(sf32lb52_bridge_mapping_apply(&mapping, &input, &output));
    assert(output.left_trigger == 0U && output.right_trigger == 0U);
}

static void test_wire_and_commands(void)
{
    sf32lb52_bridge_mapping_config_t mapping;
    sf32lb52_bridge_mapping_config_t decoded;
    sf32lb52_bridge_mapping_profiles_t profiles;
    sf32lb52_bridge_mapping_profiles_t decoded_profiles;
    sf32lb52_bridge_mapping_command_t command;
    uint8_t legacy_wire[SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE];
    uint8_t profiles_wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];
    char json[1200];

    sf32lb52_bridge_mapping_defaults(&mapping);
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_SOUTH,
        SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_EAST,
        SF32LB52_BRIDGE_MAPPING_NONE));
    assert(sf32lb52_bridge_mapping_serialize(
               &mapping, legacy_wire, sizeof(legacy_wire)) ==
           sizeof(legacy_wire));
    assert(sf32lb52_bridge_mapping_deserialize(
        legacy_wire, sizeof(legacy_wire), &decoded));
    assert(memcmp(&mapping, &decoded, sizeof(mapping)) == 0);
    legacy_wire[8] ^= 1U;
    assert(!sf32lb52_bridge_mapping_deserialize(
        legacy_wire, sizeof(legacy_wire), &decoded));

    sf32lb52_bridge_mapping_profiles_defaults(&profiles);
    assert(sf32lb52_bridge_mapping_set(
        &profiles.ds5, SF32LB52_BRIDGE_BUTTON_SOUTH,
        SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_mapping_set(
        &profiles.ns2pro, SF32LB52_BRIDGE_BUTTON_NORTH,
        SF32LB52_BRIDGE_BUTTON_WEST));
    assert(sf32lb52_bridge_mapping_profiles_serialize(
               &profiles, profiles_wire, sizeof(profiles_wire)) ==
           sizeof(profiles_wire));
    assert(sf32lb52_bridge_mapping_profiles_deserialize(
        profiles_wire, sizeof(profiles_wire), &decoded_profiles));
    assert(memcmp(&profiles, &decoded_profiles, sizeof(profiles)) == 0);
    assert(decoded_profiles.ds5.source_for_target[
               SF32LB52_BRIDGE_BUTTON_SOUTH] ==
           SF32LB52_BRIDGE_BUTTON_EAST);
    assert(decoded_profiles.ns2pro.source_for_target[
               SF32LB52_BRIDGE_BUTTON_NORTH] ==
           SF32LB52_BRIDGE_BUTTON_WEST);
    profiles_wire[48] ^= 1U;
    assert(!sf32lb52_bridge_mapping_profiles_deserialize(
        profiles_wire, sizeof(profiles_wire), &decoded_profiles));

    assert(sf32lb52_bridge_mapping_parse_command("mapping get", &command) == 1);
    assert(command.kind == SF32LB52_BRIDGE_MAPPING_COMMAND_GET);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping get ds5", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_DS5);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping get ns2pro", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set south east", &command) == 1);
    assert(command.kind == SF32LB52_BRIDGE_MAPPING_COMMAND_SET);
    assert(command.target == SF32LB52_BRIDGE_BUTTON_SOUTH);
    assert(command.source == SF32LB52_BRIDGE_BUTTON_EAST);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set ds5 south east", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_DS5);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set ns2pro north west", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set east none", &command) == 1);
    assert(command.source == SF32LB52_BRIDGE_MAPPING_NONE);
    assert(sf32lb52_bridge_mapping_parse_command("mapping reset", &command) == 1);
    assert(sf32lb52_bridge_mapping_parse_command("mapping save", &command) == 1);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping reset ds5", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_DS5);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping save ns2pro", &command) == 1);
    assert(command.profile == SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO);
    assert(sf32lb52_bridge_mapping_parse_command("bridge status", &command) == 0);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set SOUTH east", &command) == -1);
    assert(sf32lb52_bridge_mapping_parse_command(
        "mapping set south east extra", &command) == -1);

    assert(sf32lb52_bridge_mapping_format_json(
        &mapping, true, json, sizeof(json)) > 0);
    assert(strstr(json, "\"identity\":false") != 0);
    assert(strstr(json, "\"dirty\":true") != 0);
    assert(strstr(json, "\"south\":\"east\"") != 0);
    assert(strstr(json, "\"east\":\"none\"") != 0);
    assert(sf32lb52_bridge_mapping_format_profile_json(
               SF32LB52_BRIDGE_MAPPING_PROFILE_DS5, &mapping, true,
               json, sizeof(json)) > 0);
    assert(strstr(json, "\"profile\":\"ds5\"") != 0);
    assert(sf32lb52_bridge_mapping_format_profile_json(
               SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
               &profiles.ns2pro, false, json, sizeof(json)) > 0);
    assert(strstr(json, "\"profile\":\"ns2pro\"") != 0);
}

static void assert_preserved_except(const uint8_t *before,
                                    const uint8_t *after,
                                    size_t len,
                                    const uint8_t *changed,
                                    size_t changed_count)
{
    size_t index;

    for (index = 0U; index < len; ++index) {
        size_t item;
        int may_change = 0;

        for (item = 0U; item < changed_count; ++item) {
            if (index == changed[item]) {
                may_change = 1;
            }
        }
        if (!may_change) {
            assert(before[index] == after[index]);
        }
    }
}

static void test_native_report_patching(void)
{
    static const uint8_t ds5_changed[] = {5U, 6U, 8U, 9U, 10U};
    static const uint8_t ns2_changed[] = {5U, 6U, 7U, 8U};
    sf32lb52_bridge_mapping_config_t mapping;
    sf32lb52_bridge_input_state_t input = make_input();
    uint8_t report[64];
    uint8_t before[64];

    sf32lb52_bridge_mapping_defaults(&mapping);
    memset(report, 0x3c, sizeof(report));
    report[0] = 0x01U;
    memcpy(before, report, sizeof(report));
    assert(sf32lb52_bridge_mapping_patch_native_report(
        &mapping, SF32LB52_BRIDGE_ROLE_DUALSENSE,
        &input, report, sizeof(report)));
    assert(memcmp(before, report, sizeof(report)) == 0);

    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_SOUTH,
        SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_mapping_set(
        &mapping, SF32LB52_BRIDGE_BUTTON_EAST,
        SF32LB52_BRIDGE_MAPPING_NONE));

    memset(report, 0xa5, sizeof(report));
    report[0] = 0x01U;
    report[5] = 0U;
    report[6] = 0U;
    report[8] = 0x48U; /* neutral d-pad + east */
    report[9] = 0U;
    report[10] = 0x08U; /* reserved bit must survive */
    memcpy(before, report, sizeof(report));
    assert(sf32lb52_bridge_mapping_patch_native_report(
        &mapping, SF32LB52_BRIDGE_ROLE_DUALSENSE,
        &input, report, sizeof(report)));
    assert(report[8] == 0x28U); /* neutral d-pad + mapped south */
    assert(report[10] == 0x08U);
    assert_preserved_except(before, report, sizeof(report),
                            ds5_changed, sizeof(ds5_changed));

    memset(report, 0x5a, sizeof(report));
    report[0] = 0x05U;
    report[5] = 0x38U; /* east + reserved bits */
    report[6] = 0x80U;
    report[7] = 0x30U;
    report[8] = 0xfcU;
    memcpy(before, report, sizeof(report));
    assert(sf32lb52_bridge_mapping_patch_native_report(
        &mapping, SF32LB52_BRIDGE_ROLE_NS2PRO,
        &input, report, sizeof(report)));
    assert(report[5] == 0x34U); /* mapped south + reserved bits */
    assert(report[6] == 0x80U && report[7] == 0x30U && report[8] == 0xfcU);
    assert_preserved_except(before, report, sizeof(report),
                            ns2_changed, sizeof(ns2_changed));
}

static void repair_wire_crc(uint8_t *wire, size_t len)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;
    unsigned int bit;

    for (i = 0U; i < len - 4U; ++i) {
        crc ^= wire[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    crc = ~crc;
    for (i = 0U; i < 4U; ++i) {
        wire[len - 4U + i] = (uint8_t)(crc >> (8U * i));
    }
}

static void test_wire_validation_is_atomic(void)
{
    static const size_t invalid_fields[] = {0U, 4U, 5U, 6U, 7U, 8U, 48U};
    sf32lb52_bridge_mapping_profiles_t profiles;
    sf32lb52_bridge_mapping_profiles_t decoded;
    sf32lb52_bridge_mapping_profiles_t sentinel;
    sf32lb52_bridge_mapping_config_t legacy;
    sf32lb52_bridge_mapping_config_t legacy_before;
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE + 1U];
    uint8_t good[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];
    uint8_t legacy_wire[SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE];
    size_t i;

    sf32lb52_bridge_mapping_profiles_defaults(&profiles);
    profiles.ds5.source_for_target[0] = SF32LB52_BRIDGE_MAPPING_NONE;
    profiles.ns2pro.source_for_target[1] = SF32LB52_BRIDGE_BUTTON_C;
    assert(sf32lb52_bridge_mapping_profiles_serialize(
        &profiles, good, sizeof(good)) == sizeof(good));
    memset(&sentinel, 0xa5, sizeof(sentinel));
    for (i = 0U; i < SF32LB52_BRIDGE_MAPPING_WIRE_SIZE; ++i) {
        memset(wire, 0x5a, sizeof(wire));
        assert(sf32lb52_bridge_mapping_profiles_serialize(
            &profiles, wire, i) == 0U);
        assert(wire[0] == 0x5aU && wire[sizeof(wire) - 1U] == 0x5aU);
        decoded = sentinel;
        assert(!sf32lb52_bridge_mapping_profiles_deserialize(good, i, &decoded));
        assert(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    }
    memcpy(wire, good, sizeof(good));
    wire[sizeof(good)] = 0U;
    decoded = sentinel;
    assert(!sf32lb52_bridge_mapping_profiles_deserialize(
        wire, sizeof(wire), &decoded));
    assert(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);

    for (i = 0U; i < sizeof(invalid_fields) / sizeof(invalid_fields[0]); ++i) {
        memcpy(wire, good, sizeof(good));
        wire[invalid_fields[i]] = 0xfeU;
        repair_wire_crc(wire, sizeof(good));
        decoded = sentinel;
        assert(!sf32lb52_bridge_mapping_profiles_deserialize(
            wire, sizeof(good), &decoded));
        assert(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    }
    assert(sf32lb52_bridge_mapping_serialize(
        &profiles.ds5, legacy_wire, sizeof(legacy_wire)) == sizeof(legacy_wire));
    legacy_wire[8] = SF32LB52_BRIDGE_BUTTON_COUNT;
    repair_wire_crc(legacy_wire, sizeof(legacy_wire));
    legacy_before = profiles.ns2pro;
    legacy = legacy_before;
    assert(!sf32lb52_bridge_mapping_deserialize(
        legacy_wire, sizeof(legacy_wire), &legacy));
    assert(memcmp(&legacy, &legacy_before, sizeof(legacy)) == 0);

    for (i = 0U; i < 2U; ++i) {
        sf32lb52_bridge_mapping_profiles_t invalid = profiles;
        sf32lb52_bridge_mapping_config_t *config =
            i == 0U ? &invalid.ds5 : &invalid.ns2pro;
        config->source_for_target[SF32LB52_BRIDGE_BUTTON_C] =
            SF32LB52_BRIDGE_BUTTON_COUNT;
        memset(wire, 0xa5, sizeof(wire));
        assert(!sf32lb52_bridge_mapping_profiles_validate(&invalid));
        assert(sf32lb52_bridge_mapping_profiles_serialize(
            &invalid, wire, sizeof(wire)) == 0U);
        assert(wire[0] == 0xa5U && wire[sizeof(wire) - 1U] == 0xa5U);
    }
    assert(!sf32lb52_bridge_mapping_profiles_deserialize(
        NULL, sizeof(good), &decoded));
    assert(!sf32lb52_bridge_mapping_profiles_deserialize(
        good, sizeof(good), NULL));
}

static void test_four_route_wire_and_commands(void)
{
    const sf32lb52_bridge_mapping_profile_t sources[] = {
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
    };
    const sf32lb52_bridge_mapping_output_t outputs[] = {
        SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5,
        SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO,
        SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX,
    };
    const char *verbs[] = {"get", "reset", "save", "set"};
    const char *bad_commands[] = {
        "mapping get ds5 unknown", "mapping get ds5 ns2pro extra",
        "mapping get none ds5", "mapping set ds5 ns2pro south",
        "mapping set ds5 ns2pro none south", "mapping set ds5 ns2pro south BAD",
        "mapping set ds5 ns2pro south east extra",
        "mapping reset ns2pro unknown", "mapping save ds5 ns2pro extra",
        "mapping set ds5 ds5 this_token_is_far_too_long_for_the_parser east",
    };
    sf32lb52_bridge_mapping_routes_t routes, decoded, sentinel;
    sf32lb52_bridge_mapping_command_t command;
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE + 1U];
    uint8_t good[SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE];
    char text[128], json[2049];
    size_t s, o, v, i;

    sf32lb52_bridge_mapping_routes_defaults(&routes);
    assert(sf32lb52_bridge_mapping_routes_is_identity(&routes));
    for (s = 0U; s < 2U; ++s) {
        for (o = 0U; o < 3U; ++o) {
            sf32lb52_bridge_mapping_config_t *config =
                sf32lb52_bridge_mapping_route(&routes, sources[s], outputs[o]);
            assert(config != NULL);
            config->source_for_target[0] = (uint8_t)(s * 3U + o + 1U);
            config->source_for_target[24] = SF32LB52_BRIDGE_MAPPING_NONE;
            for (v = 0U; v < 4U; ++v) {
                snprintf(text, sizeof(text), "mapping %s %s %s%s", verbs[v],
                         sf32lb52_bridge_mapping_profile_name(sources[s]),
                         sf32lb52_bridge_mapping_output_name(outputs[o]),
                         v == 3U ? " south none" : "");
                assert(sf32lb52_bridge_mapping_parse_command(text, &command) == 1);
                assert(command.profile == sources[s] && command.output == outputs[o]);
                if (v == 3U) {
                    assert(command.source == SF32LB52_BRIDGE_MAPPING_NONE);
                }
            }
            assert(sf32lb52_bridge_mapping_format_route_json(
                sources[s], outputs[o], config, true, json, sizeof(json)) > 0);
            snprintf(text, sizeof(text), "\"profile\":\"%s\",\"output\":\"%s\"",
                     sf32lb52_bridge_mapping_profile_name(sources[s]),
                     sf32lb52_bridge_mapping_output_name(outputs[o]));
            assert(strstr(json, text) != NULL);
            assert(strstr(json, "\"mapping_schema\":4") != NULL);
            assert(sf32lb52_bridge_mapping_format_route_json(
                sources[s], outputs[o], config, true, text, 8U) == -1);
        }
    }
    assert(!sf32lb52_bridge_mapping_routes_is_identity(&routes));
    assert(sf32lb52_bridge_mapping_routes_serialize(&routes, good, sizeof(good)) ==
           sizeof(good));
    assert(good[4] == 4U && good[5] == 106U && good[6] == 25U && good[7] == 6U);
    assert(sf32lb52_bridge_mapping_routes_deserialize(good, sizeof(good), &decoded));
    assert(memcmp(&routes, &decoded, sizeof(routes)) == 0);
    memset(&sentinel, 0xa5, sizeof(sentinel));
    memset(wire, 0, sizeof(wire));
    for (i = 0U; i <= sizeof(good); ++i) {
        memcpy(wire, good, sizeof(good));
        wire[i] ^= 1U;
        decoded = sentinel;
        assert(!sf32lb52_bridge_mapping_routes_deserialize(
            wire, i == sizeof(good) ? sizeof(wire) : sizeof(good), &decoded));
        assert(memcmp(&sentinel, &decoded, sizeof(decoded)) == 0);
    }
    for (i = 0U; i < sizeof(good); ++i) {
        assert(!sf32lb52_bridge_mapping_routes_deserialize(good, i, &decoded));
        assert(sf32lb52_bridge_mapping_routes_serialize(&routes, wire, i) == 0U);
    }
    for (i = 0U; i < 6U; ++i) {
        size_t bit = i * 25U * 5U;
        size_t byte = 8U + bit / 8U;
        unsigned int shift = (unsigned int)(bit % 8U);
        uint16_t pair;
        memcpy(wire, good, sizeof(good));
        pair = (uint16_t)(wire[byte] | ((uint16_t)wire[byte + 1U] << 8U));
        pair |= (uint16_t)(31U << shift);
        wire[byte] = (uint8_t)pair;
        wire[byte + 1U] = (uint8_t)(pair >> 8U);
        repair_wire_crc(wire, sizeof(good));
        decoded = sentinel;
        assert(!sf32lb52_bridge_mapping_routes_deserialize(wire, sizeof(good), &decoded));
        assert(memcmp(&sentinel, &decoded, sizeof(decoded)) == 0);
        decoded = routes;
        sf32lb52_bridge_mapping_route(&decoded, sources[i / 3U], outputs[i % 3U])
            ->source_for_target[0] = SF32LB52_BRIDGE_BUTTON_COUNT;
        assert(sf32lb52_bridge_mapping_routes_serialize(&decoded, wire, sizeof(wire)) == 0U);
    }
    assert(sf32lb52_bridge_mapping_route(&routes, sources[0],
        SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED) == NULL);
    assert(sf32lb52_bridge_mapping_route(&routes,
        SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED, outputs[0]) == NULL);
    assert(!sf32lb52_bridge_mapping_routes_deserialize(NULL, sizeof(good), &decoded));
    assert(!sf32lb52_bridge_mapping_routes_deserialize(good, sizeof(good), NULL));
    for (i = 0U; i < sizeof(bad_commands) / sizeof(bad_commands[0]); ++i) {
        assert(sf32lb52_bridge_mapping_parse_command(bad_commands[i], &command) == -1);
    }
    assert(sf32lb52_bridge_mapping_parse_command("mapping get edge dse", &command) == 1);
    assert(command.profile == sources[0] && command.output == outputs[0]);
    assert(sf32lb52_bridge_mapping_parse_command("mapping get ds5", &command) == 1);
    assert(command.output == SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED);
    assert(sf32lb52_bridge_mapping_output_for_role(SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE) ==
           outputs[0]);
    assert(sf32lb52_bridge_mapping_output_for_role(SF32LB52_BRIDGE_ROLE_XBOX_360) ==
           outputs[2]);
}

int main(void)
{
    test_identity_and_remap();
    test_trigger_targets();
    test_wire_and_commands();
    test_native_report_patching();
    test_wire_validation_is_atomic();
    test_four_route_wire_and_commands();
    return 0;
}
