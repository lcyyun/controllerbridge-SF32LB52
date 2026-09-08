#include "sf32lb52_audio_haptics.h"

#include <limits.h>
#include <string.h>

#define AUDIO_HAPTICS_HOP 36U
#define AUDIO_HAPTICS_LOW_BIN_MIN 2U
#define AUDIO_HAPTICS_LOW_BIN_MAX 5U
#define AUDIO_HAPTICS_HIGH_BIN_MIN 6U
#define AUDIO_HAPTICS_HIGH_BIN_MAX 13U
#define AUDIO_HAPTICS_ENVELOPE_THRESHOLD 512
#define AUDIO_HAPTICS_PEAK_THRESHOLD 2048
#define AUDIO_HAPTICS_PHYSICAL_MAX 29000U
#define AUDIO_HAPTICS_IMPULSE_PEAK 30000
#define AUDIO_HAPTICS_IMPULSE_NEIGHBOR 64
#define AUDIO_HAPTICS_SPARSE_ACTIVE_SAMPLES 2U
#define AUDIO_HAPTICS_OUTLIER_MAX_SAMPLES 1U

typedef struct audio_haptics_band {
    uint16_t frequency;
    uint16_t rms;
} audio_haptics_band_t;

/* 2*cos(2*pi*k/64), Q14, for bins 2 through 13. */
static const int32_t goertzel_coefficients_q14[] = {
    32138, 31357, 30274, 28899, 27246, 25330,
    23170, 20788, 18205, 15447, 12540, 9512,
};

static int32_t sample_abs(int16_t value)
{
    return value == INT16_MIN ? 32768 :
           (value < 0 ? -(int32_t)value : (int32_t)value);
}

static int32_t smooth_envelope(int32_t previous, int32_t value)
{
    if (value > previous) {
        return (previous * 2 + value * 6 + 4) / 8;
    }
    return (previous * 7 + value + 4) / 8;
}

static int16_t filtered_haptic_sample(const int16_t *interleaved,
                                      size_t frames,
                                      size_t frame,
                                      size_t channel,
                                      size_t active_count,
                                      size_t candidate_count)
{
    int16_t value = interleaved[frame * 2U + channel];
    int32_t absolute = sample_abs(value);
    int32_t neighbor_peak = 0;
    int32_t replacement_sum = 0;
    uint8_t replacement_count = 0U;
    size_t neighbor;

    if (candidate_count != 0U &&
        active_count <= AUDIO_HAPTICS_SPARSE_ACTIVE_SAMPLES) {
        return absolute > AUDIO_HAPTICS_IMPULSE_NEIGHBOR ? 0 : value;
    }
    if (absolute < AUDIO_HAPTICS_IMPULSE_PEAK ||
        candidate_count > AUDIO_HAPTICS_OUTLIER_MAX_SAMPLES) {
        return value;
    }

    for (neighbor = frame > 0U ? frame - 1U : frame;
         neighbor <= frame + 1U && neighbor < frames;
         neighbor++) {
        int16_t neighbor_value;
        int32_t neighbor_absolute;

        if (neighbor == frame) {
            continue;
        }
        neighbor_value = interleaved[neighbor * 2U + channel];
        neighbor_absolute = sample_abs(neighbor_value);
        if (neighbor_absolute > neighbor_peak) {
            neighbor_peak = neighbor_absolute;
        }
        if (neighbor_absolute < AUDIO_HAPTICS_IMPULSE_PEAK) {
            replacement_sum += neighbor_value;
            replacement_count++;
        }
    }
    if (neighbor_peak <= AUDIO_HAPTICS_IMPULSE_NEIGHBOR) {
        return 0;
    }
    if (neighbor_peak * 4 < absolute * 3 && replacement_count != 0U) {
        return (int16_t)(replacement_sum / replacement_count);
    }
    return value;
}

static uint64_t integer_sqrt(uint64_t value)
{
    uint64_t result = 0U;
    uint64_t bit = UINT64_C(1) << 62;

    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

static uint64_t goertzel_power(const int16_t *samples, uint8_t bin)
{
    int64_t first = 0;
    int64_t second = 0;
    int32_t coefficient = goertzel_coefficients_q14[bin - 2U];
    size_t index;

    for (index = 0U; index < SF32LB52_AUDIO_HAPTICS_WINDOW; index++) {
        int64_t next = (int64_t)samples[index] +
                       ((int64_t)coefficient * first >> 14U) - second;
        second = first;
        first = next;
    }

    {
        int64_t power = first * first + second * second -
                        (((int64_t)coefficient * first * second) >> 14U);
        return power > 0 ? (uint64_t)power : 0U;
    }
}

static audio_haptics_band_t analyze_band(const int16_t *samples,
                                         uint8_t minimum_bin,
                                         uint8_t maximum_bin)
{
    uint64_t band_power = 0U;
    uint64_t best_power = 0U;
    uint8_t best_bin = minimum_bin;
    uint8_t bin;
    audio_haptics_band_t result;

    for (bin = minimum_bin; bin <= maximum_bin; bin++) {
        uint64_t power = goertzel_power(samples, bin);
        band_power += power;
        if (power > best_power) {
            best_power = power;
            best_bin = bin;
        }
    }

    result.frequency = (uint16_t)(
        ((uint32_t)best_bin * SF32LB52_AUDIO_HAPTICS_SAMPLE_RATE +
         SF32LB52_AUDIO_HAPTICS_WINDOW / 2U) /
        SF32LB52_AUDIO_HAPTICS_WINDOW);
    if (band_power == 0U) {
        result.rms = 0U;
    } else {
        uint64_t rms = integer_sqrt(band_power * 2U) /
                       SF32LB52_AUDIO_HAPTICS_WINDOW;
        result.rms = (uint16_t)(rms > UINT16_MAX ? UINT16_MAX : rms);
    }
    return result;
}

static uint16_t remap_low_frequency(uint16_t frequency)
{
    if (frequency <= 94U) {
        return 70U;
    }
    if (frequency >= 234U) {
        return 300U;
    }
    return (uint16_t)(70U +
        (((uint32_t)frequency - 94U) * 230U + 70U) / 140U);
}

static uint16_t remap_high_frequency(uint16_t frequency)
{
    if (frequency <= 281U) {
        return 281U;
    }
    if (frequency >= 609U) {
        return 369U;
    }
    return (uint16_t)(281U +
        (((uint32_t)frequency - 281U) * 88U + 164U) / 328U);
}

static uint16_t map_physical_amplitude(uint16_t rms)
{
    uint32_t amplitude = ((uint32_t)rms * AUDIO_HAPTICS_PHYSICAL_MAX +
                          8192U) / 16384U;

    if (amplitude > AUDIO_HAPTICS_PHYSICAL_MAX) {
        amplitude = AUDIO_HAPTICS_PHYSICAL_MAX;
    }
    return (uint16_t)(amplitude & UINT32_C(0xffffffc0));
}

static void fill_motor(const int16_t *samples,
                       sf32lb52_audio_haptics_motor_t *motor)
{
    audio_haptics_band_t low = analyze_band(samples,
                                             AUDIO_HAPTICS_LOW_BIN_MIN,
                                             AUDIO_HAPTICS_LOW_BIN_MAX);
    audio_haptics_band_t high = analyze_band(samples,
                                              AUDIO_HAPTICS_HIGH_BIN_MIN,
                                              AUDIO_HAPTICS_HIGH_BIN_MAX);

    motor->low_frequency = remap_low_frequency(low.frequency);
    motor->low_amplitude = map_physical_amplitude(low.rms);
    motor->high_frequency = remap_high_frequency(high.frequency);
    motor->high_amplitude = map_physical_amplitude(high.rms);
}

static int output_has_amplitude(const sf32lb52_audio_haptics_output_t *output)
{
    return output->left.low_amplitude != 0U ||
           output->left.high_amplitude != 0U ||
           output->right.low_amplitude != 0U ||
           output->right.high_amplitude != 0U;
}

static void shift_window(int16_t samples[SF32LB52_AUDIO_HAPTICS_WINDOW])
{
    memmove(samples,
            samples + AUDIO_HAPTICS_HOP,
            (SF32LB52_AUDIO_HAPTICS_WINDOW - AUDIO_HAPTICS_HOP) *
                sizeof(samples[0]));
}

void sf32lb52_audio_haptics_reset(
    sf32lb52_audio_haptics_processor_t *processor)
{
    if (processor != 0) {
        memset(processor, 0, sizeof(*processor));
    }
}

void sf32lb52_audio_haptics_init(
    sf32lb52_audio_haptics_processor_t *processor)
{
    sf32lb52_audio_haptics_reset(processor);
}

sf32lb52_audio_haptics_event_t sf32lb52_audio_haptics_process_s16(
    sf32lb52_audio_haptics_processor_t *processor,
    const int16_t *interleaved,
    size_t frames,
    sf32lb52_audio_haptics_output_t *output)
{
    int64_t sum_left = 0;
    int64_t sum_right = 0;
    int32_t peak_left = 0;
    int32_t peak_right = 0;
    size_t active_left = 0U;
    size_t active_right = 0U;
    size_t candidates_left = 0U;
    size_t candidates_right = 0U;
    size_t frame;
    size_t source = 0U;
    sf32lb52_audio_haptics_event_t event =
        SF32LB52_AUDIO_HAPTICS_NO_OUTPUT;

    if (processor == 0 || output == 0 ||
        (interleaved == 0 && frames != 0U)) {
        return SF32LB52_AUDIO_HAPTICS_NO_OUTPUT;
    }
    if (frames == 0U) {
        int was_active = processor->output_active != 0U;
        sf32lb52_audio_haptics_reset(processor);
        memset(output, 0, sizeof(*output));
        return was_active ? SF32LB52_AUDIO_HAPTICS_STOP :
                            SF32LB52_AUDIO_HAPTICS_NO_OUTPUT;
    }

    processor->input_blocks++;
    for (frame = 0U; frame < frames; frame++) {
        int32_t abs_left = sample_abs(interleaved[frame * 2U]);
        int32_t abs_right = sample_abs(interleaved[frame * 2U + 1U]);

        if (abs_left > AUDIO_HAPTICS_IMPULSE_NEIGHBOR) active_left++;
        if (abs_right > AUDIO_HAPTICS_IMPULSE_NEIGHBOR) active_right++;
        if (abs_left >= AUDIO_HAPTICS_IMPULSE_PEAK) candidates_left++;
        if (abs_right >= AUDIO_HAPTICS_IMPULSE_PEAK) candidates_right++;
    }
    for (frame = 0U; frame < frames; frame++) {
        int16_t left = filtered_haptic_sample(interleaved, frames, frame, 0U,
                                              active_left, candidates_left);
        int16_t right = filtered_haptic_sample(interleaved, frames, frame, 1U,
                                               active_right, candidates_right);
        int32_t abs_left = sample_abs(left);
        int32_t abs_right = sample_abs(right);

        if (left != interleaved[frame * 2U] &&
            sample_abs(interleaved[frame * 2U]) >=
                AUDIO_HAPTICS_IMPULSE_PEAK) {
            processor->suppressed_impulses++;
        }
        if (right != interleaved[frame * 2U + 1U] &&
            sample_abs(interleaved[frame * 2U + 1U]) >=
                AUDIO_HAPTICS_IMPULSE_PEAK) {
            processor->suppressed_impulses++;
        }
        sum_left += abs_left;
        sum_right += abs_right;
        if (abs_left > peak_left) peak_left = abs_left;
        if (abs_right > peak_right) peak_right = abs_right;
    }
    processor->envelope_left = smooth_envelope(
        processor->envelope_left, (int32_t)(sum_left / (int64_t)frames));
    processor->envelope_right = smooth_envelope(
        processor->envelope_right, (int32_t)(sum_right / (int64_t)frames));

    if (processor->envelope_left < AUDIO_HAPTICS_ENVELOPE_THRESHOLD &&
        processor->envelope_right < AUDIO_HAPTICS_ENVELOPE_THRESHOLD &&
        peak_left < AUDIO_HAPTICS_PEAK_THRESHOLD &&
        peak_right < AUDIO_HAPTICS_PEAK_THRESHOLD) {
        if (processor->output_active != 0U) {
            processor->output_active = 0U;
            processor->sample_count = 0U;
            memset(processor->left, 0, sizeof(processor->left));
            memset(processor->right, 0, sizeof(processor->right));
            memset(output, 0, sizeof(*output));
            processor->stop_outputs++;
            return SF32LB52_AUDIO_HAPTICS_STOP;
        }
        return SF32LB52_AUDIO_HAPTICS_NO_OUTPUT;
    }

    while (source < frames) {
        size_t available = SF32LB52_AUDIO_HAPTICS_WINDOW -
                           processor->sample_count;
        size_t take = frames - source;
        size_t index;

        if (take > available) {
            take = available;
        }
        for (index = 0U; index < take; index++) {
            processor->left[processor->sample_count + index] =
                filtered_haptic_sample(interleaved, frames, source + index,
                                       0U, active_left, candidates_left);
            processor->right[processor->sample_count + index] =
                filtered_haptic_sample(interleaved, frames, source + index,
                                       1U, active_right, candidates_right);
        }
        processor->sample_count = (uint8_t)(processor->sample_count + take);
        source += take;
        if (processor->sample_count < SF32LB52_AUDIO_HAPTICS_WINDOW) {
            continue;
        }

        memset(output, 0, sizeof(*output));
        fill_motor(processor->left, &output->left);
        fill_motor(processor->right, &output->right);
        processor->spectral_windows++;
        shift_window(processor->left);
        shift_window(processor->right);
        processor->sample_count =
            SF32LB52_AUDIO_HAPTICS_WINDOW - AUDIO_HAPTICS_HOP;

        if (output_has_amplitude(output)) {
            processor->output_active = 1U;
            processor->active_outputs++;
            event = SF32LB52_AUDIO_HAPTICS_ACTIVE;
        }
    }
    return event;
}
