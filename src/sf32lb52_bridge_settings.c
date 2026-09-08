#include "sf32lb52_bridge_settings.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("bf0_sibles_nvds.h")
#include "bf0_sibles_nvds.h"
#define SF32LB52_BRIDGE_HAS_NVDS 1
#endif
#endif

#define BRIDGE_SETTINGS_KEY "sf32_bridge_v1"
#define BRIDGE_SETTINGS_WIRE_SIZE 32U
#define BRIDGE_SETTINGS_CRC_OFFSET 28U
#define BRIDGE_SETTINGS_VERSION 1U

static const uint8_t g_settings_magic[4] = {'S', 'F', 'B', '3'};
static sf32lb52_bridge_settings_status_t g_status;

#if !defined(SF32LB52_BRIDGE_HAS_NVDS)
static uint8_t g_host_settings[BRIDGE_SETTINGS_WIRE_SIZE];
static uint8_t g_host_settings_valid;
#endif

static uint32_t crc32_bytes(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xffffffffU;
    size_t i;

    for (i = 0U; i < len; ++i) {
        uint8_t bit;
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

static void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffU);
    out[1] = (uint8_t)(value >> 8U);
}

static uint16_t get_u16_le(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8U));
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

static int valid_role(sf32lb52_bridge_role_t role)
{
    return role == SF32LB52_BRIDGE_ROLE_XBOX_360 ||
           role == SF32LB52_BRIDGE_ROLE_DUALSENSE ||
           role == SF32LB52_BRIDGE_ROLE_NS2PRO ||
           role == SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
}

static int valid_preference(sf32lb52_bridge_input_preference_t preference)
{
    return preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO ||
           preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE ||
           preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO;
}

static int valid_source(sf32lb52_bridge_input_source_t source)
{
    return source == SF32LB52_BRIDGE_INPUT_SOURCE_NONE ||
           source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT ||
           source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE;
}

static void encode_config(const sf32lb52_bridge_persisted_config_t *config,
                          uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE])
{
    uint32_t crc;

    memset(wire, 0, BRIDGE_SETTINGS_WIRE_SIZE);
    memcpy(wire, g_settings_magic, sizeof(g_settings_magic));
    wire[4] = BRIDGE_SETTINGS_VERSION;
    wire[5] = BRIDGE_SETTINGS_WIRE_SIZE;
    wire[6] = (uint8_t)config->output_role;
    wire[7] = (uint8_t)config->input_preference;
    wire[8] = (uint8_t)config->last_active_input;
    wire[9] = config->auto_connect ? 1U : 0U;
    wire[10] = config->ds5_address_valid ? 1U : 0U;
    wire[11] = config->ns2_address_valid ? 1U : 0U;
    wire[12] = config->ns2_address_type;
    memcpy(wire + 14U, config->ds5_address, sizeof(config->ds5_address));
    memcpy(wire + 20U, config->ns2_address, sizeof(config->ns2_address));
    put_u16_le(wire + 26U, config->generation);
    crc = crc32_bytes(wire, BRIDGE_SETTINGS_CRC_OFFSET);
    put_u32_le(wire + BRIDGE_SETTINGS_CRC_OFFSET, crc);
}

static int decode_config(const uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE],
                         sf32lb52_bridge_persisted_config_t *config)
{
    sf32lb52_bridge_persisted_config_t decoded;
    uint32_t expected_crc;

    if (memcmp(wire, g_settings_magic, sizeof(g_settings_magic)) != 0 ||
        wire[4] != BRIDGE_SETTINGS_VERSION ||
        wire[5] != BRIDGE_SETTINGS_WIRE_SIZE) {
        return 0;
    }

    expected_crc = crc32_bytes(wire, BRIDGE_SETTINGS_CRC_OFFSET);
    if (get_u32_le(wire + BRIDGE_SETTINGS_CRC_OFFSET) != expected_crc) {
        return 0;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.output_role = (sf32lb52_bridge_role_t)wire[6];
    decoded.input_preference = (sf32lb52_bridge_input_preference_t)wire[7];
    decoded.last_active_input = (sf32lb52_bridge_input_source_t)wire[8];
    decoded.auto_connect = wire[9] ? 1U : 0U;
    decoded.ds5_address_valid = wire[10] ? 1U : 0U;
    decoded.ns2_address_valid = wire[11] ? 1U : 0U;
    decoded.ns2_address_type = wire[12];
    memcpy(decoded.ds5_address, wire + 14U, sizeof(decoded.ds5_address));
    memcpy(decoded.ns2_address, wire + 20U, sizeof(decoded.ns2_address));
    decoded.generation = get_u16_le(wire + 26U);

    if (!valid_role(decoded.output_role) ||
        !valid_preference(decoded.input_preference) ||
        !valid_source(decoded.last_active_input)) {
        return 0;
    }

    *config = decoded;
    return 1;
}

static int backend_init(void)
{
#if defined(SF32LB52_BRIDGE_HAS_NVDS)
    g_status.backend_available = 1U;
    if (sifli_nvds_flash_adaptor_init() != NVDS_OK) {
        return 0;
    }
#else
    g_status.backend_available = 0U;
#endif
    g_status.initialized = 1U;
    return 1;
}

static size_t backend_read(uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE])
{
#if defined(SF32LB52_BRIDGE_HAS_NVDS)
    return sifli_nvds_flash_read(BRIDGE_SETTINGS_KEY,
                                 wire,
                                 BRIDGE_SETTINGS_WIRE_SIZE);
#else
    if (!g_host_settings_valid) {
        return 0U;
    }
    memcpy(wire, g_host_settings, BRIDGE_SETTINGS_WIRE_SIZE);
    return BRIDGE_SETTINGS_WIRE_SIZE;
#endif
}

static int backend_write(const uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE])
{
#if defined(SF32LB52_BRIDGE_HAS_NVDS)
    return sifli_nvds_flash_write(BRIDGE_SETTINGS_KEY,
                                  wire,
                                  BRIDGE_SETTINGS_WIRE_SIZE) == NVDS_OK;
#else
    memcpy(g_host_settings, wire, BRIDGE_SETTINGS_WIRE_SIZE);
    g_host_settings_valid = 1U;
    return 1;
#endif
}

void sf32lb52_bridge_settings_defaults(sf32lb52_bridge_persisted_config_t *config)
{
    if (config == 0) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->output_role = SF32LB52_BRIDGE_ROLE_NS2PRO;
    config->input_preference = SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO;
    config->last_active_input = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    config->auto_connect = 1U;
}

bool sf32lb52_bridge_settings_load(sf32lb52_bridge_persisted_config_t *config)
{
    uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE];

    if (config == 0) {
        return false;
    }

    sf32lb52_bridge_settings_defaults(config);
    g_status.loaded = 0U;
    g_status.used_defaults = 1U;
    if (!backend_init() ||
        backend_read(wire) != BRIDGE_SETTINGS_WIRE_SIZE ||
        !decode_config(wire, config)) {
        g_status.load_failures++;
        return false;
    }

    g_status.loaded = 1U;
    g_status.used_defaults = 0U;
    return true;
}

bool sf32lb52_bridge_settings_save(sf32lb52_bridge_persisted_config_t *config)
{
    uint8_t wire[BRIDGE_SETTINGS_WIRE_SIZE];

    if (config == 0 || !valid_role(config->output_role) ||
        !valid_preference(config->input_preference) ||
        !valid_source(config->last_active_input)) {
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }

    config->generation++;
    encode_config(config, wire);
    if (!backend_init() || !backend_write(wire)) {
        config->generation--;
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }

    g_status.last_save_ok = 1U;
    return true;
}

bool sf32lb52_bridge_settings_reset(sf32lb52_bridge_persisted_config_t *config)
{
    sf32lb52_bridge_persisted_config_t defaults;

    if (config == 0) {
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }
    sf32lb52_bridge_settings_defaults(&defaults);
#if defined(SF32LB52_BRIDGE_HAS_NVDS)
    if (!backend_init() ||
        sifli_nvds_flash_adaptor_delete(BRIDGE_SETTINGS_KEY) != NVDS_OK) {
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }
#else
    g_host_settings_valid = 0U;
#endif
    *config = defaults;
    g_status.last_save_ok = 1U;
    g_status.loaded = 0U;
    g_status.used_defaults = 1U;
    return true;
}

void sf32lb52_bridge_settings_get_status(sf32lb52_bridge_settings_status_t *status)
{
    if (status != 0) {
        *status = g_status;
    }
}
