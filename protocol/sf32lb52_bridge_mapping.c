#include "sf32lb52_bridge_mapping.h"

#include <stdio.h>
#include <string.h>

#define MAPPING_WIRE_VERSION 1U
#define MAPPING_PROFILES_WIRE_VERSION 2U
#define MAPPING_CONFIG_CRC_OFFSET 36U
#define MAPPING_PROFILES_CRC_OFFSET 76U
#define MAPPING_ROUTES_V3_CRC_OFFSET 108U
#define MAPPING_ROUTES_CRC_OFFSET 102U
#define MAPPING_ROUTES_DATA_OFFSET 8U
#define MAPPING_PACKED_BITS_PER_SOURCE 5U
#define MAPPING_PACKED_NONE SF32LB52_BRIDGE_BUTTON_COUNT
#define DS5_REPORT_ID 0x01U
#define NS2_REPORT_ID 0x05U

_Static_assert(SF32LB52_BRIDGE_BUTTON_COUNT == 25U,
               "Mapping wire format requires exactly 25 buttons");

static const uint8_t g_mapping_magic[4] = {'S', 'F', 'M', '1'};
static const char *const g_button_names[SF32LB52_BRIDGE_BUTTON_COUNT] = {
    "south",
    "east",
    "west",
    "north",
    "dpad_up",
    "dpad_down",
    "dpad_left",
    "dpad_right",
    "left_shoulder",
    "right_shoulder",
    "left_trigger",
    "right_trigger",
    "back",
    "start",
    "left_stick",
    "right_stick",
    "guide",
    "touchpad",
    "mute",
    "capture",
    "left_paddle",
    "right_paddle",
    "left_function",
    "right_function",
    "c",
};

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;

    for (i = 0U; i < len; ++i) {
        uint8_t bit;

        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)((value >> 8U) & 0xffU);
    out[2] = (uint8_t)((value >> 16U) & 0xffU);
    out[3] = (uint8_t)((value >> 24U) & 0xffU);
}

static uint32_t get_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0] |
           ((uint32_t)in[1] << 8U) |
           ((uint32_t)in[2] << 16U) |
           ((uint32_t)in[3] << 24U);
}

static bool source_valid(uint8_t source)
{
    return source < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT ||
           source == SF32LB52_BRIDGE_MAPPING_NONE;
}

static bool source_active(const sf32lb52_bridge_input_state_t *input,
                          uint8_t source)
{
    if (source == SF32LB52_BRIDGE_MAPPING_NONE) {
        return false;
    }
    if (source == (uint8_t)SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER &&
        input->left_trigger != 0U) {
        return true;
    }
    if (source == (uint8_t)SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER &&
        input->right_trigger != 0U) {
        return true;
    }
    return (input->buttons & SF32LB52_BRIDGE_BUTTON_MASK(source)) != 0U;
}

static uint16_t mapped_trigger(const sf32lb52_bridge_input_state_t *input,
                               uint8_t source)
{
    if (source == (uint8_t)SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER) {
        return input->left_trigger;
    }
    if (source == (uint8_t)SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER) {
        return input->right_trigger;
    }
    return source_active(input, source) ? UINT16_MAX : 0U;
}

static const char *skip_spaces(const char *text)
{
    while (text != 0 && (*text == ' ' || *text == '\t')) {
        ++text;
    }
    return text;
}

static bool next_token(const char **cursor, char *token, size_t token_len)
{
    const char *start;
    const char *end;
    size_t len;

    if (cursor == 0 || *cursor == 0 || token == 0 || token_len == 0U) {
        return false;
    }
    start = skip_spaces(*cursor);
    if (*start == 0) {
        return false;
    }
    end = start;
    while (*end != 0 && *end != ' ' && *end != '\t') {
        ++end;
    }
    len = (size_t)(end - start);
    if (len == 0U || len >= token_len) {
        return false;
    }
    memcpy(token, start, len);
    token[len] = 0;
    *cursor = end;
    return true;
}

static bool at_end(const char *cursor)
{
    return cursor != 0 && *skip_spaces(cursor) == 0;
}

void sf32lb52_bridge_mapping_defaults(
    sf32lb52_bridge_mapping_config_t *config)
{
    uint8_t button;

    if (config == 0) {
        return;
    }
    for (button = 0U;
         button < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++button) {
        config->source_for_target[button] = button;
    }
}

void sf32lb52_bridge_mapping_profiles_defaults(
    sf32lb52_bridge_mapping_profiles_t *profiles)
{
    if (profiles == 0) {
        return;
    }
    sf32lb52_bridge_mapping_defaults(&profiles->ds5);
    sf32lb52_bridge_mapping_defaults(&profiles->ns2pro);
}

bool sf32lb52_bridge_mapping_validate(
    const sf32lb52_bridge_mapping_config_t *config)
{
    uint8_t target;

    if (config == 0) {
        return false;
    }
    for (target = 0U;
         target < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++target) {
        if (!source_valid(config->source_for_target[target])) {
            return false;
        }
    }
    return true;
}

bool sf32lb52_bridge_mapping_profiles_validate(
    const sf32lb52_bridge_mapping_profiles_t *profiles)
{
    return profiles != 0 &&
           sf32lb52_bridge_mapping_validate(&profiles->ds5) &&
           sf32lb52_bridge_mapping_validate(&profiles->ns2pro);
}

bool sf32lb52_bridge_mapping_is_identity(
    const sf32lb52_bridge_mapping_config_t *config)
{
    uint8_t target;

    if (!sf32lb52_bridge_mapping_validate(config)) {
        return false;
    }
    for (target = 0U;
         target < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++target) {
        if (config->source_for_target[target] != target) {
            return false;
        }
    }
    return true;
}

bool sf32lb52_bridge_mapping_set(
    sf32lb52_bridge_mapping_config_t *config,
    sf32lb52_bridge_button_t target,
    uint8_t source)
{
    if (config == 0 ||
        (uint8_t)target >= (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT ||
        !source_valid(source)) {
        return false;
    }
    config->source_for_target[(uint8_t)target] = source;
    return true;
}

bool sf32lb52_bridge_mapping_apply(
    const sf32lb52_bridge_mapping_config_t *config,
    const sf32lb52_bridge_input_state_t *input,
    sf32lb52_bridge_input_state_t *output)
{
    uint8_t target;

    if (!sf32lb52_bridge_mapping_validate(config) ||
        input == 0 || output == 0) {
        return false;
    }
    if (sf32lb52_bridge_mapping_is_identity(config)) {
        *output = *input;
        return true;
    }

    *output = *input;
    output->buttons = 0U;
    for (target = 0U;
         target < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++target) {
        if (source_active(input, config->source_for_target[target])) {
            output->buttons |= SF32LB52_BRIDGE_BUTTON_MASK(target);
        }
    }
    output->left_trigger = mapped_trigger(
        input,
        config->source_for_target[SF32LB52_BRIDGE_BUTTON_LEFT_TRIGGER]);
    output->right_trigger = mapped_trigger(
        input,
        config->source_for_target[SF32LB52_BRIDGE_BUTTON_RIGHT_TRIGGER]);
    return true;
}

bool sf32lb52_bridge_mapping_patch_native_report(
    const sf32lb52_bridge_mapping_config_t *config,
    sf32lb52_bridge_role_t role,
    const sf32lb52_bridge_input_state_t *input,
    uint8_t *report,
    size_t report_len)
{
    sf32lb52_bridge_input_state_t mapped;
    uint8_t encoded[SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE];
    bool ds5_report;
    bool ns2_report;

    if (!sf32lb52_bridge_mapping_validate(config) || input == 0 ||
        report == 0) {
        return false;
    }
    ds5_report = (role == SF32LB52_BRIDGE_ROLE_DUALSENSE ||
                  role == SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE) &&
                 report_len == SF32LB52_BRIDGE_DS5_INPUT_REPORT_SIZE &&
                 report[0] == DS5_REPORT_ID;
    ns2_report = role == SF32LB52_BRIDGE_ROLE_NS2PRO &&
                 report_len == SF32LB52_BRIDGE_NS2PRO_INPUT_REPORT_SIZE &&
                 report[0] == NS2_REPORT_ID;
    if (!ds5_report && !ns2_report) {
        return false;
    }
    if (sf32lb52_bridge_mapping_is_identity(config)) {
        return true;
    }
    if (!sf32lb52_bridge_mapping_apply(config, input, &mapped)) {
        return false;
    }
    if (ds5_report &&
        sf32lb52_bridge_encode_ds5_input(&mapped, 0U, encoded) == 0) {
        report[5] = encoded[5];
        report[6] = encoded[6];
        report[8] = encoded[8];
        report[9] = encoded[9];
        report[10] = (uint8_t)((report[10] & 0x08U) |
                               (encoded[10] & 0xf7U));
        return true;
    }
    if (ns2_report &&
        sf32lb52_bridge_encode_ns2pro_input(&mapped, 0U, encoded) == 0) {
        report[5] = (uint8_t)((report[5] & 0x30U) |
                              (encoded[5] & 0xcfU));
        report[6] = (uint8_t)((report[6] & 0x80U) |
                              (encoded[6] & 0x7fU));
        report[7] = (uint8_t)((report[7] & 0x30U) |
                              (encoded[7] & 0xcfU));
        report[8] = (uint8_t)((report[8] & 0xfcU) |
                              (encoded[8] & 0x03U));
        return true;
    }
    return false;
}

const char *sf32lb52_bridge_button_name(uint8_t button)
{
    if (button == SF32LB52_BRIDGE_MAPPING_NONE) {
        return "none";
    }
    if (button >= (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT) {
        return 0;
    }
    return g_button_names[button];
}

bool sf32lb52_bridge_button_parse(const char *name, uint8_t *button)
{
    uint8_t index;

    if (name == 0 || button == 0) {
        return false;
    }
    if (strcmp(name, "none") == 0) {
        *button = SF32LB52_BRIDGE_MAPPING_NONE;
        return true;
    }
    for (index = 0U;
         index < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++index) {
        if (strcmp(name, g_button_names[index]) == 0) {
            *button = index;
            return true;
        }
    }
    return false;
}

size_t sf32lb52_bridge_mapping_serialize(
    const sf32lb52_bridge_mapping_config_t *config,
    uint8_t *wire,
    size_t wire_capacity)
{
    uint32_t crc;

    if (!sf32lb52_bridge_mapping_validate(config) || wire == 0 ||
        wire_capacity < SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE) {
        return 0U;
    }
    memset(wire, 0, SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE);
    memcpy(wire, g_mapping_magic, sizeof(g_mapping_magic));
    wire[4] = MAPPING_WIRE_VERSION;
    wire[5] = (uint8_t)SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE;
    wire[6] = (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
    memcpy(wire + 8U, config->source_for_target,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    crc = crc32_bytes(wire, MAPPING_CONFIG_CRC_OFFSET);
    put_u32_le(wire + MAPPING_CONFIG_CRC_OFFSET, crc);
    return SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE;
}

bool sf32lb52_bridge_mapping_deserialize(
    const uint8_t *wire,
    size_t wire_len,
    sf32lb52_bridge_mapping_config_t *config)
{
    sf32lb52_bridge_mapping_config_t decoded;

    if (wire == 0 || config == 0 ||
        wire_len != SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE ||
        memcmp(wire, g_mapping_magic, sizeof(g_mapping_magic)) != 0 ||
        wire[4] != MAPPING_WIRE_VERSION ||
        wire[5] != (uint8_t)SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE ||
        wire[6] != (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT ||
        get_u32_le(wire + MAPPING_CONFIG_CRC_OFFSET) !=
            crc32_bytes(wire, MAPPING_CONFIG_CRC_OFFSET)) {
        return false;
    }
    memcpy(decoded.source_for_target, wire + 8U,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    if (!sf32lb52_bridge_mapping_validate(&decoded)) {
        return false;
    }
    *config = decoded;
    return true;
}

size_t sf32lb52_bridge_mapping_profiles_serialize(
    const sf32lb52_bridge_mapping_profiles_t *profiles,
    uint8_t *wire,
    size_t wire_capacity)
{
    uint32_t crc;

    if (!sf32lb52_bridge_mapping_profiles_validate(profiles) || wire == 0 ||
        wire_capacity < SF32LB52_BRIDGE_MAPPING_WIRE_SIZE) {
        return 0U;
    }
    memset(wire, 0, SF32LB52_BRIDGE_MAPPING_WIRE_SIZE);
    memcpy(wire, g_mapping_magic, sizeof(g_mapping_magic));
    wire[4] = MAPPING_PROFILES_WIRE_VERSION;
    wire[5] = (uint8_t)SF32LB52_BRIDGE_MAPPING_WIRE_SIZE;
    wire[6] = (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
    wire[7] = 2U;
    memcpy(wire + 8U, profiles->ds5.source_for_target,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    memcpy(wire + 48U, profiles->ns2pro.source_for_target,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    crc = crc32_bytes(wire, MAPPING_PROFILES_CRC_OFFSET);
    put_u32_le(wire + MAPPING_PROFILES_CRC_OFFSET, crc);
    return SF32LB52_BRIDGE_MAPPING_WIRE_SIZE;
}

bool sf32lb52_bridge_mapping_profiles_deserialize(
    const uint8_t *wire,
    size_t wire_len,
    sf32lb52_bridge_mapping_profiles_t *profiles)
{
    sf32lb52_bridge_mapping_profiles_t decoded;

    if (wire == 0 || profiles == 0 ||
        wire_len != SF32LB52_BRIDGE_MAPPING_WIRE_SIZE ||
        memcmp(wire, g_mapping_magic, sizeof(g_mapping_magic)) != 0 ||
        wire[4] != MAPPING_PROFILES_WIRE_VERSION ||
        wire[5] != (uint8_t)SF32LB52_BRIDGE_MAPPING_WIRE_SIZE ||
        wire[6] != (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT || wire[7] != 2U ||
        get_u32_le(wire + MAPPING_PROFILES_CRC_OFFSET) !=
            crc32_bytes(wire, MAPPING_PROFILES_CRC_OFFSET)) {
        return false;
    }
    memcpy(decoded.ds5.source_for_target, wire + 8U,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    memcpy(decoded.ns2pro.source_for_target, wire + 48U,
           SF32LB52_BRIDGE_BUTTON_COUNT);
    if (!sf32lb52_bridge_mapping_profiles_validate(&decoded)) {
        return false;
    }
    *profiles = decoded;
    return true;
}

void sf32lb52_bridge_mapping_routes_defaults(
    sf32lb52_bridge_mapping_routes_t *routes)
{
    if (routes != 0) {
        sf32lb52_bridge_mapping_defaults(&routes->ds5.ds5);
        sf32lb52_bridge_mapping_defaults(&routes->ds5.ns2pro);
        sf32lb52_bridge_mapping_defaults(&routes->ds5.xbox);
        sf32lb52_bridge_mapping_defaults(&routes->ns2pro.ds5);
        sf32lb52_bridge_mapping_defaults(&routes->ns2pro.ns2pro);
        sf32lb52_bridge_mapping_defaults(&routes->ns2pro.xbox);
    }
}

bool sf32lb52_bridge_mapping_routes_validate(
    const sf32lb52_bridge_mapping_routes_t *routes)
{
    return routes != 0 &&
        sf32lb52_bridge_mapping_validate(&routes->ds5.ds5) &&
        sf32lb52_bridge_mapping_validate(&routes->ds5.ns2pro) &&
        sf32lb52_bridge_mapping_validate(&routes->ds5.xbox) &&
        sf32lb52_bridge_mapping_validate(&routes->ns2pro.ds5) &&
        sf32lb52_bridge_mapping_validate(&routes->ns2pro.ns2pro) &&
        sf32lb52_bridge_mapping_validate(&routes->ns2pro.xbox);
}

bool sf32lb52_bridge_mapping_routes_is_identity(
    const sf32lb52_bridge_mapping_routes_t *routes)
{
    return routes != 0 &&
        sf32lb52_bridge_mapping_is_identity(&routes->ds5.ds5) &&
        sf32lb52_bridge_mapping_is_identity(&routes->ds5.ns2pro) &&
        sf32lb52_bridge_mapping_is_identity(&routes->ds5.xbox) &&
        sf32lb52_bridge_mapping_is_identity(&routes->ns2pro.ds5) &&
        sf32lb52_bridge_mapping_is_identity(&routes->ns2pro.ns2pro) &&
        sf32lb52_bridge_mapping_is_identity(&routes->ns2pro.xbox);
}

sf32lb52_bridge_mapping_config_t *sf32lb52_bridge_mapping_route(
    sf32lb52_bridge_mapping_routes_t *routes,
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output)
{
    sf32lb52_bridge_mapping_outputs_t *outputs;

    if (routes == 0) {
        return 0;
    }
    if (profile == SF32LB52_BRIDGE_MAPPING_PROFILE_DS5) {
        outputs = &routes->ds5;
    } else if (profile == SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO) {
        outputs = &routes->ns2pro;
    } else {
        return 0;
    }
    if (output == SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5) {
        return &outputs->ds5;
    }
    if (output == SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO) {
        return &outputs->ns2pro;
    }
    if (output == SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX) {
        return &outputs->xbox;
    }
    return 0;
}

void sf32lb52_bridge_mapping_routes_from_profiles(
    sf32lb52_bridge_mapping_routes_t *routes,
    const sf32lb52_bridge_mapping_profiles_t *profiles)
{
    if (routes != 0 && sf32lb52_bridge_mapping_profiles_validate(profiles)) {
        routes->ds5.ds5 = profiles->ds5;
        routes->ds5.ns2pro = profiles->ds5;
        routes->ds5.xbox = profiles->ds5;
        routes->ns2pro.ds5 = profiles->ns2pro;
        routes->ns2pro.ns2pro = profiles->ns2pro;
        routes->ns2pro.xbox = profiles->ns2pro;
    }
}

static sf32lb52_bridge_mapping_config_t *route_by_index(
    sf32lb52_bridge_mapping_routes_t *routes, uint8_t index)
{
    static const sf32lb52_bridge_mapping_profile_t profiles[] = {
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
    };
    static const sf32lb52_bridge_mapping_output_t outputs[] = {
        SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5,
        SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO,
        SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX,
    };

    if (index >= 6U) {
        return 0;
    }
    return sf32lb52_bridge_mapping_route(
        routes, profiles[index / 3U], outputs[index % 3U]);
}

static void packed_source_write(uint8_t *wire, size_t item, uint8_t source)
{
    size_t bit = item * MAPPING_PACKED_BITS_PER_SOURCE;
    size_t byte = MAPPING_ROUTES_DATA_OFFSET + bit / 8U;
    uint8_t shift = (uint8_t)(bit % 8U);
    uint16_t value = source == SF32LB52_BRIDGE_MAPPING_NONE
        ? MAPPING_PACKED_NONE : source;
    uint16_t pair = (uint16_t)wire[byte] |
        (uint16_t)((uint16_t)wire[byte + 1U] << 8U);

    pair = (uint16_t)((pair & ~((uint16_t)0x1fU << shift)) |
                      (uint16_t)(value << shift));
    wire[byte] = (uint8_t)pair;
    wire[byte + 1U] = (uint8_t)(pair >> 8U);
}

static bool packed_source_read(const uint8_t *wire, size_t item,
                               uint8_t *source)
{
    size_t bit = item * MAPPING_PACKED_BITS_PER_SOURCE;
    size_t byte = MAPPING_ROUTES_DATA_OFFSET + bit / 8U;
    uint8_t shift = (uint8_t)(bit % 8U);
    uint16_t pair = (uint16_t)wire[byte] |
        (uint16_t)((uint16_t)wire[byte + 1U] << 8U);
    uint8_t value = (uint8_t)((pair >> shift) & 0x1fU);

    if (value == MAPPING_PACKED_NONE) {
        *source = SF32LB52_BRIDGE_MAPPING_NONE;
        return true;
    }
    if (value >= SF32LB52_BRIDGE_BUTTON_COUNT) {
        return false;
    }
    *source = value;
    return true;
}

size_t sf32lb52_bridge_mapping_routes_serialize(
    const sf32lb52_bridge_mapping_routes_t *routes,
    uint8_t *wire, size_t wire_capacity)
{
    uint8_t route_index;

    if (!sf32lb52_bridge_mapping_routes_validate(routes) || wire == 0 ||
        wire_capacity < SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE) {
        return 0U;
    }
    memset(wire, 0, SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE);
    memcpy(wire, g_mapping_magic, sizeof(g_mapping_magic));
    wire[4] = SF32LB52_BRIDGE_MAPPING_SCHEMA;
    wire[5] = SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE;
    wire[6] = SF32LB52_BRIDGE_BUTTON_COUNT;
    wire[7] = 6U;
    for (route_index = 0U; route_index < 6U; ++route_index) {
        const sf32lb52_bridge_mapping_config_t *config =
            route_by_index((sf32lb52_bridge_mapping_routes_t *)routes,
                           route_index);
        uint8_t target;

        for (target = 0U; target < SF32LB52_BRIDGE_BUTTON_COUNT; ++target) {
            packed_source_write(
                wire, (size_t)route_index * SF32LB52_BRIDGE_BUTTON_COUNT + target,
                config->source_for_target[target]);
        }
    }
    put_u32_le(wire + MAPPING_ROUTES_CRC_OFFSET,
               crc32_bytes(wire, MAPPING_ROUTES_CRC_OFFSET));
    return SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE;
}

bool sf32lb52_bridge_mapping_routes_deserialize(
    const uint8_t *wire, size_t wire_len,
    sf32lb52_bridge_mapping_routes_t *routes)
{
    sf32lb52_bridge_mapping_routes_t decoded;
    uint8_t route_index;

    if (wire == 0 || routes == 0 ||
        wire_len != SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE ||
        memcmp(wire, g_mapping_magic, sizeof(g_mapping_magic)) != 0 ||
        wire[4] != SF32LB52_BRIDGE_MAPPING_SCHEMA ||
        wire[5] != SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE ||
        wire[6] != SF32LB52_BRIDGE_BUTTON_COUNT || wire[7] != 6U ||
        get_u32_le(wire + MAPPING_ROUTES_CRC_OFFSET) !=
            crc32_bytes(wire, MAPPING_ROUTES_CRC_OFFSET)) {
        return false;
    }
    sf32lb52_bridge_mapping_routes_defaults(&decoded);
    for (route_index = 0U; route_index < 6U; ++route_index) {
        sf32lb52_bridge_mapping_config_t *config =
            route_by_index(&decoded, route_index);
        uint8_t target;

        for (target = 0U; target < SF32LB52_BRIDGE_BUTTON_COUNT; ++target) {
            if (!packed_source_read(
                    wire,
                    (size_t)route_index * SF32LB52_BRIDGE_BUTTON_COUNT + target,
                    &config->source_for_target[target])) {
                return false;
            }
        }
    }
    if (!sf32lb52_bridge_mapping_routes_validate(&decoded)) {
        return false;
    }
    *routes = decoded;
    return true;
}

bool sf32lb52_bridge_mapping_routes_v3_deserialize(
    const uint8_t *wire, size_t wire_len,
    sf32lb52_bridge_mapping_routes_t *routes)
{
    sf32lb52_bridge_mapping_routes_t decoded;

    if (wire == 0 || routes == 0 ||
        wire_len != SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE ||
        memcmp(wire, g_mapping_magic, sizeof(g_mapping_magic)) != 0 ||
        wire[4] != 3U ||
        wire[5] != SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE ||
        wire[6] != SF32LB52_BRIDGE_BUTTON_COUNT || wire[7] != 4U ||
        get_u32_le(wire + MAPPING_ROUTES_V3_CRC_OFFSET) !=
            crc32_bytes(wire, MAPPING_ROUTES_V3_CRC_OFFSET)) {
        return false;
    }
    sf32lb52_bridge_mapping_routes_defaults(&decoded);
    memcpy(decoded.ds5.ds5.source_for_target, wire + 8U, 25U);
    memcpy(decoded.ds5.ns2pro.source_for_target, wire + 33U, 25U);
    memcpy(decoded.ns2pro.ds5.source_for_target, wire + 58U, 25U);
    memcpy(decoded.ns2pro.ns2pro.source_for_target, wire + 83U, 25U);
    decoded.ds5.xbox = decoded.ds5.ds5;
    decoded.ns2pro.xbox = decoded.ns2pro.ds5;
    if (!sf32lb52_bridge_mapping_routes_validate(&decoded)) {
        return false;
    }
    *routes = decoded;
    return true;
}

sf32lb52_bridge_mapping_output_t sf32lb52_bridge_mapping_output_for_role(
    sf32lb52_bridge_role_t role)
{
    switch (role) {
    case SF32LB52_BRIDGE_ROLE_NS2PRO:
        return SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        return SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5;
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
        return SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX;
    default:
        return SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED;
    }
}

const char *sf32lb52_bridge_mapping_output_name(
    sf32lb52_bridge_mapping_output_t output)
{
    switch (output) {
    case SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5: return "ds5";
    case SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO: return "ns2pro";
    case SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX: return "xbox";
    default: return "active";
    }
}

bool sf32lb52_bridge_mapping_output_parse(
    const char *name, sf32lb52_bridge_mapping_output_t *output)
{
    if (name == 0 || output == 0) {
        return false;
    }
    if (strcmp(name, "ds5") == 0 || strcmp(name, "dse") == 0 ||
        strcmp(name, "edge") == 0 || strcmp(name, "dualsense-edge") == 0) {
        *output = SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5;
        return true;
    }
    if (strcmp(name, "ns2pro") == 0) {
        *output = SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO;
        return true;
    }
    if (strcmp(name, "xbox") == 0 || strcmp(name, "xbox360") == 0 ||
        strcmp(name, "xinput") == 0) {
        *output = SF32LB52_BRIDGE_MAPPING_OUTPUT_XBOX;
        return true;
    }
    return false;
}

const char *sf32lb52_bridge_mapping_profile_name(
    sf32lb52_bridge_mapping_profile_t profile)
{
    switch (profile) {
    case SF32LB52_BRIDGE_MAPPING_PROFILE_DS5:
        return "ds5";
    case SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO:
        return "ns2pro";
    case SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED:
    default:
        return "active";
    }
}

bool sf32lb52_bridge_mapping_profile_parse(
    const char *name,
    sf32lb52_bridge_mapping_profile_t *profile)
{
    if (name == 0 || profile == 0) {
        return false;
    }
    if (strcmp(name, "ds5") == 0 || strcmp(name, "ps") == 0 ||
        strcmp(name, "dualsense") == 0 || strcmp(name, "dse") == 0 ||
        strcmp(name, "edge") == 0 || strcmp(name, "dualsense-edge") == 0) {
        *profile = SF32LB52_BRIDGE_MAPPING_PROFILE_DS5;
        return true;
    }
    if (strcmp(name, "ns2") == 0 || strcmp(name, "ns2pro") == 0 ||
        strcmp(name, "nintendo") == 0 || strcmp(name, "ns") == 0) {
        *profile = SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO;
        return true;
    }
    return false;
}

int sf32lb52_bridge_mapping_parse_command(
    const char *text,
    sf32lb52_bridge_mapping_command_t *command)
{
    const char *cursor;
    char verb[8];
    char args[4][24];
    size_t count = 0U;
    size_t first_button = 0U;
    uint8_t target;
    uint8_t source;

    if (text == 0 || command == 0) {
        return -1;
    }
    memset(command, 0, sizeof(*command));
    command->profile = SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED;
    cursor = skip_spaces(text);
    if (strncmp(cursor, "mapping", 7U) != 0 ||
        (cursor[7] != 0 && cursor[7] != ' ' && cursor[7] != '\t')) {
        return 0;
    }
    cursor += 7U;
    if (!next_token(&cursor, verb, sizeof(verb))) {
        return -1;
    }
    while (!at_end(cursor)) {
        if (count == 4U ||
            !next_token(&cursor, args[count], sizeof(args[count]))) {
            return -1;
        }
        ++count;
    }
    if (strcmp(verb, "get") == 0 || strcmp(verb, "reset") == 0 ||
        strcmp(verb, "save") == 0) {
        if (count > 2U ||
            (count >= 1U && !sf32lb52_bridge_mapping_profile_parse(
                args[0], &command->profile)) ||
            (count == 2U && !sf32lb52_bridge_mapping_output_parse(
                args[1], &command->output))) {
            return -1;
        }
        command->kind = strcmp(verb, "get") == 0
            ? SF32LB52_BRIDGE_MAPPING_COMMAND_GET
            : strcmp(verb, "reset") == 0
                ? SF32LB52_BRIDGE_MAPPING_COMMAND_RESET
                : SF32LB52_BRIDGE_MAPPING_COMMAND_SAVE;
        return 1;
    }
    if (strcmp(verb, "set") != 0 || count < 2U) {
        return -1;
    }
    if (count >= 3U) {
        if (!sf32lb52_bridge_mapping_profile_parse(
                args[0], &command->profile)) {
            return -1;
        }
        first_button = 1U;
    }
    if (count == 4U) {
        if (!sf32lb52_bridge_mapping_output_parse(args[1], &command->output)) {
            return -1;
        }
        first_button = 2U;
    }
    if (!sf32lb52_bridge_button_parse(args[first_button], &target) ||
        target == SF32LB52_BRIDGE_MAPPING_NONE ||
        !sf32lb52_bridge_button_parse(args[first_button + 1U], &source)) {
        return -1;
    }
    command->kind = SF32LB52_BRIDGE_MAPPING_COMMAND_SET;
    command->target = (sf32lb52_bridge_button_t)target;
    command->source = source;
    return 1;
}

int sf32lb52_bridge_mapping_format_route_json(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty,
    char *json,
    size_t json_len)
{
    size_t used = 0U;
    uint8_t target;
    int written;

    if (!sf32lb52_bridge_mapping_validate(config) || json == 0 ||
        json_len == 0U) {
        return -1;
    }
    written = snprintf(json, json_len,
                       "{\"ok\":true,\"profile\":\"%s\","
                       "\"output\":\"%s\",\"mapping_schema\":4,"
                       "\"identity\":%s,\"dirty\":%s,\"entries\":{",
                       sf32lb52_bridge_mapping_profile_name(profile),
                       sf32lb52_bridge_mapping_output_name(output),
                       sf32lb52_bridge_mapping_is_identity(config) ?
                           "true" : "false",
                       dirty ? "true" : "false");
    if (written < 0 || (size_t)written >= json_len) {
        return -1;
    }
    used = (size_t)written;
    for (target = 0U;
         target < (uint8_t)SF32LB52_BRIDGE_BUTTON_COUNT;
         ++target) {
        const char *target_name = sf32lb52_bridge_button_name(target);
        const char *source_name = sf32lb52_bridge_button_name(
            config->source_for_target[target]);

        written = snprintf(json + used, json_len - used,
                           "%s\"%s\":\"%s\"",
                           target == 0U ? "" : ",",
                           target_name, source_name);
        if (written < 0 || (size_t)written >= json_len - used) {
            return -1;
        }
        used += (size_t)written;
    }
    written = snprintf(json + used, json_len - used, "}}");
    if (written < 0 || (size_t)written >= json_len - used) {
        return -1;
    }
    used += (size_t)written;
    return (int)used;
}

int sf32lb52_bridge_mapping_format_profile_json(
    sf32lb52_bridge_mapping_profile_t profile,
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty, char *json, size_t json_len)
{
    return sf32lb52_bridge_mapping_format_route_json(
        profile, SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED,
        config, dirty, json, json_len);
}

int sf32lb52_bridge_mapping_format_json(
    const sf32lb52_bridge_mapping_config_t *config,
    bool dirty,
    char *json,
    size_t json_len)
{
    return sf32lb52_bridge_mapping_format_profile_json(
        SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED,
        config, dirty, json, json_len);
}
