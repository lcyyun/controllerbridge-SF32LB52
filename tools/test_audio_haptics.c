#include "sf32lb52_audio_haptics.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const int16_t low_pattern[16] = {
    0, 6144, 11520, 15104, 16384, 15104, 11520, 6144,
    0, -6144, -11520, -15104, -16384, -15104, -11520, -6144,
};

static const int16_t high_pattern[8] = {
    0, 11520, 16384, 11520, 0, -11520, -16384, -11520,
};

static void fill_stereo(int16_t *samples, size_t frames,
                        const int16_t *left, size_t left_len,
                        const int16_t *right, size_t right_len)
{
    size_t index;

    for (index = 0U; index < frames; index++) {
        samples[index * 2U] = left != 0 ? left[index % left_len] : 0;
        samples[index * 2U + 1U] = right != 0 ? right[index % right_len] : 0;
    }
}

static sf32lb52_audio_haptics_event_t feed_window(
    sf32lb52_audio_haptics_processor_t *processor,
    const int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW * 2U],
    sf32lb52_audio_haptics_output_t *output)
{
    sf32lb52_audio_haptics_event_t event;

    event = sf32lb52_audio_haptics_process_s16(processor, samples, 32U, output);
    assert(event == SF32LB52_AUDIO_HAPTICS_NO_OUTPUT);
    return sf32lb52_audio_haptics_process_s16(processor,
                                              samples + 64U,
                                              32U,
                                              output);
}

static void test_left_low_and_right_high(void)
{
    sf32lb52_audio_haptics_processor_t processor;
    sf32lb52_audio_haptics_output_t output;
    int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW * 2U];

    sf32lb52_audio_haptics_init(&processor);
    fill_stereo(samples, SF32LB52_AUDIO_HAPTICS_WINDOW,
                low_pattern, sizeof(low_pattern) / sizeof(low_pattern[0]),
                0, 0U);
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_ACTIVE);
    assert(output.left.low_amplitude > output.left.high_amplitude);
    assert(output.left.low_amplitude > 0U);
    assert(output.left.low_frequency >= 70U &&
           output.left.low_frequency <= 300U);
    assert(output.right.low_amplitude == 0U);
    assert(output.right.high_amplitude == 0U);

    sf32lb52_audio_haptics_reset(&processor);
    fill_stereo(samples, SF32LB52_AUDIO_HAPTICS_WINDOW,
                0, 0U, high_pattern,
                sizeof(high_pattern) / sizeof(high_pattern[0]));
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_ACTIVE);
    assert(output.right.high_amplitude > output.right.low_amplitude);
    assert(output.right.high_amplitude > 0U);
    assert(output.right.high_frequency >= 281U &&
           output.right.high_frequency <= 369U);
    assert(output.left.low_amplitude == 0U);
    assert(output.left.high_amplitude == 0U);
}

static void test_silence_and_reset_stop(void)
{
    sf32lb52_audio_haptics_processor_t processor;
    sf32lb52_audio_haptics_output_t output;
    int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW * 2U];
    int16_t silence[64] = {0};
    sf32lb52_audio_haptics_event_t event;
    unsigned int attempts;

    sf32lb52_audio_haptics_init(&processor);
    fill_stereo(samples, SF32LB52_AUDIO_HAPTICS_WINDOW,
                low_pattern, sizeof(low_pattern) / sizeof(low_pattern[0]),
                0, 0U);
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_ACTIVE);

    event = SF32LB52_AUDIO_HAPTICS_NO_OUTPUT;
    for (attempts = 0U; attempts < 64U; attempts++) {
        event = sf32lb52_audio_haptics_process_s16(&processor,
                                                  silence,
                                                  32U,
                                                  &output);
        if (event == SF32LB52_AUDIO_HAPTICS_STOP) {
            break;
        }
    }
    assert(event == SF32LB52_AUDIO_HAPTICS_STOP);
    assert(output.left.low_amplitude == 0U);
    assert(output.right.high_amplitude == 0U);

    fill_stereo(samples, SF32LB52_AUDIO_HAPTICS_WINDOW,
                low_pattern, sizeof(low_pattern) / sizeof(low_pattern[0]),
                0, 0U);
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_ACTIVE);
    assert(sf32lb52_audio_haptics_process_s16(&processor, 0, 0U, &output) ==
           SF32LB52_AUDIO_HAPTICS_STOP);
    assert(processor.output_active == 0U);
}

static void test_isolated_fullscale_impulse_is_suppressed(void)
{
    sf32lb52_audio_haptics_processor_t processor;
    sf32lb52_audio_haptics_output_t output;
    int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW * 2U] = {0};

    sf32lb52_audio_haptics_init(&processor);
    samples[10U * 2U] = INT16_MAX;
    samples[45U * 2U + 1U] = INT16_MIN;
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_NO_OUTPUT);
    assert(processor.spectral_windows == 0U);
    assert(processor.output_active == 0U);
    assert(processor.suppressed_impulses == 2U);
}

static void test_dense_signal_outlier_is_suppressed(void)
{
    sf32lb52_audio_haptics_processor_t processor;
    sf32lb52_audio_haptics_output_t output;
    int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW * 2U];

    sf32lb52_audio_haptics_init(&processor);
    fill_stereo(samples, SF32LB52_AUDIO_HAPTICS_WINDOW,
                low_pattern, sizeof(low_pattern) / sizeof(low_pattern[0]),
                0, 0U);
    samples[20U * 2U] = INT16_MAX;
    assert(feed_window(&processor, samples, &output) ==
           SF32LB52_AUDIO_HAPTICS_ACTIVE);
    assert(processor.suppressed_impulses == 1U);
    assert(output.left.low_amplitude > output.left.high_amplitude);
}

int main(void)
{
    test_left_low_and_right_high();
    test_silence_and_reset_stop();
    test_isolated_fullscale_impulse_is_suppressed();
    test_dense_signal_outlier_is_suppressed();
    return 0;
}
