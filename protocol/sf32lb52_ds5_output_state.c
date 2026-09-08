#include "sf32lb52_ds5_output_state.h"

#include <string.h>

static const uint8_t sf32lb52_ds5_output_state_initial[
    SF32LB52_DS5_OUTPUT_STATE_SIZE] = {
    0xfdU, 0xf7U, 0x00U, 0x00U,
    0x7fU, 0x64U,
    0xffU, 0x09U, 0x00U, 0x0fU, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0aU,
    0x07U, 0x00U, 0x00U, 0x02U, 0x01U,
    0x00U,
    0xffU, 0xd7U, 0x00U,
};

static void sf32lb52_ds5_set_bit(uint8_t *value, uint8_t bit, int enabled)
{
    uint8_t mask = (uint8_t)(1U << bit);

    if (enabled) {
        *value |= mask;
    } else {
        *value &= (uint8_t)~mask;
    }
}

void sf32lb52_ds5_output_state_init(sf32lb52_ds5_output_state_t *state)
{
    if (state != 0) {
        memcpy(state->data, sf32lb52_ds5_output_state_initial,
               sizeof(state->data));
    }
}

int sf32lb52_ds5_output_state_update(sf32lb52_ds5_output_state_t *state,
                                     const uint8_t *data,
                                     size_t len)
{
    if (state == 0 || data == 0 || len < SF32LB52_DS5_SET_STATE_SIZE) {
        return -1;
    }

    sf32lb52_ds5_set_bit(&state->data[0], 0U, (data[0] & 0x01U) != 0U);
    sf32lb52_ds5_set_bit(&state->data[0], 1U, (data[0] & 0x02U) != 0U);
    sf32lb52_ds5_set_bit(&state->data[38], 2U,
                         (data[38] & 0x04U) != 0U);

    if ((data[0] & 0x03U) != 0U) {
        memcpy(state->data + 2U, data + 2U, 2U);
    }
    if ((data[1] & 0x01U) != 0U) {
        state->data[8] = data[8];
    }
    if ((data[0] & 0x04U) != 0U) {
        memcpy(state->data + 10U, data + 10U, 11U);
    }
    if ((data[0] & 0x08U) != 0U) {
        memcpy(state->data + 21U, data + 21U, 11U);
    }
    if ((data[38] & 0x02U) != 0U) {
        state->data[41] = data[41];
    }
    if ((data[38] & 0x01U) != 0U) {
        state->data[42] = data[42];
    }
    if ((data[1] & 0x10U) != 0U) {
        state->data[43] = data[43];
    }
    if ((data[1] & 0x04U) != 0U) {
        memcpy(state->data + 44U, data + 44U, 3U);
    }
    return 0;
}

int sf32lb52_ds5_output_state_copy(
    const sf32lb52_ds5_output_state_t *state,
    uint8_t *data,
    size_t len)
{
    if (state == 0 || data == 0 || len > SF32LB52_DS5_OUTPUT_STATE_SIZE) {
        return -1;
    }
    memcpy(data, state->data, len);
    return 0;
}
