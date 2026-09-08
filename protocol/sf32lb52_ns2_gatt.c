#include "sf32lb52_ns2_gatt.h"

#include <string.h>

static const uint8_t ns2_manufacturer_prefix[] = {0x01U, 0x00U, 0x03U, 0x7eU};

static const uint8_t uuid_notify_fd2[NS2_GATT_UUID128_LEN] = {
    0xabU, 0x7dU, 0xe9U, 0xbeU, 0x89U, 0xfeU, 0x49U, 0xadU,
    0x82U, 0x8fU, 0x11U, 0x8fU, 0x09U, 0xdfU, 0x7fU, 0xd2U
};

static const uint8_t uuid_notify_legacy[NS2_GATT_UUID128_LEN] = {
    0x74U, 0x92U, 0x86U, 0x6cU, 0xecU, 0x3eU, 0x46U, 0x19U,
    0x82U, 0x58U, 0x32U, 0x75U, 0x5fU, 0xfcU, 0xc0U, 0xf8U
};

static const uint8_t uuid_ack[NS2_GATT_UUID128_LEN] = {
    0xc7U, 0x65U, 0xa9U, 0x61U, 0xd9U, 0xd8U, 0x4dU, 0x36U,
    0xa2U, 0x0aU, 0x53U, 0x15U, 0xb1U, 0x11U, 0x83U, 0x6aU
};

static const uint8_t uuid_command[NS2_GATT_UUID128_LEN] = {
    0x64U, 0x9dU, 0x4aU, 0xc9U, 0x8eU, 0xb7U, 0x4eU, 0x6cU,
    0xafU, 0x44U, 0x1eU, 0xa5U, 0x4fU, 0xe5U, 0xf0U, 0x05U
};

static const uint8_t uuid_rumble_output[NS2_GATT_UUID128_LEN] = {
    0x28U, 0x93U, 0x26U, 0xcbU, 0xa4U, 0x71U, 0x48U, 0x5dU,
    0xa8U, 0xf4U, 0x24U, 0x0cU, 0x14U, 0xf1U, 0x82U, 0x41U
};

static const uint8_t uuid_rumble_cc48[NS2_GATT_UUID128_LEN] = {
    0xccU, 0x48U, 0x3fU, 0x51U, 0x92U, 0x58U, 0x42U, 0x7dU,
    0xa9U, 0x39U, 0x63U, 0x0cU, 0x31U, 0xf7U, 0x2bU, 0x05U
};

static uint8_t init_cmd_0[] = {
    0x03U, 0x91U, 0x01U, 0x0dU, 0x00U, 0x08U, 0x00U, 0x00U,
    0x01U, 0x00U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU
};
static const uint8_t init_cmd_1[] = {0x07U, 0x91U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_2[] = {0x16U, 0x91U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_3[] = {0x15U, 0x91U, 0x01U, 0x03U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_4[] = {0x0cU, 0x91U, 0x01U, 0x02U, 0x00U, 0x04U, 0x00U, 0x00U, 0xffU, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_5[] = {0x11U, 0x91U, 0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_6[] = {
    0x0aU, 0x91U, 0x01U, 0x08U, 0x00U, 0x14U, 0x00U, 0x00U,
    0x01U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0x35U, 0x00U, 0x46U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U
};
static const uint8_t init_cmd_7[] = {0x0cU, 0x91U, 0x01U, 0x04U, 0x00U, 0x04U, 0x00U, 0x00U, 0xffU, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_8[] = {0x03U, 0x91U, 0x01U, 0x0aU, 0x00U, 0x04U, 0x00U, 0x00U, 0x09U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_9[] = {0x10U, 0x91U, 0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_10[] = {0x01U, 0x91U, 0x01U, 0x0cU, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_11[] = {0x01U, 0x91U, 0x01U, 0x01U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_12[] = {0x09U, 0x91U, 0x01U, 0x07U, 0x00U, 0x08U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
static const uint8_t init_cmd_13[] = {0x02U, 0x91U, 0x01U, 0x04U, 0x00U, 0x08U, 0x00U, 0x00U, 0x09U, 0x7eU, 0x00U, 0x00U, 0xa8U, 0x30U, 0x01U, 0x00U};
static const uint8_t init_cmd_14[] = {0x02U, 0x91U, 0x01U, 0x04U, 0x00U, 0x08U, 0x00U, 0x00U, 0x09U, 0x7eU, 0x00U, 0x00U, 0xe8U, 0x30U, 0x01U, 0x00U};

static const ns2_gatt_init_command_t init_commands[] = {
    {"INIT", init_cmd_0, sizeof(init_cmd_0)},
    {"CMD_07", init_cmd_1, sizeof(init_cmd_1)},
    {"CMD_16", init_cmd_2, sizeof(init_cmd_2)},
    {"CMD_15_03", init_cmd_3, sizeof(init_cmd_3)},
    {"FEATSEL_SET_MASK", init_cmd_4, sizeof(init_cmd_4)},
    {"CMD_11", init_cmd_5, sizeof(init_cmd_5)},
    {"VIBRATE_CFG", init_cmd_6, sizeof(init_cmd_6)},
    {"FEATSEL_ENABLE", init_cmd_7, sizeof(init_cmd_7)},
    {"SELECT_REPORT", init_cmd_8, sizeof(init_cmd_8)},
    {"FW_INFO_GET", init_cmd_9, sizeof(init_cmd_9)},
    {"CMD_01_0C", init_cmd_10, sizeof(init_cmd_10)},
    {"RUMBLE_ENABLE", init_cmd_11, sizeof(init_cmd_11)},
    {"SET_PLAYER_LED", init_cmd_12, sizeof(init_cmd_12)},
    {"CALIB_LEFT", init_cmd_13, sizeof(init_cmd_13)},
    {"CALIB_RIGHT", init_cmd_14, sizeof(init_cmd_14)},
};

static int starts_with(const uint8_t *data, uint16_t len,
                       const uint8_t *prefix, uint16_t prefix_len)
{
    return data != 0 && prefix != 0 && len >= prefix_len &&
           memcmp(data, prefix, prefix_len) == 0;
}

static int uuid128_adv_matches(const uint8_t *adv_uuid_le, const uint8_t uuid_be[NS2_GATT_UUID128_LEN])
{
    size_t i;

    if (adv_uuid_le == 0 || uuid_be == 0) {
        return 0;
    }

    for (i = 0U; i < NS2_GATT_UUID128_LEN; i++) {
        if (adv_uuid_le[i] != uuid_be[NS2_GATT_UUID128_LEN - 1U - i]) {
            return 0;
        }
    }
    return 1;
}

static int ns2_mfg_looks_pairable(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (!starts_with(data, len, ns2_manufacturer_prefix, sizeof(ns2_manufacturer_prefix)) ||
        len < 16U) {
        return 0;
    }

    for (i = 10U; i < 16U; i++) {
        if (data[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

int ns2_gatt_uuid128_equals(const uint8_t a[NS2_GATT_UUID128_LEN],
                            const uint8_t b[NS2_GATT_UUID128_LEN])
{
    return a != 0 && b != 0 && memcmp(a, b, NS2_GATT_UUID128_LEN) == 0;
}

ns2_gatt_role_t ns2_gatt_classify_uuid(uint16_t uuid16,
                                       const uint8_t uuid128[NS2_GATT_UUID128_LEN])
{
    if (uuid16 != 0U || uuid128 == 0) {
        return NS2_GATT_ROLE_OTHER;
    }
    if (ns2_gatt_uuid128_equals(uuid128, uuid_ack)) {
        return NS2_GATT_ROLE_ACK_NOTIFY;
    }
    if (ns2_gatt_uuid128_equals(uuid128, uuid_notify_fd2) ||
        ns2_gatt_uuid128_equals(uuid128, uuid_notify_legacy)) {
        return NS2_GATT_ROLE_INPUT_NOTIFY;
    }
    if (ns2_gatt_uuid128_equals(uuid128, uuid_command)) {
        return NS2_GATT_ROLE_COMMAND;
    }
    if (ns2_gatt_uuid128_equals(uuid128, uuid_rumble_output) ||
        ns2_gatt_uuid128_equals(uuid128, uuid_rumble_cc48)) {
        return NS2_GATT_ROLE_RUMBLE;
    }
    return NS2_GATT_ROLE_OTHER;
}

ns2_gatt_role_t ns2_gatt_classify_sifli_uuid(uint8_t uuid_len,
                                             const uint8_t *uuid)
{
    uint8_t standard_uuid[NS2_GATT_UUID128_LEN];
    uint8_t i;

    if (uuid == 0) {
        return NS2_GATT_ROLE_OTHER;
    }

    if (uuid_len == 2U) {
        uint16_t uuid16 = (uint16_t)uuid[0] | (uint16_t)((uint16_t)uuid[1] << 8U);
        return ns2_gatt_classify_uuid(uuid16, 0);
    }

    if (uuid_len != NS2_GATT_UUID128_LEN) {
        return NS2_GATT_ROLE_OTHER;
    }

    for (i = 0U; i < NS2_GATT_UUID128_LEN; i++) {
        standard_uuid[i] = uuid[NS2_GATT_UUID128_LEN - 1U - i];
    }

    return ns2_gatt_classify_uuid(0U, standard_uuid);
}

int ns2_gatt_is_fd2_sifli_uuid(uint8_t uuid_len, const uint8_t *uuid)
{
    return ns2_gatt_input_kind_sifli_uuid(uuid_len, uuid) ==
        NS2_GATT_INPUT_KIND_FD2;
}

uint8_t ns2_gatt_input_kind_sifli_uuid(uint8_t uuid_len, const uint8_t *uuid)
{
    uint8_t standard_uuid[NS2_GATT_UUID128_LEN];
    uint8_t i;

    if (uuid == 0 || uuid_len != NS2_GATT_UUID128_LEN) {
        return NS2_GATT_INPUT_KIND_UNKNOWN;
    }

    for (i = 0U; i < NS2_GATT_UUID128_LEN; i++) {
        standard_uuid[i] = uuid[NS2_GATT_UUID128_LEN - 1U - i];
    }
    if (ns2_gatt_uuid128_equals(standard_uuid, uuid_notify_fd2)) {
        return NS2_GATT_INPUT_KIND_FD2;
    }
    if (ns2_gatt_uuid128_equals(standard_uuid, uuid_notify_legacy)) {
        return NS2_GATT_INPUT_KIND_LEGACY;
    }
    return NS2_GATT_INPUT_KIND_UNKNOWN;
}

const char *ns2_gatt_role_name(ns2_gatt_role_t role)
{
    switch (role) {
    case NS2_GATT_ROLE_ACK_NOTIFY:
        return "ack";
    case NS2_GATT_ROLE_INPUT_NOTIFY:
        return "input_notify";
    case NS2_GATT_ROLE_COMMAND:
        return "command";
    case NS2_GATT_ROLE_RUMBLE:
        return "rumble";
    case NS2_GATT_ROLE_OTHER:
    default:
        return "other";
    }
}

void ns2_gatt_parse_advertisement(const uint8_t *data,
                                  uint16_t len,
                                  ns2_gatt_adv_info_t *info)
{
    uint16_t offset;

    if (info == 0) {
        return;
    }

    memset(info, 0, sizeof(*info));
    if (data == 0) {
        return;
    }

    offset = 0U;
    while ((uint16_t)(offset + 1U) < len) {
        uint8_t field_len = data[offset];
        uint8_t type;
        const uint8_t *payload;
        uint16_t payload_len;

        if (field_len == 0U || (uint16_t)(offset + field_len) >= len) {
            break;
        }

        type = data[offset + 1U];
        payload = &data[offset + 2U];
        payload_len = (uint16_t)(field_len - 1U);

        switch (type) {
        case 0x08U:
        case 0x09U:
        {
            uint16_t copy_len = payload_len;
            if (copy_len >= sizeof(info->name)) {
                copy_len = (uint16_t)(sizeof(info->name) - 1U);
            }
            memcpy(info->name, payload, copy_len);
            info->name[copy_len] = 0;
            break;
        }
        case 0xffU:
            if (payload_len >= 2U) {
                uint16_t company = (uint16_t)payload[0] |
                                   (uint16_t)((uint16_t)payload[1] << 8U);
                if (company == NS2_GATT_NINTENDO_COMPANY_ID) {
                    const uint8_t *mfg_data = payload + 2U;
                    uint16_t mfg_len = (uint16_t)(payload_len - 2U);
                    info->nintendo_mfg = 1U;
                    info->ns2_mfg_prefix =
                        (uint8_t)starts_with(mfg_data, mfg_len,
                                             ns2_manufacturer_prefix,
                                             sizeof(ns2_manufacturer_prefix));
                    info->ns2_pairing_mfg =
                        (uint8_t)ns2_mfg_looks_pairable(mfg_data, mfg_len);
                }
            }
            break;
        case 0x19U:
            if (payload_len >= 2U) {
                uint16_t appearance = (uint16_t)payload[0] |
                                      (uint16_t)((uint16_t)payload[1] << 8U);
                if ((appearance & 0xffc0U) == 0x03c0U) {
                    info->appearance_match = 1U;
                }
            }
            break;
        case 0x06U:
        case 0x07U:
        {
            uint16_t i;
            for (i = 0U; (uint16_t)(i + NS2_GATT_UUID128_LEN) <= payload_len;
                 i = (uint16_t)(i + NS2_GATT_UUID128_LEN)) {
                if (uuid128_adv_matches(payload + i, uuid_notify_fd2)) {
                    info->service_match = 1U;
                }
            }
            break;
        }
        default:
            break;
        }

        offset = (uint16_t)(offset + field_len + 1U);
    }

    info->candidate = (uint8_t)(info->ns2_mfg_prefix ||
                                info->ns2_pairing_mfg ||
                                info->service_match ||
                                (info->appearance_match && info->nintendo_mfg));
}

uint8_t ns2_gatt_init_command_count(void)
{
    return (uint8_t)(sizeof(init_commands) / sizeof(init_commands[0]));
}

const ns2_gatt_init_command_t *ns2_gatt_init_command(uint8_t index)
{
    if (index >= ns2_gatt_init_command_count()) {
        return 0;
    }

    return &init_commands[index];
}

void ns2_gatt_set_console_mac(const uint8_t address[6])
{
    if (address == 0) {
        return;
    }
    memcpy(&init_cmd_0[10], address, 6U);
}
