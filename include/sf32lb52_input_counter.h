#ifndef SF32LB52_INPUT_COUNTER_H
#define SF32LB52_INPUT_COUNTER_H

#include <stdint.h>

/* Single writer: the transport input callback. The application only advances
 * reset_epoch on link teardown; it never modifies the callback's tracker. */
typedef struct {
    uint32_t reports;
    uint32_t timestamp;
    uint32_t sequence;
    uint32_t epoch;
    uint8_t initialized;
    uint8_t timestamp_valid;
    uint8_t sequence_valid;
} sf32lb52_input_counter_t;

static inline uint32_t sf32lb52_input_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static inline uint8_t sf32lb52_input_counter_record(
    sf32lb52_input_counter_t *counter, uint32_t reset_epoch,
    uint8_t timestamp_valid, uint32_t timestamp,
    uint8_t sequence_valid, uint32_t sequence)
{
    uint8_t fresh = 0U;
    if (!timestamp_valid && !sequence_valid) {
        return 0U;
    }
    if (!counter->initialized || counter->epoch != reset_epoch ||
        counter->timestamp_valid != timestamp_valid ||
        counter->sequence_valid != sequence_valid ||
        (timestamp_valid && counter->timestamp != timestamp) ||
        (sequence_valid && counter->sequence != sequence)) {
        counter->reports++;
        fresh = 1U;
    }
    counter->initialized = 1U;
    counter->epoch = reset_epoch;
    counter->timestamp_valid = timestamp_valid;
    counter->sequence_valid = sequence_valid;
    counter->timestamp = timestamp;
    counter->sequence = sequence;
    return fresh;
}

#endif
