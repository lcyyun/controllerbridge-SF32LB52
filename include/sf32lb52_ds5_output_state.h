#ifndef SF32LB52_DS5_OUTPUT_STATE_H
#define SF32LB52_DS5_OUTPUT_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_DS5_OUTPUT_STATE_SIZE 63U
#define SF32LB52_DS5_SET_STATE_SIZE 47U

typedef struct sf32lb52_ds5_output_state {
    uint8_t data[SF32LB52_DS5_OUTPUT_STATE_SIZE];
} sf32lb52_ds5_output_state_t;

void sf32lb52_ds5_output_state_init(sf32lb52_ds5_output_state_t *state);

/* Applies the same validity-bit merge rules as DS5Dongle's state manager. */
int sf32lb52_ds5_output_state_update(sf32lb52_ds5_output_state_t *state,
                                     const uint8_t *data,
                                     size_t len);

int sf32lb52_ds5_output_state_copy(
    const sf32lb52_ds5_output_state_t *state,
    uint8_t *data,
    size_t len);

#ifdef __cplusplus
}
#endif

#endif
