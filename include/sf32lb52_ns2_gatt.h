#ifndef NS2PRO_BRIDGE_SF32LB52_NS2_GATT_H
#define NS2PRO_BRIDGE_SF32LB52_NS2_GATT_H

#include <stddef.h>
#include <stdint.h>

#define NS2_GATT_UUID128_LEN 16U
#define NS2_GATT_NINTENDO_COMPANY_ID 0x0553U
#define NS2_GATT_KNOWN_INPUT_FD2_VALUE_HANDLE 0x000aU
#define NS2_GATT_KNOWN_RUMBLE_VALUE_HANDLE 0x0012U
#define NS2_GATT_KNOWN_COMMAND_VALUE_HANDLE 0x0014U
#define NS2_GATT_KNOWN_ACK_VALUE_HANDLE 0x001aU
#define NS2_GATT_INPUT_KIND_UNKNOWN 0U
#define NS2_GATT_INPUT_KIND_FD2 1U
#define NS2_GATT_INPUT_KIND_LEGACY 2U

typedef enum
{
    NS2_GATT_ROLE_OTHER = 0,
    NS2_GATT_ROLE_ACK_NOTIFY,
    NS2_GATT_ROLE_INPUT_NOTIFY,
    NS2_GATT_ROLE_COMMAND,
    NS2_GATT_ROLE_RUMBLE,
} ns2_gatt_role_t;

typedef struct
{
    char name[32];
    uint8_t nintendo_mfg;
    uint8_t ns2_mfg_prefix;
    uint8_t ns2_pairing_mfg;
    uint8_t service_match;
    uint8_t appearance_match;
    uint8_t candidate;
} ns2_gatt_adv_info_t;

typedef struct
{
    const char *name;
    const uint8_t *data;
    uint16_t len;
} ns2_gatt_init_command_t;

int ns2_gatt_uuid128_equals(const uint8_t a[NS2_GATT_UUID128_LEN],
                            const uint8_t b[NS2_GATT_UUID128_LEN]);
ns2_gatt_role_t ns2_gatt_classify_uuid(uint16_t uuid16,
                                       const uint8_t uuid128[NS2_GATT_UUID128_LEN]);
ns2_gatt_role_t ns2_gatt_classify_sifli_uuid(uint8_t uuid_len,
                                             const uint8_t *uuid);
int ns2_gatt_is_fd2_sifli_uuid(uint8_t uuid_len, const uint8_t *uuid);
uint8_t ns2_gatt_input_kind_sifli_uuid(uint8_t uuid_len, const uint8_t *uuid);
const char *ns2_gatt_role_name(ns2_gatt_role_t role);
void ns2_gatt_parse_advertisement(const uint8_t *data,
                                  uint16_t len,
                                  ns2_gatt_adv_info_t *info);
uint8_t ns2_gatt_init_command_count(void);
const ns2_gatt_init_command_t *ns2_gatt_init_command(uint8_t index);
void ns2_gatt_set_console_mac(const uint8_t address[6]);

#endif /* NS2PRO_BRIDGE_SF32LB52_NS2_GATT_H */
