#include "platform.h"

#include <stdarg.h>

#if defined(__has_include)
#if __has_include("rtconfig.h")
#include "rtconfig.h"
#endif
#if __has_include("rtthread.h")
#include "rtthread.h"
#define SF32LB52_HAS_RTTHREAD 1
#endif
#if __has_include("bf0_hal.h")
#include "bf0_hal.h"
#define SF32LB52_HAS_HAL_GTIMER 1
#endif
#endif

#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
#include "bf0_sibles.h"
#endif

static uint8_t g_bluetooth_start_requested;

void platform_init(void)
{
    /* Board clocks, pinmux, console, and RT-Thread are initialized by the
     * selected SiFli BSP before the application entry point.  Bluetooth and
     * USB are deliberately started by their bridge modules after callbacks
     * and persisted configuration are ready. */
}

void platform_log(const char *tag, const char *fmt, ...)
{
#if defined(SF32LB52_HAS_RTTHREAD)
    va_list args;
    char line[160];

    rt_kprintf("[%s] ", tag ? tag : "log");
    va_start(args, fmt);
    rt_vsnprintf(line, sizeof(line), fmt ? fmt : "", args);
    va_end(args);
    rt_kprintf("%s\n", line);
#else
    (void)tag;
    (void)fmt;
#endif
}

void platform_sleep_ms(uint32_t delay_ms)
{
#if defined(SF32LB52_HAS_RTTHREAD)
    rt_thread_mdelay(delay_ms);
#else
    (void)delay_ms;
#endif
}

uint32_t platform_millis(void)
{
#if defined(SF32LB52_HAS_RTTHREAD)
    return (uint32_t)(((uint64_t)rt_tick_get() * 1000ULL) / RT_TICK_PER_SECOND);
#endif
    return 0U;
}

uint32_t platform_micros(void)
{
#if defined(SF32LB52_HAS_HAL_GTIMER)
    static uint32_t previous_count;
    static uint64_t extended_count;
    static uint8_t initialized;
    uint32_t count = HAL_GTIMER_READ();
    uint32_t frequency = (uint32_t)HAL_LPTIM_GetFreq();

    if (!initialized) {
        previous_count = count;
        extended_count = count;
        initialized = 1U;
    } else {
        extended_count += (uint32_t)(count - previous_count);
        previous_count = count;
    }
    if (frequency != 0U) {
        return (uint32_t)((extended_count * 1000000ULL) / frequency);
    }
#endif
    return platform_millis() * 1000U;
}

int platform_bluetooth_start_once(void)
{
#if defined(BLUETOOTH) || defined(CONFIG_BLUETOOTH)
    if (!g_bluetooth_start_requested) {
        g_bluetooth_start_requested = 1U;
        sifli_ble_enable();
    }
    return 0;
#else
    return -1;
#endif
}
