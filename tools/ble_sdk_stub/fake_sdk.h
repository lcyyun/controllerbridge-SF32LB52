#ifndef SF32_BLE_FAKE_SDK_H
#define SF32_BLE_FAKE_SDK_H
/* Host test surface only. Field names and GAP report bits match pinned
 * SiFli-Bluetooth 7fae1794 include/bf0_ble_gap.h; this is not a radio emulator. */
#include <stdint.h>
#include <stddef.h>
typedef int rt_base_t;
rt_base_t rt_hw_interrupt_disable(void);
void rt_hw_interrupt_enable(rt_base_t level);
typedef struct { uint8_t addr[6]; } bd_addr_t;
typedef struct { bd_addr_t addr; uint8_t addr_type; } ble_gap_addr_t;
enum {
    HL_ERR_NO_ERROR = 0, CO_ERROR_REMOTE_USER_TERM_CON = 0x13,
    GAPM_STATIC_ADDR = 0, GAPM_INIT_TYPE_DIRECT_CONN_EST = 0,
    GAPM_SCAN_TYPE_OBSERVER = 2,
    GAPM_REPORT_INFO_REPORT_TYPE_MASK = 0x07,
    GAPM_REPORT_INFO_CONN_ADV_BIT = 0x10,
    GAPM_REPORT_INFO_DIR_ADV_BIT = 0x40,
    GAPM_REPORT_TYPE_ADV_EXT = 0, GAPM_REPORT_TYPE_ADV_LEG = 1,
    GAPM_REPORT_TYPE_SCAN_RSP_EXT = 2, GAPM_REPORT_TYPE_SCAN_RSP_LEG = 3,
    BLE_POWER_ON_IND = 1, BLE_GAP_SCAN_START_CNF,
    BLE_GAP_SCAN_STOPPED_IND, BLE_GAP_EXT_ADV_REPORT_IND,
    BLE_GAP_CREATE_CONNECTION_CNF, BLE_GAP_CREATE_CONNECTION_STOP_IND,
    CONNECTION_MANAGER_CONNCTED_IND, BLE_GAP_DISCONNECTED_IND,
    SIBLES_MTU_EXCHANGE_IND, SIBLES_SEARCH_SVC_RSP,
    SIBLES_WRITE_REMOTE_VALUE_RSP, SIBLES_REGISTER_REMOTE_SVC_RSP,
    SIBLES_REMOTE_EVENT_IND
};
#define BLE_EVENT_REGISTER(handler, context)
typedef struct { uint16_t scan_intv, scan_wd, conn_intv_min, conn_intv_max,
    conn_latency, supervision_to, ce_len_min, ce_len_max; } ble_gap_conn_param_t;
typedef struct { uint8_t own_addr_type, type; uint16_t conn_to;
    ble_gap_conn_param_t conn_param_1m; ble_gap_addr_t peer_addr;
} ble_gap_connection_create_param_t;
typedef struct { uint8_t own_addr_type, type, dup_filt_pol;
    ble_gap_conn_param_t scan_param_1m; uint16_t duration, period;
} ble_gap_scan_start_t;
typedef struct { uint8_t conn_idx, reason; } ble_gap_disconnect_t;
typedef struct { uint8_t status; } ble_gap_start_scan_cnf_t;
typedef struct { uint8_t status; } ble_gap_create_connection_cnf_t;
typedef struct { uint8_t conn_idx, role; } connection_manager_connect_ind_t;
typedef struct { uint8_t conn_idx, reason; } ble_gap_disconnected_ind_t;
typedef struct { uint8_t actv_idx, info; ble_gap_addr_t addr, direct_addr;
    int8_t tx_pwr, rssi; uint8_t phy_prim, phy_second, adv_sid;
    uint16_t period_adv_intv, length; uint8_t data[64];
} ble_gap_ext_adv_report_ind_t;
uint8_t ble_gap_create_connection(ble_gap_connection_create_param_t *param);
uint8_t ble_gap_cancel_create_connection(void);
uint8_t ble_gap_scan_start(ble_gap_scan_start_t *param);
uint8_t ble_gap_scan_stop(void);
uint8_t ble_gap_disconnect(ble_gap_disconnect_t *param);
uint8_t ble_get_public_address(bd_addr_t *address);
typedef enum { SIBLES_WRITE, SIBLES_WRITE_WITHOUT_RSP } sibles_write_type_t;
enum { SIBLES_WRITE_NO_ERR = 0, SIBLES_ERROR_REMOTE_HANDLE = 0xffff };
typedef struct { sibles_write_type_t write_type; uint16_t handle, len;
    uint8_t *value; } sibles_write_remote_value_t;
typedef struct { uint8_t conn_idx; uint16_t mtu; } sibles_mtu_exchange_ind_t;
typedef struct { uint8_t conn_idx, status; } sibles_register_remote_svc_rsp_t;
typedef struct { uint8_t conn_idx, result; } sibles_write_value_rsp_t;
typedef struct { uint8_t conn_idx; uint16_t handle, length; uint8_t *value;
} sibles_remote_event_ind_t;
struct sibles_disc_char_desc_ind { uint16_t attr_hdl; };
typedef struct { uint8_t uuid_len, uuid[16], desc_count; uint16_t pointer_hdl;
    struct sibles_disc_char_desc_ind desc[1]; } sibles_svc_search_char_t;
typedef struct { uint16_t hdl_start, hdl_end; uint8_t char_count; void *att_db;
} sibles_svc_remote_svc_t;
typedef struct { uint8_t conn_idx, result; sibles_svc_remote_svc_t *svc;
} sibles_svc_search_rsp_t;
typedef int (*fake_remote_handler_t)(uint16_t, uint8_t *, uint16_t);
int8_t sibles_write_remote_value(uint16_t handle, uint8_t index,
                               sibles_write_remote_value_t *value);
uint16_t sibles_descriptor_handle_find(sibles_svc_search_char_t *chara, uint16_t uuid);
uint8_t sibles_exchange_mtu(uint8_t index);
uint8_t sibles_search_service(uint8_t index, uint8_t len, uint8_t *uuid);
uint16_t sibles_register_remote_svc(uint8_t index, uint16_t start, uint16_t end,
                                  fake_remote_handler_t handler);
void sibles_unregister_remote_svc(uint8_t index, uint16_t start, uint16_t end,
                                 fake_remote_handler_t handler);
#endif
