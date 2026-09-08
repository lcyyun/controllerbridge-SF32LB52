#ifndef SF32LB52_AUDIO_HAPTICS_H
#define SF32LB52_AUDIO_HAPTICS_H

#include <stddef.h>
#include <stdint.h>

#define SF32LB52_AUDIO_HAPTICS_SAMPLE_RATE 3000U
#define SF32LB52_AUDIO_HAPTICS_WINDOW 64U

typedef enum sf32lb52_audio_haptics_event {
    SF32LB52_AUDIO_HAPTICS_NO_OUTPUT = 0,
    SF32LB52_AUDIO_HAPTICS_ACTIVE,
    SF32LB52_AUDIO_HAPTICS_STOP,
} sf32lb52_audio_haptics_event_t;

typedef struct sf32lb52_audio_haptics_motor {
    uint16_t low_frequency;
    uint16_t low_amplitude;
    uint16_t high_frequency;
    uint16_t high_amplitude;
} sf32lb52_audio_haptics_motor_t;

typedef struct sf32lb52_audio_haptics_output {
    sf32lb52_audio_haptics_motor_t left;
    sf32lb52_audio_haptics_motor_t right;
} sf32lb52_audio_haptics_output_t;

typedef struct sf32lb52_audio_haptics_processor {
    int16_t left[SF32LB52_AUDIO_HAPTICS_WINDOW];
    int16_t right[SF32LB52_AUDIO_HAPTICS_WINDOW];
    uint8_t sample_count;
    int32_t envelope_left;
    int32_t envelope_right;
    uint8_t output_active;
    uint32_t input_blocks;
    uint32_t spectral_windows;
    uint32_t active_outputs;
    uint32_t stop_outputs;
    uint32_t suppressed_impulses;
} sf32lb52_audio_haptics_processor_t;

void sf32lb52_audio_haptics_init(
    sf32lb52_audio_haptics_processor_t *processor);
void sf32lb52_audio_haptics_reset(
    sf32lb52_audio_haptics_processor_t *processor);

/*
 * Accepts high-precision signed stereo samples at 3 kHz, interleaved
 * left/right. Amplitudes in the result retain the DualSense physical
 * 0..29000 scale and are mapped to NS2's 10-bit range only at the BLE output
 * boundary.
 */
sf32lb52_audio_haptics_event_t sf32lb52_audio_haptics_process_s16(
    sf32lb52_audio_haptics_processor_t *processor,
    const int16_t *interleaved,
    size_t frames,
    sf32lb52_audio_haptics_output_t *output);

#endif /* SF32LB52_AUDIO_HAPTICS_H */
