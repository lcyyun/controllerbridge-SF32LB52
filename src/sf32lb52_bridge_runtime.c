#include "sf32lb52_bridge_runtime.h"
#include "sf32lb52_bridge_mapping_store.h"
#include "sf32lb52_usb_device.h"

#include <string.h>

#define SETTINGS_DEFER_MS 2000U

typedef struct {
    sf32lb52_bridge_persisted_config_t config;
    sf32lb52_bridge_runtime_status_t status;
    sf32lb52_bridge_input_state_t input;
    sf32lb52_bridge_mapping_routes_t mapping_profiles;
    sf32lb52_bridge_mapping_routes_t saved_mapping;
    volatile uint32_t input_sequence;
    volatile uint32_t mapping_sequence;
    uint8_t report_sequence;
    uint32_t settings_dirty_since_ms;
    uint32_t last_poll_ms;
    sf32lb52_bridge_role_change_callback_t role_change_callback;
    sf32lb52_bridge_feedback_callback_t feedback_callback;
    void *callback_context;
} bridge_runtime_t;

static bridge_runtime_t g_runtime;

static sf32lb52_bridge_role_t actual_output_role(void)
{
    /* Requested config can lead USB re-enumeration by several task ticks. */
    switch (sf32lb52_usb_get_role()) {
    case Sf32lb52UsbRoleNintendo: return SF32LB52_BRIDGE_ROLE_NS2PRO;
    case Sf32lb52UsbRoleDualSense: return SF32LB52_BRIDGE_ROLE_DUALSENSE;
    case Sf32lb52UsbRoleDualSenseEdge: return SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
    case Sf32lb52UsbRoleXbox360: return SF32LB52_BRIDGE_ROLE_XBOX_360;
    default: return (sf32lb52_bridge_role_t)-1;
    }
}

static int valid_role(sf32lb52_bridge_role_t role)
{
    return role == SF32LB52_BRIDGE_ROLE_XBOX_360 ||
           role == SF32LB52_BRIDGE_ROLE_DUALSENSE ||
           role == SF32LB52_BRIDGE_ROLE_NS2PRO ||
           role == SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
}

static int valid_preference(sf32lb52_bridge_input_preference_t preference)
{
    return preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO ||
           preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE ||
           preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO;
}

static int preference_accepts(sf32lb52_bridge_input_preference_t preference,
                              sf32lb52_bridge_input_source_t source)
{
    if (preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO) {
        return 1;
    }
    if (preference == SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE) {
        return source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT;
    }
    return source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE;
}

static void memory_barrier(void)
{
#if defined(__GNUC__)
    __sync_synchronize();
#endif
}

static void write_input(const sf32lb52_bridge_input_state_t *state)
{
    g_runtime.input_sequence++;
    memory_barrier();
    g_runtime.input = *state;
    memory_barrier();
    g_runtime.input_sequence++;
}

static bool read_input(sf32lb52_bridge_input_state_t *state)
{
    uint32_t before;
    uint32_t after;
    uint8_t attempts = 0U;

    do {
        before = g_runtime.input_sequence;
        memory_barrier();
        *state = g_runtime.input;
        memory_barrier();
        after = g_runtime.input_sequence;
        attempts++;
        if (before == after && (before & 1U) == 0U) {
            return true;
        }
    } while (attempts < 8U);
    return false;
}

static void write_mapping(
    const sf32lb52_bridge_mapping_routes_t *mapping_profiles)
{
    g_runtime.mapping_sequence++;
    memory_barrier();
    g_runtime.mapping_profiles = *mapping_profiles;
    memory_barrier();
    g_runtime.mapping_sequence++;
    g_runtime.status.mapping_custom =
        sf32lb52_bridge_mapping_routes_is_identity(mapping_profiles)
            ? 0U : 1U;
    g_runtime.status.mapping_dirty =
        memcmp(mapping_profiles, &g_runtime.saved_mapping,
               sizeof(*mapping_profiles)) != 0 ? 1U : 0U;
}

static bool read_mapping(
    sf32lb52_bridge_mapping_routes_t *mapping_profiles)
{
    uint32_t before;
    uint32_t after;
    uint8_t attempts = 0U;

    if (mapping_profiles == 0) {
        return false;
    }
    do {
        before = g_runtime.mapping_sequence;
        memory_barrier();
        *mapping_profiles = g_runtime.mapping_profiles;
        memory_barrier();
        after = g_runtime.mapping_sequence;
        attempts++;
        if (before == after && (before & 1U) == 0U) {
            return true;
        }
    } while (attempts < 8U);
    return false;
}

static sf32lb52_bridge_mapping_profile_t mapping_profile_for_source(
    sf32lb52_bridge_input_source_t source)
{
    return source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE
        ? SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO
        : SF32LB52_BRIDGE_MAPPING_PROFILE_DS5;
}

static void set_neutral_input(void)
{
    sf32lb52_bridge_input_state_t neutral;

    sf32lb52_bridge_input_state_reset(&neutral);
    neutral.valid = 1U;
    neutral.source = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    write_input(&neutral);
}

static void mark_settings_dirty(void)
{
    if (!g_runtime.status.settings_dirty) {
        g_runtime.settings_dirty_since_ms = g_runtime.last_poll_ms;
    }
    g_runtime.status.settings_dirty = 1U;
}

static int text_equal_ci(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return 0;
    }
    while (*left != 0 && *right != 0) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (a != b) {
            return 0;
        }
    }
    return *left == 0 && *right == 0;
}

void sf32lb52_bridge_runtime_init(void)
{
    bool loaded;
    bool mapping_loaded;

    memset(&g_runtime, 0, sizeof(g_runtime));
    loaded = sf32lb52_bridge_settings_load(&g_runtime.config);
    mapping_loaded = sf32lb52_bridge_mapping_store_load(
        &g_runtime.mapping_profiles);
    g_runtime.saved_mapping = g_runtime.mapping_profiles;
    if (!valid_role(g_runtime.config.output_role)) {
        sf32lb52_bridge_settings_defaults(&g_runtime.config);
        loaded = false;
    }
    g_runtime.status.output_role = g_runtime.config.output_role;
    g_runtime.status.input_preference = g_runtime.config.input_preference;
    g_runtime.status.active_input = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    g_runtime.status.last_persist_ok = loaded ? 1U : 0U;
    g_runtime.status.mapping_custom =
        sf32lb52_bridge_mapping_routes_is_identity(&g_runtime.mapping_profiles)
            ? 0U : 1U;
    g_runtime.status.mapping_last_persist_ok = mapping_loaded ? 1U : 0U;
    set_neutral_input();
}

void sf32lb52_bridge_runtime_set_callbacks(
    sf32lb52_bridge_role_change_callback_t role_change,
    sf32lb52_bridge_feedback_callback_t feedback,
    void *context)
{
    g_runtime.role_change_callback = role_change;
    g_runtime.feedback_callback = feedback;
    g_runtime.callback_context = context;
}

void sf32lb52_bridge_runtime_poll(uint32_t now_ms)
{
    g_runtime.last_poll_ms = now_ms;
    if (g_runtime.status.input_valid &&
        (uint32_t)(now_ms - g_runtime.status.last_input_ms) >
            SF32LB52_BRIDGE_INPUT_STALE_MS) {
        g_runtime.status.input_stale = 1U;
    }
    if (g_runtime.status.settings_dirty &&
        (uint32_t)(now_ms - g_runtime.settings_dirty_since_ms) >=
            SETTINGS_DEFER_MS) {
        if (!sf32lb52_bridge_runtime_save_settings()) {
            /* Back off after NVDS/FlashDB failure instead of retrying from the
             * 1 ms application loop and starving Bluetooth/USB work. */
            g_runtime.settings_dirty_since_ms = now_ms;
        }
    }
}

sf32lb52_bridge_role_t sf32lb52_bridge_runtime_role(void)
{
    return g_runtime.config.output_role;
}

bool sf32lb52_bridge_runtime_set_role(sf32lb52_bridge_role_t role,
                                      bool persist)
{
    sf32lb52_bridge_persisted_config_t next;
    sf32lb52_bridge_role_t old_role;

    if (!valid_role(role)) {
        return false;
    }
    if (role == g_runtime.config.output_role) {
        return !persist || sf32lb52_bridge_runtime_save_settings();
    }
    old_role = g_runtime.config.output_role;
    if (g_runtime.role_change_callback != 0 &&
        g_runtime.role_change_callback(role, g_runtime.callback_context) != 0) {
        return false;
    }

    next = g_runtime.config;
    next.output_role = role;
    if (persist && !sf32lb52_bridge_settings_save(&next)) {
        g_runtime.status.last_persist_ok = 0U;
        if (g_runtime.role_change_callback != 0 &&
            g_runtime.role_change_callback(old_role,
                                           g_runtime.callback_context) != 0) {
            /* USB could not roll back. Keep runtime aligned with the actual
             * requested persona and retry persistence later. */
            g_runtime.config = next;
            g_runtime.status.output_role = role;
            mark_settings_dirty();
        }
        return false;
    }

    g_runtime.config = next;
    g_runtime.status.output_role = role;
    g_runtime.status.role_switches++;
    g_runtime.report_sequence = 0U;
    if (persist) {
        g_runtime.status.last_persist_ok = 1U;
        g_runtime.status.settings_dirty = 0U;
        return true;
    }
    mark_settings_dirty();
    return true;
}

bool sf32lb52_bridge_runtime_sync_role(sf32lb52_bridge_role_t role,
                                       bool persist)
{
    sf32lb52_bridge_persisted_config_t next;
    bool changed;

    if (!valid_role(role)) {
        return false;
    }

    changed = role != g_runtime.config.output_role;
    g_runtime.config.output_role = role;
    g_runtime.status.output_role = role;
    if (changed) {
        g_runtime.report_sequence = 0U;
        set_neutral_input();
    }

    if (!persist) {
        mark_settings_dirty();
        return true;
    }

    next = g_runtime.config;
    if (!sf32lb52_bridge_settings_save(&next)) {
        /* The USB persona is authoritative here. Keep runtime aligned with it
         * and retry only the persistence operation from runtime_poll(). */
        g_runtime.status.last_persist_ok = 0U;
        mark_settings_dirty();
        return false;
    }

    g_runtime.config = next;
    g_runtime.status.last_persist_ok = 1U;
    g_runtime.status.settings_dirty = 0U;
    return true;
}

bool sf32lb52_bridge_runtime_cycle_role(bool persist)
{
    sf32lb52_bridge_role_t next;

    switch (g_runtime.config.output_role) {
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
        next = SF32LB52_BRIDGE_ROLE_DUALSENSE;
        break;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
        next = SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
        break;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        next = SF32LB52_BRIDGE_ROLE_NS2PRO;
        break;
    default:
        next = SF32LB52_BRIDGE_ROLE_XBOX_360;
        break;
    }
    return sf32lb52_bridge_runtime_set_role(next, persist);
}

bool sf32lb52_bridge_runtime_set_input_preference(
    sf32lb52_bridge_input_preference_t preference,
    bool persist)
{
    sf32lb52_bridge_persisted_config_t next;

    if (!valid_preference(preference)) {
        return false;
    }
    next = g_runtime.config;
    next.input_preference = preference;
    if (persist && !sf32lb52_bridge_settings_save(&next)) {
        g_runtime.status.last_persist_ok = 0U;
        return false;
    }

    g_runtime.config = next;
    g_runtime.status.input_preference = preference;
    if (g_runtime.status.active_input != SF32LB52_BRIDGE_INPUT_SOURCE_NONE &&
        !preference_accepts(preference, g_runtime.status.active_input)) {
        sf32lb52_bridge_runtime_release_input(g_runtime.status.active_input,
                                              g_runtime.last_poll_ms);
    }
    if (persist) {
        g_runtime.status.last_persist_ok = 1U;
        g_runtime.status.settings_dirty = 0U;
        return true;
    }
    mark_settings_dirty();
    return true;
}

bool sf32lb52_bridge_runtime_set_auto_connect(bool enabled, bool persist)
{
    sf32lb52_bridge_persisted_config_t next = g_runtime.config;

    next.auto_connect = enabled ? 1U : 0U;
    if (persist && !sf32lb52_bridge_settings_save(&next)) {
        g_runtime.status.last_persist_ok = 0U;
        return false;
    }

    g_runtime.config = next;
    if (persist) {
        g_runtime.status.last_persist_ok = 1U;
        g_runtime.status.settings_dirty = 0U;
        return true;
    }
    mark_settings_dirty();
    return true;
}

bool sf32lb52_bridge_runtime_accept_input(
    const sf32lb52_bridge_input_state_t *state,
    uint32_t now_ms)
{
    sf32lb52_bridge_input_source_t active;

    if (state == 0 || !state->valid ||
        state->source == SF32LB52_BRIDGE_INPUT_SOURCE_NONE ||
        !preference_accepts(g_runtime.config.input_preference, state->source)) {
        g_runtime.status.ignored_reports++;
        return false;
    }

    active = g_runtime.status.active_input;
    if (active != SF32LB52_BRIDGE_INPUT_SOURCE_NONE &&
        active != state->source &&
        (uint32_t)(now_ms - g_runtime.status.last_input_ms) <=
            SF32LB52_BRIDGE_INPUT_TAKEOVER_MS) {
        g_runtime.status.ignored_reports++;
        return false;
    }

    if (active != state->source) {
        g_runtime.status.active_input = state->source;
        g_runtime.status.active_since_ms = now_ms;
        g_runtime.status.source_switches++;
        g_runtime.config.last_active_input = state->source;
        mark_settings_dirty();
    }

    write_input(state);
    g_runtime.status.input_valid = 1U;
    g_runtime.status.input_stale = 0U;
    g_runtime.status.last_input_ms = now_ms;
    g_runtime.status.accepted_reports++;
    return true;
}

void sf32lb52_bridge_runtime_release_input(
    sf32lb52_bridge_input_source_t source,
    uint32_t now_ms)
{
    if (source != SF32LB52_BRIDGE_INPUT_SOURCE_NONE &&
        g_runtime.status.active_input != source) {
        return;
    }
    g_runtime.status.active_input = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
    g_runtime.status.input_valid = 0U;
    g_runtime.status.input_stale = 1U;
    g_runtime.status.last_input_ms = now_ms;
    set_neutral_input();
}

size_t sf32lb52_bridge_runtime_make_input_report(uint32_t now_ms,
                                                 uint8_t *report,
                                                 size_t report_capacity)
{
    sf32lb52_bridge_input_state_t state;
    sf32lb52_bridge_input_state_t mapped;
    sf32lb52_bridge_mapping_routes_t mapping_profiles;
    sf32lb52_bridge_mapping_config_t *mapping;
    sf32lb52_bridge_role_t role = actual_output_role();
    size_t required;

    /* The app labels its report cache with the requested role. Until USB
     * catches up, retain its queued neutral report instead of mislabelling
     * a report encoded for the old persona as the new one. */
    if (report == 0 || role != g_runtime.config.output_role) {
        return 0U;
    }
    required = sf32lb52_bridge_input_report_size(role);
    if (required == 0U || report_capacity < required) {
        return 0U;
    }

    if (!read_input(&state) || !g_runtime.status.input_valid ||
        (uint32_t)(now_ms - g_runtime.status.last_input_ms) >
            SF32LB52_BRIDGE_INPUT_STALE_MS) {
        sf32lb52_bridge_input_state_reset(&state);
        state.valid = 1U;
        state.source = SF32LB52_BRIDGE_INPUT_SOURCE_NONE;
        g_runtime.status.input_stale = 1U;
        g_runtime.status.neutral_reports++;
    }

    if (!read_mapping(&mapping_profiles)) {
        return 0U;
    }
    mapping = sf32lb52_bridge_mapping_route(
        &mapping_profiles, mapping_profile_for_source(state.source),
        sf32lb52_bridge_mapping_output_for_role(role));
    if (mapping == 0 ||
        !sf32lb52_bridge_mapping_apply(mapping, &state, &mapped) ||
        sf32lb52_bridge_encode_input(role,
                                     &mapped,
                                     g_runtime.report_sequence++,
                                     report,
                                     report_capacity) != 0) {
        return 0U;
    }
    return required;
}

int sf32lb52_bridge_runtime_on_usb_output(const uint8_t *report,
                                          size_t report_len)
{
    sf32lb52_bridge_feedback_t feedback;
    int result;

    if (report == 0 || report_len == 0U) {
        return -1;
    }
    switch (g_runtime.config.output_role) {
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
        result = sf32lb52_bridge_decode_xbox_output(report, report_len, &feedback);
        break;
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        result = sf32lb52_bridge_decode_ds5_output(report, report_len, &feedback);
        break;
    case SF32LB52_BRIDGE_ROLE_NS2PRO:
        result = sf32lb52_bridge_decode_ns2pro_output(report, report_len, &feedback);
        break;
    default:
        return -1;
    }

    g_runtime.status.usb_output_reports++;
    if (result != 0 || !feedback.valid ||
        g_runtime.status.active_input == SF32LB52_BRIDGE_INPUT_SOURCE_NONE ||
        g_runtime.feedback_callback == 0) {
        return result != 0 ? result : -1;
    }
    if (g_runtime.feedback_callback(g_runtime.status.active_input,
                                    &feedback,
                                    g_runtime.callback_context) != 0) {
        g_runtime.status.feedback_failed++;
        return -1;
    }
    g_runtime.status.feedback_forwarded++;
    return 0;
}

bool sf32lb52_bridge_runtime_remember_ds5(const uint8_t address[6])
{
    if (address == 0) {
        return false;
    }
    if (!g_runtime.config.ds5_address_valid ||
        memcmp(g_runtime.config.ds5_address, address, 6U) != 0) {
        memcpy(g_runtime.config.ds5_address, address, 6U);
        g_runtime.config.ds5_address_valid = 1U;
        mark_settings_dirty();
    }
    return true;
}

bool sf32lb52_bridge_runtime_remember_ns2(uint8_t address_type,
                                         const uint8_t address[6])
{
    if (address == 0) {
        return false;
    }
    if (!g_runtime.config.ns2_address_valid ||
        g_runtime.config.ns2_address_type != address_type ||
        memcmp(g_runtime.config.ns2_address, address, 6U) != 0) {
        memcpy(g_runtime.config.ns2_address, address, 6U);
        g_runtime.config.ns2_address_type = address_type;
        g_runtime.config.ns2_address_valid = 1U;
        mark_settings_dirty();
    }
    return true;
}

bool sf32lb52_bridge_runtime_forget_input(
    sf32lb52_bridge_input_source_t source,
    bool persist)
{
    sf32lb52_bridge_persisted_config_t next = g_runtime.config;

    if (source == SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT) {
        next.ds5_address_valid = 0U;
        memset(next.ds5_address, 0, 6U);
    } else if (source == SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE) {
        next.ns2_address_valid = 0U;
        next.ns2_address_type = 0U;
        memset(next.ns2_address, 0, 6U);
    } else {
        return false;
    }
    if (persist && !sf32lb52_bridge_settings_save(&next)) {
        g_runtime.status.last_persist_ok = 0U;
        return false;
    }

    g_runtime.config = next;
    sf32lb52_bridge_runtime_release_input(source, g_runtime.last_poll_ms);
    if (persist) {
        g_runtime.status.last_persist_ok = 1U;
        g_runtime.status.settings_dirty = 0U;
        return true;
    }
    mark_settings_dirty();
    return true;
}

bool sf32lb52_bridge_runtime_save_settings(void)
{
    sf32lb52_bridge_persisted_config_t next = g_runtime.config;
    bool ok = sf32lb52_bridge_settings_save(&next);

    g_runtime.status.last_persist_ok = ok ? 1U : 0U;
    if (ok) {
        g_runtime.config = next;
        g_runtime.status.settings_dirty = 0U;
    } else if (g_runtime.status.settings_dirty) {
        g_runtime.settings_dirty_since_ms = g_runtime.last_poll_ms;
    }
    return ok;
}

bool sf32lb52_bridge_runtime_reset_settings(void)
{
    sf32lb52_bridge_role_t old_role = g_runtime.config.output_role;
    sf32lb52_bridge_persisted_config_t next;

    sf32lb52_bridge_settings_defaults(&next);
    if (old_role != next.output_role &&
        g_runtime.role_change_callback != 0 &&
        g_runtime.role_change_callback(next.output_role,
                                       g_runtime.callback_context) != 0) {
        return false;
    }

    if (!sf32lb52_bridge_settings_reset(&next)) {
        g_runtime.status.last_persist_ok = 0U;
        if (old_role != next.output_role &&
            g_runtime.role_change_callback != 0) {
            if (g_runtime.role_change_callback(
                    old_role, g_runtime.callback_context) != 0) {
                /* The reset could not be persisted and USB could not return
                 * to the old persona. Preserve all old settings except the
                 * output role so runtime continues to describe the persona
                 * that is actually on the wire, then retry persistence. */
                g_runtime.config.output_role = next.output_role;
                g_runtime.status.output_role = next.output_role;
                g_runtime.report_sequence = 0U;
                mark_settings_dirty();
            }
        }
        return false;
    }
    g_runtime.config = next;
    g_runtime.status.output_role = g_runtime.config.output_role;
    g_runtime.status.input_preference = g_runtime.config.input_preference;
    if (old_role != g_runtime.config.output_role) {
        g_runtime.status.role_switches++;
    }
    g_runtime.report_sequence = 0U;
    g_runtime.status.settings_dirty = 0U;
    g_runtime.status.last_persist_ok = 1U;
    sf32lb52_bridge_runtime_release_input(SF32LB52_BRIDGE_INPUT_SOURCE_NONE,
                                          g_runtime.last_poll_ms);
    return true;
}

void sf32lb52_bridge_runtime_get_config(
    sf32lb52_bridge_persisted_config_t *config)
{
    if (config != 0) {
        *config = g_runtime.config;
    }
}

void sf32lb52_bridge_runtime_get_status(
    sf32lb52_bridge_runtime_status_t *status)
{
    if (status != 0) {
        *status = g_runtime.status;
    }
}

bool sf32lb52_bridge_runtime_get_input_state(
    sf32lb52_bridge_input_state_t *state)
{
    if (state == 0) {
        return false;
    }
    return read_input(state);
}

sf32lb52_bridge_mapping_profile_t
sf32lb52_bridge_runtime_active_mapping_profile(void)
{
    sf32lb52_bridge_input_state_t input;

    /* A USB sync temporarily publishes neutral input; preserve the legacy
     * command source selection until the next physical snapshot arrives. */
    return read_input(&input) && input.source != SF32LB52_BRIDGE_INPUT_SOURCE_NONE
        ? mapping_profile_for_source(input.source)
        : mapping_profile_for_source(g_runtime.status.active_input);
}

sf32lb52_bridge_mapping_output_t
sf32lb52_bridge_runtime_active_mapping_output(void)
{
    return sf32lb52_bridge_mapping_output_for_role(actual_output_role());
}

bool sf32lb52_bridge_runtime_set_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    sf32lb52_bridge_button_t target,
    uint8_t source)
{
    sf32lb52_bridge_mapping_routes_t profiles;
    sf32lb52_bridge_mapping_config_t *mapping;

    if (!read_mapping(&profiles) ||
        (mapping = sf32lb52_bridge_mapping_route(
            &profiles, profile, output)) == 0 ||
        !sf32lb52_bridge_mapping_set(mapping, target, source)) {
        return false;
    }
    write_mapping(&profiles);
    return true;
}

bool sf32lb52_bridge_runtime_reset_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output)
{
    sf32lb52_bridge_mapping_routes_t profiles;
    sf32lb52_bridge_mapping_config_t *mapping;

    if (!read_mapping(&profiles) ||
        (mapping = sf32lb52_bridge_mapping_route(
            &profiles, profile, output)) == 0) {
        return false;
    }
    sf32lb52_bridge_mapping_defaults(mapping);
    write_mapping(&profiles);
    return true;
}

bool sf32lb52_bridge_runtime_save_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile)
{
    sf32lb52_bridge_mapping_routes_t profiles;

    if (profile != SF32LB52_BRIDGE_MAPPING_PROFILE_DS5 &&
        profile != SF32LB52_BRIDGE_MAPPING_PROFILE_NS2PRO) {
        return false;
    }
    if (!read_mapping(&profiles) ||
        !sf32lb52_bridge_mapping_store_save(&profiles)) {
        g_runtime.status.mapping_last_persist_ok = 0U;
        return false;
    }
    g_runtime.saved_mapping = profiles;
    g_runtime.status.mapping_dirty = 0U;
    g_runtime.status.mapping_last_persist_ok = 1U;
    return true;
}

bool sf32lb52_bridge_runtime_save_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output)
{
    sf32lb52_bridge_mapping_routes_t live;
    sf32lb52_bridge_mapping_routes_t saved = g_runtime.saved_mapping;
    sf32lb52_bridge_mapping_config_t *from;
    sf32lb52_bridge_mapping_config_t *to;

    if (!read_mapping(&live) ||
        (from = sf32lb52_bridge_mapping_route(&live, profile, output)) == 0 ||
        (to = sf32lb52_bridge_mapping_route(&saved, profile, output)) == 0) {
        return false;
    }
    *to = *from;
    if (!sf32lb52_bridge_mapping_store_save(&saved)) {
        g_runtime.status.mapping_last_persist_ok = 0U;
        return false;
    }
    g_runtime.saved_mapping = saved;
    g_runtime.status.mapping_dirty =
        memcmp(&live, &saved, sizeof(live)) != 0 ? 1U : 0U;
    g_runtime.status.mapping_last_persist_ok = 1U;
    return true;
}

bool sf32lb52_bridge_runtime_get_route_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    sf32lb52_bridge_mapping_config_t *config)
{
    sf32lb52_bridge_mapping_routes_t profiles;
    sf32lb52_bridge_mapping_config_t *mapping;

    if (!read_mapping(&profiles) ||
        (mapping = sf32lb52_bridge_mapping_route(
            &profiles, profile, output)) == 0 ||
        config == 0) {
        return false;
    }
    *config = *mapping;
    return true;
}

bool sf32lb52_bridge_runtime_set_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_button_t target, uint8_t source)
{
    return sf32lb52_bridge_runtime_set_route_button_mapping(
        profile, sf32lb52_bridge_runtime_active_mapping_output(), target, source);
}

bool sf32lb52_bridge_runtime_get_route_mapping_dirty(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_output_t output,
    bool *dirty)
{
    sf32lb52_bridge_mapping_config_t live;
    sf32lb52_bridge_mapping_config_t *saved;

    if (dirty == 0 ||
        !sf32lb52_bridge_runtime_get_route_button_mapping(profile, output, &live) ||
        (saved = sf32lb52_bridge_mapping_route(
            &g_runtime.saved_mapping, profile, output)) == 0) {
        return false;
    }
    *dirty = memcmp(&live, saved, sizeof(live)) != 0;
    return true;
}

bool sf32lb52_bridge_runtime_reset_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile)
{
    return sf32lb52_bridge_runtime_reset_route_button_mapping(
        profile, sf32lb52_bridge_runtime_active_mapping_output());
}

bool sf32lb52_bridge_runtime_get_profile_button_mapping(
    sf32lb52_bridge_mapping_profile_t profile,
    sf32lb52_bridge_mapping_config_t *config)
{
    return sf32lb52_bridge_runtime_get_route_button_mapping(
        profile, sf32lb52_bridge_runtime_active_mapping_output(), config);
}

bool sf32lb52_bridge_runtime_set_button_mapping(
    sf32lb52_bridge_button_t target,
    uint8_t source)
{
    return sf32lb52_bridge_runtime_set_profile_button_mapping(
        sf32lb52_bridge_runtime_active_mapping_profile(), target, source);
}

bool sf32lb52_bridge_runtime_reset_button_mapping(void)
{
    return sf32lb52_bridge_runtime_reset_profile_button_mapping(
        sf32lb52_bridge_runtime_active_mapping_profile());
}

bool sf32lb52_bridge_runtime_save_button_mapping(void)
{
    return sf32lb52_bridge_runtime_save_profile_button_mapping(
        sf32lb52_bridge_runtime_active_mapping_profile());
}

bool sf32lb52_bridge_runtime_get_button_mapping(
    sf32lb52_bridge_mapping_config_t *config)
{
    return sf32lb52_bridge_runtime_get_profile_button_mapping(
        sf32lb52_bridge_runtime_active_mapping_profile(), config);
}

bool sf32lb52_bridge_runtime_mapping_is_identity(void)
{
    sf32lb52_bridge_mapping_routes_t profiles;

    return read_mapping(&profiles) &&
           sf32lb52_bridge_mapping_routes_is_identity(&profiles);
}

bool sf32lb52_bridge_runtime_patch_native_input_report(
    uint32_t now_ms,
    uint8_t *report,
    size_t report_len)
{
    sf32lb52_bridge_input_state_t input;
    sf32lb52_bridge_mapping_routes_t profiles;
    sf32lb52_bridge_mapping_config_t *mapping;
    sf32lb52_bridge_role_t role = actual_output_role();

    if (report == 0 || role != g_runtime.config.output_role ||
        !read_mapping(&profiles) || !read_input(&input)) {
        return false;
    }
    mapping = sf32lb52_bridge_mapping_route(
        &profiles, mapping_profile_for_source(input.source),
        sf32lb52_bridge_mapping_output_for_role(role));
    if (mapping == 0) {
        return false;
    }
    if (!input.valid || input.source == SF32LB52_BRIDGE_INPUT_SOURCE_NONE ||
        !g_runtime.status.input_valid ||
        (uint32_t)(now_ms - g_runtime.status.last_input_ms) >
            SF32LB52_BRIDGE_INPUT_STALE_MS) {
        return false;
    }
    return sf32lb52_bridge_mapping_patch_native_report(
        mapping, role, &input, report, report_len);
}

const char *sf32lb52_bridge_role_name(sf32lb52_bridge_role_t role)
{
    switch (role) {
    case SF32LB52_BRIDGE_ROLE_XBOX_360:
        return "xbox";
    case SF32LB52_BRIDGE_ROLE_DUALSENSE:
        return "ds5";
    case SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE:
        return "dse";
    case SF32LB52_BRIDGE_ROLE_NS2PRO:
        return "ns2pro";
    default:
        return "unknown";
    }
}

const char *sf32lb52_bridge_source_name(sf32lb52_bridge_input_source_t source)
{
    switch (source) {
    case SF32LB52_BRIDGE_INPUT_SOURCE_DUALSENSE_BT:
        return "ds5";
    case SF32LB52_BRIDGE_INPUT_SOURCE_NS2PRO_BLE:
        return "ns2pro";
    default:
        return "none";
    }
}

const char *sf32lb52_bridge_preference_name(
    sf32lb52_bridge_input_preference_t preference)
{
    switch (preference) {
    case SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE:
        return "ds5";
    case SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO:
        return "ns2pro";
    default:
        return "auto";
    }
}

bool sf32lb52_bridge_parse_role(const char *text,
                                sf32lb52_bridge_role_t *role)
{
    if (role == 0) {
        return false;
    }
    if (text_equal_ci(text, "xbox") || text_equal_ci(text, "xbox360") ||
        text_equal_ci(text, "xinput")) {
        *role = SF32LB52_BRIDGE_ROLE_XBOX_360;
    } else if (text_equal_ci(text, "ds5") || text_equal_ci(text, "dualsense")) {
        *role = SF32LB52_BRIDGE_ROLE_DUALSENSE;
    } else if (text_equal_ci(text, "dse") ||
               text_equal_ci(text, "dualsense-edge") ||
               text_equal_ci(text, "edge")) {
        *role = SF32LB52_BRIDGE_ROLE_DUALSENSE_EDGE;
    } else if (text_equal_ci(text, "ns2") || text_equal_ci(text, "ns2pro") ||
               text_equal_ci(text, "nintendo")) {
        *role = SF32LB52_BRIDGE_ROLE_NS2PRO;
    } else {
        return false;
    }
    return true;
}

bool sf32lb52_bridge_parse_input_preference(
    const char *text,
    sf32lb52_bridge_input_preference_t *preference)
{
    if (preference == 0) {
        return false;
    }
    if (text_equal_ci(text, "auto")) {
        *preference = SF32LB52_BRIDGE_INPUT_PREFERENCE_AUTO;
    } else if (text_equal_ci(text, "ds5") || text_equal_ci(text, "dualsense")) {
        *preference = SF32LB52_BRIDGE_INPUT_PREFERENCE_DUALSENSE;
    } else if (text_equal_ci(text, "ns2") || text_equal_ci(text, "ns2pro")) {
        *preference = SF32LB52_BRIDGE_INPUT_PREFERENCE_NS2PRO;
    } else {
        return false;
    }
    return true;
}
