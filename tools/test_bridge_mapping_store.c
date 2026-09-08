#include "sf32lb52_bridge_mapping_store.h"
#include "bf0_sibles_nvds.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define V1_KEY "sf32_map_v1"
#define V2_KEY "sf32_map_v2"

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

static void assert_defaults(const sf32lb52_bridge_mapping_profiles_t *profiles)
{
    sf32lb52_bridge_mapping_store_status_t status;

    assert(sf32lb52_bridge_mapping_is_identity(&profiles->ds5));
    assert(sf32lb52_bridge_mapping_is_identity(&profiles->ns2pro));
    sf32lb52_bridge_mapping_store_get_status(&status);
    assert(status.loaded == 0U && status.used_defaults == 1U);
}

static void seed_legacy(void)
{
    /* Frozen v1 record, with CRC independently computed using zlib.crc32. */
    static const uint8_t wire[] = {
        0x53U, 0x46U, 0x4dU, 0x31U, 0x01U, 0x28U, 0x19U, 0x00U,
        0x01U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x00U, 0x00U, 0x00U, 0x45U, 0x9aU, 0xb0U, 0x26U,
    };
    fake_nvds_seed(V1_KEY, wire, sizeof(wire));
}

static void test_migration_and_v2_precedence(void)
{
    sf32lb52_bridge_mapping_profiles_t expected = custom_profiles();
    sf32lb52_bridge_mapping_profiles_t loaded;
    sf32lb52_bridge_mapping_store_status_t status;
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];

    fake_nvds_clear();
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    seed_legacy();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded.ds5, &expected.ds5, sizeof(loaded.ds5)) == 0);
    assert(memcmp(&loaded.ns2pro, &expected.ds5, sizeof(loaded.ns2pro)) == 0);
    assert(fake_nvds_write_count() == 0U); /* Loading never writes flash. */
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded.ds5, &loaded.ns2pro, sizeof(loaded.ds5)) == 0);

    loaded.ns2pro = expected.ns2pro;
    assert(sf32lb52_bridge_mapping_store_save(&loaded));
    assert(sifli_nvds_flash_read(V2_KEY, wire, sizeof(wire)) == sizeof(wire));
    assert(wire[4] == 2U && wire[5] == 80U && wire[7] == 2U);
    fake_nvds_reboot();
    memset(&loaded, 0xa5, sizeof(loaded));
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
    sf32lb52_bridge_mapping_store_get_status(&status);
    assert(status.backend_available == 1U && status.initialized == 1U);
    assert(status.loaded == 1U && status.used_defaults == 0U);
    assert(fake_nvds_write_count() == 0U);
}

static void test_corrupt_v2_does_not_resurrect_legacy(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    sf32lb52_bridge_mapping_profiles_t loaded;
    uint8_t good[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];
    uint8_t bad[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE + 1U];
    size_t i;

    assert(sf32lb52_bridge_mapping_profiles_serialize(
        &profiles, good, sizeof(good)) == sizeof(good));
    fake_nvds_clear();
    seed_legacy();
    for (i = 0U; i < sizeof(good); ++i) {
        memcpy(bad, good, sizeof(good));
        bad[i] ^= 1U;
        fake_nvds_seed(V2_KEY, bad, sizeof(good));
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    for (i = 1U; i < sizeof(good); ++i) {
        fake_nvds_seed(V2_KEY, good, i);
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    memcpy(bad, good, sizeof(good));
    bad[sizeof(good)] = 0x5aU;
    fake_nvds_seed(V2_KEY, bad, sizeof(bad));
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    assert(fake_nvds_write_count() == 0U);
}

static void test_corrupt_legacy(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    sf32lb52_bridge_mapping_profiles_t loaded;
    uint8_t good[SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE];
    uint8_t bad[SF32LB52_BRIDGE_MAPPING_CONFIG_WIRE_SIZE + 1U];
    size_t i;

    assert(sf32lb52_bridge_mapping_serialize(
        &profiles.ds5, good, sizeof(good)) == sizeof(good));
    fake_nvds_clear();
    for (i = 0U; i < sizeof(good); ++i) {
        memcpy(bad, good, sizeof(good));
        bad[i] ^= 1U;
        fake_nvds_seed(V1_KEY, bad, sizeof(good));
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    for (i = 0U; i < sizeof(good); ++i) {
        fake_nvds_seed(V1_KEY, good, i);
        assert(!sf32lb52_bridge_mapping_store_load(&loaded));
        assert_defaults(&loaded);
    }
    memcpy(bad, good, sizeof(good));
    bad[sizeof(good)] = 0xa5U;
    fake_nvds_seed(V1_KEY, bad, sizeof(bad));
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
}

static void test_failed_save_and_reset(void)
{
    sf32lb52_bridge_mapping_profiles_t saved = custom_profiles();
    sf32lb52_bridge_mapping_profiles_t edited = saved;
    sf32lb52_bridge_mapping_profiles_t loaded;
    sf32lb52_bridge_mapping_store_status_t before;
    sf32lb52_bridge_mapping_store_status_t after;

    fake_nvds_clear();
    seed_legacy();
    assert(sf32lb52_bridge_mapping_store_save(&saved));
    edited.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] =
        SF32LB52_BRIDGE_MAPPING_NONE;
    sf32lb52_bridge_mapping_store_get_status(&before);
    fake_nvds_fail_writes(true);
    assert(!sf32lb52_bridge_mapping_store_save(&edited));
    assert(!sf32lb52_bridge_mapping_store_reset(&edited));
    assert(edited.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] ==
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
    assert(sf32lb52_bridge_mapping_is_identity(&loaded.ds5));
    assert(sf32lb52_bridge_mapping_is_identity(&loaded.ns2pro));
    assert(sf32lb52_bridge_mapping_store_reset(&loaded)); /* Idempotent. */

    fake_nvds_clear();
    seed_legacy(); /* Reset a device that has never saved a v2 record. */
    assert(sf32lb52_bridge_mapping_store_reset(&loaded));
    fake_nvds_reboot();
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    assert(sf32lb52_bridge_mapping_is_identity(&loaded.ds5));
    assert(sf32lb52_bridge_mapping_is_identity(&loaded.ns2pro));
}

static void test_init_failure_and_invalid_save(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles = custom_profiles();
    sf32lb52_bridge_mapping_profiles_t loaded;
    sf32lb52_bridge_mapping_store_status_t before;
    sf32lb52_bridge_mapping_store_status_t after;

    fake_nvds_clear();
    assert(sf32lb52_bridge_mapping_store_save(&profiles));
    fake_nvds_reboot();
    sf32lb52_bridge_mapping_store_get_status(&before);
    fake_nvds_fail_init(true);
    assert(!sf32lb52_bridge_mapping_store_load(&loaded));
    assert_defaults(&loaded);
    assert(!sf32lb52_bridge_mapping_store_save(&profiles));
    sf32lb52_bridge_mapping_store_get_status(&after);
    assert(after.load_failures == before.load_failures + 1U);
    assert(after.save_failures == before.save_failures + 1U);
    assert(after.last_save_ok == 0U);
    fake_nvds_fail_init(false);
    profiles.ns2pro.source_for_target[0] = SF32LB52_BRIDGE_BUTTON_COUNT;
    assert(!sf32lb52_bridge_mapping_store_save(&profiles));
    assert(!sf32lb52_bridge_mapping_store_save(NULL));
    assert(!sf32lb52_bridge_mapping_store_reset(NULL));
    assert(!sf32lb52_bridge_mapping_store_load(NULL));
    assert(fake_nvds_write_count() == 0U);
    assert(sf32lb52_bridge_mapping_store_load(&loaded));
    profiles = custom_profiles();
    assert(memcmp(&profiles, &loaded, sizeof(loaded)) == 0);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "corrupt") == 0) {
        test_corrupt_v2_does_not_resurrect_legacy();
    } else if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        test_failed_save_and_reset();
    } else {
        test_migration_and_v2_precedence();
        test_corrupt_v2_does_not_resurrect_legacy();
        test_corrupt_legacy();
        test_failed_save_and_reset();
        test_init_failure_and_invalid_save();
    }
    puts("OK mapping NVDS migration/corruption/reset/failure tests");
    return 0;
}
