#ifndef SF32LB52_DS5_FEATURE_H
#define SF32LB52_DS5_FEATURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_DS5_FEATURE_CACHE_SLOTS 24U
#define SF32LB52_DS5_FEATURE_DATA_MAX 63U
#define SF32LB52_DS5_FEATURE_REPORT_MAX 64U
#define SF32LB52_DS5_FEATURE_CONTROL_PACKET_MAX 65U

typedef struct sf32lb52_ds5_feature_entry {
    uint8_t report_id;
    uint8_t data[SF32LB52_DS5_FEATURE_DATA_MAX];
    uint16_t len;
} sf32lb52_ds5_feature_entry_t;

typedef struct sf32lb52_ds5_feature_cache {
    sf32lb52_ds5_feature_entry_t entries[SF32LB52_DS5_FEATURE_CACHE_SLOTS];
    uint8_t next_replacement;
    uint8_t edge_known;
    uint8_t is_edge;
} sf32lb52_ds5_feature_cache_t;

void sf32lb52_ds5_feature_cache_reset(sf32lb52_ds5_feature_cache_t *cache);

/* Stores a Bluetooth feature body. data excludes the report ID. */
int sf32lb52_ds5_feature_cache_store(sf32lb52_ds5_feature_cache_t *cache,
                                     uint8_t report_id,
                                     const uint8_t *data,
                                     size_t len);

/* Returns a complete USB feature report, including report ID. */
size_t sf32lb52_ds5_feature_cache_get(
    const sf32lb52_ds5_feature_cache_t *cache,
    uint8_t report_id,
    uint8_t *report,
    size_t report_capacity);

/* Safe local reports used until the real controller response is cached. */
size_t sf32lb52_ds5_feature_build_fallback(uint8_t report_id,
                                           uint8_t *report,
                                           size_t report_capacity);

/* HIDP control packets, including the 0x43/0x53 transaction byte. */
size_t sf32lb52_ds5_feature_build_get_request(uint8_t report_id,
                                              uint8_t *packet,
                                              size_t packet_capacity);
size_t sf32lb52_ds5_feature_build_set_request(uint8_t report_id,
                                              const uint8_t *data,
                                              size_t len,
                                              uint8_t *packet,
                                              size_t packet_capacity);

/* Parses HIDP DATA(FEATURE) or HANDSHAKE control packets into the cache. */
int sf32lb52_ds5_feature_handle_control(sf32lb52_ds5_feature_cache_t *cache,
                                        const uint8_t *packet,
                                        size_t len,
                                        uint8_t *report_id);

#ifdef __cplusplus
}
#endif

#endif
