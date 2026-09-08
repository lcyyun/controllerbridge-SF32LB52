#include "sf32lb52_ns2_vendor.h"

#include <string.h>

#define NS2_STICK_CENTER_12BIT 2048U
#define NS2_STICK_RANGE_12BIT 2048U

static void write_bytes(uint8_t *out, size_t out_len, size_t offset,
                        const uint8_t *data, size_t data_len)
{
    size_t copy_len;

    if (out == 0 || data == 0 || offset >= out_len) {
        return;
    }
    copy_len = data_len;
    if (copy_len > out_len - offset) {
        copy_len = out_len - offset;
    }
    memcpy(out + offset, data, copy_len);
}

static void pack12_pair(uint8_t *out, size_t offset, uint16_t x, uint16_t y)
{
    out[offset] = (uint8_t)(x & 0xffU);
    out[offset + 1U] =
        (uint8_t)(((x >> 8U) & 0x0fU) | ((y & 0x0fU) << 4U));
    out[offset + 2U] = (uint8_t)((y >> 4U) & 0xffU);
}

static void write_default_stick_calibration(uint8_t *data, size_t data_len)
{
    uint8_t calibration[9];

    pack12_pair(calibration, 0U, NS2_STICK_RANGE_12BIT,
                NS2_STICK_RANGE_12BIT);
    pack12_pair(calibration, 3U, NS2_STICK_CENTER_12BIT,
                NS2_STICK_CENTER_12BIT);
    pack12_pair(calibration, 6U, NS2_STICK_RANGE_12BIT,
                NS2_STICK_RANGE_12BIT);
    write_bytes(data, data_len, 0x28U, calibration, sizeof(calibration));
}

static uint32_t command_address(const uint8_t *command, size_t command_len)
{
    if (command == 0 || command_len < 16U) {
        return 0U;
    }
    return (uint32_t)command[12] |
           ((uint32_t)command[13] << 8U) |
           ((uint32_t)command[14] << 16U) |
           ((uint32_t)command[15] << 24U);
}

static size_t flash_read_length(uint32_t address)
{
    if (address == 0x13040U) {
        return 0x10U;
    }
    if (address == 0x13100U) {
        return 0x18U;
    }
    if (address == 0x13060U) {
        return 0x20U;
    }
    return 0x40U;
}

static size_t build_ack(const uint8_t *command, size_t command_len,
                        uint8_t *reply, size_t reply_capacity,
                        size_t reply_len)
{
    if (command == 0 || reply == 0 || reply_len == 0U ||
        reply_len > reply_capacity) {
        return 0U;
    }
    memset(reply, 0, reply_len);
    reply[0] = command[0];
    if (reply_len > 1U) reply[1] = 0x01U;
    if (reply_len > 2U && command_len > 2U) reply[2] = command[2];
    if (reply_len > 3U && command_len > 3U) reply[3] = command[3];
    if (reply_len > 4U && command_len > 4U) reply[4] = command[4];
    if (reply_len > 5U) reply[5] = 0xf8U;
    return reply_len;
}

static size_t build_flash_read_reply(const uint8_t *command,
                                     size_t command_len,
                                     uint8_t *reply,
                                     size_t reply_capacity)
{
    uint32_t address;
    size_t data_len;
    size_t reply_len;
    uint8_t *data;

    if (command_len < 16U || reply == 0) {
        return 0U;
    }
    address = command_address(command, command_len);
    data_len = flash_read_length(address);
    reply_len = 0x10U + data_len;
    if (reply_len > reply_capacity) {
        return 0U;
    }

    memset(reply, 0, reply_len);
    data = reply + 0x10U;
    if (address == 0x13000U) {
        static const uint8_t serial[] = {
            'H', 'A', '2', 'F', '8', '3', 'J', 'F'
        };
        write_bytes(data, data_len, 2U, serial, sizeof(serial));
    }
    if (address == 0x13080U || address == 0x130c0U) {
        memset(data, 0xff, data_len);
        write_default_stick_calibration(data, data_len);
    }
    if (address == 0x1fc040U || address == 0x1fc080U ||
        address == 0x13060U) {
        memset(data, 0xff, data_len);
    }
    if (address == 0x13040U) {
        static const uint8_t block[] = {
            0x16, 0xf4, 0xd3, 0x41, 0x48, 0xce, 0x85, 0xba,
            0xf1, 0x05, 0x71, 0xba, 0x1f, 0x27, 0xcb, 0x3b,
        };
        write_bytes(data, data_len, 0U, block, sizeof(block));
    }
    if (address == 0x13100U) {
        static const uint8_t block[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x2d, 0x10, 0xa7, 0x3d,
            0xe7, 0x49, 0x35, 0x3c, 0xa4, 0x2d, 0x20, 0x41,
        };
        write_bytes(data, data_len, 0U, block, sizeof(block));
    }

    reply[0] = 0x02U;
    reply[1] = 0x01U;
    reply[2] = command[2];
    reply[3] = command[3];
    reply[5] = 0xf8U;
    reply[8] = (uint8_t)data_len;
    memcpy(reply + 12U, command + 12U, 4U);
    return reply_len > 0x50U ? 0x50U : reply_len;
}

size_t sf32lb52_ns2_vendor_build_reply(const uint8_t *command,
                                       size_t command_len,
                                       uint8_t *reply,
                                       size_t reply_capacity)
{
    uint8_t opcode;
    uint8_t argument;
    size_t len;

    if (command == 0 || command_len == 0U || reply == 0 ||
        reply_capacity == 0U) {
        return 0U;
    }
    opcode = command[0];
    argument = command_len > 3U ? command[3] : 0U;

    if (command_len >= 16U && opcode == 0x02U) {
        return build_flash_read_reply(command, command_len, reply,
                                      reply_capacity);
    }
    if ((opcode == 0x0cU && argument == 0x02U) || opcode == 0x10U) {
        return 0U;
    }
    if (opcode == 0x03U && argument == 0x0dU) {
        len = build_ack(command, command_len, reply, reply_capacity, 12U);
        if (len != 0U) reply[8] = 0x01U;
        return len;
    }
    if (opcode == 0x15U && argument == 0x01U) {
        static const uint8_t mac_le[] = {
            0x2d, 0xfc, 0x27, 0xce, 0xc6, 0x38
        };
        len = build_ack(command, command_len, reply, reply_capacity, 17U);
        if (len != 0U) {
            reply[8] = 0x01U;
            reply[9] = 0x04U;
            reply[10] = 0x01U;
            write_bytes(reply, len, 11U, mac_le, sizeof(mac_le));
        }
        return len;
    }
    if (opcode == 0x15U && argument == 0x02U) {
        len = build_ack(command, command_len, reply, reply_capacity, 25U);
        if (len != 0U) reply[8] = 0x01U;
        return len;
    }
    if (opcode == 0x15U && argument == 0x03U) {
        len = build_ack(command, command_len, reply, reply_capacity, 9U);
        if (len != 0U) reply[8] = 0x01U;
        return len;
    }
    if (opcode == 0x11U) {
        static const uint8_t payload[] = {
            0x20, 0x03, 0x00, 0x00, 0x0a, 0xe8, 0x1c, 0x3b,
            0x79, 0x7d, 0x8b, 0x3a, 0x0a, 0xe8, 0x9c, 0x42,
            0x58, 0xa0, 0x0b, 0x42, 0x0a, 0xe8, 0x9c, 0x41,
            0x58, 0xa0, 0x0b, 0x41,
        };
        len = build_ack(command, command_len, reply, reply_capacity, 37U);
        if (len != 0U) {
            reply[8] = 0x01U;
            write_bytes(reply, len, 9U, payload, sizeof(payload));
        }
        return len;
    }
    if (opcode == 0x01U && argument == 0x0cU) {
        static const uint8_t payload[] = {0x61, 0x12, 0x50, 0x10};
        len = build_ack(command, command_len, reply, reply_capacity, 12U);
        if (len != 0U) write_bytes(reply, len, 8U, payload, sizeof(payload));
        return len;
    }
    if (opcode == 0x03U && argument == 0x01U) {
        len = build_ack(command, command_len, reply, reply_capacity, 16U);
        if (len != 0U) {
            reply[10] = 0x40U;
            reply[11] = 0xf0U;
            reply[14] = 0x60U;
        }
        return len;
    }
    return build_ack(command, command_len, reply, reply_capacity, 8U);
}
