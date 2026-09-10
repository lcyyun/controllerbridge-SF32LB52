#include "sf32lb52_bridge_runtime.h"
#include "sf32lb52_usb_device.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#if defined(TEST_MAPPING_NVDS)
#include "bf0_sibles_nvds.h"
#endif

static int actual_usb_override = -1;

Sf32lb52UsbRole sf32lb52_usb_get_role(void)
{
    if (actual_usb_override >= 0) {
        return (Sf32lb52UsbRole)actual_usb_override;
    }
    switch (sf32lb52_bridge_runtime_role()) {
    case SF32LB52_BRIDGE_ROLE_NS2PRO: return Sf32lb52UsbRoleNintendo;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE: return Sf32lb52UsbRoleDualSense;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE: return Sf32lb52UsbRoleDualSenseEdge;
    default: return Sf32lb52UsbRoleXbox360;
    }
}

typedef struct {
    int reject_role;
    unsigned int role_calls;
    unsigned int feedback_calls;
    sf32lb52_bridge_role_t last_role;
    sf32lb52_bridge_input_source_t feedback_source;
    sf32lb52_bridge_feedback_t feedback;
} callback_state_t;

static int on_role(sf32lb52_bridge_role_t role, void *context)
{
    callback_state_t *state = (callback_state_t *)context;

    state->role_calls++;
    state->last_role = role;
    return state->reject_role ? -1 : 0;
}

static int on_feedback(sf32lb52_bridge_input_source_t source,
                       const sf32lb52_bridge_feedback_t *feedback,
                       void *context)
{
    callback_state_t *state = (callback_state_t *)context;

    state->feedback_calls++;
    state->feedback_source = source;
    state->feedback = *feedback;
    return 0;
}

static sf32lb52_bridge_input_state_t make_input(
    sf32lb52_bridge_input_source_t source,
    sf32lb52_bridge_button_t button)
{
    sf32lb52_bridge_input_state_t state;

    sf32lb52_bridge_input_state_reset(&state);
    state.valid = 1U;
    state.source = source;
    state.buttons = SF32LB52_BRIDGE_BUTTON_MASK(button);
    state.left_x = 1234;
    state.left_y = -2345;
    return state;
}

static void reset_to_defaults(void)
{
    actual_usb_override = -1;
    sf32lb52_bridge_runtime_init();
    assert(sf32lb52_bridge_runtime_reset_settings());
    assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5));
    assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO));
    assert(sf32lb52_bridge_runtime_reset_route_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5, SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5));
    assert(sf32lb52_bridge_runtime_reset_route_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO, SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5));
    assert(sf32lb52_bridge_runtime_save_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO));
    sf32lb52_bridge_runtime_init();
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_NS2PRO);
    assert(sf32lb52_bridge_runtime_mapping_is_identity());
}

static void test_single_active_and_stale_neutral(void)
{
    sf32lb52_bridge_input_state_t ds5 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
        SF32LB52_BRIDGE_BUTTON_SOUTH);
    sf32lb52_bridge_input_state_t ns2 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
        SF32LB52_BRIDGE_BUTTON_NORTH);
    sf32lb52_bridge_runtime_status_t status;
    uint8_t report[64];

    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_XBOX_360, false));

    assert(sf32lb52_bridge_runtime_accept_input(&ds5, 100U));
    assert(!sf32lb52_bridge_runtime_accept_input(&ns2, 200U));
    assert(sf32lb52_bridge_runtime_make_input_report(200U,
                                                     report,
                                                     sizeof(report)) == 20U);
    assert((report[3] & 0x10U) != 0U);

    assert(sf32lb52_bridge_runtime_make_input_report(351U,
                                                     report,
                                                     sizeof(report)) == 20U);
    assert(report[2] == 0U && report[3] == 0U);
    assert(!sf32lb52_bridge_runtime_accept_input(&ns2, 1600U));
    assert(sf32lb52_bridge_runtime_accept_input(&ns2, 1601U));

    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE);
    assert(status.accepted_reports == 2U);
    assert(status.ignored_reports == 2U);
    assert(status.source_switches == 2U);
    assert(status.neutral_reports == 1U);
}

static void test_role_preference_feedback_and_persistence(void)
{
    callback_state_t callbacks;
    sf32lb52_bridge_input_state_t ds5 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
        SF32LB52_BRIDGE_BUTTON_EAST);
    sf32lb52_bridge_runtime_status_t status;
    sf32lb52_bridge_persisted_config_t config;
    uint8_t xbox_output[8] = {0x00U, 0x08U, 0x00U, 0x40U, 0x80U};
    uint8_t report[64];
    const uint8_t ds5_address[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    const uint8_t ns2_address[6] = {6U, 5U, 4U, 3U, 2U, 1U};

    memset(&callbacks, 0, sizeof(callbacks));
    sf32lb52_bridge_runtime_set_callbacks(on_role, on_feedback, &callbacks);
    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_XBOX_360, false));
    callbacks.role_calls = 0U;
    assert(sf32lb52_bridge_runtime_accept_input(&ds5, 2000U));
    assert(sf32lb52_bridge_runtime_on_usb_output(xbox_output,
                                                 sizeof(xbox_output)) == 0);
    assert(callbacks.feedback_calls == 1U);
    assert(callbacks.feedback_source ==
           SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT);
    assert(callbacks.feedback.left_motor == (uint16_t)(0x40U * 257U));
    assert(callbacks.feedback.right_motor == (uint16_t)(0x80U * 257U));

    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_DUALSENSE, true));
    assert(callbacks.role_calls == 1U);
    assert(callbacks.last_role == SF32LB52_BRIDGE_ROLE_DUALSENSE);
    assert(sf32lb52_bridge_runtime_make_input_report(2001U,
                                                     report,
                                                     sizeof(report)) == 64U);
    assert(report[0] == 0x01U);

    callbacks.reject_role = 1;
    assert(!sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_NS2PRO, true));
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_DUALSENSE);
    callbacks.reject_role = 0;

    callbacks.reject_role = 1;
    assert(sf32lb52_bridge_runtime_sync_role(
        SF32LB52_BRIDGE_ROLE_NS2PRO, true));
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_NS2PRO);
    assert(callbacks.role_calls == 2U);
    callbacks.reject_role = 0;
    assert(sf32lb52_bridge_runtime_sync_role(
        SF32LB52_BRIDGE_ROLE_DUALSENSE, true));

    assert(sf32lb52_bridge_runtime_set_input_preference(
        SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO, true));
    assert(sf32lb52_bridge_runtime_set_auto_connect(false, true));
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_NONE);
    assert(sf32lb52_bridge_runtime_remember_ds5(ds5_address));
    assert(sf32lb52_bridge_runtime_remember_ns2(1U, ns2_address));
    assert(sf32lb52_bridge_runtime_save_settings());

    sf32lb52_bridge_runtime_init();
    sf32lb52_bridge_runtime_get_config(&config);
    assert(config.output_role == SF32LB52_BRIDGE_ROLE_DUALSENSE);
    assert(config.input_preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO);
    assert(config.auto_connect == 0U);
    assert(config.ds5_address_valid != 0U);
    assert(config.ns2_address_valid != 0U && config.ns2_address_type == 1U);
    assert(memcmp(config.ds5_address, ds5_address, 6U) == 0);
    assert(memcmp(config.ns2_address, ns2_address, 6U) == 0);
    assert(sf32lb52_bridge_runtime_set_auto_connect(true, false));
    sf32lb52_bridge_runtime_get_config(&config);
    assert(config.auto_connect == 1U);
}

static void test_parsers(void)
{
    sf32lb52_bridge_role_t role;
    sf32lb52_bridge_input_preference_t preference;

    assert(sf32lb52_bridge_parse_role("XInput", &role));
    assert(role == SF32LB52_BRIDGE_ROLE_XBOX_360);
    assert(sf32lb52_bridge_parse_role("DS5", &role));
    assert(role == SF32LB52_BRIDGE_ROLE_DUALSENSE);
    assert(sf32lb52_bridge_parse_role("DualSense-Edge", &role));
    assert(role == SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE);
    assert(sf32lb52_bridge_parse_role("nintendo", &role));
    assert(role == SF32LB52_BRIDGE_ROLE_NS2PRO);
    assert(!sf32lb52_bridge_parse_role("gip", &role));
    assert(sf32lb52_bridge_parse_input_preference("AUTO", &preference));
    assert(preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO);
}

static void test_four_role_cycle(void)
{
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_NS2PRO);
    assert(sf32lb52_bridge_runtime_cycle_role(false));
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_XBOX_360);
    assert(sf32lb52_bridge_runtime_cycle_role(false));
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_DUALSENSE);
    assert(sf32lb52_bridge_runtime_cycle_role(false));
    assert(sf32lb52_bridge_runtime_role() ==
           SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE);
    assert(sf32lb52_bridge_runtime_cycle_role(false));
    assert(sf32lb52_bridge_runtime_role() == SF32LB52_BRIDGE_ROLE_NS2PRO);
}

static void test_saved_inputs_are_independent(void)
{
    const uint8_t ds5_address[6] = {1U, 3U, 5U, 7U, 9U, 11U};
    const uint8_t ns2_address[6] = {2U, 4U, 6U, 8U, 10U, 12U};
    sf32lb52_bridge_persisted_config_t config;

    assert(sf32lb52_bridge_runtime_remember_ds5(ds5_address));
    assert(sf32lb52_bridge_runtime_remember_ns2(1U, ns2_address));
    assert(sf32lb52_bridge_runtime_save_settings());
    assert(sf32lb52_bridge_runtime_forget_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT, true));
    sf32lb52_bridge_runtime_get_config(&config);
    assert(config.ds5_address_valid == 0U);
    assert(config.ns2_address_valid == 1U);
    assert(memcmp(config.ns2_address, ns2_address, sizeof(ns2_address)) == 0);

    assert(sf32lb52_bridge_runtime_forget_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE, true));
    sf32lb52_bridge_runtime_get_config(&config);
    assert(config.ns2_address_valid == 0U);
}

static void test_mapping_runtime_persistence_and_native_paths(void)
{
    sf32lb52_bridge_input_state_t ds5 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
        SF32LB52_BRIDGE_BUTTON_EAST);
    sf32lb52_bridge_input_state_t ns2 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
        SF32LB52_BRIDGE_BUTTON_EAST);
    sf32lb52_bridge_mapping_config_t mapping;
    sf32lb52_bridge_mapping_config_t ns2_mapping;
    sf32lb52_bridge_runtime_status_t status;
    uint8_t report[64];

    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_XBOX_360, false));
    assert(sf32lb52_bridge_runtime_accept_input(&ds5, 100U));
    assert(sf32lb52_bridge_runtime_set_button_mapping(
        SF32LB52_BRIDGE_BUTTON_SOUTH, SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_runtime_set_button_mapping(
        SF32LB52_BRIDGE_BUTTON_EAST, SF32LB52_BRIDGE_MAPPING_NONE));
    assert(!sf32lb52_bridge_runtime_mapping_is_identity());
    assert(sf32lb52_bridge_runtime_make_input_report(
        101U, report, sizeof(report)) == SF32LB52_BRIDGE_XINPUT_REPORT_SIZE);
    assert((report[3] & 0x10U) != 0U);
    assert((report[3] & 0x20U) == 0U);
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_custom != 0U && status.mapping_dirty != 0U);

    assert(sf32lb52_bridge_runtime_save_button_mapping());
    sf32lb52_bridge_runtime_init();
    assert(sf32lb52_bridge_runtime_set_role(SF32LB52_BRIDGE_ROLE_DUALSENSE, false));
    assert(sf32lb52_bridge_runtime_get_button_mapping(&mapping));
    assert(mapping.source_for_target[SF32LB52_BRIDGE_BUTTON_SOUTH] ==
           SF32LB52_BRIDGE_BUTTON_EAST);
    assert(mapping.source_for_target[SF32LB52_BRIDGE_BUTTON_EAST] ==
           SF32LB52_BRIDGE_MAPPING_NONE);

    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_DUALSENSE, false));
    assert(sf32lb52_bridge_runtime_accept_input(&ds5, 200U));
    memset(report, 0xa5, sizeof(report));
    report[0] = 0x01U;
    report[5] = 0U;
    report[6] = 0U;
    report[8] = 0x48U;
    report[9] = 0U;
    report[10] = 0x08U;
    assert(sf32lb52_bridge_runtime_patch_native_input_report(
        201U, report, sizeof(report)));
    assert(report[8] == 0x28U);
    assert(report[10] == 0x08U);
    assert(report[20] == 0xa5U);

    sf32lb52_bridge_runtime_release_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT, 300U);
    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_NS2PRO, false));
    assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
        SF32LB52_BRIDGE_BUTTON_SOUTH, SF32LB52_BRIDGE_BUTTON_EAST));
    assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
        SF32LB52_BRIDGE_BUTTON_EAST, SF32LB52_BRIDGE_MAPPING_NONE));
    assert(sf32lb52_bridge_runtime_accept_input(&ns2, 301U));
    memset(report, 0x5a, sizeof(report));
    report[0] = 0x05U;
    report[5] = 0x38U;
    report[6] = 0x80U;
    report[7] = 0x30U;
    report[8] = 0xfcU;
    assert(sf32lb52_bridge_runtime_patch_native_input_report(
        302U, report, sizeof(report)));
    assert(report[5] == 0x34U);
    assert(report[6] == 0x80U && report[7] == 0x30U && report[8] == 0xfcU);
    assert(report[20] == 0x5aU);

    reset_to_defaults();
    assert(sf32lb52_bridge_runtime_mapping_is_identity());
    assert(sf32lb52_bridge_runtime_get_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5, &mapping));
    assert(sf32lb52_bridge_runtime_get_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO, &ns2_mapping));
    assert(sf32lb52_bridge_mapping_is_identity(&mapping));
    assert(sf32lb52_bridge_mapping_is_identity(&ns2_mapping));
}

static sf32lb52_bridge_mapping_profiles_t get_profiles(void)
{
    sf32lb52_bridge_mapping_profiles_t profiles;

    assert(sf32lb52_bridge_runtime_get_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5, &profiles.ds5));
    assert(sf32lb52_bridge_runtime_get_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO, &profiles.ns2pro));
    return profiles;
}

static void assert_profiles_equal(
    const sf32lb52_bridge_mapping_profiles_t *expected)
{
    sf32lb52_bridge_mapping_profiles_t actual = get_profiles();

    assert(memcmp(&actual, expected, sizeof(actual)) == 0);
}

static void configure_distinct_profiles(void)
{
    /* Baseline migrated behavior: source maps equal across USB outputs. */
    sf32lb52_bridge_role_t old_role = sf32lb52_bridge_runtime_role();
    int output;

    for (output = 0; output < 2; ++output) {
        assert(sf32lb52_bridge_runtime_set_role(output == 0
            ? SF32LB52_BRIDGE_ROLE_DUALSENSE : SF32LB52_BRIDGE_ROLE_NS2PRO, false));
        assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
            SF32LB52_BRIDGE_BUTTON_SOUTH, SF32LB52_BRIDGE_BUTTON_EAST));
        assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
            SF32LB52_BRIDGE_BUTTON_EAST, SF32LB52_BRIDGE_MAPPING_NONE));
        assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
            SF32LB52_BRIDGE_BUTTON_NORTH, SF32LB52_BRIDGE_BUTTON_EAST));
        assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
            SF32LB52_BRIDGE_BUTTON_EAST, SF32LB52_BRIDGE_MAPPING_NONE));
    }
    assert(sf32lb52_bridge_runtime_set_role(old_role, false));
}

static void mapping_reboot(void)
{
#if defined(TEST_MAPPING_NVDS)
    fake_nvds_reboot();
#endif
    sf32lb52_bridge_runtime_init();
}

static void assert_face_report(sf32lb52_bridge_role_t role,
                               const uint8_t *report, bool ds5_source)
{
    if (role == SF32LB52_BRIDGE_ROLE_XBOX_360) {
        assert(report[0] == 0U && report[1] == 20U);
        assert((report[3] & 0xf0U) == (ds5_source ? 0x10U : 0x80U));
    } else if (role == SF32LB52_BRIDGE_ROLE_NS2PRO) {
        assert(report[0] == 0x05U);
        assert((report[5] & 0x0fU) == (ds5_source ? 0x04U : 0x02U));
    } else {
        assert(report[0] == 0x01U);
        assert(report[8] == (ds5_source ? 0x28U : 0x88U));
    }
}

static void test_mapping_source_role_matrix(void)
{
    static const sf32lb52_bridge_role_t roles[] = {
        SF32LB52_BRIDGE_ROLE_XBOX_360,
        SF32LB52_BRIDGE_ROLE_DUALSENSE,
        SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE,
        SF32LB52_BRIDGE_ROLE_NS2PRO,
    };
    static const sf32lb52_bridge_input_source_t sources[] = {
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
        SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
        SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
    };
    sf32lb52_bridge_mapping_profiles_t expected;
    sf32lb52_bridge_runtime_status_t status;
    uint8_t report[64];
    uint8_t before[64];
    size_t source_index;
    size_t role_index;

    configure_distinct_profiles();
    expected = get_profiles();
    for (source_index = 0U; source_index < 3U; ++source_index) {
        bool ds5 = sources[source_index] ==
            SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT;
        sf32lb52_bridge_mapping_profile_t profile = ds5
            ? SF32LB52_BRIDGE_MAPPING_PROFILE_DS5
            : SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO;
        sf32lb52_bridge_input_state_t input =
            make_input(sources[source_index], SF32LB52_BRIDGE_BUTTON_EAST);
        sf32lb52_bridge_input_state_t competing = make_input(
            ds5 ? SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE :
                  SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT,
            SF32LB52_BRIDGE_BUTTON_EAST);

        sf32lb52_bridge_runtime_release_input(
            SF32LB52_BRIDGE_INPUT_SOURCE_NONE, 99U);
        assert(sf32lb52_bridge_runtime_accept_input(&input, 100U));
        assert(!sf32lb52_bridge_runtime_accept_input(&competing, 101U));
        for (role_index = 0U; role_index < 4U; ++role_index) {
            sf32lb52_bridge_role_t role = roles[role_index];
            sf32lb52_bridge_input_state_t raw;

            assert(sf32lb52_bridge_runtime_set_role(role, false));
            assert(sf32lb52_bridge_runtime_active_mapping_profile() == profile);
            assert(sf32lb52_bridge_runtime_make_input_report(
                102U, report, sizeof(report)) ==
                sf32lb52_bridge_input_report_size(role));
            assert_face_report(role, report, ds5);
            assert(sf32lb52_bridge_runtime_get_input_state(&raw));
            assert(memcmp(&raw, &input, sizeof(raw)) == 0);

            if ((!ds5 && role == SF32LB52_BRIDGE_ROLE_NS2PRO) ||
                (ds5 && (role == SF32LB52_BRIDGE_ROLE_DUALSENSE ||
                         role == SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE))) {
                size_t byte;

                memset(report, 0xa5, sizeof(report));
                report[0] = ds5 ? 0x01U : 0x05U;
                memcpy(before, report, sizeof(before));
                assert(sf32lb52_bridge_runtime_patch_native_input_report(
                    102U, report, sizeof(report)));
                assert_face_report(role, report, ds5);
                for (byte = 0U; byte < sizeof(report); ++byte) {
                    bool patched = ds5
                        ? (byte == 5U || byte == 6U ||
                           (byte >= 8U && byte <= 10U))
                        : (byte >= 5U && byte <= 8U);
                    if (!patched) {
                        assert(report[byte] == before[byte]);
                    }
                }
                memcpy(before, report, sizeof(before));
                assert(!sf32lb52_bridge_runtime_patch_native_input_report(
                    351U, report, sizeof(report)));
                assert(memcmp(before, report, sizeof(report)) == 0);
            }
            assert_profiles_equal(&expected);
        }

        /* The delayed USB path synchronizes roles through a separate API. */
        for (role_index = 0U; role_index < 4U; ++role_index) {
            assert(sf32lb52_bridge_runtime_sync_role(roles[role_index], false));
            assert(sf32lb52_bridge_runtime_accept_input(&input, 200U));
            assert(sf32lb52_bridge_runtime_active_mapping_profile() == profile);
            assert(sf32lb52_bridge_runtime_make_input_report(
                201U, report, sizeof(report)) ==
                sf32lb52_bridge_input_report_size(roles[role_index]));
            assert_face_report(roles[role_index], report, ds5);
            assert_profiles_equal(&expected);
        }
        assert(sf32lb52_bridge_runtime_make_input_report(
            451U, report, sizeof(report)) == 64U);
        assert((report[5] & 0x0fU) == 0U);
        sf32lb52_bridge_runtime_get_status(&status);
        assert(status.mapping_custom == 1U && status.mapping_dirty == 1U);
    }
}

static void test_mapping_reboot_and_independent_reset(void)
{
    static const sf32lb52_bridge_mapping_profile_t profiles[] = {
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
        SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO,
    };
    sf32lb52_bridge_mapping_profiles_t saved;
    sf32lb52_bridge_mapping_profiles_t expected;
    sf32lb52_bridge_runtime_status_t status;
    size_t i;

    for (i = 0U; i < 2U; ++i) {
        reset_to_defaults();
        configure_distinct_profiles();
        saved = get_profiles();
        /* Legacy save still checkpoints all routes, using the v3 record. */
        assert(sf32lb52_bridge_runtime_save_profile_button_mapping(profiles[i]));
        assert(sf32lb52_bridge_runtime_set_role(
            SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE, true));
        assert(sf32lb52_bridge_runtime_set_profile_button_mapping(
            profiles[i], SF32LB52_BRIDGE_BUTTON_GUIDE,
            SF32LB52_BRIDGE_MAPPING_NONE));
        mapping_reboot();
        assert(sf32lb52_bridge_runtime_role() ==
               SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE);
        assert_profiles_equal(&saved);
        sf32lb52_bridge_runtime_get_status(&status);
        assert(status.mapping_dirty == 0U && status.mapping_custom == 1U);
        assert(status.mapping_last_persist_ok == 1U);

        assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(profiles[i]));
        expected = saved;
        sf32lb52_bridge_mapping_defaults(i == 0U ? &expected.ds5 :
                                                   &expected.ns2pro);
        assert_profiles_equal(&expected);
        assert(!sf32lb52_bridge_runtime_mapping_is_identity());
        mapping_reboot(); /* An unsaved reset must not reach persistent state. */
        assert_profiles_equal(&saved);
        assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(profiles[i]));
        assert(sf32lb52_bridge_runtime_save_profile_button_mapping(profiles[i]));
        mapping_reboot();
        assert_profiles_equal(&expected);
        sf32lb52_bridge_runtime_get_status(&status);
        assert(status.mapping_custom == 1U && status.mapping_dirty == 0U);
    }
}

static void test_mapping_legacy_active_commands(void)
{
    sf32lb52_bridge_mapping_profiles_t before;
    sf32lb52_bridge_mapping_profiles_t after;
    sf32lb52_bridge_mapping_config_t active;
    sf32lb52_bridge_input_state_t ns2 = make_input(
        SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
        SF32LB52_BRIDGE_BUTTON_EAST);

    configure_distinct_profiles();
    before = get_profiles();
    assert(sf32lb52_bridge_runtime_set_role(
        SF32LB52_BRIDGE_ROLE_DUALSENSE, true));
    assert(sf32lb52_bridge_runtime_accept_input(&ns2, 100U));
    assert(sf32lb52_bridge_runtime_active_mapping_profile() ==
           SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO);
    assert(sf32lb52_bridge_runtime_set_button_mapping(
        SF32LB52_BRIDGE_BUTTON_GUIDE, SF32LB52_BRIDGE_MAPPING_NONE));
    assert(sf32lb52_bridge_runtime_get_button_mapping(&active));
    assert(active.source_for_target[SF32LB52_BRIDGE_BUTTON_GUIDE] ==
           SF32LB52_BRIDGE_MAPPING_NONE);
    after = get_profiles();
    assert(memcmp(&before.ds5, &after.ds5, sizeof(before.ds5)) == 0);
    assert(sf32lb52_bridge_runtime_reset_button_mapping());
    after = get_profiles();
    assert(memcmp(&before.ds5, &after.ds5, sizeof(before.ds5)) == 0);
    assert(sf32lb52_bridge_mapping_is_identity(&after.ns2pro));
    assert(sf32lb52_bridge_runtime_save_button_mapping());
    mapping_reboot();
    assert_profiles_equal(&after);

    assert(!sf32lb52_bridge_runtime_reset_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_UNSPECIFIED));
    assert(!sf32lb52_bridge_runtime_save_profile_button_mapping(
        (sf32lb52_bridge_mapping_profile_t)99));
    assert(!sf32lb52_bridge_runtime_set_profile_button_mapping(
        (sf32lb52_bridge_mapping_profile_t)99,
        SF32LB52_BRIDGE_BUTTON_SOUTH, SF32LB52_BRIDGE_BUTTON_EAST));
    assert_profiles_equal(&after);
}

#if defined(TEST_MAPPING_NVDS)
static void test_mapping_runtime_nvds_failure(void)
{
    sf32lb52_bridge_mapping_profiles_t saved;
    sf32lb52_bridge_mapping_profiles_t edited;
    sf32lb52_bridge_runtime_status_t status;

    configure_distinct_profiles();
    saved = get_profiles();
    assert(sf32lb52_bridge_runtime_save_button_mapping());
    assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5));
    edited = get_profiles();
    fake_nvds_fail_writes(true);
    assert(!sf32lb52_bridge_runtime_save_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5));
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_dirty == 1U && status.mapping_last_persist_ok == 0U);
    assert_profiles_equal(&edited);
    mapping_reboot();
    assert_profiles_equal(&saved);

    assert(sf32lb52_bridge_runtime_reset_profile_button_mapping(
        SF32LB52_BRIDGE_MAPPING_PROFILE_DS5));
    fake_nvds_fail_init(true);
    assert(!sf32lb52_bridge_runtime_save_button_mapping());
    sf32lb52_bridge_runtime_get_status(&status);
    assert(status.mapping_dirty == 1U && status.mapping_last_persist_ok == 0U);
    fake_nvds_fail_init(false);
    assert(sf32lb52_bridge_runtime_save_button_mapping());
    mapping_reboot();
    assert_profiles_equal(&edited);
}
#endif

static sf32lb52_bridge_mapping_routes_t get_routes(void)
{
    sf32lb52_bridge_mapping_routes_t routes;
    int s, o;

    for (s = 1; s <= 2; ++s) {
        for (o = 1; o <= 2; ++o) {
            assert(sf32lb52_bridge_runtime_get_route_button_mapping(
                (sf32lb52_bridge_mapping_profile_t)s,
                (sf32lb52_bridge_mapping_output_t)o,
                sf32lb52_bridge_mapping_route(&routes,
                    (sf32lb52_bridge_mapping_profile_t)s,
                    (sf32lb52_bridge_mapping_output_t)o)));
        }
    }
    return routes;
}

static void assert_routes_equal(const sf32lb52_bridge_mapping_routes_t *expected)
{
    sf32lb52_bridge_mapping_routes_t actual = get_routes();
    assert(memcmp(&actual, expected, sizeof(actual)) == 0);
}

static void configure_four_routes(void)
{
    int s, o, button;

    for (s = 1; s <= 2; ++s) {
        for (o = 1; o <= 2; ++o) {
            /* A single east press becomes a different face button per route. */
            int target = (s - 1) * 2 + o - 1;
            for (button = 0; button < 4; ++button) {
                assert(sf32lb52_bridge_runtime_set_route_button_mapping(
                    (sf32lb52_bridge_mapping_profile_t)s,
                    (sf32lb52_bridge_mapping_output_t)o,
                    (sf32lb52_bridge_button_t)button,
                    button == target ? SF32LB52_BRIDGE_BUTTON_EAST :
                                       SF32LB52_BRIDGE_MAPPING_NONE));
            }
        }
    }
}

static void test_four_route_runtime_isolation(void)
{
    const Sf32lb52UsbRole roles[] = {
        Sf32lb52UsbRoleDualSense, Sf32lb52UsbRoleNintendo,
        Sf32lb52UsbRoleDualSenseEdge, Sf32lb52UsbRoleXbox360,
    };
    const uint8_t ds5_faces[] = {0x20U, 0x40U, 0x10U, 0x80U};
    const uint8_t ns2_faces[] = {0x04U, 0x08U, 0x01U, 0x02U};
    const uint8_t xbox_faces[] = {0x10U, 0x20U, 0x40U, 0x80U};
    sf32lb52_bridge_mapping_routes_t expected;
    sf32lb52_bridge_mapping_config_t legacy;
    int s;
    size_t r;
    uint8_t report[64], before[64];

    reset_to_defaults();
    configure_four_routes();
    expected = get_routes();
    for (s = 1; s <= 2; ++s) {
        sf32lb52_bridge_input_state_t input = make_input(
            s == 1 ? SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT :
                     SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
            SF32LB52_BRIDGE_BUTTON_EAST);
        sf32lb52_bridge_runtime_release_input(SF32LB52_BRIDGE_INPUT_SOURCE_NONE, 99U);
        assert(sf32lb52_bridge_runtime_accept_input(&input, 100U));
        for (r = 0U; r < 4U; ++r) {
            bool ns2_output = roles[r] == Sf32lb52UsbRoleNintendo;
            int target = (s - 1) * 2 + (ns2_output ? 1 : 0);
            sf32lb52_bridge_role_t role = ns2_output
                ? SF32LB52_BRIDGE_ROLE_NS2PRO
                : roles[r] == Sf32lb52UsbRoleDualSenseEdge
                    ? SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE
                    : roles[r] == Sf32lb52UsbRoleXbox360
                        ? SF32LB52_BRIDGE_ROLE_XBOX_360 : SF32LB52_BRIDGE_ROLE_DUALSENSE;
            size_t len;

            actual_usb_override = roles[r];
            /* Legacy mapping selects actual USB; report caches must wait for
             * re-enumeration before publishing the new persona's reports. */
            assert(sf32lb52_bridge_runtime_set_role(ns2_output
                ? SF32LB52_BRIDGE_ROLE_DUALSENSE : SF32LB52_BRIDGE_ROLE_NS2PRO, false));
            assert(sf32lb52_bridge_runtime_active_mapping_output() == (ns2_output
                ? SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO : SF32LB52_BRIDGE_MAPPING_OUTPUT_DS5));
            assert(sf32lb52_bridge_runtime_get_profile_button_mapping(
                (sf32lb52_bridge_mapping_profile_t)s, &legacy));
            assert(memcmp(&legacy, sf32lb52_bridge_mapping_route(&expected,
                (sf32lb52_bridge_mapping_profile_t)s,
                sf32lb52_bridge_runtime_active_mapping_output()), sizeof(legacy)) == 0);
            memset(report, 0xa5, sizeof(report));
            memcpy(before, report, sizeof(report));
            assert(sf32lb52_bridge_runtime_make_input_report(
                101U, report, sizeof(report)) == 0U);
            assert(!sf32lb52_bridge_runtime_patch_native_input_report(
                101U, report, sizeof(report)));
            assert(memcmp(before, report, sizeof(report)) == 0);
            assert(sf32lb52_bridge_runtime_sync_role(role, false));
            assert(sf32lb52_bridge_runtime_accept_input(&input, 100U));
            len = sf32lb52_bridge_runtime_make_input_report(101U, report, sizeof(report));
            if (roles[r] == Sf32lb52UsbRoleXbox360) {
                assert(len == 20U && (report[3] & 0xf0U) == xbox_faces[target]);
            } else if (ns2_output) {
                assert(len == 64U && (report[5] & 0x0fU) == ns2_faces[target]);
            } else {
                assert(len == 64U && (report[8] & 0xf0U) == ds5_faces[target]);
            }
            if (roles[r] != Sf32lb52UsbRoleXbox360) {
                size_t byte;
                memset(report, 0xa5, sizeof(report));
                report[0] = ns2_output ? 5U : 1U;
                memcpy(before, report, sizeof(before));
                assert(sf32lb52_bridge_runtime_patch_native_input_report(
                    101U, report, sizeof(report)));
                assert(ns2_output ? (report[5] & 0x0fU) == ns2_faces[target] :
                    (report[8] & 0xf0U) == ds5_faces[target]);
                for (byte = 0U; byte < sizeof(report); ++byte) {
                    bool patched = ns2_output ? (byte >= 5U && byte <= 8U) :
                        (byte == 5U || byte == 6U || (byte >= 8U && byte <= 10U));
                    if (!patched) {
                        assert(report[byte] == before[byte]);
                    }
                }
            }
            assert_routes_equal(&expected);
        }
    }
    actual_usb_override = -1;
}

static void test_four_route_save_reset_isolation(void)
{
    sf32lb52_bridge_mapping_routes_t edited, expected, saved, actual;
    sf32lb52_bridge_runtime_status_t status;
    int s, o;

    for (s = 1; s <= 2; ++s) {
        for (o = 1; o <= 2; ++o) {
            sf32lb52_bridge_mapping_profile_t source = (sf32lb52_bridge_mapping_profile_t)s;
            sf32lb52_bridge_mapping_output_t output = (sf32lb52_bridge_mapping_output_t)o;
            reset_to_defaults();
            configure_four_routes();
            edited = get_routes();
            sf32lb52_bridge_mapping_routes_defaults(&expected);
            *sf32lb52_bridge_mapping_route(&expected, source, output) =
                *sf32lb52_bridge_mapping_route(&edited, source, output);
            assert(sf32lb52_bridge_runtime_save_route_button_mapping(source, output));
            assert_routes_equal(&edited); /* Other unsaved routes remain live. */
            sf32lb52_bridge_runtime_get_status(&status);
            assert(status.mapping_dirty == 1U && status.mapping_last_persist_ok == 1U);
            mapping_reboot();
            assert_routes_equal(&expected); /* Only the chosen route was saved. */

            configure_four_routes();
            assert(sf32lb52_bridge_runtime_save_button_mapping());
            saved = get_routes();
            assert(sf32lb52_bridge_runtime_reset_route_button_mapping(source, output));
            expected = saved;
            sf32lb52_bridge_mapping_defaults(
                sf32lb52_bridge_mapping_route(&expected, source, output));
            assert_routes_equal(&expected);
            mapping_reboot(); /* Unsaved reset cannot alter flash. */
            assert_routes_equal(&saved);
            assert(sf32lb52_bridge_runtime_reset_route_button_mapping(source, output));
#if defined(TEST_MAPPING_NVDS)
            fake_nvds_fail_writes(true);
            assert(!sf32lb52_bridge_runtime_save_route_button_mapping(source, output));
            sf32lb52_bridge_runtime_get_status(&status);
            assert(status.mapping_dirty == 1U && status.mapping_last_persist_ok == 0U);
            mapping_reboot();
            assert_routes_equal(&saved);
            assert(sf32lb52_bridge_runtime_reset_route_button_mapping(source, output));
#endif
            assert(sf32lb52_bridge_runtime_save_route_button_mapping(source, output));
            mapping_reboot();
            assert_routes_equal(&expected);
            sf32lb52_bridge_runtime_get_status(&status);
            assert(status.mapping_dirty == 0U && status.mapping_last_persist_ok == 1U);
            actual = get_routes();
            assert(!sf32lb52_bridge_runtime_set_route_button_mapping(source,
                SF32LB52_BRIDGE_MAPPING_OUTPUT_UNSPECIFIED,
                SF32LB52_BRIDGE_BUTTON_SOUTH, 0U));
            assert(!sf32lb52_bridge_runtime_save_route_button_mapping(source,
                (sf32lb52_bridge_mapping_output_t)99));
            assert_routes_equal(&actual);
        }
    }
}

#if defined(TEST_MAPPING_NVDS)
static void test_migrated_runtime_and_first_route_save(void)
{
    sf32lb52_bridge_mapping_profiles_t old;
    sf32lb52_bridge_mapping_routes_t expected;
    uint8_t wire[SF32LB52_BRIDGE_MAPPING_WIRE_SIZE];
    uint8_t report[64];
    int version, source, output;

    sf32lb52_bridge_mapping_profiles_defaults(&old);
    old.ds5.source_for_target[SF32LB52_BRIDGE_BUTTON_SOUTH] =
        SF32LB52_BRIDGE_BUTTON_EAST;
    old.ds5.source_for_target[SF32LB52_BRIDGE_BUTTON_EAST] =
        SF32LB52_BRIDGE_MAPPING_NONE;
    old.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_NORTH] =
        SF32LB52_BRIDGE_BUTTON_EAST;
    old.ns2pro.source_for_target[SF32LB52_BRIDGE_BUTTON_EAST] =
        SF32LB52_BRIDGE_MAPPING_NONE;
    for (version = 1; version <= 2; ++version) {
        size_t len = version == 1
            ? sf32lb52_bridge_mapping_serialize(&old.ds5, wire, sizeof(wire))
            : sf32lb52_bridge_mapping_profiles_serialize(&old, wire, sizeof(wire));
        fake_nvds_clear();
        fake_nvds_seed(version == 1 ? "sf32_map_v1" : "sf32_map_v2", wire, len);
        sf32lb52_bridge_runtime_init();
        expected.ds5.ds5 = expected.ds5.ns2pro = old.ds5;
        expected.ns2pro.ds5 = expected.ns2pro.ns2pro =
            version == 1 ? old.ds5 : old.ns2pro;
        assert_routes_equal(&expected);
        assert(fake_nvds_write_count() == 0U);
        for (source = 1; source <= 2; ++source) {
            sf32lb52_bridge_input_state_t input = make_input(source == 1
                ? SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT
                : SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE,
                SF32LB52_BRIDGE_BUTTON_EAST);
            sf32lb52_bridge_runtime_release_input(
                SF32LB52_BRIDGE_INPUT_SOURCE_NONE, 99U);
            assert(sf32lb52_bridge_runtime_accept_input(&input, 100U));
            for (output = 0; output < 2; ++output) {
                bool south = version == 1 || source == 1;
                actual_usb_override = output == 0
                    ? Sf32lb52UsbRoleDualSense : Sf32lb52UsbRoleNintendo;
                assert(sf32lb52_bridge_runtime_set_role(output == 0
                    ? SF32LB52_BRIDGE_ROLE_DUALSENSE : SF32LB52_BRIDGE_ROLE_NS2PRO, false));
                assert(sf32lb52_bridge_runtime_make_input_report(
                    101U, report, sizeof(report)) == sizeof(report));
                assert_face_report(output == 0
                    ? SF32LB52_BRIDGE_ROLE_DUALSENSE : SF32LB52_BRIDGE_ROLE_NS2PRO,
                    report, south);
            }
        }
        assert(sf32lb52_bridge_runtime_reset_route_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
            SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO));
        assert(sf32lb52_bridge_runtime_save_route_button_mapping(
            SF32LB52_BRIDGE_MAPPING_PROFILE_DS5,
            SF32LB52_BRIDGE_MAPPING_OUTPUT_NS2PRO));
        sf32lb52_bridge_mapping_defaults(&expected.ds5.ns2pro);
        mapping_reboot();
        assert_routes_equal(&expected); /* First v3 save retains all legacy maps. */
    }
    actual_usb_override = -1;
}
#endif

int main(void)
{
    reset_to_defaults();
    test_single_active_and_stale_neutral();
    reset_to_defaults();
    test_role_preference_feedback_and_persistence();
    test_parsers();
    reset_to_defaults();
    test_four_role_cycle();
    reset_to_defaults();
    test_saved_inputs_are_independent();
    reset_to_defaults();
    test_mapping_runtime_persistence_and_native_paths();
    reset_to_defaults();
    test_mapping_source_role_matrix();
    test_mapping_reboot_and_independent_reset();
    reset_to_defaults();
    test_mapping_legacy_active_commands();
    test_four_route_runtime_isolation();
    test_four_route_save_reset_isolation();
#if defined(TEST_MAPPING_NVDS)
    reset_to_defaults();
    test_mapping_runtime_nvds_failure();
    test_migrated_runtime_and_first_route_save();
#endif
    return 0;
}
