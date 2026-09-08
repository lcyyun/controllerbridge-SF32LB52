#include "sf32lb52_bridge_mapping.h"

#include <stdio.h>
#include <string.h>

#define MAPPING_WIRE_VERSION 1U
#define MAPPING_PROFILES_WIRE_VERSION 2U
#define MAPPING_CONFIG_CRC_OFFSET 36U
#define MAPPING_PROFILES_CRC_OFFSET 76U
#define DS5_REPORT_ID 0x01U
#define NS2_REPORT_ID 0x05U

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
        strcmp(name, "dualsense") == 0) {
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
    char profile_name[16];
    char target_name[24];
    char source_name[24];
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
    if (strcmp(verb, "get") == 0) {
        if (!at_end(cursor)) {
            if (!next_token(&cursor, profile_name, sizeof(profile_name)) ||
                !sf32lb52_bridge_mapping_profile_parse(
                    profile_name, &command->profile) || !at_end(cursor)) {
                return -1;
            }
        }
        command->kind = SF32LB52_BRIDGE_MAPPING_COMMAND_GET;
        return 1;
    }
    if (strcmp(verb, "reset") == 0 || strcmp(verb, "save") == 0) {
        if (!at_end(cursor)) {
            if (!next_token(&cursor, profile_name, sizeof(profile_name)) ||
                !sf32lb52_bridge_mapping_profile_parse(
                    profile_name, &command->profile) || !at_end(cursor)) {
                return -1;
            }
        }
        command->kind = strcmp(verb, "reset") == 0
            ? SF32LB52_BRIDGE_MAPPING_COMMAND_RESET
            : SF32LB52_BRIDGE_MAPPING_COMMAND_SAVE;
        return 1;
    }
    if (strcmp(verb, "set") != 0) {
        return -1;
    }
    if (!next_token(&cursor, target_name, sizeof(target_name))) {
        return -1;
    }
    /* New form: mapping set ds5 south east. Keep the old three-token form. */
    if (!sf32lb52_bridge_button_parse(target_name, &target)) {
        if (!sf32lb52_bridge_mapping_profile_parse(
                target_name, &command->profile) ||
            !next_token(&cursor, target_name, sizeof(target_name)) ||
            !next_token(&cursor, source_name, sizeof(source_name))) {
            return -1;
        }
    } else if (!next_token(&cursor, source_name, sizeof(source_name))) {
        return -1;
    }
    if (!at_end(cursor) ||
        !sf32lb52_bridge_button_parse(target_name, &target) ||
        target == SF32LB52_BRIDGE_MAPPING_NONE ||
        !sf32lb52_bridge_button_parse(source_name, &source)) {
        return -1;
    }
    command->kind = SF32LB52_BRIDGE_MAPPING_COMMAND_SET;
    command->target = (sf32lb52_bridge_button_t)target;
    command->source = source;
    return 1;
}

int sf32lb52_bridge_mapping_format_profile_json(
    sf32lb52_bridge_mapping_profile_t profile,
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
                       "\"identity\":%s,\"dirty\":%s,\"entries\":{",
                       sf32lb52_bridge_mapping_profile_name(profile),
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
