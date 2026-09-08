#include "sf32lb52_bridge_mapping_store.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("bf0_sibles_nvds.h")
#include "bf0_sibles_nvds.h"
#define SF32LB52_BRIDGE_MAPPING_HAS_NVDS 1
#endif
#endif

#define BRIDGE_MAPPING_KEY "sf32_map_v2"
#define BRIDGE_MAPPING_LEGACY_KEY "sf32_map_v1"

static sf32lb52_bridge_mapping_store_status_t g_status;

#if !defined(SF32LB52_BRIDGE_MAPPING_HAS_NVDS)
static uint8_t g_host_mapping[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];
static uint8_t g_host_mapping_valid;
static uint8_t g_host_legacy_mapping[
    SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE];
static uint8_t g_host_legacy_mapping_valid;
#endif

static bool backend_init(void)
{
#if defined(SF32LB52_BRIDGE_MAPPING_HAS_NVDS)
    g_status.backend_available = 1U;
    if (sifli_nvds_flash_adaptor_init() != NVDS_OK) {
        return false;
    }
#else
    g_status.backend_available = 0U;
#endif
    g_status.initialized = 1U;
    return true;
}

static size_t backend_read(const char *key, uint8_t *wire, size_t wire_len)
{
#if defined(SF32LB52_BRIDGE_MAPPING_HAS_NVDS)
    return sifli_nvds_flash_read(key, wire, wire_len);
#else
    if (strcmp(key, BRIDGE_MAPPING_KEY) == 0) {
        if (!g_host_mapping_valid) {
            return 0U;
        }
        if (wire_len > sizeof(g_host_mapping)) {
            wire_len = sizeof(g_host_mapping);
        }
        memcpy(wire, g_host_mapping, wire_len);
        return wire_len;
    }
    if (!g_host_legacy_mapping_valid ||
        strcmp(key, BRIDGE_MAPPING_LEGACY_KEY) != 0) {
        return 0U;
    }
    if (wire_len > sizeof(g_host_legacy_mapping)) {
        wire_len = sizeof(g_host_legacy_mapping);
    }
    memcpy(wire, g_host_legacy_mapping, wire_len);
    return wire_len;
#endif
}

static bool backend_write(const char *key, const uint8_t *wire, size_t wire_len)
{
#if defined(SF32LB52_BRIDGE_MAPPING_HAS_NVDS)
    return sifli_nvds_flash_write(key, wire, wire_len) == NVDS_OK;
#else
    if (strcmp(key, BRIDGE_MAPPING_KEY) == 0 &&
        wire_len == SF32LB52_BRIDGE_MAPPING_WIRE_SIZE) {
        memcpy(g_host_mapping, wire, wire_len);
        g_host_mapping_valid = 1U;
        return true;
    }
    if (strcmp(key, BRIDGE_MAPPING_LEGACY_KEY) == 0 &&
        wire_len == SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE) {
        memcpy(g_host_legacy_mapping, wire, wire_len);
        g_host_legacy_mapping_valid = 1U;
        return true;
    }
    return false;
#endif
}

bool sf32lb52_bridge_mapping_store_load(
    sf32lb52_bridge_mapping_profiles_t *profiles)
{
    /* NVDS returns bytes copied; one extra byte detects oversized records. */
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE + 1U];
    uint8_t legacy_wire[SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE + 1U];
    sf32lb52_bridge_mapping_config_t legacy;
    size_t wire_len;

    if (profiles == 0) {
        return false;
    }
    sf32lb52_bridge_mapping_profiles_defaults(profiles);
    g_status.loaded = 0U;
    g_status.used_defaults = 1U;
    if (!backend_init()) {
        g_status.load_failures++;
        return false;
    }
    wire_len = backend_read(BRIDGE_MAPPING_KEY, wire, sizeof(wire));
    if (wire_len != 0U) {
        if (sf32lb52_bridge_mapping_profiles_deserialize(
                wire, wire_len, profiles)) {
            g_status.loaded = 1U;
            g_status.used_defaults = 0U;
            return true;
        }
        /* A saved v2 record supersedes v1, even if it later becomes corrupt.
         * Never resurrect an obsolete shared mapping into both profiles. */
        g_status.load_failures++;
        return false;
    }
    wire_len = backend_read(BRIDGE_MAPPING_LEGACY_KEY, legacy_wire,
                            sizeof(legacy_wire));
    if (sf32lb52_bridge_mapping_deserialize(
            legacy_wire, wire_len, &legacy)) {
        profiles->ds5 = legacy;
        profiles->ns2pro = legacy;
        g_status.loaded = 1U;
        g_status.used_defaults = 0U;
        return true;
    }
    g_status.load_failures++;
    return false;
}

bool sf32lb52_bridge_mapping_store_save(
    const sf32lb52_bridge_mapping_profiles_t *profiles)
{
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];

    if (sf32lb52_bridge_mapping_profiles_serialize(
            profiles, wire, sizeof(wire)) == 0U ||
        !backend_init() ||
        !backend_write(BRIDGE_MAPPING_KEY, wire, sizeof(wire))) {
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }
    g_status.last_save_ok = 1U;
    return true;
}

bool sf32lb52_bridge_mapping_store_reset(
    sf32lb52_bridge_mapping_profiles_t *profiles)
{
    sf32lb52_bridge_mapping_profiles_t defaults;

    if (profiles == 0) {
        g_status.save_failures++;
        g_status.last_save_ok = 0U;
        return false;
    }
    sf32lb52_bridge_mapping_profiles_defaults(&defaults);
    /* Persist one authoritative record. The SDK delete adaptor hides errors,
     * and deleting two keys can leave a legacy mapping live after a failure. */
    if (!sf32lb52_bridge_mapping_store_save(&defaults)) {
        return false;
    }
    *profiles = defaults;
    g_status.last_save_ok = 1U;
    g_status.loaded = 0U;
    g_status.used_defaults = 1U;
    return true;
}

void sf32lb52_bridge_mapping_store_get_status(
    sf32lb52_bridge_mapping_store_status_t *status)
{
    if (status != 0) {
        *status = g_status;
    }
}
