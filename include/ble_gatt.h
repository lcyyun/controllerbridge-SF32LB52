#ifndef NS2PRO_BRIDGE_BLE_GATT_H
#define NS2PRO_BRIDGE_BLE_GATT_H

#include <stddef.h>
#include <stdint.h>

typedef void (*ble_gatt_input_callback_t)(uint8_t kind, const uint8_t *data, size_t len);

typedef struct
{
    uint8_t sdk_available;
    uint8_t powered;
    uint8_t scanning;
    uint8_t connecting;
    uint8_t connected;
    uint8_t conn_idx;
    uint8_t last_candidate_valid;
    uint8_t last_candidate_addr_type;
    uint8_t last_candidate_addr[6];
    uint8_t target_valid;
    uint8_t target_addr_type;
    uint8_t target_addr[6];
    int8_t last_rssi;
    uint32_t adv_reports;
    uint32_t ns2_candidates;
    uint32_t notifications;
    uint32_t writes_ok;
    uint32_t writes_failed;
    uint16_t mtu;
    int8_t last_write_ret;
    uint16_t remote_handle;
    uint16_t service_start;
    uint16_t service_end;
    uint16_t command_handle;
    uint16_t rumble_handle;
    uint16_t ack_cccd_handle;
    uint16_t input_cccd_handle;
    uint16_t last_write_handle;
    uint8_t input_notify_count;
    uint8_t last_notify_kind;
    uint16_t last_notify_handle;
    uint8_t recovery_action;
    uint8_t recovery_retry_count;
    uint32_t recovery_failures;
    uint32_t ack_timeouts;
    uint32_t recovery_disconnects;
    uint32_t control_event_drops;
    uint8_t local_addr_valid;
    uint8_t local_addr[6];
    uint8_t last_connect_status;
    uint8_t last_disconnect_reason;
    uint32_t connect_attempts;
    uint32_t connect_failures;
    uint32_t disconnects;
} ble_gatt_status_t;

int ble_gatt_init(ble_gatt_input_callback_t input_callback);
int ble_gatt_start_scan(void);
int ble_gatt_stop_scan(void);
int ble_gatt_disconnect(uint8_t resume_scan);
int ble_gatt_set_target(uint8_t address_type, const uint8_t address[6]);
void ble_gatt_clear_target(void);
int ble_gatt_write_command(const uint8_t *data, size_t len);
int ble_gatt_write_handle(uint16_t handle, const uint8_t *data, size_t len);
void ble_gatt_poll(void);
void ble_gatt_get_status(ble_gatt_status_t *status);

#endif /* NS2PRO_BRIDGE_BLE_GATT_H */
