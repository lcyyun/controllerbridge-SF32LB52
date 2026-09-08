#include "sf32lb52_ds5_output_state.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_initial_state(void)
{
    static const uint8_t expected[SF32LB52_DS5_SET_STATE_SIZE] = {
        0xfdU, 0xf7U, 0x00U, 0x00U, 0x7fU, 0x64U, 0xffU, 0x09U,
        0x00U, 0x0fU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0aU, 0x07U, 0x00U,
        0x00U, 0x02U, 0x01U, 0x00U, 0xffU, 0xd7U, 0x00U,
    };
    sf32lb52_ds5_output_state_t state;
    uint8_t actual[SF32LB52_DS5_SET_STATE_SIZE];

    sf32lb52_ds5_output_state_init(&state);
    assert(sf32lb52_ds5_output_state_copy(&state, actual,
                                           sizeof(actual)) == 0);
    assert(memcmp(actual, expected, sizeof(expected)) == 0);
}

static void test_incremental_rumble(void)
{
    sf32lb52_ds5_output_state_t state;
    uint8_t update[SF32LB52_DS5_SET_STATE_SIZE] = {0};
    uint8_t actual[SF32LB52_DS5_SET_STATE_SIZE];

    sf32lb52_ds5_output_state_init(&state);
    update[0] = 0x03U;
    update[2] = 0x11U;
    update[3] = 0x22U;
    assert(sf32lb52_ds5_output_state_update(&state, update,
                                             sizeof(update)) == 0);
    assert(sf32lb52_ds5_output_state_copy(&state, actual,
                                           sizeof(actual)) == 0);
    assert(actual[0] == 0xffU);
    assert(actual[2] == 0x11U && actual[3] == 0x22U);
    assert(actual[4] == 0x7fU && actual[44] == 0xffU);

    memset(update, 0, sizeof(update));
    update[2] = 0xaaU;
    update[3] = 0xbbU;
    assert(sf32lb52_ds5_output_state_update(&state, update,
                                             sizeof(update)) == 0);
    assert(sf32lb52_ds5_output_state_copy(&state, actual,
                                           sizeof(actual)) == 0);
    assert(actual[0] == 0xfcU);
    assert(actual[2] == 0x11U && actual[3] == 0x22U);
}

static void test_validity_gated_sections(void)
{
    sf32lb52_ds5_output_state_t state;
    uint8_t update[SF32LB52_DS5_SET_STATE_SIZE] = {0};
    uint8_t actual[SF32LB52_DS5_SET_STATE_SIZE];
    size_t i;

    sf32lb52_ds5_output_state_init(&state);
    update[0] = 0x0cU;
    update[1] = 0x15U;
    for (i = 0; i < 11U; ++i) {
        update[10U + i] = (uint8_t)(0x20U + i);
        update[21U + i] = (uint8_t)(0x40U + i);
    }
    update[8] = 0x02U;
    update[38] = 0x03U;
    update[41] = 0x01U;
    update[42] = 0x02U;
    update[43] = 0x15U;
    update[44] = 0x12U;
    update[45] = 0x34U;
    update[46] = 0x56U;
    assert(sf32lb52_ds5_output_state_update(&state, update,
                                             sizeof(update)) == 0);
    assert(sf32lb52_ds5_output_state_copy(&state, actual,
                                           sizeof(actual)) == 0);
    assert(memcmp(actual + 10U, update + 10U, 22U) == 0);
    assert(actual[8] == 0x02U);
    assert(actual[38] == 0x03U);
    assert(actual[41] == 0x01U && actual[42] == 0x02U);
    assert(actual[43] == 0x15U);
    assert(actual[44] == 0x12U && actual[45] == 0x34U &&
           actual[46] == 0x56U);
    assert(actual[4] == 0x7fU && actual[5] == 0x64U);
}

int main(void)
{
    sf32lb52_ds5_output_state_t state;
    uint8_t short_update[SF32LB52_DS5_SET_STATE_SIZE - 1U] = {0};

    test_initial_state();
    test_incremental_rumble();
    test_validity_gated_sections();
    sf32lb52_ds5_output_state_init(&state);
    assert(sf32lb52_ds5_output_state_update(&state, short_update,
                                             sizeof(short_update)) == -1);
    return 0;
}
