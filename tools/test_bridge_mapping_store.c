#include "sf32lb52_bridge_mapping_store.h"
#include "bf0_sibles_nvds.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define V1_KEY "sf32_map_v1"
#define V2_KEY "sf32_map_v2"
#define V3_KEY "sf32_map_v3"
#define V4_KEY "sf32_map_v4"

static sf32lb52_bridge_mapping_profiles_t custom_profiles(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles;

    sf32lb52_bridge_mapping_profiles_defaults(&profiles);
    profiles.ds5.source_for_target[SF32LB52_BRIDGE_BUTTON_SOUTH] =
        SF32LB52_BRIDGE_BUTTON_EAST;
    profiles.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_NORTH] =
        SF32LB52_BRIDGE_BUTTON_WEST;
    return profiles;
}

static sf32lb52_bridge_mapping_routes_t custom_routes(void)
{
    sf32lb52_bridge_mapping_routes_t routes;
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();

    sf32lb52_bridge_mapping_routes_from_profiles(&routes, &profiles);
    routes.ds5.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] =
        SF32LB52_BRIDGE_MAPPING_NONE;
    routes.ns2pro.ds5.source_for_target[SF32LB52_BRIDGE_BUTTON_CAPTURE] =
        SF32LB52_BRIDGE_BUTTON_TOUCHPAD;
    return routes;
}

static void assert_defaults(const sf32lb52_bridge_mapping_routes_t *routes)
{
    sf32lb52_bridge_mapping_store_status_t status;

    assert(sf32lb52_bridge_mapping_routes_is_identity(routes));
    sf32lb52_bridge_mapping_store_get_status(&status);
    assert(status.loaded == 0U && status.used_defaults == 1U);
}

static void seed_legacy(void)
{
    /* Frozen v1 record, CRC independently computed using zlib.crc32. */
    static const uint8_t wire[] = {
        0x53U, 0x46U, 0x4dU, 0x31U, 0x01U, 0x28U, 0x19U, 0x00U,
        0x01U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x00U, 0x00U, 0x00U, 0x45U, 0x9aU, 0xb0U, 0x26U,
    };
    fake_nvds_seed(V1_KEY, wire, sizeof(wire));
}

static void seed_v2(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];

    assert(sf32lb52_bridge_mapping_profiles_serialize(
        &profiles, wire, sizeof(wire)) == sizeof(wire));
    fake_nvds_seed(V2_KEY, wire, sizeof(wire));
}

static void make_v3(uint8_t wire[SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE])
{
    sf32lb52_bridge_mapping_routes_t routes = custom_routes();
    uint32_t crc = UINT32_C(0xffffffff);
    size_t i;
    unsigned int bit;

    memset(wire, 0, SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE);
    memcpy(wire, "SFM1", 4U);
    wire[4] = 3U;
    wire[5] = 112U;
    wire[6] = 25U;
    wire[7] = 4U;
    memcpy(wire + 8U, routes.ds5.ds5.source_for_target, 25U);
    memcpy(wire + 33U, routes.ds5.ns2pro.source_for_target, 25U);
    memcpy(wire + 58U, routes.ns2pro.ds5.source_for_target, 25U);
    memcpy(wire + 83U, routes.ns2pro.ns2pro.source_for_target, 25U);
    for (i = 0U; i < 108U; ++i) {
        crc ^= wire[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    crc = ~crc;
    for (i = 0U; i < 4U; ++i) {
        wire[108U + i] = (uint8_t)(crc >> (i * 8U));
    }
}

static void test_migration_and_precedence(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    sf32lb52_bridge_mapping_routes_t expected = custom_routes();
    sf32lb52_bridge_mapping_routes_t loaded;
    sf32lb52_bridge_mapping_store_status_t status;
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_ROUTES_WIRE_SIZE];

    fake_nvds_clear();
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    seed_legacy();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded.ds5.ds5, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ds5.ns2pro, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ns2pro.ds5, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ns2pro.ns2pro, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(fake_nvds_write_count() == 0U);
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));

    seed_v2(); /* Each old source map retains its effect in both outputs. */
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded.ds5.ds5, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ds5.ns2pro, &profiles.ds5, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ns2pro.ds5, &profiles.ns2pro, sizeof(profiles.ds5)) == 0);
    assert(memcmp(&loaded.ns2pro.ns2pro, &profiles.ns2pro, sizeof(profiles.ds5)) == 0);
    assert(fake_nvds_write_count() == 0U); /* Migration never writes on boot. */

    {
        uint8_t v3[SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE];
        make_v3(v3);
        fake_nvds_seed(V3_KEY, v3, sizeof(v3));
        assert(sf32lb52_bridge_mapping_store_load(&loaded));
        expected.ds5.xbox = expected.ds5.ds5;
        expected.ns2pro.xbox = expected.ns2pro.ds5;
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        assert(fake_nvds_write_count() == 0U);
        expected.ds5.xbox.source_for_target[0] = SF32LB52_BRIDGE_MAPPING_NONE;
        expected.ns2pro.xbox.source_for_target[1] = SF32LB52_BRIDGE_BUTTON_NORTH;
    }
    assert(sf32lb52_bridge_mapping_store_save(&expected));
    assert(sifli_nvds_flash_read(V4_KEY, wire, sizeof(wire)) == sizeof(wire));
    assert(wire[4] == 4U && wire[5] == 106U && wire[7] == 6U);
    fake_nvds_reboot();
    memset(&loaded, 0xa5, sizeof(loaded));
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
    sf32lb52_bridge_mapping_store_get_status(&status);
    assert(status.backend_available == 1U && status.initialized == 1U);
    assert(status.loaded == 1U && status.used_defaults == 0U);
    assert(fake_nvds_write_count() == 0U);
}

static void assert_corruption_rejected(const char *key,
                                      const uint8_t *good, size_t len)
{
    sf32lb52_bridge_mapping_routes_t loaded;
    uint8_t bad[SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE + 1U];
    size_t i;

    assert(len != 0U);
    for (i = 0U; i < len; ++i) {
        memcpy(bad, good, len);
        bad[i] ^= 1U;
        fake_nvds_seed(key, bad, len);
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    for (i = 1U; i < len; ++i) {
        fake_nvds_seed(key, good, i);
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    memcpy(bad, good, len);
    bad[len] = 0x5aU;
    fake_nvds_seed(key, bad, len + 1U);
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    assert(fake_nvds_write_count() == 0U);
}

static void test_corrupt_records_do_not_resurrect_older_maps(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    sf32lb52_bridge_mapping_routes_t routes = custom_routes();
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_ROUTES_V3_WIRE_SIZE];
    size_t len;

    fake_nvds_clear();
    len = sf32lb52_bridge_mapping_serialize(&profiles.ds5, wire, sizeof(wire));
    assert_corruption_rejected(V1_KEY, wire, len);
    fake_nvds_clear();
    seed_legacy();
    len = sf32lb52_bridge_mapping_profiles_serialize(&profiles, wire, sizeof(wire));
    assert_corruption_rejected(V2_KEY, wire, len);
    fake_nvds_clear();
    seed_legacy();
    seed_v2();
    make_v3(wire);
    assert_corruption_rejected(V3_KEY, wire, sizeof(wire));
    fake_nvds_clear();
    seed_legacy();
    seed_v2();
    make_v3(wire);
    fake_nvds_seed(V3_KEY, wire, sizeof(wire));
    len = sf32lb52_bridge_mapping_routes_serialize(&routes, wire, sizeof(wire));
    assert_corruption_rejected(V4_KEY, wire, len);
}

static void test_failed_save_and_reset(void)
{
    sf32lb52_bridge_mapping_routes_t saved = custom_routes();
    sf32lb52_bridge_mapping_routes_t edited = saved;
    sf32lb52_bridge_mapping_routes_t loaded;
    sf32lb52_bridge_mapping_store_status_t before;
    sf32lb52_bridge_mapping_store_status_t after;

    fake_nvds_clear();
    seed_legacy();
    seed_v2();
    assert(sf32lb52_bridge_mapping_store_save(&saved));
    edited.ns2pro.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] =
        SF32LB52_BRIDGE_MAPPING_NONE;
    sf32lb52_bridge_mapping_store_get_status(&before);
    fake_nvds_fail_writes(true);
    assert(!sf32lb52_bridge_mapping_store_save(&edited));
    assert(!sf32lb52_bridge_mapping_store_reset(&edited));
    assert(edited.ns2pro.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] ==
           SF32LB52_BRIDGE_MAPPING_NONE);
    sf32lb52_bridge_mapping_store_get_status(&after);
    assert(after.last_save_ok == 0U);
    assert(after.save_failures == before.save_failures + 2U);
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded, &saved, sizeof(loaded)) == 0);

    assert(sf32lb52_bridge_mapping_store_reset(&loaded));
    assert_defaults(&loaded);
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(sf32lb52_bridge_mapping_routes_is_identity(&loaded));
    assert(sf32lb52_bridge_mapping_store_reset(&loaded));

    fake_nvds_clear();
    seed_legacy(); /* Reset supersedes devices with only old records too. */
    seed_v2();
    assert(sf32lb52_bridge_mapping_store_reset(&loaded));
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(sf32lb52_bridge_mapping_routes_is_identity(&loaded));
}

static void test_init_failure_and_invalid_save(void)
{
    sf32lb52_bridge_mapping_routes_t routes = custom_routes();
    sf32lb52_bridge_mapping_routes_t loaded;
    sf32lb52_bridge_mapping_store_status_t before;
    sf32lb52_bridge_mapping_store_status_t after;

    fake_nvds_clear();
    assert(sf32lb52_bridge_mapping_store_save(&routes));
    fake_nvds_reboot();
    sf32lb52_bridge_mapping_store_get_status(&before);
    fake_nvds_fail_init(true);
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    assert(!sf32lb52_bridge_mapping_store_save(&routes));
    sf32lb52_bridge_mapping_store_get_status(&after);
    assert(after.load_failures == before.load_failures + 1U);
    assert(after.save_failures == before.save_failures + 1U);
    assert(after.last_save_ok == 0U);
    fake_nvds_fail_init(false);
    routes.ns2pro.ds5.source_for_target[0] = SF32LB52_BRIDGE_BUTTON_COUNT;
    assert(!sf32lb52_bridge_mapping_store_save(&routes));
    assert(!sf32lb52_bridge_mapping_store_save(NULL));
    assert(!sf32lb52_bridge_mapping_store_reset(NULL));
    assert(!sf32lb52_bridge_mapping_store_load(NULL));
    assert(fake_nvds_write_count() == 0U);
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    routes = custom_routes();
    assert(memcmp(&routes, &loaded, sizeof(loaded)) == 0);
}

int main(void)
{
    test_migration_and_precedence();
    test_corrupt_records_do_not_resurrect_older_maps();
    test_failed_save_and_reset();
    test_init_failure_and_invalid_save();
    puts("OK mapping migration, v4 NVDS precedence/CRC/reset/failure tests");
    return 0;
}
