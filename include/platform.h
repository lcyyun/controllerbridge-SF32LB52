#ifndef NS2PRO_BRIDGE_PLATFORM_H
#define NS2PRO_BRIDGE_PLATFORM_H

#include <stdint.h>

void platform_init(void);
void platform_log(const char *tag, const char *fmt, ...);
void platform_sleep_ms(uint32_t delay_ms);
uint32_t platform_millis(void);
uint32_t platform_micros(void);
int platform_bluetooth_start_once(void);

#endif /* NS2PRO_BRIDGE_PLATFORM_H */
