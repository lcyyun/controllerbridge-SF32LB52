#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_sdk_stub/fake_sdk.h"

/* Compile the production adapter, including its event queue and polling path.
 * Only the platform SDK boundary is mocked. */
#include "../src/ble_gatt_sifli.c"

static uint32_t fake_now;
static int in_callback;
static unsigned scan_calls, connect_calls, cancel_calls, disconnect_calls, gatt_calls;
static uint8_t scan_result, stop_result, connect_result, cancel_result;
static ble_gap_addr_t connected_peer;

rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t level) { (void)level; }
uint32_t platform_millis(void) { return fake_now; }
void platform_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
uint8_t ble_gap_scan_start(ble_gap_scan_start_t *param)
{ assert(!in_callback); assert(param->duration == 0); scan_calls++; return scan_result; }
uint8_t ble_gap_scan_stop(void) { assert(!in_callback); return stop_result; }
uint8_t ble_gap_create_connection(ble_gap_connection_create_param_t *param)
{ assert(!in_callback); connect_calls++; connected_peer = param->peer_addr; return connect_result; }
uint8_t ble_gap_cancel_create_connection(void)
{ assert(!in_callback); cancel_calls++; return cancel_result; }
uint8_t ble_gap_disconnect(ble_gap_disconnect_t *param)
{ assert(!in_callback); assert(param->conn_idx != 0xff); disconnect_calls++; return 0; }
uint8_t ble_get_public_address(bd_addr_t *address)
{ assert(!in_callback); memset(address, 0x12, sizeof(*address)); return 0; }
int8_t sibles_write_remote_value(uint16_t handle, uint8_t index,
                                sibles_write_remote_value_t *value)
{ assert(!in_callback); (void)handle; (void)index; (void)value; gatt_calls++; return 0; }
uint16_t sibles_descriptor_handle_find(sibles_svc_search_char_t *chara, uint16_t uuid)
{ (void)chara; (void)uuid; return 0; }
uint8_t sibles_exchange_mtu(uint8_t index)
{ assert(!in_callback); (void)index; gatt_calls++; return 0; }
uint8_t sibles_search_service(uint8_t index, uint8_t len, uint8_t *uuid)
{ assert(!in_callback); (void)index; (void)len; (void)uuid; gatt_calls++; return 0; }
uint16_t sibles_register_remote_svc(uint8_t index, uint16_t start, uint16_t end,
                                  fake_remote_handler_t handler)
{ assert(!in_callback); (void)index; (void)start; (void)end; (void)handler; gatt_calls++; return 1; }
void sibles_unregister_remote_svc(uint8_t index, uint16_t start, uint16_t end,
                                 fake_remote_handler_t handler)
{ assert(!in_callback); (void)index; (void)start; (void)end; (void)handler; gatt_calls++; }

static const uint8_t target[6] = {1, 2, 3, 4, 5, 6};

static void event(uint16_t id, void *data)
{
    in_callback = 1;
    assert(ble_gatt_sifli_event_handler(id, data, 0, 0) == 0);
    in_callback = 0;
}

static void reset(void)
{
    memset(&g_status, 0, sizeof(g_status));
    g_status.conn_idx = BLE_GATT_INVALID_CONN_IDX;
    g_ble_enable_requested = g_scan_requested = g_target_valid = 0;
    g_remote_service_registered = 0;
    g_connect_deadline_ms = g_scan_retry_deadline_ms = 0;
    ble_gatt_recovery_clear();
    fake_now = 100;
    scan_calls = connect_calls = cancel_calls = disconnect_calls = gatt_calls = 0;
    scan_result = stop_result = connect_result = cancel_result = 0;
    assert(ble_gatt_init(NULL) == 0);
    assert(ble_gatt_set_target(1, target) == 0);
    event(BLE_POWER_ON_IND, NULL);
    assert(scan_calls == 0);
    ble_gatt_poll();
    assert(ble_gatt_start_scan() == 0);
    assert(scan_calls == 1);
}

static void advertise(const uint8_t addr[6], uint8_t type, uint8_t info, int named)
{
    ble_gap_ext_adv_report_ind_t adv;
    memset(&adv, 0, sizeof(adv));
    adv.addr.addr_type = type;
    memcpy(adv.addr.addr.addr, addr, 6);
    adv.info = info;
    if (named) {
        /* Nintendo company 0x0553 plus NS2 manufacturer prefix. */
        static const uint8_t payload[] = {7, 0xff, 0x53, 0x05, 1, 0, 3, 0x7e};
        memcpy(adv.data, payload, sizeof(payload));
        adv.length = sizeof(payload);
    }
    event(BLE_GAP_EXT_ADV_REPORT_IND, &adv);
}

static void wake_advert(void)
{
    advertise(target, 1, GAPM_REPORT_TYPE_ADV_LEG |
              GAPM_REPORT_INFO_CONN_ADV_BIT | GAPM_REPORT_INFO_DIR_ADV_BIT, 0);
}

static void test_power_off_any_key_wake(void)
{
    connection_manager_connect_ind_t up = {2, 0};
    ble_gap_disconnected_ind_t down = {2, CO_ERROR_REMOTE_USER_TERM_CON};
    reset();
    wake_advert();
    assert(connect_calls == 0); /* No synchronous command from callback. */
    ble_gatt_poll();
    assert(connect_calls == 1 && g_status.connecting);
    assert(memcmp(connected_peer.addr.addr, target, 6) == 0);
    event(CONNECTION_MANAGER_CONNCTED_IND, &up);
    assert(gatt_calls == 0);
    ble_gatt_poll();
    assert(g_status.connected && !g_status.connecting && gatt_calls == 2);
    event(BLE_GAP_DISCONNECTED_IND, &down);
    ble_gatt_poll();
    assert(!g_status.connected && scan_calls == 1);
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    ble_gatt_poll();
    assert(scan_calls == 2);
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 2 && g_status.connecting);
}

static void test_target_and_report_filters(void)
{
    uint8_t foreign[6] = {9, 9, 9, 9, 9, 9};
    reset();
    advertise(foreign, 1, GAPM_REPORT_TYPE_ADV_LEG | GAPM_REPORT_INFO_CONN_ADV_BIT, 1);
    advertise(target, 0, GAPM_REPORT_TYPE_ADV_LEG | GAPM_REPORT_INFO_CONN_ADV_BIT, 1);
    advertise(target, 1, GAPM_REPORT_TYPE_SCAN_RSP_LEG | GAPM_REPORT_INFO_CONN_ADV_BIT, 1);
    advertise(target, 1, GAPM_REPORT_TYPE_ADV_LEG, 1);
    ble_gatt_poll();
    assert(connect_calls == 0 && !g_status.last_candidate_valid);
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 1);
    advertise(foreign, 1, GAPM_REPORT_TYPE_ADV_LEG | GAPM_REPORT_INFO_CONN_ADV_BIT, 1);
    assert(memcmp(g_status.last_candidate_addr, target, 6) == 0);
}

static void test_stop_cancels_queued_and_late_adverts(void)
{
    reset();
    wake_advert();
    assert(ble_gatt_stop_scan() == 0);
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 0 && !g_candidate_pending);
    assert(ble_gatt_start_scan() == 0);
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 1);
    assert(ble_gatt_stop_scan() == 0 && cancel_calls == 1);
    connection_manager_connect_ind_t up = {3, 0};
    event(CONNECTION_MANAGER_CONNCTED_IND, &up);
    ble_gatt_poll();
    assert(disconnect_calls == 1 && gatt_calls == 0);
}

static void test_failed_initiation_resumes_scanning(void)
{
    reset();
    wake_advert();
    ble_gatt_poll();
    event(BLE_GAP_CREATE_CONNECTION_STOP_IND, NULL);
    ble_gatt_poll();
    assert(!g_status.connecting && scan_calls == 1);
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    ble_gatt_poll();
    assert(scan_calls == 2);
    wake_advert();
    ble_gatt_poll();
    ble_gap_create_connection_cnf_t failed = {0x3e};
    event(BLE_GAP_CREATE_CONNECTION_CNF, &failed);
    ble_gatt_poll();
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    ble_gatt_poll();
    assert(scan_calls == 3 && !g_status.connecting);
}

static void test_missing_stop_and_counter_wrap(void)
{
    reset();
    fake_now = UINT32_MAX - 2000U;
    wake_advert();
    ble_gatt_poll();
    fake_now += BLE_GATT_CONNECT_TIMEOUT_MS - 1U;
    ble_gatt_poll();
    assert(cancel_calls == 0 && g_status.connecting);
    fake_now++;
    cancel_result = 1;
    ble_gatt_poll();
    assert(cancel_calls == 1 && g_status.connecting);
    fake_now += 500;
    cancel_result = 0;
    ble_gatt_poll();
    assert(cancel_calls == 2 && !g_status.connecting);
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    ble_gatt_poll();
    assert(scan_calls == 2);
}

static void test_scan_failures_remain_retryable(void)
{
    reset();
    ble_gap_start_scan_cnf_t failed = {1};
    event(BLE_GAP_SCAN_START_CNF, &failed);
    ble_gatt_poll();
    fake_now += 500;
    scan_result = 1;
    ble_gatt_poll();
    assert(scan_calls == 2 && !g_status.scanning);
    fake_now += 500;
    scan_result = 0;
    ble_gatt_poll();
    assert(scan_calls == 3 && g_status.scanning);
    assert(ble_gatt_start_scan() == 0 && scan_calls == 3);
}

static void test_stop_failure_retries_without_reconnecting(void)
{
    reset();
    stop_result = 1;
    assert(ble_gatt_stop_scan() == -1);
    assert(g_status.scanning);
    wake_advert();
    fake_now += 500;
    stop_result = 0;
    ble_gatt_poll();
    assert(!g_status.scanning && connect_calls == 0);
    assert(ble_gatt_start_scan() == 0);
    wake_advert();
    ble_gatt_poll();
    cancel_result = 1;
    assert(ble_gatt_stop_scan() == -1 && g_status.connecting);
    fake_now += 500;
    cancel_result = 0;
    ble_gatt_poll();
    assert(!g_status.connecting && cancel_calls == 2);
    fake_now += 1000;
    ble_gatt_poll();
    assert(connect_calls == 1 && scan_calls == 2);
}

static void test_candidate_stop_and_connect_request_failures(void)
{
    reset();
    stop_result = 1;
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 0 && g_status.scanning && g_candidate_pending);
    stop_result = 0;
    ble_gatt_poll();
    assert(connect_calls == 0);
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    connect_result = 1;
    ble_gatt_poll();
    assert(connect_calls == 1 && !g_status.scanning && !g_status.connecting);
    fake_now += BLE_GATT_SCAN_RETRY_MS;
    ble_gatt_poll();
    assert(scan_calls == 2 && g_status.scanning);
    connect_result = 0;
    wake_advert();
    ble_gatt_poll();
    assert(connect_calls == 2 && g_status.connecting);
}

static void test_forget_cannot_republish_saved_peer(void)
{
    reset();
    wake_advert();
    ble_gatt_poll();
    connection_manager_connect_ind_t up = {4, 0};
    event(CONNECTION_MANAGER_CONNCTED_IND, &up);
    ble_gatt_poll();
    assert(g_status.connected && g_status.last_candidate_valid);
    ble_gatt_clear_target();
    assert(!g_status.last_candidate_valid && !g_status.target_valid);
    assert(ble_gatt_disconnect(0) == 0);
    /* Disconnect completes asynchronously; persistence must already be off. */
    assert(g_status.connected && !g_status.last_candidate_valid);
}

int main(void)
{
    test_power_off_any_key_wake();
    test_target_and_report_filters();
    test_stop_cancels_queued_and_late_adverts();
    test_failed_initiation_resumes_scanning();
    test_missing_stop_and_counter_wrap();
    test_scan_failures_remain_retryable();
    test_stop_failure_retries_without_reconnecting();
    test_candidate_stop_and_connect_request_failures();
    test_forget_cannot_republish_saved_peer();
    puts("OK BLE adapter wake/reconnect and callback deferral");
    return 0;
}
