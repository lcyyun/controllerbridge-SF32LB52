#ifndef SF32LB52_NS2_VENDOR_H
#define SF32LB52_NS2_VENDOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SF32LB52_NS2_VENDOR_REPLY_MAX 128U

/* Builds the Nintendo vendor-interface reply used by the Switch 2 USB
 * handshake. A zero return means the command intentionally has no reply. */
size_t sf32lb52_ns2_vendor_build_reply(const uint8_t *command,
                                       size_t command_len,
                                       uint8_t *reply,
                                       size_t reply_capacity);

#ifdef __cplusplus
}
#endif

#endif /* SF32LB52_NS2_VENDOR_H */
