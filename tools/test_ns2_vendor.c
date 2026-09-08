#include "sf32lb52_ns2_vendor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void set_address(uint8_t command[16], uint32_t address)
{
    command[12] = (uint8_t)address;
    command[13] = (uint8_t)(address >> 8U);
    command[14] = (uint8_t)(address >> 16U);
    command[15] = (uint8_t)(address >> 24U);
}

static void test_identity_reply(void)
{
    uint8_t command[16] = {0x15, 0, 0x44, 0x01};
    uint8_t reply[SF32LB52_NS2_VENDOR_REPLY_MAX];
    static const uint8_t expected_mac[] = {
        0x2d, 0xfc, 0x27, 0xce, 0xc6, 0x38
    };
    size_t len = sf32lb52_ns2_vendor_build_reply(
        command, sizeof(command), reply, sizeof(reply));

    assert(len == 17U);
    assert(reply[0] == 0x15U && reply[1] == 0x01U);
    assert(reply[2] == 0x44U && reply[8] == 0x01U);
    assert(memcmp(reply + 11U, expected_mac, sizeof(expected_mac)) == 0);
}

static void test_flash_calibration_reply(void)
{
    uint8_t command[16] = {0x02, 0, 0x12, 0x34};
    uint8_t reply[SF32LB52_NS2_VENDOR_REPLY_MAX];
    size_t len;

    set_address(command, 0x13080U);
    len = sf32lb52_ns2_vendor_build_reply(command, sizeof(command),
                                           reply, sizeof(reply));
    assert(len == 0x50U);
    assert(reply[8] == 0x40U);
    assert(memcmp(reply + 12U, command + 12U, 4U) == 0);
    assert(reply[0x10U + 0x28U] == 0x00U);
    assert(reply[0x10U + 0x29U] == 0x08U);
    assert(reply[0x10U + 0x2aU] == 0x80U);
}

static void test_no_reply_commands(void)
{
    uint8_t command[8] = {0x10};
    uint8_t reply[SF32LB52_NS2_VENDOR_REPLY_MAX];

    assert(sf32lb52_ns2_vendor_build_reply(
               command, sizeof(command), reply, sizeof(reply)) == 0U);
    command[0] = 0x0cU;
    command[3] = 0x02U;
    assert(sf32lb52_ns2_vendor_build_reply(
               command, sizeof(command), reply, sizeof(reply)) == 0U);
}

static void test_unknown_command_ack(void)
{
    uint8_t command[8] = {0x77, 0, 0x33, 0x22, 0x11};
    uint8_t reply[SF32LB52_NS2_VENDOR_REPLY_MAX];
    size_t len = sf32lb52_ns2_vendor_build_reply(
        command, sizeof(command), reply, sizeof(reply));

    assert(len == 8U);
    assert(reply[0] == 0x77U && reply[1] == 0x01U);
    assert(reply[2] == 0x33U && reply[3] == 0x22U && reply[4] == 0x11U);
    assert(reply[5] == 0xf8U);
}

int main(void)
{
    test_identity_reply();
    test_flash_calibration_reply();
    test_no_reply_commands();
    test_unknown_command_ack();
    puts("OK NS2 vendor handshake tests");
    return 0;
}
