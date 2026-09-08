#include "sf32lb52_ds5_feature.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void test_fallbacks(void)
{
    uint8_t report[64];

    assert(sf32lb52_ds5_feature_build_fallback(0x09U, report,
                                                sizeof(report)) == 20U);
    assert(report[0] == 0x09U && report[2] == 0x05U);
    assert(sf32lb52_ds5_feature_build_fallback(0x20U, report,
                                                sizeof(report)) == 64U);
    assert(report[44] == 0x21U && report[45] == 0x02U);
    assert(sf32lb52_ds5_feature_build_fallback(0x05U, report,
                                                sizeof(report)) == 41U);
    assert(report[0] == 0x05U);
    assert(sf32lb52_ds5_feature_build_fallback(0x22U, report,
                                                sizeof(report)) == 0U);
}

static void test_control_cache(void)
{
    sf32lb52_ds5_feature_cache_t cache;
    uint8_t packet[] = {0xa3U, 0x22U, 1U, 2U, 3U};
    uint8_t report[64];
    uint8_t report_id;

    sf32lb52_ds5_feature_cache_reset(&cache);
    assert(sf32lb52_ds5_feature_handle_control(&cache, packet,
                                                sizeof(packet),
                                                &report_id) == 1);
    assert(report_id == 0x22U);
    assert(sf32lb52_ds5_feature_cache_get(&cache, 0x22U, report,
                                          sizeof(report)) == 4U);
    assert(memcmp(report, packet + 1U, 4U) == 0);

    packet[1] = 0x70U;
    assert(sf32lb52_ds5_feature_handle_control(&cache, packet,
                                                sizeof(packet), 0) == 1);
    assert(cache.edge_known == 1U && cache.is_edge == 1U);
}

static void test_control_requests(void)
{
    uint8_t packet[65];
    uint8_t data[8] = {1U, 2U, 3U, 4U, 0U, 0U, 0U, 0U};

    assert(sf32lb52_ds5_feature_build_get_request(0x05U, packet,
                                                   sizeof(packet)) == 2U);
    assert(packet[0] == 0x43U && packet[1] == 0x05U);
    assert(sf32lb52_ds5_feature_build_set_request(0x60U, data,
                                                   sizeof(data), packet,
                                                   sizeof(packet)) == 10U);
    assert(packet[0] == 0x53U && packet[1] == 0x60U);
    assert(read_le32(packet + 6U) != 0U);
}

int main(void)
{
    test_fallbacks();
    test_control_cache();
    test_control_requests();
    return 0;
}
