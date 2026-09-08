#include "ble_gatt.h"
#include "platform.h"
#include "sf32lb52_ns2_gatt.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("rtconfig.h")
#include "rtconfig.h"
#endif
#endif

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
#include <rtthread.h>

#include "bf0_ble_err.h"
#include "bf0_ble_gap.h"
#include "bf0_ble_common.h"
#include "bf0_sibles.h"
#include "ble_connection_manager.h"
#endif

#define BLE_GATT_INVALID_CONN_IDX 0xffU
#define BLE_GATT_MAX_INPUT_NOTIFY 4U
#define BLE_GATT_RECOVERY_MAX_RETRIES 3U
#define BLE_GATT_ACK_TIMEOUT_MS 800U
#define BLE_GATT_OPERATION_TIMEOUT_MS 800U
#define BLE_GATT_CONTROL_EVENT_COUNT 16U
#define BLE_GATT_CONTROL_EVENT_BUDGET 16U

typedef enum BleGattStage {
    BLE_GATT_STAGE_IDLE = 0,
    BLE_GATT_STAGE_SUBSCRIBE_ACK,
    BLE_GATT_STAGE_INITIALIZING,
    BLE_GATT_STAGE_SUBSCRIBE_INPUT,
    BLE_GATT_STAGE_READY,
} BleGattStage;

typedef enum BleGattRecoveryAction {
    BLE_GATT_RECOVERY_NONE = 0,
    BLE_GATT_RECOVERY_REGISTER,
    BLE_GATT_RECOVERY_ACK_CCCD,
    BLE_GATT_RECOVERY_INIT_COMMAND,
    BLE_GATT_RECOVERY_INPUT_CCCD,
    BLE_GATT_RECOVERY_DISCONNECT,
} BleGattRecoveryAction;

typedef struct {
    uint16_t value_handle;
    uint16_t cccd_handle;
    uint8_t kind;
} BleGattInputNotify;

typedef struct {
    uint16_t service_start;
    uint16_t service_end;
    uint16_t command_value_handle;
    uint16_t rumble_value_handle;
    uint16_t ack_value_handle;
    uint16_t input_value_handle;
    uint16_t ack_cccd_handle;
    uint16_t input_cccd_handle;
    BleGattInputNotify input_notify[BLE_GATT_MAX_INPUT_NOTIFY];
    uint8_t input_notify_count;
} BleGattDiscoveredService;

typedef enum BleGattControlEventType {
    BLE_GATT_CONTROL_POWER_ON = 0,
    BLE_GATT_CONTROL_SCAN_START_FAILED,
    BLE_GATT_CONTROL_SCAN_STOPPED,
    BLE_GATT_CONTROL_CONNECT_FAILED,
    BLE_GATT_CONTROL_CONNECTED,
    BLE_GATT_CONTROL_DISCONNECTED,
    BLE_GATT_CONTROL_MTU,
    BLE_GATT_CONTROL_SERVICE_SEARCH,
    BLE_GATT_CONTROL_REGISTER_RESPONSE,
    BLE_GATT_CONTROL_WRITE_RESPONSE,
    BLE_GATT_CONTROL_INIT_ACK,
} BleGattControlEventType;

typedef struct {
    BleGattControlEventType type;
    uint8_t conn_idx;
    uint8_t status;
    uint8_t reason;
    uint16_t mtu;
    uint8_t service_valid;
    uint8_t ack_len;
    uint8_t ack_data[16];
    BleGattDiscoveredService service;
} BleGattControlEvent;

static ble_gatt_input_callback_t g_input_callback;
static ble_gatt_status_t g_status = {
    .conn_idx = BLE_GATT_INVALID_CONN_IDX,
};

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
static uint8_t g_ble_enable_requested;
static uint8_t g_scan_requested;
static uint8_t g_remote_service_registered;
static uint8_t g_init_index;
static BleGattStage g_gatt_stage;
static uint16_t g_remote_service_handle;
static uint16_t g_command_value_handle;
static uint16_t g_rumble_value_handle;
static uint16_t g_ack_value_handle;
static uint16_t g_input_value_handle;
static uint16_t g_ack_cccd_handle;
static uint16_t g_input_cccd_handle;
static BleGattInputNotify g_input_notify[BLE_GATT_MAX_INPUT_NOTIFY];
static uint8_t g_input_notify_count;
static uint8_t g_target_valid;
static ble_gap_addr_t g_target_address;
static BleGattRecoveryAction g_recovery_action;
static uint8_t g_recovery_retry_count;
static uint32_t g_recovery_deadline_ms;
static uint8_t g_init_waiting_ack;
static uint32_t g_init_ack_deadline_ms;
static uint16_t g_registration_start_handle;
static uint16_t g_registration_end_handle;
static uint32_t g_scan_retry_deadline_ms;
static uint8_t g_remote_registration_pending;
static uint8_t g_cccd_write_pending;
static BleGattRecoveryAction g_cccd_write_action;
static BleGattControlEvent g_control_events[BLE_GATT_CONTROL_EVENT_COUNT];
static volatile uint8_t g_control_event_read;
static volatile uint8_t g_control_event_write;
static ble_gap_addr_t g_candidate_address;
static volatile uint8_t g_candidate_pending;
static int8_t g_candidate_rssi;

static const uint8_t g_ns2_service_uuid_le[NS2_GATT_UUID128_LEN] = {
    0xd0U, 0x7fU, 0xdfU, 0x09U, 0x8fU, 0x11U, 0x8fU, 0x82U,
    0xadU, 0x49U, 0xfeU, 0x89U, 0xbeU, 0xe9U, 0x7dU, 0xabU
};

static int ble_gatt_remote_event_handler(uint16_t event_id, uint8_t *data,
                                         uint16_t len);

static rt_base_t ble_gatt_critical_enter(void)
{
    return rt_hw_interrupt_disable();
}

static void ble_gatt_critical_exit(rt_base_t level)
{
    rt_hw_interrupt_enable(level);
}

static uint8_t ble_gatt_control_event_next(uint8_t index)
{
    index++;
    return index >= BLE_GATT_CONTROL_EVENT_COUNT ? 0U : index;
}

static int ble_gatt_control_event_push(const BleGattControlEvent *event)
{
    rt_base_t level;
    uint8_t next;

    if (event == 0) {
        return -1;
    }

    level = ble_gatt_critical_enter();
    next = ble_gatt_control_event_next(g_control_event_write);
    if (next == g_control_event_read) {
        g_status.control_event_drops++;
        if (event->type != BLE_GATT_CONTROL_DISCONNECTED) {
            ble_gatt_critical_exit(level);
            return -1;
        }

        /* A disconnect supersedes every queued operation for that link. */
        g_control_event_read = 0U;
        g_control_event_write = 0U;
        next = 1U;
    }

    g_control_events[g_control_event_write] = *event;
    g_control_event_write = next;
    ble_gatt_critical_exit(level);
    return 0;
}

static int ble_gatt_control_event_pop(BleGattControlEvent *event)
{
    rt_base_t level;

    if (event == 0) {
        return 0;
    }

    level = ble_gatt_critical_enter();
    if (g_control_event_read == g_control_event_write) {
        ble_gatt_critical_exit(level);
        return 0;
    }

    *event = g_control_events[g_control_event_read];
    g_control_event_read = ble_gatt_control_event_next(g_control_event_read);
    ble_gatt_critical_exit(level);
    return 1;
}

static void ble_gatt_control_event_simple(BleGattControlEventType type,
                                          uint8_t conn_idx,
                                          uint8_t status,
                                          uint8_t reason)
{
    BleGattControlEvent event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.conn_idx = conn_idx;
    event.status = status;
    event.reason = reason;
    (void)ble_gatt_control_event_push(&event);
}

static int ble_gatt_time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t ble_gatt_retry_delay_ms(uint8_t retry_count)
{
    static const uint16_t delays[BLE_GATT_RECOVERY_MAX_RETRIES] = {
        100U, 250U, 500U
    };

    if (retry_count == 0U) {
        return delays[0];
    }
    if (retry_count > BLE_GATT_RECOVERY_MAX_RETRIES) {
        retry_count = BLE_GATT_RECOVERY_MAX_RETRIES;
    }
    return delays[retry_count - 1U];
}

static void ble_gatt_recovery_clear(void)
{
    rt_base_t level;

    g_recovery_action = BLE_GATT_RECOVERY_NONE;
    g_recovery_retry_count = 0U;
    g_recovery_deadline_ms = 0U;
    g_init_waiting_ack = 0U;
    g_init_ack_deadline_ms = 0U;
    g_remote_registration_pending = 0U;
    g_cccd_write_pending = 0U;
    g_cccd_write_action = BLE_GATT_RECOVERY_NONE;
    level = ble_gatt_critical_enter();
    g_status.recovery_action = BLE_GATT_RECOVERY_NONE;
    g_status.recovery_retry_count = 0U;
    ble_gatt_critical_exit(level);
}

static void ble_gatt_recovery_prepare(BleGattRecoveryAction action)
{
    rt_base_t level;

    if (g_recovery_action != action) {
        g_recovery_action = action;
        g_recovery_retry_count = 0U;
        g_recovery_deadline_ms = 0U;
    }
    level = ble_gatt_critical_enter();
    g_status.recovery_action = (uint8_t)g_recovery_action;
    g_status.recovery_retry_count = g_recovery_retry_count;
    ble_gatt_critical_exit(level);
}

static void ble_gatt_recovery_fail(BleGattRecoveryAction action,
                                   const char *reason)
{
    uint32_t delay_ms;
    rt_base_t level;

    ble_gatt_recovery_prepare(action);
    level = ble_gatt_critical_enter();
    g_status.recovery_failures++;
    ble_gatt_critical_exit(level);
    g_init_waiting_ack = 0U;
    g_init_ack_deadline_ms = 0U;

    if (g_recovery_retry_count >= BLE_GATT_RECOVERY_MAX_RETRIES) {
        platform_log("ble", "recovery exhausted action=%u reason=%s; disconnecting",
                     (unsigned int)action,
                     reason != 0 ? reason : "-");
        g_recovery_action = BLE_GATT_RECOVERY_DISCONNECT;
        g_recovery_deadline_ms = platform_millis();
        level = ble_gatt_critical_enter();
        g_status.recovery_action = BLE_GATT_RECOVERY_DISCONNECT;
        ble_gatt_critical_exit(level);
        return;
    }

    g_recovery_retry_count++;
    delay_ms = ble_gatt_retry_delay_ms(g_recovery_retry_count);
    g_recovery_deadline_ms = platform_millis() + delay_ms;
    level = ble_gatt_critical_enter();
    g_status.recovery_retry_count = g_recovery_retry_count;
    ble_gatt_critical_exit(level);
    platform_log("ble", "recovery retry action=%u attempt=%u in %u ms reason=%s",
                 (unsigned int)action,
                 (unsigned int)g_recovery_retry_count,
                 (unsigned int)delay_ms,
                 reason != 0 ? reason : "-");
}

static void ble_gatt_recovery_disconnect_now(const char *reason)
{
    rt_base_t level;

    g_init_waiting_ack = 0U;
    g_init_ack_deadline_ms = 0U;
    g_cccd_write_pending = 0U;
    g_cccd_write_action = BLE_GATT_RECOVERY_NONE;
    g_remote_registration_pending = 0U;
    g_recovery_action = BLE_GATT_RECOVERY_DISCONNECT;
    g_recovery_deadline_ms = platform_millis();
    level = ble_gatt_critical_enter();
    g_status.recovery_failures++;
    g_status.recovery_action = BLE_GATT_RECOVERY_DISCONNECT;
    ble_gatt_critical_exit(level);
    platform_log("ble", "%s; disconnecting",
                 reason != 0 ? reason : "recovery timeout");
}

static int ble_gatt_write_remote(uint16_t handle, const uint8_t *data,
                                 uint16_t len, sibles_write_type_t type)
{
    sibles_write_remote_value_t value;
    int8_t ret;
    rt_base_t level;
    uint8_t conn_idx;
    uint16_t remote_service_handle;

    level = ble_gatt_critical_enter();
    conn_idx = g_status.conn_idx;
    remote_service_handle = g_remote_service_handle;
    if (data == 0 || len == 0U || g_remote_service_registered == 0U ||
        g_status.connected == 0U || handle == 0U) {
        g_status.writes_failed++;
        g_status.last_write_ret = -1;
        g_status.last_write_handle = handle;
        ble_gatt_critical_exit(level);
        return -1;
    }
    ble_gatt_critical_exit(level);

    memset(&value, 0, sizeof(value));
    value.write_type = type;
    value.handle = handle;
    value.len = len;
    value.value = (uint8_t *)data;

    ret = sibles_write_remote_value(remote_service_handle,
                                    conn_idx,
                                    &value);
    level = ble_gatt_critical_enter();
    g_status.last_write_ret = ret;
    g_status.last_write_handle = handle;
    if (ret == SIBLES_WRITE_NO_ERR) {
        g_status.writes_ok++;
        ble_gatt_critical_exit(level);
        return 0;
    }

    g_status.writes_failed++;
    ble_gatt_critical_exit(level);
    return -1;
}

static int ble_gatt_write_cccd(uint16_t handle)
{
    static const uint8_t notify_on[2] = {0x01U, 0x00U};

    return ble_gatt_write_remote(handle, notify_on, sizeof(notify_on), SIBLES_WRITE);
}

static void ble_gatt_start_input_subscription(void);

static void ble_gatt_clear_input_notify(void)
{
    memset(g_input_notify, 0, sizeof(g_input_notify));
    g_input_notify_count = 0U;
    g_status.input_notify_count = 0U;
    g_status.last_notify_kind = NS2_GATT_INPUT_KIND_UNKNOWN;
    g_status.last_notify_handle = 0U;
}

static void ble_gatt_add_input_notify(uint16_t value_handle,
                                      uint16_t cccd_handle,
                                      uint8_t kind)
{
    uint8_t i;

    if (value_handle == 0U || cccd_handle == 0U) {
        return;
    }

    for (i = 0U; i < g_input_notify_count; i++) {
        if (g_input_notify[i].value_handle == value_handle) {
            g_input_notify[i].cccd_handle = cccd_handle;
            g_input_notify[i].kind = kind;
            return;
        }
    }

    if (g_input_notify_count >= BLE_GATT_MAX_INPUT_NOTIFY) {
        return;
    }

    g_input_notify[g_input_notify_count].value_handle = value_handle;
    g_input_notify[g_input_notify_count].cccd_handle = cccd_handle;
    g_input_notify[g_input_notify_count].kind = kind;
    g_input_notify_count++;
    g_status.input_notify_count = g_input_notify_count;
}

static const BleGattInputNotify *ble_gatt_find_input_notify(uint16_t value_handle)
{
    uint8_t i;

    for (i = 0U; i < g_input_notify_count; i++) {
        if (g_input_notify[i].value_handle == value_handle) {
            return &g_input_notify[i];
        }
    }
    return 0;
}

static void ble_gatt_send_current_init_command(void)
{
    uint8_t count;
    const ns2_gatt_init_command_t *command;

    if (g_command_value_handle == 0U) {
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_INIT_COMMAND,
                               "missing command handle");
        return;
    }

    count = ns2_gatt_init_command_count();
    if (g_init_index >= count) {
        ble_gatt_recovery_clear();
        platform_log("ble", "init complete; enabling input notify");
        ble_gatt_start_input_subscription();
        return;
    }

    command = ns2_gatt_init_command(g_init_index);
    if (command == 0) {
        g_init_index++;
        ble_gatt_recovery_clear();
        ble_gatt_send_current_init_command();
        return;
    }

    ble_gatt_recovery_prepare(BLE_GATT_RECOVERY_INIT_COMMAND);
    if (ble_gatt_write_remote(g_command_value_handle,
                              command->data,
                              command->len,
                              SIBLES_WRITE_WITHOUT_RSP) == 0) {
        g_recovery_deadline_ms = 0U;
        g_init_waiting_ack = 1U;
        g_init_ack_deadline_ms = platform_millis() + BLE_GATT_ACK_TIMEOUT_MS;
        platform_log("ble", "init write %u/%u %s len=%u",
                     (unsigned int)(g_init_index + 1U),
                     (unsigned int)count,
                     command->name,
                     (unsigned int)command->len);
    } else {
        platform_log("ble", "init write failed %u/%u %s",
                     (unsigned int)(g_init_index + 1U),
                     (unsigned int)count,
                     command->name);
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_INIT_COMMAND,
                               "init write immediate failure");
    }
}

static void ble_gatt_finish_remote_registration(void)
{
    g_gatt_stage = BLE_GATT_STAGE_SUBSCRIBE_ACK;
    ble_gatt_recovery_prepare(BLE_GATT_RECOVERY_ACK_CCCD);
    if (ble_gatt_write_cccd(g_ack_cccd_handle) != 0) {
        platform_log("ble", "ACK subscribe failed cccd=0x%04x",
                     (unsigned int)g_ack_cccd_handle);
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_ACK_CCCD,
                               "ACK CCCD immediate failure");
        return;
    }

    /* SIBLES_WRITE is asynchronous.  Advance only after its response. */
    g_cccd_write_pending = 1U;
    g_cccd_write_action = BLE_GATT_RECOVERY_ACK_CCCD;
    g_recovery_deadline_ms = platform_millis() + BLE_GATT_OPERATION_TIMEOUT_MS;
}

static void ble_gatt_start_input_subscription(void)
{
    g_gatt_stage = BLE_GATT_STAGE_SUBSCRIBE_INPUT;
    ble_gatt_recovery_prepare(BLE_GATT_RECOVERY_INPUT_CCCD);
    if (ble_gatt_write_cccd(g_input_cccd_handle) != 0) {
        platform_log("ble", "input subscribe failed cccd=0x%04x",
                     (unsigned int)g_input_cccd_handle);
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_INPUT_CCCD,
                               "input CCCD immediate failure");
        return;
    }

    g_cccd_write_pending = 1U;
    g_cccd_write_action = BLE_GATT_RECOVERY_INPUT_CCCD;
    g_recovery_deadline_ms = platform_millis() + BLE_GATT_OPERATION_TIMEOUT_MS;
    platform_log("ble", "input subscribe requested value=0x%04x cccd=0x%04x",
                 (unsigned int)g_input_value_handle,
                 (unsigned int)g_input_cccd_handle);
}

static void ble_gatt_unregister_remote_service(void)
{
    rt_base_t level;
    uint8_t conn_idx;
    uint16_t start_handle;
    uint16_t end_handle;

    level = ble_gatt_critical_enter();
    if (g_remote_service_registered == 0U) {
        ble_gatt_critical_exit(level);
        return;
    }
    conn_idx = g_status.conn_idx;
    start_handle = g_registration_start_handle;
    end_handle = g_registration_end_handle;
    ble_gatt_critical_exit(level);

    sibles_unregister_remote_svc(conn_idx,
                                 start_handle,
                                 end_handle,
                                 ble_gatt_remote_event_handler);
    level = ble_gatt_critical_enter();
    g_remote_service_registered = 0U;
    g_remote_service_handle = 0U;
    g_remote_registration_pending = 0U;
    g_status.remote_handle = 0U;
    ble_gatt_critical_exit(level);
}

static int ble_gatt_attempt_remote_registration(void)
{
    uint16_t remote_handle;
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t conn_idx;
    rt_base_t level;

    level = ble_gatt_critical_enter();
    conn_idx = g_status.conn_idx;
    start_handle = g_registration_start_handle;
    end_handle = g_registration_end_handle;
    if (g_status.connected == 0U || conn_idx == BLE_GATT_INVALID_CONN_IDX) {
        ble_gatt_critical_exit(level);
        return -1;
    }
    ble_gatt_critical_exit(level);

    ble_gatt_recovery_prepare(BLE_GATT_RECOVERY_REGISTER);
    remote_handle = sibles_register_remote_svc(conn_idx,
                                                start_handle,
                                                end_handle,
                                                ble_gatt_remote_event_handler);
    if (remote_handle == SIBLES_ERROR_REMOTE_HANDLE) {
        level = ble_gatt_critical_enter();
        g_remote_service_registered = 0U;
        g_remote_service_handle = 0U;
        g_remote_registration_pending = 0U;
        g_status.remote_handle = 0U;
        ble_gatt_critical_exit(level);
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_REGISTER,
                               "remote service handle unavailable");
        return -1;
    }

    level = ble_gatt_critical_enter();
    g_remote_service_handle = remote_handle;
    g_remote_service_registered = 1U;
    g_remote_registration_pending = 1U;
    g_status.remote_handle = g_remote_service_handle;
    ble_gatt_critical_exit(level);
    g_recovery_deadline_ms = platform_millis() + BLE_GATT_OPERATION_TIMEOUT_MS;
    platform_log("ble", "remote svc registration requested hdl=%u start=0x%04x end=0x%04x",
                 (unsigned int)g_remote_service_handle,
                 (unsigned int)start_handle,
                 (unsigned int)end_handle);
    return 0;
}

static void ble_gatt_begin_remote_registration(uint16_t start_handle,
                                               uint16_t end_handle)
{
    rt_base_t level = ble_gatt_critical_enter();
    g_registration_start_handle = start_handle;
    g_registration_end_handle = end_handle;
    g_status.service_start = start_handle;
    g_status.service_end = end_handle;
    ble_gatt_critical_exit(level);
    ble_gatt_recovery_clear();
    (void)ble_gatt_attempt_remote_registration();
}

static void ble_gatt_register_known_ns2_handles(uint8_t conn_idx)
{
    rt_base_t level;

    if (conn_idx != g_status.conn_idx || g_status.connected == 0U) {
        return;
    }

    level = ble_gatt_critical_enter();
    g_status.service_start = 0x0001U;
    g_status.service_end = 0xffffU;
    g_command_value_handle = NS2_GATT_KNOWN_COMMAND_VALUE_HANDLE;
    g_rumble_value_handle = NS2_GATT_KNOWN_RUMBLE_VALUE_HANDLE;
    g_ack_value_handle = NS2_GATT_KNOWN_ACK_VALUE_HANDLE;
    g_input_value_handle = NS2_GATT_KNOWN_INPUT_FD2_VALUE_HANDLE;
    g_ack_cccd_handle = (uint16_t)(NS2_GATT_KNOWN_ACK_VALUE_HANDLE + 1U);
    g_input_cccd_handle = (uint16_t)(NS2_GATT_KNOWN_INPUT_FD2_VALUE_HANDLE + 1U);
    ble_gatt_clear_input_notify();
    ble_gatt_add_input_notify(g_input_value_handle,
                              g_input_cccd_handle,
                              NS2_GATT_INPUT_KIND_FD2);
    g_status.command_handle = g_command_value_handle;
    g_status.rumble_handle = g_rumble_value_handle;
    g_status.ack_cccd_handle = g_ack_cccd_handle;
    g_status.input_cccd_handle = g_input_cccd_handle;
    ble_gatt_critical_exit(level);

    platform_log("ble", "known NS2 handles svc=%u rumble=0x%04x cmd=0x%04x ack_cccd=0x%04x input_cccd=0x%04x",
                 (unsigned int)g_status.remote_handle,
                 (unsigned int)g_rumble_value_handle,
                 (unsigned int)g_command_value_handle,
                 (unsigned int)g_ack_cccd_handle,
                 (unsigned int)g_input_cccd_handle);
    ble_gatt_begin_remote_registration(0x0001U, 0xffffU);
}

static void ble_gatt_discovered_add_input(BleGattDiscoveredService *service,
                                          uint16_t value_handle,
                                          uint16_t cccd_handle,
                                          uint8_t kind)
{
    uint8_t i;

    if (service == 0 || value_handle == 0U || cccd_handle == 0U) {
        return;
    }

    for (i = 0U; i < service->input_notify_count; i++) {
        if (service->input_notify[i].value_handle == value_handle) {
            service->input_notify[i].cccd_handle = cccd_handle;
            service->input_notify[i].kind = kind;
            return;
        }
    }

    if (service->input_notify_count >= BLE_GATT_MAX_INPUT_NOTIFY) {
        return;
    }

    service->input_notify[service->input_notify_count].value_handle = value_handle;
    service->input_notify[service->input_notify_count].cccd_handle = cccd_handle;
    service->input_notify[service->input_notify_count].kind = kind;
    service->input_notify_count++;
}

static void ble_gatt_parse_searched_service(const sibles_svc_remote_svc_t *svc,
                                             BleGattDiscoveredService *service)
{
    uint8_t i;
    sibles_svc_search_char_t *chara;

    if (service == 0) {
        return;
    }

    memset(service, 0, sizeof(*service));
    if (svc == 0 || svc->att_db == 0) {
        return;
    }

    service->service_start = svc->hdl_start;
    service->service_end = svc->hdl_end;

    chara = (sibles_svc_search_char_t *)svc->att_db;
    for (i = 0U; i < svc->char_count; i++) {
        uint16_t offset;
        uint16_t cccd;
        ns2_gatt_role_t role = ns2_gatt_classify_sifli_uuid(chara->uuid_len,
                                                            chara->uuid);

        cccd = sibles_descriptor_handle_find(chara, 0x2902U);
        if (cccd == 0U && chara->desc_count > 0U) {
            cccd = chara->desc[0].attr_hdl;
        }

        switch (role) {
        case NS2_GATT_ROLE_COMMAND:
            service->command_value_handle = chara->pointer_hdl;
            break;
        case NS2_GATT_ROLE_RUMBLE:
            service->rumble_value_handle = chara->pointer_hdl;
            break;
        case NS2_GATT_ROLE_ACK_NOTIFY:
            service->ack_value_handle = chara->pointer_hdl;
            service->ack_cccd_handle = cccd;
            break;
        case NS2_GATT_ROLE_INPUT_NOTIFY:
        {
            uint8_t kind = ns2_gatt_input_kind_sifli_uuid(chara->uuid_len,
                                                          chara->uuid);
            ble_gatt_discovered_add_input(service,
                                          chara->pointer_hdl,
                                          cccd,
                                          kind);
            if (kind == NS2_GATT_INPUT_KIND_FD2 ||
                service->input_value_handle == 0U) {
                service->input_value_handle = chara->pointer_hdl;
                service->input_cccd_handle = cccd;
            }
            break;
        }
        default:
            break;
        }

        offset = (uint16_t)(sizeof(sibles_svc_search_char_t) +
                            (uint16_t)chara->desc_count *
                                sizeof(struct sibles_disc_char_desc_ind));
        chara = (sibles_svc_search_char_t *)((uint8_t *)chara + offset);
    }
}

static void ble_gatt_apply_discovered_service(
    const BleGattDiscoveredService *service)
{
    rt_base_t level;

    if (service == 0) {
        return;
    }

    level = ble_gatt_critical_enter();
    g_command_value_handle = service->command_value_handle;
    g_rumble_value_handle = service->rumble_value_handle;
    g_ack_value_handle = service->ack_value_handle;
    g_input_value_handle = service->input_value_handle;
    g_ack_cccd_handle = service->ack_cccd_handle;
    g_input_cccd_handle = service->input_cccd_handle;
    memset(g_input_notify, 0, sizeof(g_input_notify));
    memcpy(g_input_notify, service->input_notify, sizeof(g_input_notify));
    g_input_notify_count = service->input_notify_count;
    g_status.service_start = service->service_start;
    g_status.service_end = service->service_end;
    g_status.command_handle = g_command_value_handle;
    g_status.rumble_handle = g_rumble_value_handle;
    g_status.ack_cccd_handle = g_ack_cccd_handle;
    g_status.input_cccd_handle = g_input_cccd_handle;
    g_status.input_notify_count = g_input_notify_count;
    g_status.last_notify_kind = NS2_GATT_INPUT_KIND_UNKNOWN;
    g_status.last_notify_handle = 0U;
    ble_gatt_critical_exit(level);
}

static int ble_gatt_connect_candidate(const ble_gap_addr_t *addr)
{
    ble_gap_connection_create_param_t conn_param;
    uint8_t ret;

    if (addr == 0 || g_status.connecting || g_status.connected) {
        return -1;
    }

    memset(&conn_param, 0, sizeof(conn_param));
    conn_param.own_addr_type = GAPM_STATIC_ADDR;
    conn_param.type = GAPM_INIT_TYPE_DIRECT_CONN_EST;
    conn_param.conn_to = 500U;
    conn_param.conn_param_1m.scan_intv = 0x10U;
    conn_param.conn_param_1m.scan_wd = 0x10U;
    conn_param.conn_param_1m.conn_intv_min = 6U;
    conn_param.conn_param_1m.conn_intv_max = 6U;
    conn_param.conn_param_1m.conn_latency = 0U;
    conn_param.conn_param_1m.supervision_to = 100U;
    conn_param.conn_param_1m.ce_len_min = 0U;
    conn_param.conn_param_1m.ce_len_max = 48U;
    memcpy(&conn_param.peer_addr, addr, sizeof(conn_param.peer_addr));

    if (g_status.scanning) {
        (void)ble_gap_scan_stop();
        g_status.scanning = 0U;
    }

    ret = ble_gap_create_connection(&conn_param);
    if (ret != HL_ERR_NO_ERROR) {
        g_status.connect_failures++;
        g_status.last_connect_status = ret;
        platform_log("ble", "connect request failed ret=%u", (unsigned int)ret);
        return -1;
    }

    g_status.connect_attempts++;
    g_status.last_connect_status = 0U;
    g_status.connecting = 1U;
    platform_log("ble", "connect requested type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x",
                 (unsigned int)addr->addr_type,
                 (unsigned int)addr->addr.addr[5],
                 (unsigned int)addr->addr.addr[4],
                 (unsigned int)addr->addr.addr[3],
                 (unsigned int)addr->addr.addr[2],
                 (unsigned int)addr->addr.addr[1],
                 (unsigned int)addr->addr.addr[0]);
    return 0;
}

static int ble_gatt_start_scan_now(void)
{
    ble_gap_scan_start_t scan_param;
    uint8_t ret;

    memset(&scan_param, 0, sizeof(scan_param));
    scan_param.own_addr_type = GAPM_STATIC_ADDR;
    scan_param.type = GAPM_SCAN_TYPE_OBSERVER;
    scan_param.dup_filt_pol = 1U;
    scan_param.scan_param_1m.scan_intv = 0x60U;
    scan_param.scan_param_1m.scan_wd = 0x30U;
    scan_param.duration = 0U;
    scan_param.period = 0U;

    ret = ble_gap_scan_start(&scan_param);
    if (ret != HL_ERR_NO_ERROR) {
        platform_log("ble", "scan start failed ret=%u", (unsigned int)ret);
        g_scan_retry_deadline_ms = platform_millis() + 500U;
        return -1;
    }

    g_scan_retry_deadline_ms = 0U;
    g_status.scanning = 1U;
    platform_log("ble", "scan start requested");
    return 0;
}

static void ble_gatt_on_connected(uint8_t conn_idx)
{
    rt_base_t level;

    ble_gatt_recovery_clear();
    level = ble_gatt_critical_enter();
    g_status.connected = 0U;
    g_status.connecting = 0U;
    g_status.scanning = 0U;
    g_scan_retry_deadline_ms = 0U;
    g_remote_service_handle = 0U;
    g_remote_service_registered = 0U;
    g_registration_start_handle = 0U;
    g_registration_end_handle = 0U;
    g_command_value_handle = 0U;
    g_rumble_value_handle = 0U;
    g_ack_value_handle = 0U;
    g_input_value_handle = 0U;
    g_ack_cccd_handle = 0U;
    g_input_cccd_handle = 0U;
    ble_gatt_clear_input_notify();
    g_init_index = 0U;
    g_gatt_stage = BLE_GATT_STAGE_IDLE;
    g_status.remote_handle = 0U;
    g_status.service_start = 0U;
    g_status.service_end = 0U;
    g_status.command_handle = 0U;
    g_status.rumble_handle = 0U;
    g_status.ack_cccd_handle = 0U;
    g_status.input_cccd_handle = 0U;
    g_status.mtu = 0U;
    g_status.conn_idx = conn_idx;
    g_status.connected = 1U;
    ble_gatt_critical_exit(level);
    (void)sibles_exchange_mtu(conn_idx);
    if (sibles_search_service(conn_idx,
                              NS2_GATT_UUID128_LEN,
                              (uint8_t *)g_ns2_service_uuid_le) != HL_ERR_NO_ERROR) {
        platform_log("ble", "service search start failed; using known handles");
        ble_gatt_register_known_ns2_handles(conn_idx);
    }
    platform_log("ble", "connected conn_idx=%u", (unsigned int)conn_idx);
}

static void ble_gatt_on_disconnected(uint8_t conn_idx, uint8_t reason)
{
    rt_base_t level;
    uint8_t matches;

    level = ble_gatt_critical_enter();
    matches = g_status.conn_idx == conn_idx;
    if (matches) {
        /* Publish disconnected before invalidating conn_idx. */
        g_status.connected = 0U;
        g_status.connecting = 0U;
    }
    ble_gatt_critical_exit(level);

    if (matches) {
        g_status.disconnects++;
        g_status.last_disconnect_reason = reason;
        ble_gatt_unregister_remote_service();
        ble_gatt_recovery_clear();
        level = ble_gatt_critical_enter();
        g_status.mtu = 0U;
        g_remote_service_handle = 0U;
        g_remote_service_registered = 0U;
        g_registration_start_handle = 0U;
        g_registration_end_handle = 0U;
        g_command_value_handle = 0U;
        g_rumble_value_handle = 0U;
        g_ack_value_handle = 0U;
        g_input_value_handle = 0U;
        g_ack_cccd_handle = 0U;
        g_input_cccd_handle = 0U;
        ble_gatt_clear_input_notify();
        g_init_index = 0U;
        g_gatt_stage = BLE_GATT_STAGE_IDLE;
        g_status.remote_handle = 0U;
        g_status.service_start = 0U;
        g_status.service_end = 0U;
        g_status.command_handle = 0U;
        g_status.rumble_handle = 0U;
        g_status.ack_cccd_handle = 0U;
        g_status.input_cccd_handle = 0U;
        g_status.conn_idx = BLE_GATT_INVALID_CONN_IDX;
        ble_gatt_critical_exit(level);
    }
    platform_log("ble", "disconnected conn_idx=%u reason=%u",
                 (unsigned int)conn_idx,
                 (unsigned int)reason);
    if (g_scan_requested) {
        (void)ble_gatt_start_scan_now();
    }
}

static void ble_gatt_process_candidate(void)
{
    ble_gap_addr_t address;
    rt_base_t level;
    int8_t rssi;
    uint8_t pending;
    uint8_t can_connect;

    level = ble_gatt_critical_enter();
    pending = g_candidate_pending;
    if (pending != 0U) {
        address = g_candidate_address;
        rssi = g_candidate_rssi;
        g_candidate_pending = 0U;
    } else {
        memset(&address, 0, sizeof(address));
        rssi = 0;
    }
    can_connect = g_status.connected == 0U && g_status.connecting == 0U;
    ble_gatt_critical_exit(level);

    if (pending == 0U || can_connect == 0U) {
        return;
    }

    platform_log("ble", "NS2 candidate rssi=%d addr=%02x:%02x:%02x:%02x:%02x:%02x",
                 (int)rssi,
                 (unsigned int)address.addr.addr[5],
                 (unsigned int)address.addr.addr[4],
                 (unsigned int)address.addr.addr[3],
                 (unsigned int)address.addr.addr[2],
                 (unsigned int)address.addr.addr[1],
                 (unsigned int)address.addr.addr[0]);
    (void)ble_gatt_connect_candidate(&address);
}

static int ble_gatt_init_ack_matches(const BleGattControlEvent *event)
{
    const ns2_gatt_init_command_t *command;

    if (event == 0 || event->ack_len < 4U) {
        return 0;
    }

    command = ns2_gatt_init_command(g_init_index);
    if (command == 0 || command->data == 0 || command->len < 4U ||
        event->ack_data[0] != command->data[0] ||
        event->ack_data[3] != command->data[3]) {
        return 0;
    }

    /* FLASH_READ commands share opcode/subcommand.  Their ACK echoes the
     * requested address at bytes 12..15, which distinguishes left/right
     * calibration and rejects the second ACK from a timed-out retransmit. */
    if (command->data[0] == 0x02U && command->len >= 16U) {
        return event->ack_len >= 16U &&
               memcmp(&event->ack_data[12], &command->data[12], 4U) == 0;
    }

    return 1;
}

static void ble_gatt_process_control_event(const BleGattControlEvent *event)
{
    rt_base_t level;

    if (event == 0) {
        return;
    }

    switch (event->type) {
    case BLE_GATT_CONTROL_POWER_ON:
    {
        bd_addr_t local_addr;
        uint8_t console_mac[6];
        uint8_t i;

        level = ble_gatt_critical_enter();
        g_status.powered = 1U;
        ble_gatt_critical_exit(level);
        if (ble_get_public_address(&local_addr) == HL_ERR_NO_ERROR) {
            for (i = 0U; i < 6U; i++) {
                console_mac[i] = local_addr.addr[5U - i];
            }
            ns2_gatt_set_console_mac(console_mac);
            level = ble_gatt_critical_enter();
            memcpy(g_status.local_addr, local_addr.addr, 6U);
            g_status.local_addr_valid = 1U;
            ble_gatt_critical_exit(level);
            platform_log("ble", "NS2 console MAC=%02x:%02x:%02x:%02x:%02x:%02x",
                         (unsigned int)console_mac[0],
                         (unsigned int)console_mac[1],
                         (unsigned int)console_mac[2],
                         (unsigned int)console_mac[3],
                         (unsigned int)console_mac[4],
                         (unsigned int)console_mac[5]);
        }
        platform_log("ble", "powered on");
        if (g_scan_requested) {
            (void)ble_gatt_start_scan_now();
        }
        break;
    }

    case BLE_GATT_CONTROL_SCAN_START_FAILED:
        level = ble_gatt_critical_enter();
        g_status.scanning = 0U;
        ble_gatt_critical_exit(level);
        g_scan_retry_deadline_ms = platform_millis() + 500U;
        platform_log("ble", "scan start cnf status=%u",
                     (unsigned int)event->status);
        break;

    case BLE_GATT_CONTROL_SCAN_STOPPED:
        level = ble_gatt_critical_enter();
        g_status.scanning = 0U;
        ble_gatt_critical_exit(level);
        platform_log("ble", "scan stopped");
        break;

    case BLE_GATT_CONTROL_CONNECT_FAILED:
        level = ble_gatt_critical_enter();
        g_status.connecting = 0U;
        g_status.connect_failures++;
        g_status.last_connect_status = event->status;
        ble_gatt_critical_exit(level);
        g_scan_retry_deadline_ms = platform_millis() + 250U;
        platform_log("ble", "connect cnf failed status=%u",
                     (unsigned int)event->status);
        break;

    case BLE_GATT_CONTROL_CONNECTED:
        ble_gatt_on_connected(event->conn_idx);
        break;

    case BLE_GATT_CONTROL_DISCONNECTED:
        ble_gatt_on_disconnected(event->conn_idx, event->reason);
        break;

    case BLE_GATT_CONTROL_MTU:
        level = ble_gatt_critical_enter();
        if (event->conn_idx == g_status.conn_idx && g_status.connected != 0U) {
            g_status.mtu = event->mtu;
        }
        ble_gatt_critical_exit(level);
        platform_log("ble", "mtu=%u", (unsigned int)event->mtu);
        break;

    case BLE_GATT_CONTROL_SERVICE_SEARCH:
        if (event->conn_idx != g_status.conn_idx || g_status.connected == 0U) {
            break;
        }
        if (event->status == HL_ERR_NO_ERROR && event->service_valid != 0U &&
            event->service.command_value_handle != 0U &&
            event->service.rumble_value_handle != 0U &&
            event->service.ack_value_handle != 0U &&
            event->service.ack_cccd_handle != 0U &&
            event->service.input_value_handle != 0U &&
            event->service.input_cccd_handle != 0U) {
            ble_gatt_apply_discovered_service(&event->service);
            platform_log("ble", "discovered svc start=0x%04x end=0x%04x rumble=0x%04x cmd=0x%04x ack_cccd=0x%04x input_cccd=0x%04x",
                         (unsigned int)event->service.service_start,
                         (unsigned int)event->service.service_end,
                         (unsigned int)event->service.rumble_value_handle,
                         (unsigned int)event->service.command_value_handle,
                         (unsigned int)event->service.ack_cccd_handle,
                         (unsigned int)event->service.input_cccd_handle);
            ble_gatt_begin_remote_registration(event->service.service_start,
                                               event->service.service_end);
        } else {
            platform_log("ble", "service search failed/incomplete result=%u; using known handles",
                         (unsigned int)event->status);
            ble_gatt_register_known_ns2_handles(event->conn_idx);
        }
        break;

    case BLE_GATT_CONTROL_REGISTER_RESPONSE:
        platform_log("ble", "remote svc register conn_idx=%u status=%u",
                     (unsigned int)event->conn_idx,
                     (unsigned int)event->status);
        if (event->conn_idx != g_status.conn_idx || g_status.connected == 0U) {
            break;
        }
        if (g_remote_registration_pending == 0U) {
            platform_log("ble", "ignored stale remote svc registration response");
            break;
        }
        if (event->status == HL_ERR_NO_ERROR) {
            ble_gatt_recovery_clear();
            ble_gatt_finish_remote_registration();
        } else {
            ble_gatt_unregister_remote_service();
            ble_gatt_recovery_fail(BLE_GATT_RECOVERY_REGISTER,
                                   "remote service registration rejected");
        }
        break;

    case BLE_GATT_CONTROL_WRITE_RESPONSE:
        level = ble_gatt_critical_enter();
        if (event->status == HL_ERR_NO_ERROR) {
            g_status.writes_ok++;
        } else {
            g_status.writes_failed++;
        }
        ble_gatt_critical_exit(level);

        if (event->conn_idx != g_status.conn_idx ||
            g_status.connected == 0U || g_cccd_write_pending == 0U) {
            break;
        }
        {
            BleGattRecoveryAction cccd_action = g_cccd_write_action;
            g_cccd_write_pending = 0U;
            g_cccd_write_action = BLE_GATT_RECOVERY_NONE;
            g_recovery_deadline_ms = 0U;

            if (event->status != HL_ERR_NO_ERROR) {
                ble_gatt_recovery_fail(cccd_action,
                                       "CCCD asynchronous failure");
                break;
            }

            ble_gatt_recovery_clear();
            if (cccd_action == BLE_GATT_RECOVERY_ACK_CCCD) {
                g_gatt_stage = BLE_GATT_STAGE_INITIALIZING;
                g_init_index = 0U;
                ble_gatt_send_current_init_command();
            } else if (cccd_action == BLE_GATT_RECOVERY_INPUT_CCCD) {
                if (g_input_notify_count == 0U) {
                    level = ble_gatt_critical_enter();
                    ble_gatt_add_input_notify(g_input_value_handle,
                                              g_input_cccd_handle,
                                              NS2_GATT_INPUT_KIND_FD2);
                    ble_gatt_critical_exit(level);
                }
                g_gatt_stage = BLE_GATT_STAGE_READY;
                platform_log("ble", "input notify ready cccd=0x%04x",
                             (unsigned int)g_input_cccd_handle);
            }
        }
        break;

    case BLE_GATT_CONTROL_INIT_ACK:
        if (event->conn_idx != g_status.conn_idx || g_status.connected == 0U ||
            g_gatt_stage != BLE_GATT_STAGE_INITIALIZING ||
            g_init_waiting_ack == 0U) {
            break;
        }
        if (!ble_gatt_init_ack_matches(event)) {
            platform_log("ble", "ignored stale init ACK index=%u cmd=0x%02x arg=0x%02x",
                         (unsigned int)g_init_index,
                         event->ack_len > 0U ? (unsigned int)event->ack_data[0] : 0U,
                         event->ack_len > 3U ? (unsigned int)event->ack_data[3] : 0U);
            break;
        }
        /* The controller does not use byte 1 as a uniform success flag.  In
         * particular, RUMBLE_ENABLE can return a matching ACK with another
         * value here.  The proven Pico path advances on every matching ACK. */
        platform_log("ble", "init ACK index=%u cmd=0x%02x status=0x%02x len=%u",
                     (unsigned int)g_init_index,
                     (unsigned int)event->ack_data[0],
                     event->ack_len > 1U ? (unsigned int)event->ack_data[1] : 0U,
                     (unsigned int)event->ack_len);
        ble_gatt_recovery_clear();
        g_init_index++;
        ble_gatt_send_current_init_command();
        break;

    default:
        break;
    }
}

static void ble_gatt_process_control_events(void)
{
    BleGattControlEvent event;
    uint8_t processed = 0U;

    while (processed < BLE_GATT_CONTROL_EVENT_BUDGET &&
           ble_gatt_control_event_pop(&event)) {
        ble_gatt_process_control_event(&event);
        processed++;
    }
}

static int ble_gatt_remote_event_handler(uint16_t event_id, uint8_t *data,
                                         uint16_t len)
{
    (void)len;

    switch (event_id) {
    case SIBLES_REMOTE_EVENT_IND:
    {
        sibles_remote_event_ind_t *ind = (sibles_remote_event_ind_t *)data;
        ble_gatt_input_callback_t input_callback = 0;
        rt_base_t level;
        uint8_t input_kind = NS2_GATT_INPUT_KIND_UNKNOWN;
        uint8_t is_ack = 0U;

        if (ind == 0 || ind->value == 0) {
            break;
        }

        level = ble_gatt_critical_enter();
        if (ind->conn_idx != g_status.conn_idx || g_status.connected == 0U) {
            ble_gatt_critical_exit(level);
            break;
        }

        g_status.notifications++;
        if (ind->handle == g_ack_value_handle) {
            is_ack = 1U;
        } else if (g_input_callback != 0) {
            const BleGattInputNotify *input =
                ble_gatt_find_input_notify(ind->handle);
            if (input != 0) {
                g_status.last_notify_handle = ind->handle;
                g_status.last_notify_kind = input->kind;
                input_kind = input->kind;
                input_callback = g_input_callback;
            }
        }
        ble_gatt_critical_exit(level);

        if (is_ack != 0U) {
            BleGattControlEvent event;
            uint16_t copy_len = ind->length;
            if (copy_len > sizeof(event.ack_data)) {
                copy_len = sizeof(event.ack_data);
            }
            memset(&event, 0, sizeof(event));
            event.type = BLE_GATT_CONTROL_INIT_ACK;
            event.conn_idx = ind->conn_idx;
            event.ack_len = (uint8_t)copy_len;
            if (copy_len != 0U) {
                memcpy(event.ack_data, ind->value, copy_len);
            }
            (void)ble_gatt_control_event_push(&event);
        } else if (input_callback != 0) {
            input_callback(input_kind, ind->value, ind->length);
        }
        break;
    }
    case SIBLES_REGISTER_REMOTE_SVC_RSP:
    {
        sibles_register_remote_svc_rsp_t *rsp =
            (sibles_register_remote_svc_rsp_t *)data;
        if (rsp == 0) {
            break;
        }
        ble_gatt_control_event_simple(BLE_GATT_CONTROL_REGISTER_RESPONSE,
                                      rsp->conn_idx,
                                      rsp->status,
                                      0U);
        break;
    }
    default:
        break;
    }

    return 0;
}

int ble_gatt_sifli_event_handler(uint16_t event_id, uint8_t *data,
                                 uint16_t len, uint32_t context)
{
    (void)len;
    (void)context;

    switch (event_id) {
    case BLE_POWER_ON_IND:
        ble_gatt_control_event_simple(BLE_GATT_CONTROL_POWER_ON,
                                      BLE_GATT_INVALID_CONN_IDX,
                                      0U,
                                      0U);
        break;

    case BLE_GAP_SCAN_START_CNF:
    {
        ble_gap_start_scan_cnf_t *cnf = (ble_gap_start_scan_cnf_t *)data;
        if (cnf != 0 && cnf->status != HL_ERR_NO_ERROR) {
            ble_gatt_control_event_simple(BLE_GATT_CONTROL_SCAN_START_FAILED,
                                          BLE_GATT_INVALID_CONN_IDX,
                                          cnf->status,
                                          0U);
        }
        break;
    }

    case BLE_GAP_SCAN_STOPPED_IND:
        ble_gatt_control_event_simple(BLE_GATT_CONTROL_SCAN_STOPPED,
                                      BLE_GATT_INVALID_CONN_IDX,
                                      0U,
                                      0U);
        break;

    case BLE_GAP_EXT_ADV_REPORT_IND:
    {
        ble_gap_ext_adv_report_ind_t *ind =
            (ble_gap_ext_adv_report_ind_t *)data;
        if (ind != 0) {
            ns2_gatt_adv_info_t adv_info;
            rt_base_t level;
            uint8_t target_match;

            ns2_gatt_parse_advertisement(ind->data, ind->length, &adv_info);
            level = ble_gatt_critical_enter();
            g_status.adv_reports++;
            g_status.last_rssi = ind->rssi;
            if (adv_info.candidate) {
                g_status.ns2_candidates++;
                g_status.last_candidate_valid = 0U;
                g_status.last_candidate_addr_type = ind->addr.addr_type;
                memcpy(g_status.last_candidate_addr,
                       ind->addr.addr.addr,
                       sizeof(g_status.last_candidate_addr));
                /* Publish valid last, after the complete address is visible. */
                g_status.last_candidate_valid = 1U;
                target_match = !g_target_valid ||
                    (g_target_address.addr_type == ind->addr.addr_type &&
                     memcmp(g_target_address.addr.addr,
                            ind->addr.addr.addr,
                            sizeof(g_target_address.addr.addr)) == 0);
                if (target_match) {
                    g_candidate_address = ind->addr;
                    g_candidate_rssi = ind->rssi;
                    g_candidate_pending = 1U;
                }
            }
            ble_gatt_critical_exit(level);
        }
        break;
    }

    case BLE_GAP_CREATE_CONNECTION_CNF:
    {
        ble_gap_create_connection_cnf_t *cnf =
            (ble_gap_create_connection_cnf_t *)data;
        if (cnf != 0 && cnf->status != HL_ERR_NO_ERROR) {
            ble_gatt_control_event_simple(BLE_GATT_CONTROL_CONNECT_FAILED,
                                          BLE_GATT_INVALID_CONN_IDX,
                                          cnf->status,
                                          0U);
        }
        break;
    }

    case CONNECTION_MANAGER_CONNCTED_IND:
    {
        connection_manager_connect_ind_t *ind =
            (connection_manager_connect_ind_t *)data;
        if (ind != 0 && ind->role == 0U) {
            ble_gatt_control_event_simple(BLE_GATT_CONTROL_CONNECTED,
                                          ind->conn_idx,
                                          0U,
                                          0U);
        }
        break;
    }

    case BLE_GAP_DISCONNECTED_IND:
    {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        if (ind != 0) {
            ble_gatt_control_event_simple(BLE_GATT_CONTROL_DISCONNECTED,
                                          ind->conn_idx,
                                          0U,
                                          ind->reason);
        }
        break;
    }

    case SIBLES_MTU_EXCHANGE_IND:
    {
        sibles_mtu_exchange_ind_t *ind = (sibles_mtu_exchange_ind_t *)data;
        if (ind != 0) {
            BleGattControlEvent event;
            memset(&event, 0, sizeof(event));
            event.type = BLE_GATT_CONTROL_MTU;
            event.conn_idx = ind->conn_idx;
            event.mtu = ind->mtu;
            (void)ble_gatt_control_event_push(&event);
        }
        break;
    }

    case SIBLES_SEARCH_SVC_RSP:
    {
        sibles_svc_search_rsp_t *rsp = (sibles_svc_search_rsp_t *)data;
        BleGattControlEvent event;

        if (rsp == 0) {
            break;
        }

        memset(&event, 0, sizeof(event));
        event.type = BLE_GATT_CONTROL_SERVICE_SEARCH;
        event.conn_idx = rsp->conn_idx;
        event.status = rsp->result;
        if (rsp->result == HL_ERR_NO_ERROR && rsp->svc != 0) {
            event.service_valid = 1U;
            ble_gatt_parse_searched_service(rsp->svc, &event.service);
        }
        (void)ble_gatt_control_event_push(&event);
        break;
    }

    case SIBLES_WRITE_REMOTE_VALUE_RSP:
    {
        sibles_write_value_rsp_t *rsp = (sibles_write_value_rsp_t *)data;
        if (rsp != 0) {
            ble_gatt_control_event_simple(BLE_GATT_CONTROL_WRITE_RESPONSE,
                                          rsp->conn_idx,
                                          rsp->result,
                                          0U);
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

BLE_EVENT_REGISTER(ble_gatt_sifli_event_handler, NULL);
#endif

int ble_gatt_init(ble_gatt_input_callback_t input_callback)
{
    g_input_callback = input_callback;

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    {
        rt_base_t level = ble_gatt_critical_enter();
        g_control_event_read = 0U;
        g_control_event_write = 0U;
        g_candidate_pending = 0U;
        g_status.conn_idx = BLE_GATT_INVALID_CONN_IDX;
        ble_gatt_critical_exit(level);
    }
    g_status.sdk_available = 1U;
    if (!g_ble_enable_requested) {
        g_ble_enable_requested = 1U;
        platform_log("ble", "SiFli BLE central initialized");
    }
    return 0;
#else
    g_status.sdk_available = 0U;
    platform_log("ble", "SiFli BLE is not enabled in this build");
    return -1;
#endif
}

int ble_gatt_start_scan(void)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    g_scan_requested = 1U;
    if (!g_status.powered) {
        platform_log("ble", "scan deferred until BLE power-on");
        return 0;
    }
    return ble_gatt_start_scan_now();
#else
    return -1;
#endif
}

int ble_gatt_stop_scan(void)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    uint8_t ret = HL_ERR_NO_ERROR;

    g_scan_requested = 0U;
    g_scan_retry_deadline_ms = 0U;
    if (g_status.connecting) {
        ret = ble_gap_cancel_create_connection();
        g_status.connecting = 0U;
    }
    if (g_status.scanning) {
        uint8_t scan_ret = ble_gap_scan_stop();
        if (ret == HL_ERR_NO_ERROR) {
            ret = scan_ret;
        }
        g_status.scanning = 0U;
    }
    return ret == HL_ERR_NO_ERROR ? 0 : -1;
#else
    return -1;
#endif
}

int ble_gatt_disconnect(uint8_t resume_scan)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    ble_gap_disconnect_t request;
    uint8_t ret;

    g_scan_requested = resume_scan ? 1U : 0U;
    if (!g_status.connected || g_status.conn_idx == BLE_GATT_INVALID_CONN_IDX) {
        return resume_scan ? ble_gatt_start_scan() : ble_gatt_stop_scan();
    }
    request.conn_idx = g_status.conn_idx;
    request.reason = CO_ERROR_REMOTE_USER_TERM_CON;
    ret = ble_gap_disconnect(&request);
    return ret == HL_ERR_NO_ERROR ? 0 : -1;
#else
    (void)resume_scan;
    return -1;
#endif
}

int ble_gatt_set_target(uint8_t address_type, const uint8_t address[6])
{
    if (address == 0) {
        return -1;
    }

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    {
        rt_base_t level = ble_gatt_critical_enter();
        g_target_valid = 0U;
        g_status.target_valid = 0U;
        memset(&g_target_address, 0, sizeof(g_target_address));
        g_target_address.addr_type = address_type;
        memcpy(g_target_address.addr.addr, address,
               sizeof(g_target_address.addr.addr));
        g_status.target_addr_type = address_type;
        memcpy(g_status.target_addr, address, sizeof(g_status.target_addr));
        g_target_valid = 1U;
        g_status.target_valid = 1U;
        ble_gatt_critical_exit(level);
    }
#else
    g_status.target_valid = 1U;
    g_status.target_addr_type = address_type;
    memcpy(g_status.target_addr, address, sizeof(g_status.target_addr));
#endif
    return 0;
}

void ble_gatt_clear_target(void)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    rt_base_t level = ble_gatt_critical_enter();
    g_target_valid = 0U;
    g_status.target_valid = 0U;
    memset(&g_target_address, 0, sizeof(g_target_address));
    g_status.target_addr_type = 0U;
    memset(g_status.target_addr, 0, sizeof(g_status.target_addr));
    g_candidate_pending = 0U;
    ble_gatt_critical_exit(level);
#else
    g_status.target_valid = 0U;
    g_status.target_addr_type = 0U;
    memset(g_status.target_addr, 0, sizeof(g_status.target_addr));
#endif
}

int ble_gatt_write_command(const uint8_t *data, size_t len)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    if (len > 0xffffU) {
        return -1;
    }

    return ble_gatt_write_remote(g_command_value_handle,
                                 data,
                                 (uint16_t)len,
                                 SIBLES_WRITE_WITHOUT_RSP);
#else
    (void)data;
    (void)len;
    return -1;
#endif
}

int ble_gatt_write_handle(uint16_t handle, const uint8_t *data, size_t len)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    if (len > 0xffffU) {
        return -1;
    }

    return ble_gatt_write_remote(handle,
                                 data,
                                 (uint16_t)len,
                                 SIBLES_WRITE_WITHOUT_RSP);
#else
    (void)handle;
    (void)data;
    (void)len;
    return -1;
#endif
}

void ble_gatt_poll(void)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    BleGattRecoveryAction action;
    uint32_t now;

    /* All control-state transitions run here, never in SDK callbacks. */
    ble_gatt_process_control_events();
    ble_gatt_process_candidate();

    now = platform_millis();
    if (g_status.connected == 0U ||
        g_status.conn_idx == BLE_GATT_INVALID_CONN_IDX) {
        if (g_scan_requested != 0U && g_status.powered != 0U &&
            g_status.scanning == 0U && g_status.connecting == 0U &&
            (g_scan_retry_deadline_ms == 0U ||
             ble_gatt_time_reached(now, g_scan_retry_deadline_ms))) {
            (void)ble_gatt_start_scan_now();
        }
        return;
    }

    if (g_init_waiting_ack != 0U &&
        ble_gatt_time_reached(now, g_init_ack_deadline_ms)) {
        g_status.ack_timeouts++;
        platform_log("ble", "init ACK timeout index=%u",
                     (unsigned int)g_init_index);
        ble_gatt_recovery_fail(BLE_GATT_RECOVERY_INIT_COMMAND,
                               "init ACK timeout");
    }

    if (g_recovery_deadline_ms == 0U ||
        !ble_gatt_time_reached(now, g_recovery_deadline_ms)) {
        return;
    }

    action = g_recovery_action;
    g_recovery_deadline_ms = 0U;
    switch (action) {
    case BLE_GATT_RECOVERY_REGISTER:
        if (g_remote_registration_pending != 0U) {
            ble_gatt_unregister_remote_service();
            ble_gatt_recovery_disconnect_now(
                "remote service registration timeout");
        } else {
            (void)ble_gatt_attempt_remote_registration();
        }
        break;
    case BLE_GATT_RECOVERY_ACK_CCCD:
        if (g_cccd_write_pending != 0U) {
            ble_gatt_recovery_disconnect_now("ACK CCCD response timeout");
        } else {
            ble_gatt_finish_remote_registration();
        }
        break;
    case BLE_GATT_RECOVERY_INIT_COMMAND:
        ble_gatt_send_current_init_command();
        break;
    case BLE_GATT_RECOVERY_INPUT_CCCD:
        if (g_cccd_write_pending != 0U) {
            ble_gatt_recovery_disconnect_now("input CCCD response timeout");
        } else {
            ble_gatt_start_input_subscription();
        }
        break;
    case BLE_GATT_RECOVERY_DISCONNECT:
    {
        uint8_t resume_scan = g_scan_requested;
        if (ble_gatt_disconnect(resume_scan) == 0) {
            g_status.recovery_disconnects++;
            ble_gatt_recovery_clear();
        } else {
            platform_log("ble", "recovery disconnect request failed; retrying");
            g_recovery_deadline_ms = now + 500U;
        }
        break;
    }
    default:
        break;
    }
#endif
}

void ble_gatt_get_status(ble_gatt_status_t *status)
{
    if (status == 0) {
        return;
    }

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    {
        rt_base_t level = ble_gatt_critical_enter();
        *status = g_status;
        ble_gatt_critical_exit(level);
    }
#else
    *status = g_status;
#endif
}
