#include "ds5_classic.h"
#include "sf32lb52_ds5_feature.h"
#include "sf32lb52_ds5_output_state.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("rtconfig.h")
#include "rtconfig.h"
#endif
#endif

#if defined(BLUETOOTH) && defined(BT_FINSH) && defined(CFG_BT_L2CAP_PROFILE)
#define DS5_CLASSIC_SIFLI_AVAILABLE 1

#include <rtthread.h>

#include "bf0_ble_common.h"
#include "bf0_sibles.h"
#include "bts2_global.h"
#include "bts2_msg.h"
#include "bts2_task.h"
#include "bt_l2cap_profile_api.h"
#include "gap_api.h"
#include "hci_api.h"
#include "sc_api.h"

#if defined(BSP_BT_CONNECTION_MANAGER)
#include "bt_connection_manager.h"
#endif

#define DS5_CLASSIC_DISCOVERY_SECONDS 15U
#define DS5_CLASSIC_L2CAP_FLUSH_TIMEOUT 0x0041U
#define DS5_CLASSIC_INPUT_CRC_SEED 0x73d37cf3UL
#define DS5_CLASSIC_OUTPUT_CRC_SEED 0xeada2d49UL
#define DS5_CLASSIC_RECONNECT_INITIAL_MS 500U
#define DS5_CLASSIC_RECONNECT_MAX_MS 8000U
#define DS5_CLASSIC_REG_RETRY_INITIAL_MS 500U
#define DS5_CLASSIC_REG_RETRY_MAX_MS 4000U
#define DS5_CLASSIC_CONTROL_OP_NONE 0U
#define DS5_CLASSIC_CONTROL_OP_GET_FEATURE 1U
#define DS5_CLASSIC_CONTROL_OP_SET_FEATURE 2U
#define DS5_CLASSIC_DSE_UNLOCK_IDLE 0U
#define DS5_CLASSIC_DSE_UNLOCK_SEND_FIRMWARE 1U
#define DS5_CLASSIC_DSE_UNLOCK_SEND_COMMAND 2U
#define DS5_CLASSIC_DSE_UNLOCK_WAIT 3U
#define DS5_CLASSIC_DSE_UNLOCK_PREFETCH 4U
#define DS5_CLASSIC_DSE_UNLOCK_READY 5U
#define DS5_CLASSIC_DSE_UNLOCK_WAIT_MS 4000U
#define DS5_CLASSIC_DSE_PREFETCH_INTERVAL_MS 80U
#define DS5_CLASSIC_FEATURE_RESPONSE_TIMEOUT_MS 750U
#define DS5_CLASSIC_FEATURE_REQUEST_MAX_RETRIES 2U

typedef struct ds5_classic_context {
    ds5_classic_status_t status;
    ds5_classic_input_callback_t input_callback;
    void *callback_context;
    ds5_classic_audio_input_callback_t audio_input_callback;
    void *audio_callback_context;
    BTS2S_BD_ADDR active_bd;
    BTS2S_BD_ADDR saved_bd;
    BTS2S_BD_ADDR candidate_bd;
    uint8_t candidate_valid;
    uint8_t security_mode_requested;
    uint8_t security_mode_ready;
    uint8_t sync_registration_requested;
    uint8_t registration_requested;
    uint8_t control_registration_in_flight;
    uint8_t interrupt_registration_in_flight;
    uint8_t registration_retry_pending;
    uint8_t registration_retry_shift;
    uint8_t saved_read_requested;
    uint8_t saved_read_in_flight;
    uint8_t connect_saved_waiting;
    uint8_t want_pairing;
    uint8_t start_discovery_pending;
    uint8_t stop_discovery_pending;
    uint8_t pair_request_pending;
    uint8_t acl_pairing_flow;
    uint8_t acl_interest_held;
    uint8_t auth_request_pending;
    uint8_t encryption_request_pending;
    uint8_t control_connect_pending;
    uint8_t interrupt_connect_pending;
    uint8_t control_connect_requested;
    uint8_t interrupt_connect_requested;
    uint8_t primer_output_pending;
    uint8_t neutral_output_pending;
    uint8_t output_busy;
    uint8_t control_busy;
    uint8_t control_operation;
    uint8_t feature_request_pending;
    uint8_t awaiting_feature_response;
    uint8_t feature_request_id;
    uint8_t feature_request_retries;
    uint8_t feature_prefetch_index;
    uint8_t dse_profile_next;
    uint8_t pending_output_valid;
    uint8_t pending_audio_valid;
    uint8_t output_next_audio;
    uint8_t injected_saved_valid;
    uint8_t reconnect_suppressed;
    uint8_t reconnect_backoff_shift;
    uint8_t output_sequence;
    rt_tick_t reconnect_due_tick;
    rt_tick_t registration_retry_due_tick;
    rt_tick_t dse_unlock_due_tick;
    rt_tick_t dse_profile_due_tick;
    rt_tick_t feature_response_due_tick;
    uint16_t pending_output_len;
    uint16_t pending_audio_len;
    uint16_t output_packet_len;
    uint16_t control_packet_len;
    sf32lb52_ds5_feature_cache_t feature_cache;
    sf32lb52_ds5_output_state_t output_state;
    uint8_t pending_output_report[DS5_CLASSIC_AUDIO_REPORT_SIZE];
    uint8_t pending_audio_report[DS5_CLASSIC_AUDIO_REPORT_SIZE];
    uint8_t output_packet[DS5_CLASSIC_BT_MAX_OUTPUT_PACKET_SIZE];
    uint8_t control_packet[SF32LB52_DS5_FEATURE_CONTROL_PACKET_MAX];
} ds5_classic_context_t;

static ds5_classic_context_t g_ds5;
static const uint8_t ds5_feature_prefetch_ids[] = {
    0x09U, 0x20U, 0x22U, 0x05U, 0x70U,
};

static void ds5_classic_set_error(int error, uint16_t sdk_result)
{
    g_ds5.status.last_error = (int16_t)error;
    g_ds5.status.last_sdk_result = sdk_result;
    if (error != DS5_CLASSIC_OK) {
        g_ds5.status.state = DS5_CLASSIC_STATE_ERROR;
    }
}

static void ds5_classic_clear_error(void)
{
    g_ds5.status.last_error = DS5_CLASSIC_OK;
    g_ds5.status.last_sdk_result = 0U;
}

static void ds5_classic_schedule_registration_retry(void)
{
    uint32_t delay_ms = DS5_CLASSIC_REG_RETRY_INITIAL_MS;

    delay_ms <<= g_ds5.registration_retry_shift;
    if (delay_ms >= DS5_CLASSIC_REG_RETRY_MAX_MS) {
        delay_ms = DS5_CLASSIC_REG_RETRY_MAX_MS;
    } else {
        g_ds5.registration_retry_shift++;
    }
    g_ds5.registration_retry_due_tick =
        rt_tick_get() + rt_tick_from_millisecond(delay_ms);
    g_ds5.registration_retry_pending = 1U;
}

static void ds5_classic_refresh_state(void)
{
    if (!g_ds5.status.sdk_available) {
        g_ds5.status.state = DS5_CLASSIC_STATE_UNAVAILABLE;
    } else if (g_ds5.status.connected) {
        g_ds5.status.state = DS5_CLASSIC_STATE_CONNECTED;
    } else if (g_ds5.status.disconnecting) {
        g_ds5.status.state = DS5_CLASSIC_STATE_DISCONNECTING;
    } else if (g_ds5.status.discovering || g_ds5.status.pairing ||
               g_ds5.want_pairing) {
        g_ds5.status.state = DS5_CLASSIC_STATE_PAIRING;
    } else if (g_ds5.status.connecting ||
               g_ds5.status.reconnect_pending ||
               g_ds5.control_connect_pending ||
               g_ds5.interrupt_connect_pending) {
        g_ds5.status.state = DS5_CLASSIC_STATE_CONNECTING;
    } else if (!g_ds5.status.stack_ready ||
               !g_ds5.status.control_registered ||
               !g_ds5.status.interrupt_registered) {
        g_ds5.status.state = DS5_CLASSIC_STATE_STARTING;
    } else if (g_ds5.status.last_error == DS5_CLASSIC_OK) {
        g_ds5.status.state = DS5_CLASSIC_STATE_IDLE;
    } else {
        g_ds5.status.state = DS5_CLASSIC_STATE_ERROR;
    }
}

static int ds5_classic_bd_equal(const BTS2S_BD_ADDR *a,
                                const BTS2S_BD_ADDR *b)
{
    return a != 0 && b != 0 &&
           a->lap == b->lap && a->uap == b->uap && a->nap == b->nap;
}

static void ds5_classic_bd_to_bytes(const BTS2S_BD_ADDR *bd, uint8_t out[6])
{
    if (bd == 0 || out == 0) {
        return;
    }
    out[0] = (uint8_t)(bd->lap & 0xffU);
    out[1] = (uint8_t)((bd->lap >> 8) & 0xffU);
    out[2] = (uint8_t)((bd->lap >> 16) & 0xffU);
    out[3] = (uint8_t)(bd->uap & 0xffU);
    out[4] = (uint8_t)(bd->nap & 0xffU);
    out[5] = (uint8_t)((bd->nap >> 8) & 0xffU);
}

static void ds5_classic_bd_from_bytes(const uint8_t address[6],
                                      BTS2S_BD_ADDR *bd)
{
    if (address == 0 || bd == 0) {
        return;
    }
    bd->lap = (uint32_t)address[0] |
              ((uint32_t)address[1] << 8) |
              ((uint32_t)address[2] << 16);
    bd->uap = address[3];
    bd->nap = (uint16_t)address[4] | ((uint16_t)address[5] << 8);
}

static void ds5_classic_set_active_bd(const BTS2S_BD_ADDR *bd)
{
    if (bd == 0) {
        return;
    }
    g_ds5.active_bd = *bd;
    g_ds5.status.active_address_valid = 1U;
    ds5_classic_bd_to_bytes(bd, g_ds5.status.active_address);
}

static void ds5_classic_set_saved_bd(const BTS2S_BD_ADDR *bd)
{
    if (bd == 0) {
        return;
    }
    g_ds5.saved_bd = *bd;
    g_ds5.status.saved_address_valid = 1U;
    ds5_classic_bd_to_bytes(bd, g_ds5.status.saved_address);
}

static uint32_t ds5_classic_crc32_seeded(const uint8_t *data,
                                         size_t len,
                                         uint32_t seed)
{
    uint32_t crc = ~seed;
    size_t i;
    unsigned int bit;

    for (i = 0U; i < len; i++) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc >> 1) ^ (0xedb88320UL &
                                (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return ~crc;
}

static uint32_t ds5_classic_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ds5_classic_write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

/* Caller holds the RT-Thread critical section and has verified the link. */
static void ds5_classic_prepare_raw_output_locked(const uint8_t *report,
                                                   uint16_t len)
{
    uint32_t crc;

    g_ds5.output_busy = 1U;
    memset(g_ds5.output_packet, 0, sizeof(g_ds5.output_packet));
    g_ds5.output_packet[0] = 0xa2U;
    memcpy(g_ds5.output_packet + 1U, report, len);
    g_ds5.output_packet_len = (uint16_t)(len + 1U);
    crc = ds5_classic_crc32_seeded(g_ds5.output_packet + 1U,
                                   len - 4U,
                                   DS5_CLASSIC_OUTPUT_CRC_SEED);
    ds5_classic_write_u32_le(
        g_ds5.output_packet + g_ds5.output_packet_len - 4U,
        crc);
    g_ds5.status.output_reports++;
    if (report[0] == 0x36U || report[0] == 0x39U) {
        g_ds5.status.audio_output_reports++;
    }
}

static void ds5_classic_try_send_pending_output(void)
{
    uint16_t cid;

    rt_enter_critical();
    if ((!g_ds5.pending_output_valid && !g_ds5.pending_audio_valid) ||
        g_ds5.output_busy ||
        !g_ds5.status.connected || g_ds5.status.interrupt_cid == 0U) {
        rt_exit_critical();
        return;
    }
    if (g_ds5.pending_output_valid && g_ds5.pending_audio_valid) {
        if (g_ds5.output_next_audio) {
            ds5_classic_prepare_raw_output_locked(g_ds5.pending_audio_report,
                                                   g_ds5.pending_audio_len);
            g_ds5.pending_audio_valid = 0U;
            g_ds5.output_next_audio = 0U;
        } else {
            ds5_classic_prepare_raw_output_locked(g_ds5.pending_output_report,
                                                   g_ds5.pending_output_len);
            g_ds5.pending_output_valid = 0U;
            g_ds5.output_next_audio = 1U;
        }
    } else if (g_ds5.pending_output_valid) {
        ds5_classic_prepare_raw_output_locked(g_ds5.pending_output_report,
                                               g_ds5.pending_output_len);
        g_ds5.pending_output_valid = 0U;
        g_ds5.output_next_audio = 1U;
    } else {
        ds5_classic_prepare_raw_output_locked(g_ds5.pending_audio_report,
                                               g_ds5.pending_audio_len);
        g_ds5.pending_audio_valid = 0U;
        g_ds5.output_next_audio = 0U;
    }
    g_ds5.status.output_pending =
        g_ds5.pending_output_valid || g_ds5.pending_audio_valid;
    cid = g_ds5.status.interrupt_cid;
    rt_exit_critical();

    bt_l2cap_profile_send_data_req(cid,
                                   (char *)g_ds5.output_packet,
                                   g_ds5.output_packet_len);
}

static void ds5_classic_schedule_feature_request(uint8_t report_id)
{
    if (!g_ds5.status.connected || g_ds5.status.control_cid == 0U ||
        g_ds5.feature_request_pending || g_ds5.awaiting_feature_response) {
        return;
    }
    if (g_ds5.feature_request_id != report_id) {
        g_ds5.feature_request_retries = 0U;
    }
    g_ds5.feature_request_id = report_id;
    g_ds5.feature_request_pending = 1U;
    g_ds5.status.feature_request_pending = 1U;
}

/* Caller holds the RT-Thread critical section. */
static void ds5_classic_recover_feature_request_locked(int timed_out)
{
    uint8_t report_id = g_ds5.feature_request_id;

    g_ds5.awaiting_feature_response = 0U;
    g_ds5.status.feature_response_pending = 0U;
    if (g_ds5.control_operation == DS5_CLASSIC_CONTROL_OP_GET_FEATURE) {
        g_ds5.control_busy = 0U;
        g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_NONE;
    }
    g_ds5.status.feature_failures++;
    if (timed_out) {
        g_ds5.status.feature_timeouts++;
    }

    if (g_ds5.feature_request_retries <
        DS5_CLASSIC_FEATURE_REQUEST_MAX_RETRIES) {
        g_ds5.feature_request_retries++;
        g_ds5.status.feature_retries++;
        ds5_classic_schedule_feature_request(report_id);
        return;
    }

    g_ds5.feature_request_retries = 0U;
    if (g_ds5.feature_prefetch_index <
            sizeof(ds5_feature_prefetch_ids) &&
        report_id == ds5_feature_prefetch_ids[g_ds5.feature_prefetch_index]) {
        g_ds5.feature_prefetch_index++;
        if (g_ds5.feature_prefetch_index <
            sizeof(ds5_feature_prefetch_ids)) {
            ds5_classic_schedule_feature_request(
                ds5_feature_prefetch_ids[g_ds5.feature_prefetch_index]);
        }
    } else if (g_ds5.status.dse_unlock_phase ==
                   DS5_CLASSIC_DSE_UNLOCK_PREFETCH &&
               report_id == g_ds5.dse_profile_next) {
        g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_IDLE;
        g_ds5.status.dse_profiles_ready = 0U;
    }
}

static void ds5_classic_poll_feature_timeout(void)
{
    rt_enter_critical();
    if (g_ds5.awaiting_feature_response &&
        (int32_t)(rt_tick_get() - g_ds5.feature_response_due_tick) >= 0) {
        ds5_classic_recover_feature_request_locked(1);
    }
    rt_exit_critical();
}

static void ds5_classic_poll_dse_unlock(void)
{
    uint16_t cid = 0U;
    uint16_t packet_len = 0U;
    uint8_t feature[SF32LB52_DS5_FEATURE_REPORT_MAX];
    size_t feature_len;

    rt_enter_critical();
    if (!g_ds5.status.connected || g_ds5.status.control_cid == 0U ||
        g_ds5.control_busy || g_ds5.awaiting_feature_response) {
        rt_exit_critical();
        return;
    }
    switch (g_ds5.status.dse_unlock_phase) {
    case DS5_CLASSIC_DSE_UNLOCK_SEND_FIRMWARE:
        feature_len = sf32lb52_ds5_feature_cache_get(
            &g_ds5.feature_cache, 0x20U, feature, sizeof(feature));
        if (feature_len < 62U) {
            g_ds5.status.feature_failures++;
            g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_IDLE;
            break;
        }
        g_ds5.control_packet[0] = 0x53U;
        g_ds5.control_packet[1] = 0x65U;
        memcpy(g_ds5.control_packet + 2U, feature + 1U, 61U);
        packet_len = 63U;
        g_ds5.status.dse_unlock_phase =
            DS5_CLASSIC_DSE_UNLOCK_SEND_COMMAND;
        break;
    case DS5_CLASSIC_DSE_UNLOCK_SEND_COMMAND:
    {
        uint8_t unlock[59] = {0};

        unlock[0] = 0x70U;
        unlock[1] = 0x01U;
        packet_len = (uint16_t)sf32lb52_ds5_feature_build_set_request(
            0x80U, unlock, sizeof(unlock), g_ds5.control_packet,
            sizeof(g_ds5.control_packet));
        if (packet_len == 0U) {
            g_ds5.status.feature_failures++;
            g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_IDLE;
            break;
        }
        g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_WAIT;
        g_ds5.dse_unlock_due_tick = rt_tick_get() +
            rt_tick_from_millisecond(DS5_CLASSIC_DSE_UNLOCK_WAIT_MS);
        g_ds5.status.dse_unlocks++;
        break;
    }
    case DS5_CLASSIC_DSE_UNLOCK_WAIT:
        if ((int32_t)(rt_tick_get() - g_ds5.dse_unlock_due_tick) >= 0) {
            g_ds5.status.dse_unlock_phase =
                DS5_CLASSIC_DSE_UNLOCK_PREFETCH;
            g_ds5.dse_profile_next = 0x70U;
            g_ds5.dse_profile_due_tick = rt_tick_get();
        }
        break;
    case DS5_CLASSIC_DSE_UNLOCK_PREFETCH:
        if ((int32_t)(rt_tick_get() - g_ds5.dse_profile_due_tick) >= 0 &&
            !g_ds5.feature_request_pending) {
            ds5_classic_schedule_feature_request(g_ds5.dse_profile_next);
        }
        break;
    default:
        break;
    }
    if (packet_len != 0U) {
        g_ds5.control_packet_len = packet_len;
        g_ds5.control_busy = 1U;
        g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_SET_FEATURE;
        g_ds5.status.feature_set_reports++;
        cid = g_ds5.status.control_cid;
    }
    rt_exit_critical();

    if (cid != 0U) {
        bt_l2cap_profile_send_data_req(cid,
                                       (char *)g_ds5.control_packet,
                                       packet_len);
    }
}

static void ds5_classic_try_send_feature_request(void)
{
    uint16_t cid;
    size_t packet_len;

    rt_enter_critical();
    if (!g_ds5.feature_request_pending || g_ds5.control_busy ||
        !g_ds5.status.connected || g_ds5.status.control_cid == 0U) {
        rt_exit_critical();
        return;
    }
    packet_len = sf32lb52_ds5_feature_build_get_request(
        g_ds5.feature_request_id,
        g_ds5.control_packet,
        sizeof(g_ds5.control_packet));
    if (packet_len == 0U) {
        g_ds5.feature_request_pending = 0U;
        g_ds5.status.feature_request_pending = 0U;
        g_ds5.status.feature_failures++;
        rt_exit_critical();
        return;
    }
    g_ds5.control_packet_len = (uint16_t)packet_len;
    g_ds5.control_busy = 1U;
    g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_GET_FEATURE;
    g_ds5.feature_request_pending = 0U;
    g_ds5.awaiting_feature_response = 1U;
    g_ds5.feature_response_due_tick = rt_tick_get() +
        rt_tick_from_millisecond(DS5_CLASSIC_FEATURE_RESPONSE_TIMEOUT_MS);
    g_ds5.status.feature_request_pending = 0U;
    g_ds5.status.feature_response_pending = 1U;
    g_ds5.status.feature_requests++;
    cid = g_ds5.status.control_cid;
    rt_exit_critical();

    bt_l2cap_profile_send_data_req(cid,
                                   (char *)g_ds5.control_packet,
                                   g_ds5.control_packet_len);
}

static int ds5_classic_name_contains(const uint8_t *name, const char *needle)
{
    char safe_name[sizeof(BTS2S_DEV_NAME) + 1U];

    if (name == 0 || needle == 0) {
        return 0;
    }
    memcpy(safe_name, name, sizeof(BTS2S_DEV_NAME));
    safe_name[sizeof(BTS2S_DEV_NAME)] = '\0';
    return strstr(safe_name, needle) != 0;
}

static int ds5_classic_is_candidate(const BTS2S_GAP_DISCOV_RES_IND *result)
{
    uint32_t cod;
    int gamepad_cod;

    if (result == 0) {
        return 0;
    }
    cod = result->dev_cls;
    gamepad_cod = ((cod & 0x000f00UL) == 0x000500UL) &&
                  ((cod & 0x0000fcUL) == 0x000008UL);
    return ds5_classic_name_contains(result->dev_disp_name, "DualSense") ||
           ds5_classic_name_contains(result->dev_disp_name,
                                     "Wireless Controller") ||
           gamepad_cod;
}

static void ds5_classic_clear_link(void)
{
    g_ds5.status.control_cid = 0U;
    g_ds5.status.interrupt_cid = 0U;
    g_ds5.status.control_remote_mtu = 0U;
    g_ds5.status.interrupt_remote_mtu = 0U;
    g_ds5.status.connected = 0U;
    g_ds5.status.connecting = 0U;
    g_ds5.status.disconnecting = 0U;
    g_ds5.control_connect_pending = 0U;
    g_ds5.interrupt_connect_pending = 0U;
    g_ds5.control_connect_requested = 0U;
    g_ds5.interrupt_connect_requested = 0U;
    g_ds5.neutral_output_pending = 0U;
    g_ds5.primer_output_pending = 0U;
    g_ds5.output_busy = 0U;
    g_ds5.control_busy = 0U;
    g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_NONE;
    g_ds5.feature_request_pending = 0U;
    g_ds5.awaiting_feature_response = 0U;
    g_ds5.feature_request_retries = 0U;
    g_ds5.feature_prefetch_index = 0U;
    g_ds5.status.feature_request_pending = 0U;
    g_ds5.status.feature_response_pending = 0U;
    g_ds5.status.edge_known = 0U;
    g_ds5.status.is_edge = 0U;
    g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_IDLE;
    g_ds5.status.dse_profiles_ready = 0U;
    g_ds5.dse_profile_next = 0U;
    sf32lb52_ds5_feature_cache_reset(&g_ds5.feature_cache);
    g_ds5.pending_output_valid = 0U;
    g_ds5.pending_audio_valid = 0U;
    g_ds5.status.output_pending = 0U;
    g_ds5.status.reconnect_pending = 0U;
    g_ds5.status.active_address_valid = 0U;
    memset(g_ds5.status.active_address, 0,
           sizeof(g_ds5.status.active_address));
}

static int ds5_classic_reconnect_allowed(void)
{
    return g_ds5.status.auto_reconnect_enabled &&
           !g_ds5.reconnect_suppressed &&
           g_ds5.status.saved_address_valid;
}

static void ds5_classic_schedule_reconnect(uint32_t delay_ms)
{
    if (!ds5_classic_reconnect_allowed()) {
        g_ds5.status.reconnect_pending = 0U;
        return;
    }
    g_ds5.reconnect_due_tick = rt_tick_get() +
                               rt_tick_from_millisecond(delay_ms);
    g_ds5.status.reconnect_pending = 1U;
}

static void ds5_classic_schedule_reconnect_backoff(void)
{
    uint32_t delay_ms = DS5_CLASSIC_RECONNECT_INITIAL_MS;

    delay_ms <<= g_ds5.reconnect_backoff_shift;
    if (delay_ms >= DS5_CLASSIC_RECONNECT_MAX_MS) {
        delay_ms = DS5_CLASSIC_RECONNECT_MAX_MS;
    } else {
        g_ds5.reconnect_backoff_shift++;
    }
    ds5_classic_schedule_reconnect(delay_ms);
}

static void ds5_classic_teardown_link(int unexpected,
                                      uint16_t sdk_result,
                                      int request_disconnect)
{
    int had_link;
    BTS2S_BD_ADDR active_bd;

    had_link = g_ds5.status.active_address_valid ||
               g_ds5.status.control_cid != 0U ||
               g_ds5.status.interrupt_cid != 0U ||
               g_ds5.status.connected || g_ds5.status.connecting ||
               g_ds5.status.disconnecting;
    active_bd = g_ds5.active_bd;

    if (request_disconnect && g_ds5.status.active_address_valid) {
        /* A HID transport is only valid while both PSMs are alive.  Asking
         * for both disconnects is intentional: the SDK API is PSM based and
         * silently tolerates a PSM that has already gone away. */
        bt_l2cap_profile_disconn_req(&active_bd,
                                     DS5_CLASSIC_HID_INTERRUPT_PSM);
        bt_l2cap_profile_disconn_req(&active_bd,
                                     DS5_CLASSIC_HID_CONTROL_PSM);
    }

    ds5_classic_clear_link();
    if (had_link) {
        g_ds5.status.disconnects++;
    }
    if (sdk_result != BTS2_SUCC) {
        ds5_classic_set_error(DS5_CLASSIC_ERROR_SDK, sdk_result);
    }
    if (unexpected) {
        ds5_classic_schedule_reconnect_backoff();
    }
    ds5_classic_refresh_state();
}

static void ds5_classic_schedule_connect(const BTS2S_BD_ADDR *bd)
{
    if (bd == 0) {
        return;
    }
    ds5_classic_set_active_bd(bd);
    g_ds5.status.connecting = 1U;
    g_ds5.status.disconnecting = 0U;
    g_ds5.status.reconnect_pending = 0U;
    g_ds5.control_connect_pending = 1U;
    g_ds5.control_connect_requested = 0U;
    g_ds5.interrupt_connect_requested = 0U;
    ds5_classic_clear_error();
    ds5_classic_refresh_state();
}

static int ds5_classic_load_saved_from_connection_manager(void)
{
    if (g_ds5.injected_saved_valid) {
        return 1;
    }
#if defined(BSP_BT_CONNECTION_MANAGER)
    bt_cm_bonded_dev_t *bonded = bt_cm_get_bonded_dev();
    uint8_t i;
    uint8_t index;

    if (bonded == 0) {
        return 0;
    }
    for (i = 0U; i < BT_CM_MAX_BOND; i++) {
        index = (uint8_t)((bonded->last_bond_idx + BT_CM_MAX_BOND - i) %
                          BT_CM_MAX_BOND);
        if (bonded->info[index].is_use &&
            (bonded->info[index].dev_cls & 0x000f00UL) == 0x000500UL &&
            (bonded->info[index].dev_cls & 0x0000fcUL) == 0x000008UL) {
            ds5_classic_set_saved_bd(&bonded->info[index].bd_addr);
            return 1;
        }
    }
#endif
    return 0;
}

static void ds5_classic_handle_input(const uint8_t *payload, uint16_t len)
{
    uint32_t expected_crc;
    uint32_t actual_crc;

    if (payload == 0 || len != DS5_CLASSIC_BT_INPUT_PACKET_SIZE ||
        payload[0] != 0xa1U || payload[1] != 0x31U) {
        g_ds5.status.short_input_reports++;
        return;
    }

    expected_crc = ds5_classic_read_u32_le(payload + len - 4U);
    actual_crc = ds5_classic_crc32_seeded(payload + 1U,
                                          len - 5U,
                                          DS5_CLASSIC_INPUT_CRC_SEED);
    if (actual_crc != expected_crc) {
        g_ds5.status.input_crc_errors++;
        return;
    }

    if ((payload[2] & 0x02U) != 0U) {
        const uint16_t opus_len = (uint16_t)(len - 8U);

        g_ds5.status.audio_input_reports++;
        g_ds5.status.audio_input_bytes += opus_len;
        if (g_ds5.audio_input_callback != 0) {
            g_ds5.audio_input_callback(payload + 4U, opus_len,
                                       g_ds5.audio_callback_context);
        }
        return;
    }

    g_ds5.status.input_reports++;
    if (g_ds5.input_callback != 0) {
        g_ds5.input_callback(payload + 3U,
                             DS5_CLASSIC_USB_INPUT_BODY_SIZE,
                             g_ds5.callback_context);
    }
}

static void ds5_classic_handle_control(const uint8_t *payload, uint16_t len)
{
    uint8_t report_id = 0U;
    uint8_t request_next = 0U;
    int result;

    rt_enter_critical();
    result = sf32lb52_ds5_feature_handle_control(&g_ds5.feature_cache,
                                                 payload, len,
                                                 &report_id);
    if (result == 1) {
        g_ds5.status.feature_responses++;
    }
    if (g_ds5.feature_cache.edge_known) {
        g_ds5.status.edge_known = 1U;
        g_ds5.status.is_edge = g_ds5.feature_cache.is_edge;
        if (g_ds5.feature_cache.is_edge && report_id == 0x70U &&
            g_ds5.status.dse_unlock_phase == DS5_CLASSIC_DSE_UNLOCK_IDLE) {
            g_ds5.status.dse_unlock_phase =
                DS5_CLASSIC_DSE_UNLOCK_SEND_FIRMWARE;
            g_ds5.status.dse_profiles_ready = 0U;
        }
    }
    if (g_ds5.awaiting_feature_response &&
        ((report_id != 0U && report_id == g_ds5.feature_request_id) ||
         (payload != 0 && len != 0U && payload[0] == 0x02U))) {
        g_ds5.awaiting_feature_response = 0U;
        g_ds5.feature_request_retries = 0U;
        g_ds5.status.feature_response_pending = 0U;
        if (g_ds5.feature_prefetch_index <
            sizeof(ds5_feature_prefetch_ids) &&
            g_ds5.feature_request_id ==
                ds5_feature_prefetch_ids[g_ds5.feature_prefetch_index]) {
            g_ds5.feature_prefetch_index++;
            request_next = 1U;
        }
    }
    if (request_next && g_ds5.feature_prefetch_index <
        sizeof(ds5_feature_prefetch_ids)) {
        ds5_classic_schedule_feature_request(
            ds5_feature_prefetch_ids[g_ds5.feature_prefetch_index]);
    }
    if (result == 1 &&
        g_ds5.status.dse_unlock_phase ==
            DS5_CLASSIC_DSE_UNLOCK_PREFETCH &&
        report_id == g_ds5.dse_profile_next) {
        g_ds5.status.dse_profile_reports++;
        if (report_id == 0x7bU) {
            g_ds5.status.dse_unlock_phase = DS5_CLASSIC_DSE_UNLOCK_READY;
            g_ds5.status.dse_profiles_ready = 1U;
        } else {
            g_ds5.dse_profile_next++;
            g_ds5.dse_profile_due_tick = rt_tick_get() +
                rt_tick_from_millisecond(
                    DS5_CLASSIC_DSE_PREFETCH_INTERVAL_MS);
        }
    }
    rt_exit_critical();
}

static void ds5_classic_handle_registration(
    const BTS2S_L2CAP_PROFILE_REG_RES *result)
{
    if (result == 0) {
        return;
    }
    rt_kprintf("[ds5] l2cap reg psm=0x%04x state=%u result=%u\n",
               result->local_psm,
               result->reg_state,
               result->res);
    g_ds5.status.last_sdk_result = result->res;
    if (result->local_psm != DS5_CLASSIC_HID_CONTROL_PSM &&
        result->local_psm != DS5_CLASSIC_HID_INTERRUPT_PSM) {
        return;
    }
    /* SiFli reports REGING first and REG after completion.  REGING is a
     * progress notification and must not clear the in-flight state or count
     * as a registration failure. */
    if (result->res == BTS2_SUCC &&
        result->reg_state == BT_L2CAP_PROFILE_REGING_STATE) {
        return;
    }
    if (result->local_psm == DS5_CLASSIC_HID_CONTROL_PSM) {
        g_ds5.control_registration_in_flight = 0U;
    } else {
        g_ds5.interrupt_registration_in_flight = 0U;
    }
    if (result->res != BTS2_SUCC ||
        result->reg_state != BT_L2CAP_PROFILE_REG_STATE) {
        g_ds5.status.registration_failures++;
        ds5_classic_schedule_registration_retry();
        ds5_classic_set_error(DS5_CLASSIC_ERROR_SDK, result->res);
        return;
    }
    if (result->local_psm == DS5_CLASSIC_HID_CONTROL_PSM) {
        g_ds5.status.control_registered = 1U;
    } else if (result->local_psm == DS5_CLASSIC_HID_INTERRUPT_PSM) {
        g_ds5.status.interrupt_registered = 1U;
    }
    if (g_ds5.status.control_registered &&
        g_ds5.status.interrupt_registered) {
        g_ds5.saved_read_requested = 1U;
        g_ds5.registration_retry_pending = 0U;
        g_ds5.registration_retry_shift = 0U;
        ds5_classic_clear_error();
    }
    ds5_classic_refresh_state();
}

static int ds5_classic_address_allowed(const BTS2S_BD_ADDR *bd)
{
    if (bd == 0) {
        return 0;
    }
    if (g_ds5.status.active_address_valid) {
        return ds5_classic_bd_equal(bd, &g_ds5.active_bd);
    }
    if ((g_ds5.status.reconnect_pending || g_ds5.reconnect_suppressed) &&
        !g_ds5.want_pairing) {
        return 0;
    }
    if (g_ds5.want_pairing && g_ds5.candidate_valid) {
        return ds5_classic_bd_equal(bd, &g_ds5.candidate_bd);
    }
    if (!g_ds5.want_pairing && g_ds5.status.saved_address_valid) {
        return ds5_classic_bd_equal(bd, &g_ds5.saved_bd);
    }
    return 1;
}

static int ds5_classic_result_matches_active(
    const BTS2S_BT_L2CAP_CONN_RES *result)
{
    if (result == 0) {
        return 0;
    }
    if (g_ds5.status.active_address_valid &&
        ds5_classic_bd_equal(&result->bd, &g_ds5.active_bd)) {
        return 1;
    }
    return (result->local_psm == DS5_CLASSIC_HID_CONTROL_PSM &&
            result->cid != 0U &&
            result->cid == g_ds5.status.control_cid) ||
           (result->local_psm == DS5_CLASSIC_HID_INTERRUPT_PSM &&
            result->cid != 0U &&
            result->cid == g_ds5.status.interrupt_cid);
}

static void ds5_classic_handle_connection(
    const BTS2S_BT_L2CAP_CONN_RES *result)
{
    int expected_disconnect;
    BTS2S_BD_ADDR event_bd;

    if (result == 0) {
        return;
    }
    event_bd = result->bd;
    if (result->local_psm != DS5_CLASSIC_HID_CONTROL_PSM &&
        result->local_psm != DS5_CLASSIC_HID_INTERRUPT_PSM) {
        return;
    }
    g_ds5.status.last_sdk_result = result->res;
    if (result->device_state == BT_L2CAP_PROFILE_DEVICE_CONNECTED &&
        result->res == BTS2_SUCC) {
        if (!ds5_classic_address_allowed(&result->bd)) {
            bt_l2cap_profile_disconn_req(&event_bd,
                                         result->local_psm);
            return;
        }
        ds5_classic_set_active_bd(&result->bd);
        g_ds5.status.connecting = 1U;
        if (result->local_psm == DS5_CLASSIC_HID_CONTROL_PSM) {
            if (g_ds5.status.control_cid != 0U &&
                g_ds5.status.control_cid != result->cid) {
                ds5_classic_teardown_link(1, result->res, 1);
                return;
            }
            g_ds5.status.control_cid = result->cid;
            g_ds5.status.control_remote_mtu = result->remote_mtu;
            g_ds5.control_connect_pending = 0U;
            g_ds5.control_connect_requested = 0U;
            if (g_ds5.status.interrupt_cid == 0U &&
                !g_ds5.interrupt_connect_requested) {
                g_ds5.interrupt_connect_pending = 1U;
            }
        } else if (result->local_psm == DS5_CLASSIC_HID_INTERRUPT_PSM) {
            if (g_ds5.status.interrupt_cid != 0U &&
                g_ds5.status.interrupt_cid != result->cid) {
                ds5_classic_teardown_link(1, result->res, 1);
                return;
            }
            g_ds5.status.interrupt_cid = result->cid;
            g_ds5.status.interrupt_remote_mtu = result->remote_mtu;
            g_ds5.interrupt_connect_pending = 0U;
            g_ds5.interrupt_connect_requested = 0U;
            if (g_ds5.status.control_cid == 0U &&
                !g_ds5.control_connect_requested) {
                g_ds5.control_connect_pending = 1U;
            }
        }

        if (g_ds5.status.control_cid != 0U &&
            g_ds5.status.interrupt_cid != 0U &&
            !g_ds5.status.connected) {
            g_ds5.status.connecting = 0U;
            g_ds5.status.connected = 1U;
            g_ds5.status.disconnecting = 0U;
            g_ds5.status.connects++;
            g_ds5.primer_output_pending = 1U;
            g_ds5.neutral_output_pending = 0U;
            g_ds5.status.reconnect_pending = 0U;
            g_ds5.reconnect_backoff_shift = 0U;
            sf32lb52_ds5_feature_cache_reset(&g_ds5.feature_cache);
            g_ds5.feature_prefetch_index = 0U;
            ds5_classic_schedule_feature_request(ds5_feature_prefetch_ids[0]);
            ds5_classic_clear_error();
        }
    } else if (result->device_state == BT_L2CAP_PROFILE_DEVICE_DISCONNECTED) {
        if (!ds5_classic_result_matches_active(result)) {
            return;
        }
        expected_disconnect = g_ds5.status.disconnecting != 0U;
        /* Losing either HID PSM invalidates the complete HID transport. */
        ds5_classic_teardown_link(!expected_disconnect,
                                  result->res,
                                  1);
    } else if (result->res != BTS2_SUCC) {
        if (ds5_classic_result_matches_active(result)) {
            /* In particular, an interrupt-channel failure must tear down an
             * already-established control channel. */
            ds5_classic_teardown_link(1, result->res, 1);
        }
    }
    ds5_classic_refresh_state();
}

static void ds5_classic_pair_complete(const BTS2S_BD_ADDR *bd,
                                      uint8_t result)
{
    if (bd == 0 ||
        (g_ds5.candidate_valid &&
         !ds5_classic_bd_equal(bd, &g_ds5.candidate_bd))) {
        return;
    }
    g_ds5.status.last_sdk_result = result;
    rt_kprintf("[ds5] pair complete addr=%04x:%02x:%06lx result=%u\n",
               bd->nap,
               bd->uap,
               (unsigned long)bd->lap,
               (unsigned int)result);
    g_ds5.status.pairing = 0U;
    g_ds5.want_pairing = 0U;
    g_ds5.pair_request_pending = 0U;
    g_ds5.acl_pairing_flow = 0U;
    g_ds5.auth_request_pending = 0U;
    g_ds5.encryption_request_pending = 0U;
    if (result == BTS2_SUCC) {
        ds5_classic_set_saved_bd(bd);
        g_ds5.injected_saved_valid = 0U;
        g_ds5.candidate_valid = 0U;
        if (!g_ds5.status.connected && !g_ds5.status.connecting) {
            ds5_classic_schedule_connect(bd);
        }
    } else {
        if (g_ds5.acl_interest_held) {
            BTS2S_BD_ADDR close_bd = *bd;

            hcia_acl_close_req(&close_bd, NULL);
            g_ds5.acl_interest_held = 0U;
        }
        g_ds5.candidate_valid = 0U;
        ds5_classic_set_error(DS5_CLASSIC_ERROR_SDK, result);
        ds5_classic_schedule_reconnect_backoff();
    }
    ds5_classic_refresh_state();
}

static int ds5_classic_bt_event_handler(U16 type,
                                        U16 event_id,
                                        uint8_t *message,
                                        uint32_t context)
{
    (void)context;
    if (!g_ds5.status.initialized || message == 0) {
        return 0;
    }

    if (type == BTS2M_GAP) {
        switch (event_id) {
        case BTS2MU_GAP_RD_LOCAL_NAME_CFM:
            if (!g_ds5.status.stack_ready) {
                g_ds5.status.stack_ready = 1U;
                g_ds5.registration_requested = 0U;
            }
            break;
        case BTS2MU_GAP_DISCOV_RES_IND:
        {
            BTS2S_GAP_DISCOV_RES_IND *result =
                (BTS2S_GAP_DISCOV_RES_IND *)message;
            int candidate = ds5_classic_is_candidate(result);
            g_ds5.status.discovery_reports++;
            if (g_ds5.want_pairing && !g_ds5.candidate_valid &&
                candidate) {
                rt_kprintf("[ds5] candidate addr=%04x:%02x:%06lx cod=0x%06lx name=%.32s\n",
                           result->bd.nap,
                           result->bd.uap,
                           (unsigned long)result->bd.lap,
                           (unsigned long)result->dev_cls,
                           (const char *)result->dev_disp_name);
                g_ds5.status.candidates_found++;
                g_ds5.candidate_bd = result->bd;
                g_ds5.candidate_valid = 1U;
                g_ds5.stop_discovery_pending = 1U;
            }
            break;
        }
        case BTS2MU_GAP_DISCOV_CFM:
        case BTS2MU_GAP_ESC_DISCOV_CFM:
            g_ds5.status.discovering = 0U;
            if (g_ds5.candidate_valid && g_ds5.want_pairing) {
                g_ds5.pair_request_pending = 1U;
            } else if (g_ds5.want_pairing) {
                g_ds5.start_discovery_pending = 1U;
            }
            break;
        case BTS2MU_GAP_KEYMISSING_IND:
            if (g_ds5.status.saved_address_valid) {
                BTS2S_GAP_KEYMISSING_IND *ind =
                    (BTS2S_GAP_KEYMISSING_IND *)message;
                if (ds5_classic_bd_equal(&ind->bd, &g_ds5.saved_bd)) {
                    g_ds5.status.key_missing_events++;
                    g_ds5.status.saved_address_valid = 0U;
                    g_ds5.injected_saved_valid = 0U;
                    g_ds5.status.reconnect_pending = 0U;
                    memset(g_ds5.status.saved_address, 0,
                           sizeof(g_ds5.status.saved_address));
                }
            }
            break;
        default:
            break;
        }
    } else if (type == BTS2M_SC) {
        switch (event_id) {
        case BTS2MU_SC_PAIR_CFM:
        {
            BTS2S_SC_PAIR_CFM *result = (BTS2S_SC_PAIR_CFM *)message;
            rt_kprintf("[ds5] pair cfm added=%u cod=0x%06lx\n",
                       result->added_to_sc_db_list ? 1U : 0U,
                       (unsigned long)result->cod);
            if (g_ds5.acl_pairing_flow && result->res == BTS2_SUCC) {
                ds5_classic_set_saved_bd(&result->bd);
                rt_kprintf("[ds5] security pairing complete, await encryption\n");
            } else {
                ds5_classic_pair_complete(&result->bd, result->res);
            }
            break;
        }
        case BTS2MU_SC_PAIR_IND:
        {
            BTS2S_SC_PAIR_IND *result = (BTS2S_SC_PAIR_IND *)message;
            rt_kprintf("[ds5] pair ind added=%u cod=0x%06lx\n",
                       result->added_to_sc_db_list ? 1U : 0U,
                       (unsigned long)result->cod);
            if (g_ds5.acl_pairing_flow && result->res == BTS2_SUCC) {
                ds5_classic_set_saved_bd(&result->bd);
                rt_kprintf("[ds5] security pairing complete, await encryption\n");
            } else {
                ds5_classic_pair_complete(&result->bd, result->res);
            }
            break;
        }
        case BTS2MU_SC_SET_SECU_LEVEL_CFM:
        {
            BTS2S_SC_SET_SECU_LEVEL_CFM *result =
                (BTS2S_SC_SET_SECU_LEVEL_CFM *)message;
            rt_kprintf("[ds5] security mode 4 result=%u\n",
                       (unsigned int)result->res);
            if (result->res == BTS2_SUCC) {
                g_ds5.security_mode_ready = 1U;
                ds5_classic_clear_error();
            } else {
                g_ds5.security_mode_requested = 0U;
                ds5_classic_set_error(DS5_CLASSIC_ERROR_SDK, result->res);
            }
            ds5_classic_refresh_state();
            break;
        }
        case BTS2MU_SC_IO_CAPABILITY_REQ_IND:
        {
            BTS2S_SC_IO_CAPABILITY_REQ_IND *result =
                (BTS2S_SC_IO_CAPABILITY_REQ_IND *)message;
            rt_kprintf("[ds5] local IO capability requested addr=%04x:%02x:%06lx\n",
                       result->bd.nap,
                       result->bd.uap,
                       (unsigned long)result->bd.lap);
            sc_io_capability_rsp(&result->bd,
                                 IO_CAPABILITY_NO_INPUT_NO_OUTPUT,
                                 FALSE,
                                 TRUE);
            break;
        }
        case BTS2MU_SC_REMOTE_IO_CAPABILITY_IND:
        {
            BTS2S_SC_REMOTE_IO_CAPABILITY_IND *result =
                (BTS2S_SC_REMOTE_IO_CAPABILITY_IND *)message;
            rt_kprintf("[ds5] remote IO capability=%u auth=%u addr=%04x:%02x:%06lx\n",
                       (unsigned int)result->io_capability,
                       (unsigned int)result->auth_requirements,
                       result->bd.nap,
                       result->bd.uap,
                        (unsigned long)result->bd.lap);
            break;
        }
        case BTS2MU_SC_REQ_USER_CONFIRM_IND:
        {
            BTS2S_SC_USER_CONF_CFM *result =
                (BTS2S_SC_USER_CONF_CFM *)message;
            int allowed = ds5_classic_address_allowed(&result->bd);
            rt_kprintf("[ds5] user confirm addr=%04x:%02x:%06lx value=%lu allowed=%u\n",
                       result->bd.nap,
                       result->bd.uap,
                       (unsigned long)result->bd.lap,
                       (unsigned long)result->num_val,
                       allowed ? 1U : 0U);
            sc_user_cfm_rsp(&result->bd, allowed ? TRUE : FALSE);
            break;
        }
        case BTS2MU_SC_AUTHORISE_IND:
        {
            BTS2S_SC_AUTHORISE_IND *result =
                (BTS2S_SC_AUTHORISE_IND *)message;
            int allowed = ds5_classic_address_allowed(&result->bd);
            rt_kprintf("[ds5] authorise addr=%04x:%02x:%06lx allowed=%u\n",
                       result->bd.nap,
                       result->bd.uap,
                       (unsigned long)result->bd.lap,
                       allowed ? 1U : 0U);
            sc_authorise_rsp(allowed ? TRUE : FALSE, &result->bd);
            break;
        }
        case BTS2MU_SC_RD_PAIRED_DEV_RECORD_CFM:
        {
            BTS2S_SC_RD_PAIRED_DEV_RECORD_CFM *result =
                (BTS2S_SC_RD_PAIRED_DEV_RECORD_CFM *)message;
            g_ds5.saved_read_in_flight = 0U;
            if (!g_ds5.status.saved_address_valid &&
                result->total_dev_num > 0U) {
                ds5_classic_set_saved_bd(
                    &result->bd[result->total_dev_num - 1U]);
            }
            if (g_ds5.connect_saved_waiting) {
                g_ds5.connect_saved_waiting = 0U;
                if (g_ds5.status.saved_address_valid) {
                    ds5_classic_schedule_connect(&g_ds5.saved_bd);
                } else {
                    /* An empty bond database is the normal first-boot state;
                     * pairing remains available and status should stay idle. */
                    ds5_classic_clear_error();
                }
            } else if (ds5_classic_reconnect_allowed() &&
                       !g_ds5.status.connected &&
                       !g_ds5.status.connecting) {
                ds5_classic_schedule_reconnect(0U);
            }
            break;
        }
        case BTS2MU_SC_UNPAIR_CFM:
            g_ds5.status.saved_address_valid = 0U;
            g_ds5.injected_saved_valid = 0U;
            g_ds5.status.reconnect_pending = 0U;
            memset(g_ds5.status.saved_address, 0,
                   sizeof(g_ds5.status.saved_address));
            break;
        default:
            break;
        }
    } else if (type == BTS2M_BT_L2CAP_PROFILE) {
        switch (event_id) {
        case BTS2MU_BT_L2CAP_PROFILE_REG_CFM:
            ds5_classic_handle_registration(
                (BTS2S_L2CAP_PROFILE_REG_RES *)message);
            break;
        case BTS2MU_BT_L2CAP_PROFILE_CONN_CFM:
            ds5_classic_handle_connection(
                (BTS2S_BT_L2CAP_CONN_RES *)message);
            break;
        case BTS2MU_BT_L2CAP_PROFILE_DISC_IND:
        case BTS2MU_BT_L2CAP_PROFILE_DISC_CFM:
            ds5_classic_handle_connection(
                (BTS2S_BT_L2CAP_CONN_RES *)message);
            break;
        case BTS2MU_BT_L2CAP_PROFILE_DATA_IND:
        {
            BTS2S_BT_L2CAP_DATA_IND *data =
                (BTS2S_BT_L2CAP_DATA_IND *)message;
            if (data->cid == g_ds5.status.interrupt_cid) {
                ds5_classic_handle_input(data->payload, data->len);
            } else if (data->cid == g_ds5.status.control_cid) {
                ds5_classic_handle_control(data->payload, data->len);
            }
            break;
        }
        case BTS2MU_BT_L2CAP_PROFILE_DATA_CFM:
        {
            BTS2S_BT_L2CAP_DATA_CFM *result =
                (BTS2S_BT_L2CAP_DATA_CFM *)message;
            if (result->cid == g_ds5.status.interrupt_cid) {
                rt_enter_critical();
                g_ds5.output_busy = 0U;
                g_ds5.status.last_sdk_result = result->res;
                if (result->res != BTS2_SUCC) {
                    g_ds5.status.output_failures++;
                }
                rt_exit_critical();
                if (result->res != BTS2_SUCC) {
                    ds5_classic_set_error(DS5_CLASSIC_ERROR_SDK,
                                          result->res);
                }
                ds5_classic_try_send_pending_output();
            } else if (result->cid == g_ds5.status.control_cid) {
                uint8_t operation;

                rt_enter_critical();
                g_ds5.control_busy = 0U;
                g_ds5.status.last_sdk_result = result->res;
                operation = g_ds5.control_operation;
                if (result->res != BTS2_SUCC) {
                    if (operation == DS5_CLASSIC_CONTROL_OP_GET_FEATURE) {
                        ds5_classic_recover_feature_request_locked(0);
                    } else {
                        g_ds5.status.feature_failures++;
                    }
                    if (operation != DS5_CLASSIC_CONTROL_OP_GET_FEATURE &&
                        (g_ds5.status.dse_unlock_phase ==
                                   DS5_CLASSIC_DSE_UNLOCK_SEND_COMMAND ||
                         g_ds5.status.dse_unlock_phase ==
                             DS5_CLASSIC_DSE_UNLOCK_WAIT)) {
                        g_ds5.status.dse_unlock_phase =
                            DS5_CLASSIC_DSE_UNLOCK_IDLE;
                        g_ds5.status.dse_profiles_ready = 0U;
                    }
                }
                g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_NONE;
                rt_exit_critical();
            }
            break;
        }
        default:
            break;
        }
    } else if (type == BTS2M_HCI_CMD) {
        switch (event_id) {
        case DM_EN_ACL_OPENED_IND:
        {
            BTS2S_DM_EN_ACL_OPENED_IND *result =
                (BTS2S_DM_EN_ACL_OPENED_IND *)message;
            if (!g_ds5.acl_pairing_flow ||
                !ds5_classic_bd_equal(&result->bd, &g_ds5.candidate_bd)) {
                break;
            }
            rt_kprintf("[ds5] acl opened status=%u handle=0x%04x incoming=%u\n",
                       (unsigned int)result->st,
                       (unsigned int)result->phdl,
                       result->incoming ? 1U : 0U);
            if (result->st == HCI_SUCC) {
                g_ds5.acl_interest_held = 1U;
                g_ds5.auth_request_pending = 1U;
            } else {
                ds5_classic_pair_complete(&result->bd,
                                          result->st != 0U ? result->st : 2U);
            }
            break;
        }
        case DM_ACL_OPEN_CFM:
        {
            BTS2S_DM_ACL_OPEN_CFM *result =
                (BTS2S_DM_ACL_OPEN_CFM *)message;
            if (!g_ds5.acl_pairing_flow ||
                !ds5_classic_bd_equal(&result->bd, &g_ds5.candidate_bd)) {
                break;
            }
            rt_kprintf("[ds5] acl open result=%u handle=0x%04x\n",
                       result->succ ? 0U : 1U,
                       result->acl_hdl);
            if (result->succ) {
                g_ds5.acl_interest_held = 1U;
                g_ds5.auth_request_pending = 1U;
            } else {
                ds5_classic_pair_complete(&result->bd, 2U);
            }
            break;
        }
        case DM_SM_AUTH_CFM:
        {
            BTS2S_DM_SM_AUTH_CFM *result =
                (BTS2S_DM_SM_AUTH_CFM *)message;
            if (!g_ds5.acl_pairing_flow ||
                !ds5_classic_bd_equal(&result->bd, &g_ds5.candidate_bd)) {
                break;
            }
            rt_kprintf("[ds5] auth result=%u reason=%u\n",
                       result->succ ? 0U : 1U,
                       (unsigned int)result->res);
            if (result->succ) {
                g_ds5.encryption_request_pending = 1U;
            } else {
                ds5_classic_pair_complete(&result->bd,
                                          result->res != 0U ? result->res : 2U);
            }
            break;
        }
        case DM_SM_ENCRYPT_CFM:
        {
            BTS2S_DM_SM_ENCRYPT_CFM *result =
                (BTS2S_DM_SM_ENCRYPT_CFM *)message;
            if (!g_ds5.acl_pairing_flow ||
                !ds5_classic_bd_equal(&result->bd, &g_ds5.candidate_bd)) {
                break;
            }
            rt_kprintf("[ds5] encryption result=%u enabled=%u\n",
                       result->succ ? 0U : 1U,
                       result->encrypted ? 1U : 0U);
            if (result->succ && result->encrypted) {
                ds5_classic_pair_complete(&result->bd, BTS2_SUCC);
            } else {
                ds5_classic_pair_complete(&result->bd, 2U);
            }
            break;
        }
        case DM_ACL_DISC_IND:
        case DM_ACL_CLOSED_IND:
            if (g_ds5.acl_pairing_flow) {
                ds5_classic_pair_complete(&g_ds5.candidate_bd, 2U);
            } else if (g_ds5.status.connected || g_ds5.status.connecting ||
                       g_ds5.status.disconnecting) {
                int expected_disconnect = g_ds5.status.disconnecting != 0U;
                ds5_classic_teardown_link(!expected_disconnect, 0U, 0);
            }
            g_ds5.acl_interest_held = 0U;
            break;
        default:
            break;
        }
    }
    return 0;
}

static int ds5_classic_ble_event_handler(uint16_t event_id,
                                         uint8_t *data,
                                         uint16_t len,
                                         uint32_t context)
{
    (void)data;
    (void)len;
    (void)context;
    if (event_id == BLE_POWER_ON_IND && g_ds5.status.initialized) {
        if (!g_ds5.status.stack_ready) {
            g_ds5.status.stack_ready = 1U;
            g_ds5.registration_requested = 0U;
        }
        ds5_classic_refresh_state();
    }
    return 0;
}

BT_EVENT_REGISTER_HIGH(ds5_classic_bt_event_handler, NULL);
BLE_EVENT_REGISTER_HIGH(ds5_classic_ble_event_handler, NULL);

/* Override the SDK custom-L2CAP weak hook so each incoming HID channel is
 * accepted exactly once and unrelated custom PSMs are rejected. */
void bt_l2cap_profile_app_conn_ind(BTS2S_BT_L2CAP_CONN_IND *connection)
{
    uint8_t accept = 0U;

    rt_enter_critical();
    if (connection != 0 && g_ds5.status.initialized &&
        (connection->psm == DS5_CLASSIC_HID_CONTROL_PSM ||
         connection->psm == DS5_CLASSIC_HID_INTERRUPT_PSM) &&
        !g_ds5.status.disconnecting &&
        ds5_classic_address_allowed(&connection->bd) &&
        !((connection->psm == DS5_CLASSIC_HID_CONTROL_PSM &&
           (g_ds5.status.control_cid != 0U ||
            g_ds5.control_connect_pending ||
            g_ds5.control_connect_requested)) ||
          (connection->psm == DS5_CLASSIC_HID_INTERRUPT_PSM &&
           (g_ds5.status.interrupt_cid != 0U ||
            g_ds5.interrupt_connect_pending ||
            g_ds5.interrupt_connect_requested)))) {
        accept = 1U;
        ds5_classic_set_active_bd(&connection->bd);
        g_ds5.status.connecting = 1U;
        g_ds5.status.reconnect_pending = 0U;
    }
    rt_exit_critical();
    if (accept) {
        ds5_classic_refresh_state();
    }
    if (connection != 0) {
        bt_l2cap_profile_send_conn_res(accept, connection);
    }
}

int ds5_classic_init(ds5_classic_input_callback_t input_callback,
                     void *callback_context)
{
    if (g_ds5.status.initialized) {
        g_ds5.input_callback = input_callback;
        g_ds5.callback_context = callback_context;
        return DS5_CLASSIC_OK;
    }

    memset(&g_ds5, 0, sizeof(g_ds5));
    sf32lb52_ds5_output_state_init(&g_ds5.output_state);
    g_ds5.input_callback = input_callback;
    g_ds5.callback_context = callback_context;
    g_ds5.status.sdk_available = 1U;
    g_ds5.status.event_bridge_available = 1U;
    g_ds5.status.initialized = 1U;
    g_ds5.status.state = DS5_CLASSIC_STATE_STARTING;
    g_ds5.status.last_error = DS5_CLASSIC_OK;
    return DS5_CLASSIC_OK;
}

void ds5_classic_set_audio_input_callback(
    ds5_classic_audio_input_callback_t audio_callback,
    void *callback_context)
{
    g_ds5.audio_input_callback = audio_callback;
    g_ds5.audio_callback_context = callback_context;
}

int ds5_classic_set_saved_address(const uint8_t address[6])
{
    BTS2S_BD_ADDR bd;
    uint8_t nonzero = 0U;
    size_t i;

    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }
    if (address == 0) {
        rt_enter_critical();
        memset(&g_ds5.saved_bd, 0, sizeof(g_ds5.saved_bd));
        g_ds5.injected_saved_valid = 0U;
        g_ds5.status.saved_address_valid = 0U;
        g_ds5.status.reconnect_pending = 0U;
        g_ds5.connect_saved_waiting = 0U;
        memset(g_ds5.status.saved_address, 0,
               sizeof(g_ds5.status.saved_address));
        rt_exit_critical();
        return DS5_CLASSIC_OK;
    }
    for (i = 0U; i < 6U; i++) {
        nonzero |= address[i];
    }
    if (!nonzero) {
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }

    ds5_classic_bd_from_bytes(address, &bd);
    rt_enter_critical();
    ds5_classic_set_saved_bd(&bd);
    g_ds5.injected_saved_valid = 1U;
    g_ds5.connect_saved_waiting = 0U;
    rt_exit_critical();

    if (!g_ds5.status.connected && !g_ds5.status.connecting &&
        !g_ds5.status.disconnecting && !g_ds5.status.discovering &&
        !g_ds5.status.pairing && !g_ds5.want_pairing &&
        ds5_classic_reconnect_allowed()) {
        ds5_classic_schedule_reconnect(0U);
    }
    ds5_classic_refresh_state();
    return DS5_CLASSIC_OK;
}

int ds5_classic_set_auto_reconnect(int enabled)
{
    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }

    g_ds5.status.auto_reconnect_enabled = enabled ? 1U : 0U;
    g_ds5.reconnect_suppressed = 0U;
    if (!enabled) {
        g_ds5.status.reconnect_pending = 0U;
        ds5_classic_refresh_state();
        return DS5_CLASSIC_OK;
    }

    if (!g_ds5.status.saved_address_valid) {
        (void)ds5_classic_load_saved_from_connection_manager();
    }
    if (!g_ds5.status.saved_address_valid) {
        g_ds5.saved_read_requested = 1U;
    } else if (!g_ds5.status.connected && !g_ds5.status.connecting &&
               !g_ds5.status.disconnecting && !g_ds5.status.discovering &&
               !g_ds5.status.pairing && !g_ds5.want_pairing) {
        g_ds5.reconnect_backoff_shift = 0U;
        ds5_classic_schedule_reconnect(0U);
    }
    ds5_classic_refresh_state();
    return DS5_CLASSIC_OK;
}

int ds5_classic_start_pairing(void)
{
    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }
    if (g_ds5.status.connected || g_ds5.status.connecting ||
        g_ds5.status.disconnecting || g_ds5.status.discovering ||
        g_ds5.status.pairing) {
        return DS5_CLASSIC_ERROR_BUSY;
    }

    g_ds5.want_pairing = 1U;
    g_ds5.candidate_valid = 0U;
    g_ds5.pair_request_pending = 0U;
    g_ds5.acl_pairing_flow = 0U;
    g_ds5.auth_request_pending = 0U;
    g_ds5.encryption_request_pending = 0U;
    g_ds5.status.pairing = 0U;
    g_ds5.status.reconnect_pending = 0U;
    g_ds5.connect_saved_waiting = 0U;
    ds5_classic_clear_error();
    if (g_ds5.status.stack_ready &&
        g_ds5.status.control_registered &&
        g_ds5.status.interrupt_registered) {
        g_ds5.start_discovery_pending = 1U;
    }
    ds5_classic_refresh_state();
    return DS5_CLASSIC_OK;
}

int ds5_classic_connect_saved(void)
{
    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }
    if (g_ds5.status.connected || g_ds5.status.connecting ||
        g_ds5.status.disconnecting || g_ds5.status.discovering ||
        g_ds5.status.pairing || g_ds5.want_pairing) {
        return DS5_CLASSIC_ERROR_BUSY;
    }

    ds5_classic_clear_error();
    g_ds5.reconnect_suppressed = 0U;
    g_ds5.status.reconnect_pending = 0U;
    g_ds5.reconnect_backoff_shift = 0U;
    if (!g_ds5.status.saved_address_valid) {
        (void)ds5_classic_load_saved_from_connection_manager();
    }
    if (g_ds5.status.saved_address_valid) {
        ds5_classic_schedule_connect(&g_ds5.saved_bd);
    } else {
        g_ds5.connect_saved_waiting = 1U;
        g_ds5.saved_read_requested = 1U;
    }
    ds5_classic_refresh_state();
    return DS5_CLASSIC_OK;
}

int ds5_classic_disconnect(void)
{
    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }
    if (!g_ds5.status.active_address_valid ||
        (!g_ds5.status.connected && !g_ds5.status.connecting)) {
        return DS5_CLASSIC_ERROR_NOT_CONNECTED;
    }

    g_ds5.status.disconnecting = 1U;
    g_ds5.status.connecting = 0U;
    g_ds5.want_pairing = 0U;
    g_ds5.reconnect_suppressed = 1U;
    g_ds5.status.reconnect_pending = 0U;
    bt_l2cap_profile_disconn_req(&g_ds5.active_bd,
                                 DS5_CLASSIC_HID_INTERRUPT_PSM);
    bt_l2cap_profile_disconn_req(&g_ds5.active_bd,
                                 DS5_CLASSIC_HID_CONTROL_PSM);
    ds5_classic_refresh_state();
    return DS5_CLASSIC_OK;
}

int ds5_classic_forget(void)
{
    uint8_t address[6];

    if (!g_ds5.status.initialized) {
        return DS5_CLASSIC_ERROR_NOT_READY;
    }
    g_ds5.reconnect_suppressed = 1U;
    g_ds5.status.reconnect_pending = 0U;
    if (!g_ds5.status.saved_address_valid) {
        (void)ds5_classic_load_saved_from_connection_manager();
    }
    if (!g_ds5.status.saved_address_valid) {
        return DS5_CLASSIC_ERROR_NO_SAVED_DEVICE;
    }

    if (g_ds5.status.connected || g_ds5.status.connecting) {
        (void)ds5_classic_disconnect();
    }
    ds5_classic_bd_to_bytes(&g_ds5.saved_bd, address);
#if defined(BSP_BT_CONNECTION_MANAGER)
    bt_cm_delete_bonded_devs_and_linkkey(address);
#else
    sc_unpair_req(bts2_task_get_app_task_id(), &g_ds5.saved_bd);
#endif
    g_ds5.status.saved_address_valid = 0U;
    g_ds5.injected_saved_valid = 0U;
    memset(g_ds5.status.saved_address, 0,
           sizeof(g_ds5.status.saved_address));
    return DS5_CLASSIC_OK;
}

void ds5_classic_poll(void)
{
    if (!g_ds5.status.initialized || !g_ds5.status.stack_ready) {
        return;
    }

    if (!g_ds5.security_mode_ready) {
        if (!g_ds5.security_mode_requested) {
            g_ds5.security_mode_requested = 1U;
            rt_kprintf("[ds5] request security mode 4\n");
            sc_set_secu_level_req(bts2_task_get_app_task_id(), 4U);
        }
        return;
    }

    if (!g_ds5.registration_requested) {
        uint8_t register_control = 0U;
        uint8_t register_interrupt = 0U;

        rt_enter_critical();
        g_ds5.registration_requested = 1U;
        if (!g_ds5.status.control_registered &&
            !g_ds5.control_registration_in_flight) {
            g_ds5.control_registration_in_flight = 1U;
            register_control = 1U;
        }
        if (!g_ds5.status.interrupt_registered &&
            !g_ds5.interrupt_registration_in_flight) {
            g_ds5.interrupt_registration_in_flight = 1U;
            register_interrupt = 1U;
        }
        rt_exit_critical();

        if ((register_control || register_interrupt) &&
            !g_ds5.sync_registration_requested) {
            g_ds5.sync_registration_requested = 1U;
            hcia_sync_reg_req(bts2_task_get_app_task_id(), 0U);
        }
        if (register_control) {
            bt_l2cap_profile_reg_req(bts2_task_get_app_task_id(),
                                     DS5_CLASSIC_HID_CONTROL_PSM,
                                     0U,
                                     DS5_CLASSIC_L2CAP_FLUSH_TIMEOUT);
        }
        if (register_interrupt) {
            bt_l2cap_profile_reg_req(bts2_task_get_app_task_id(),
                                     DS5_CLASSIC_HID_INTERRUPT_PSM,
                                     0U,
                                     DS5_CLASSIC_L2CAP_FLUSH_TIMEOUT);
        }
        if (register_control || register_interrupt) {
            return;
        }
    }

    if (g_ds5.registration_retry_pending &&
        (int32_t)(rt_tick_get() -
                  g_ds5.registration_retry_due_tick) >= 0) {
        uint8_t retry_control = 0U;
        uint8_t retry_interrupt = 0U;

        rt_enter_critical();
        g_ds5.registration_retry_pending = 0U;
        if (!g_ds5.status.control_registered &&
            !g_ds5.control_registration_in_flight) {
            g_ds5.control_registration_in_flight = 1U;
            retry_control = 1U;
        }
        if (!g_ds5.status.interrupt_registered &&
            !g_ds5.interrupt_registration_in_flight) {
            g_ds5.interrupt_registration_in_flight = 1U;
            retry_interrupt = 1U;
        }
        if (retry_control || retry_interrupt) {
            g_ds5.status.registration_retries++;
        }
        rt_exit_critical();

        if (retry_control) {
            bt_l2cap_profile_reg_req(bts2_task_get_app_task_id(),
                                     DS5_CLASSIC_HID_CONTROL_PSM,
                                     0U,
                                     DS5_CLASSIC_L2CAP_FLUSH_TIMEOUT);
        }
        if (retry_interrupt) {
            bt_l2cap_profile_reg_req(bts2_task_get_app_task_id(),
                                     DS5_CLASSIC_HID_INTERRUPT_PSM,
                                     0U,
                                     DS5_CLASSIC_L2CAP_FLUSH_TIMEOUT);
        }
        if (retry_control || retry_interrupt) {
            return;
        }
    }

    if (g_ds5.status.control_registered &&
        g_ds5.status.interrupt_registered &&
        g_ds5.saved_read_requested && !g_ds5.saved_read_in_flight) {
        g_ds5.saved_read_requested = 0U;
        g_ds5.saved_read_in_flight = 1U;
        if (!ds5_classic_load_saved_from_connection_manager()) {
            sc_rd_paired_dev_record_req(bts2_task_get_app_task_id());
        } else {
            g_ds5.saved_read_in_flight = 0U;
            if (g_ds5.connect_saved_waiting) {
                g_ds5.connect_saved_waiting = 0U;
                ds5_classic_schedule_connect(&g_ds5.saved_bd);
            } else if (ds5_classic_reconnect_allowed() &&
                       !g_ds5.status.connected &&
                       !g_ds5.status.connecting) {
                ds5_classic_schedule_reconnect(0U);
            }
        }
        return;
    }

    if (g_ds5.status.reconnect_pending) {
        if (!ds5_classic_reconnect_allowed()) {
            g_ds5.status.reconnect_pending = 0U;
        } else if ((int32_t)(rt_tick_get() - g_ds5.reconnect_due_tick) >= 0 &&
                   !g_ds5.status.connected && !g_ds5.status.connecting &&
                   !g_ds5.status.disconnecting &&
                   !g_ds5.status.discovering && !g_ds5.status.pairing &&
                   !g_ds5.want_pairing && !g_ds5.saved_read_in_flight &&
                   g_ds5.status.control_registered &&
                   g_ds5.status.interrupt_registered) {
            g_ds5.status.reconnect_attempts++;
            ds5_classic_schedule_connect(&g_ds5.saved_bd);
            return;
        }
    }

    if (g_ds5.stop_discovery_pending) {
        g_ds5.stop_discovery_pending = 0U;
        gap_esc_discov_req(bts2_task_get_app_task_id());
        return;
    }

    if (g_ds5.pair_request_pending && !g_ds5.status.discovering) {
        g_ds5.pair_request_pending = 0U;
        g_ds5.status.pairing = 1U;
        g_ds5.acl_pairing_flow = 1U;
        ds5_classic_set_active_bd(&g_ds5.candidate_bd);
        rt_kprintf("[ds5] acl pair start addr=%04x:%02x:%06lx\n",
                   g_ds5.candidate_bd.nap,
                   g_ds5.candidate_bd.uap,
                   (unsigned long)g_ds5.candidate_bd.lap);
        hcia_acl_open_req(&g_ds5.candidate_bd, NULL);
        ds5_classic_refresh_state();
        return;
    }

    if (g_ds5.auth_request_pending) {
        g_ds5.auth_request_pending = 0U;
        rt_kprintf("[ds5] request authentication\n");
        hcia_sm_auth_req(&g_ds5.candidate_bd, NULL);
        return;
    }

    if (g_ds5.encryption_request_pending) {
        g_ds5.encryption_request_pending = 0U;
        rt_kprintf("[ds5] request encryption\n");
        hcia_sm_encrypt_req(&g_ds5.candidate_bd, TRUE, NULL);
        return;
    }

    if (g_ds5.want_pairing && !g_ds5.status.discovering &&
        !g_ds5.status.pairing && !g_ds5.candidate_valid) {
        g_ds5.start_discovery_pending = 1U;
    }
    if (g_ds5.start_discovery_pending &&
        g_ds5.status.control_registered &&
        g_ds5.status.interrupt_registered) {
        BTS2S_CPL_FILTER filter;

        g_ds5.start_discovery_pending = 0U;
        memset(&filter, 0, sizeof(filter));
        filter.filter = BTS2_INQ_FILTER_CLEAR;
        filter.dev_mask_cls = 0U;
        g_ds5.status.discovering = 1U;
        rt_kprintf("[ds5] discovery start seconds=%u\n",
                   (unsigned int)DS5_CLASSIC_DISCOVERY_SECONDS);
        gap_discov_req(bts2_task_get_app_task_id(),
                       0U,
                       DS5_CLASSIC_DISCOVERY_SECONDS,
                       &filter,
                       TRUE);
        ds5_classic_refresh_state();
        return;
    }

    {
        BTS2S_BD_ADDR connect_bd;
        uint8_t connect_control = 0U;

        rt_enter_critical();
        if (g_ds5.control_connect_pending &&
            g_ds5.status.control_registered &&
            g_ds5.status.active_address_valid &&
            !g_ds5.control_connect_requested &&
            !g_ds5.status.disconnecting) {
            g_ds5.control_connect_pending = 0U;
            g_ds5.control_connect_requested = 1U;
            connect_bd = g_ds5.active_bd;
            connect_control = 1U;
        }
        rt_exit_critical();
        if (connect_control) {
            bt_l2cap_profile_conn_req(&connect_bd,
                                  DS5_CLASSIC_HID_CONTROL_PSM,
                                  DS5_CLASSIC_HID_CONTROL_PSM);
            return;
        }
    }

    {
        BTS2S_BD_ADDR connect_bd;
        uint8_t connect_interrupt = 0U;

        rt_enter_critical();
        if (g_ds5.interrupt_connect_pending &&
            g_ds5.status.interrupt_registered &&
            g_ds5.status.active_address_valid &&
            !g_ds5.interrupt_connect_requested &&
            !g_ds5.status.disconnecting) {
            g_ds5.interrupt_connect_pending = 0U;
            g_ds5.interrupt_connect_requested = 1U;
            connect_bd = g_ds5.active_bd;
            connect_interrupt = 1U;
        }
        rt_exit_critical();
        if (connect_interrupt) {
            bt_l2cap_profile_conn_req(&connect_bd,
                                  DS5_CLASSIC_HID_INTERRUPT_PSM,
                                  DS5_CLASSIC_HID_INTERRUPT_PSM);
            return;
        }
    }

    if (g_ds5.primer_output_pending && !g_ds5.output_busy &&
        !g_ds5.pending_output_valid && !g_ds5.pending_audio_valid) {
        uint8_t primer[142] = {0};

        primer[0] = 0x32U;
        primer[1] = 0x10U;
        primer[2] = 0x90U;
        primer[3] = 0x3fU;
        if (ds5_classic_send_raw_output_report(primer, sizeof(primer)) ==
            DS5_CLASSIC_OK) {
            g_ds5.primer_output_pending = 0U;
            g_ds5.neutral_output_pending = 1U;
            g_ds5.status.primer_reports++;
        }
    } else if (g_ds5.neutral_output_pending && !g_ds5.output_busy &&
        !g_ds5.pending_output_valid && !g_ds5.pending_audio_valid) {
        uint8_t neutral[DS5_CLASSIC_USB_OUTPUT_BODY_SIZE] = {0};
        if (ds5_classic_send_output_report(neutral, sizeof(neutral)) ==
            DS5_CLASSIC_OK) {
            g_ds5.neutral_output_pending = 0U;
        }
    }
    ds5_classic_try_send_pending_output();
    ds5_classic_poll_feature_timeout();
    ds5_classic_poll_dse_unlock();
    ds5_classic_try_send_feature_request();
    ds5_classic_refresh_state();
}

void ds5_classic_get_status(ds5_classic_status_t *status)
{
    if (status == 0) {
        return;
    }
    rt_enter_critical();
    *status = g_ds5.status;
    rt_exit_critical();
}

int ds5_classic_send_output_report(const uint8_t *usb_report, size_t len)
{
    const uint8_t *body;
    size_t body_len;
    uint8_t report[DS5_CLASSIC_BT_OUTPUT_PACKET_SIZE - 1U];

    if (usb_report == 0) {
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    if (len == DS5_CLASSIC_USB_OUTPUT_REPORT_SIZE &&
        usb_report[0] == 0x02U) {
        body = usb_report + 1U;
        body_len = DS5_CLASSIC_USB_OUTPUT_BODY_SIZE;
    } else if (len == DS5_CLASSIC_DSE_USB_OUTPUT_REPORT_SIZE &&
               usb_report[0] == 0x02U) {
        body = usb_report + 1U;
        body_len = DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE;
    } else if (len == DS5_CLASSIC_USB_OUTPUT_BODY_SIZE) {
        body = usb_report;
        body_len = DS5_CLASSIC_USB_OUTPUT_BODY_SIZE;
    } else if (len == DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE) {
        body = usb_report;
        body_len = DS5_CLASSIC_DSE_USB_OUTPUT_BODY_SIZE;
    } else {
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    rt_enter_critical();
    if (sf32lb52_ds5_output_state_update(&g_ds5.output_state, body,
                                          body_len) != 0) {
        rt_exit_critical();
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(report));
    report[0] = 0x31U;
    report[1] = (uint8_t)((g_ds5.output_sequence & 0x0fU) << 4);
    g_ds5.output_sequence = (uint8_t)((g_ds5.output_sequence + 1U) & 0x0fU);
    report[2] = 0x10U;
    (void)sf32lb52_ds5_output_state_copy(&g_ds5.output_state, report + 3U,
                                          SF32LB52_DS5_SET_STATE_SIZE);
    rt_exit_critical();
    return ds5_classic_send_raw_output_report(report, sizeof(report));
}

int ds5_classic_send_raw_output_report(const uint8_t *report, size_t len)
{
    uint16_t cid;

    if (report == 0 || len < 5U || len > DS5_CLASSIC_AUDIO_REPORT_SIZE) {
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    rt_enter_critical();
    if (!g_ds5.status.connected || g_ds5.status.interrupt_cid == 0U) {
        rt_exit_critical();
        return DS5_CLASSIC_ERROR_NOT_CONNECTED;
    }
    g_ds5.neutral_output_pending = 0U;
    if (g_ds5.output_busy) {
        g_ds5.status.output_busy++;
        if (report[0] == 0x32U) {
            rt_exit_critical();
            return DS5_CLASSIC_ERROR_BUSY;
        }
        if (report[0] == 0x36U || report[0] == 0x39U) {
            if (g_ds5.pending_audio_valid) {
                g_ds5.status.audio_output_dropped++;
            }
            memcpy(g_ds5.pending_audio_report, report, len);
            g_ds5.pending_audio_len = (uint16_t)len;
            g_ds5.pending_audio_valid = 1U;
        } else {
            memcpy(g_ds5.pending_output_report, report, len);
            g_ds5.pending_output_len = (uint16_t)len;
            g_ds5.pending_output_valid = 1U;
        }
        g_ds5.status.output_pending = 1U;
        g_ds5.status.output_queued++;
        rt_exit_critical();
        return DS5_CLASSIC_OK;
    }
    ds5_classic_prepare_raw_output_locked(report, (uint16_t)len);
    cid = g_ds5.status.interrupt_cid;
    rt_exit_critical();

    bt_l2cap_profile_send_data_req(cid,
                                   (char *)g_ds5.output_packet,
                                   g_ds5.output_packet_len);
    return DS5_CLASSIC_OK;
}

size_t ds5_classic_get_feature_report(uint8_t report_id,
                                      uint8_t *report,
                                      size_t report_capacity)
{
    size_t len;

    if (report == 0 || report_capacity == 0U) {
        return 0U;
    }
    rt_enter_critical();
    len = sf32lb52_ds5_feature_cache_get(&g_ds5.feature_cache,
                                         report_id,
                                         report,
                                         report_capacity);
    if (len != 0U) {
        g_ds5.status.feature_cache_hits++;
    } else {
        g_ds5.status.feature_cache_misses++;
        if (!(report_id >= 0x70U && report_id <= 0x7bU &&
              g_ds5.status.dse_unlock_phase != DS5_CLASSIC_DSE_UNLOCK_IDLE &&
              g_ds5.status.dse_unlock_phase != DS5_CLASSIC_DSE_UNLOCK_READY)) {
            ds5_classic_schedule_feature_request(report_id);
        }
    }
    rt_exit_critical();
    if (len == 0U) {
        len = sf32lb52_ds5_feature_build_fallback(report_id,
                                                  report,
                                                  report_capacity);
    }
    return len;
}

int ds5_classic_set_feature_report(uint8_t report_id,
                                   const uint8_t *data,
                                   size_t len)
{
    uint16_t cid;
    size_t packet_len;

    if (data == 0 || len == 0U) {
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    /* Match DS5Dongle/BL618: only Edge profile writes are safe to proxy. */
    if (report_id != 0x60U && report_id != 0x61U &&
        report_id != 0x62U && report_id != 0x80U) {
        return DS5_CLASSIC_ERROR_UNSUPPORTED;
    }
    rt_enter_critical();
    if (!g_ds5.status.connected || g_ds5.status.control_cid == 0U) {
        rt_exit_critical();
        return DS5_CLASSIC_ERROR_NOT_CONNECTED;
    }
    if (g_ds5.control_busy || g_ds5.awaiting_feature_response) {
        rt_exit_critical();
        return DS5_CLASSIC_ERROR_BUSY;
    }
    packet_len = sf32lb52_ds5_feature_build_set_request(
        report_id, data, len, g_ds5.control_packet,
        sizeof(g_ds5.control_packet));
    if (packet_len == 0U) {
        rt_exit_critical();
        return DS5_CLASSIC_ERROR_INVALID_ARGUMENT;
    }
    g_ds5.control_packet_len = (uint16_t)packet_len;
    g_ds5.control_busy = 1U;
    g_ds5.control_operation = DS5_CLASSIC_CONTROL_OP_SET_FEATURE;
    g_ds5.status.feature_set_reports++;
    cid = g_ds5.status.control_cid;
    rt_exit_critical();

    bt_l2cap_profile_send_data_req(cid,
                                   (char *)g_ds5.control_packet,
                                   g_ds5.control_packet_len);
    return DS5_CLASSIC_OK;
}

#else /* DS5_CLASSIC_SIFLI_AVAILABLE */

static ds5_classic_status_t g_unavailable_status = {
    .state = DS5_CLASSIC_STATE_UNAVAILABLE,
    .last_error = DS5_CLASSIC_ERROR_UNSUPPORTED,
};

int ds5_classic_init(ds5_classic_input_callback_t input_callback,
                     void *callback_context)
{
    (void)input_callback;
    (void)callback_context;
    g_unavailable_status.initialized = 1U;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

void ds5_classic_set_audio_input_callback(
    ds5_classic_audio_input_callback_t audio_callback,
    void *callback_context)
{
    (void)audio_callback;
    (void)callback_context;
}

int ds5_classic_set_saved_address(const uint8_t address[6])
{
    (void)address;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_set_auto_reconnect(int enabled)
{
    (void)enabled;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_start_pairing(void)
{
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_connect_saved(void)
{
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_disconnect(void)
{
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_forget(void)
{
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

void ds5_classic_poll(void)
{
}

void ds5_classic_get_status(ds5_classic_status_t *status)
{
    if (status != 0) {
        *status = g_unavailable_status;
    }
}

int ds5_classic_send_output_report(const uint8_t *usb_report, size_t len)
{
    (void)usb_report;
    (void)len;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

int ds5_classic_send_raw_output_report(const uint8_t *report, size_t len)
{
    (void)report;
    (void)len;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

size_t ds5_classic_get_feature_report(uint8_t report_id,
                                      uint8_t *report,
                                      size_t report_capacity)
{
    (void)report_id;
    (void)report;
    (void)report_capacity;
    return 0U;
}

int ds5_classic_set_feature_report(uint8_t report_id,
                                   const uint8_t *data,
                                   size_t len)
{
    (void)report_id;
    (void)data;
    (void)len;
    return DS5_CLASSIC_ERROR_UNSUPPORTED;
}

#endif /* DS5_CLASSIC_SIFLI_AVAILABLE */
