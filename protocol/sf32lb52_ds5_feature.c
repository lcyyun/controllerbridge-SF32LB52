#include "sf32lb52_ds5_feature.h"

#include <string.h>

#define DS5_FEATURE_CRC_SEED 0x2060efc3UL

static uint32_t crc32_seeded(const uint8_t *data, size_t len, uint32_t seed)
{
    uint32_t crc = ~seed;
    size_t i;
    unsigned int bit;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1U) ^ (0xedb88320UL &
                                  (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return ~crc;
}

static void write_le16(uint8_t *data, int16_t value)
{
    uint16_t raw = (uint16_t)value;
    data[0] = (uint8_t)raw;
    data[1] = (uint8_t)(raw >> 8U);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

void sf32lb52_ds5_feature_cache_reset(sf32lb52_ds5_feature_cache_t *cache)
{
    if (cache != 0) {
        memset(cache, 0, sizeof(*cache));
    }
}

int sf32lb52_ds5_feature_cache_store(sf32lb52_ds5_feature_cache_t *cache,
                                     uint8_t report_id,
                                     const uint8_t *data,
                                     size_t len)
{
    size_t i;
    size_t slot = SF32LB52_DS5_FEATURE_CACHE_SLOTS;

    if (cache == 0 || (data == 0 && len != 0U)) {
        return -1;
    }
    if (len > SF32LB52_DS5_FEATURE_DATA_MAX) {
        len = SF32LB52_DS5_FEATURE_DATA_MAX;
    }
    for (i = 0U; i < SF32LB52_DS5_FEATURE_CACHE_SLOTS; i++) {
        if (cache->entries[i].len != 0U &&
            cache->entries[i].report_id == report_id) {
            slot = i;
            break;
        }
        if (slot == SF32LB52_DS5_FEATURE_CACHE_SLOTS &&
            cache->entries[i].len == 0U) {
            slot = i;
        }
    }
    if (slot == SF32LB52_DS5_FEATURE_CACHE_SLOTS) {
        slot = cache->next_replacement;
        cache->next_replacement = (uint8_t)(
            (cache->next_replacement + 1U) % SF32LB52_DS5_FEATURE_CACHE_SLOTS);
    }
    cache->entries[slot].report_id = report_id;
    if (len != 0U) {
        memcpy(cache->entries[slot].data, data, len);
    }
    cache->entries[slot].len = (uint16_t)len;
    if (report_id == 0x70U) {
        cache->edge_known = 1U;
        cache->is_edge = 1U;
    }
    return 0;
}

size_t sf32lb52_ds5_feature_cache_get(
    const sf32lb52_ds5_feature_cache_t *cache,
    uint8_t report_id,
    uint8_t *report,
    size_t report_capacity)
{
    size_t i;
    size_t total;

    if (cache == 0 || report == 0 || report_capacity == 0U) {
        return 0U;
    }
    for (i = 0U; i < SF32LB52_DS5_FEATURE_CACHE_SLOTS; i++) {
        if (cache->entries[i].len == 0U ||
            cache->entries[i].report_id != report_id) {
            continue;
        }
        total = (size_t)cache->entries[i].len + 1U;
        if (total > report_capacity) {
            total = report_capacity;
        }
        report[0] = report_id;
        if (total > 1U) {
            memcpy(report + 1U, cache->entries[i].data, total - 1U);
        }
        return total;
    }
    return 0U;
}

size_t sf32lb52_ds5_feature_build_fallback(uint8_t report_id,
                                           uint8_t *report,
                                           size_t report_capacity)
{
    static const int16_t calibration[] = {
        0, 0, 0,
        8192, -8192, 8192, -8192, 8192, -8192,
        1024, -1024,
        8192, -8192, 8192, -8192, 8192, -8192,
    };
    size_t report_len;
    size_t i;

    if (report == 0) {
        return 0U;
    }
    switch (report_id) {
    case 0x09U:
        report_len = 20U;
        break;
    case 0x20U:
        report_len = 64U;
        break;
    case 0x05U:
        report_len = 41U;
        break;
    default:
        return 0U;
    }
    if (report_capacity < report_len) {
        return 0U;
    }
    memset(report, 0, report_len);
    report[0] = report_id;
    if (report_id == 0x09U) {
        report[1] = 0x00U;
        report[2] = 0x05U;
        report[3] = 0xd5U;
        report[4] = 0xbeU;
        report[5] = 0x18U;
        report[6] = 0x61U;
    } else if (report_id == 0x20U) {
        report[24] = 0x00U;
        report[25] = 0x02U;
        report[28] = 0x56U;
        report[29] = 0x02U;
        report[44] = 0x21U;
        report[45] = 0x02U;
    } else {
        for (i = 0U; i < sizeof(calibration) / sizeof(calibration[0]); i++) {
            write_le16(report + 1U + i * 2U, calibration[i]);
        }
    }
    return report_len;
}

size_t sf32lb52_ds5_feature_build_get_request(uint8_t report_id,
                                              uint8_t *packet,
                                              size_t packet_capacity)
{
    if (packet == 0 || packet_capacity < 2U) {
        return 0U;
    }
    packet[0] = 0x43U;
    packet[1] = report_id;
    return 2U;
}

size_t sf32lb52_ds5_feature_build_set_request(uint8_t report_id,
                                              const uint8_t *data,
                                              size_t len,
                                              uint8_t *packet,
                                              size_t packet_capacity)
{
    uint32_t crc;

    if (packet == 0 || (data == 0 && len != 0U) || len < 4U ||
        len > SF32LB52_DS5_FEATURE_DATA_MAX || packet_capacity < len + 2U) {
        return 0U;
    }
    packet[0] = 0x53U;
    packet[1] = report_id;
    memcpy(packet + 2U, data, len);
    crc = crc32_seeded(packet + 1U, len - 3U, DS5_FEATURE_CRC_SEED);
    write_le32(packet + len - 2U, crc);
    return len + 2U;
}

int sf32lb52_ds5_feature_handle_control(sf32lb52_ds5_feature_cache_t *cache,
                                        const uint8_t *packet,
                                        size_t len,
                                        uint8_t *report_id)
{
    if (report_id != 0) {
        *report_id = 0U;
    }
    if (cache == 0 || packet == 0 || len == 0U) {
        return -1;
    }
    if (packet[0] == 0x02U) {
        cache->edge_known = 1U;
        cache->is_edge = 0U;
        return 0;
    }
    if (packet[0] != 0xa3U || len < 2U) {
        return 0;
    }
    if (report_id != 0) {
        *report_id = packet[1];
    }
    return sf32lb52_ds5_feature_cache_store(cache, packet[1],
                                            packet + 2U, len - 2U) == 0 ? 1 : -1;
}
