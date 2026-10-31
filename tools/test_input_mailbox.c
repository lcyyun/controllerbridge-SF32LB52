/* Link only the exercised app callback/mailbox sections. No device or SDK. */
#include "../src/sf32lb52_app.c"
#include <assert.h>
#include <stdio.h>

static uint32_t now;
uint32_t platform_millis(void) { return now; }
uint32_t platform_micros(void) { return now * 1000U; }

int main(void)
{
    uint8_t ns2[60] = {0};
    uint8_t ds5[63] = {0};
    pending_ns2_input_t n;
    pending_ds5_input_t d;
    now = 10U;
    ns2[0] = 1U;
    bridge_on_ns2_input(SF32LB52_NS2_NOTIFY_KIND_FD2, ns2, sizeof(ns2));
    now = 11U;
    ns2[0] = 2U;
    bridge_on_ns2_input(SF32LB52_NS2_NOTIFY_KIND_FD2, ns2, sizeof(ns2));
    now = 200U;
    bridge_on_ns2_input(SF32LB52_NS2_NOTIFY_KIND_FD2, ns2, sizeof(ns2));
    assert(bridge_take_ns2_input(&n));
    assert(n.source_report_count_valid && n.source_report_count == 2U);
    assert(n.timestamp_ms == 11U && n.data[0] == 2U);
    assert(!bridge_take_ns2_input(&n));
    ns2_input_epoch++;
    now = 201U;
    bridge_on_ns2_input(SF32LB52_NS2_NOTIFY_KIND_FD2, ns2, sizeof(ns2));
    assert(bridge_take_ns2_input(&n));
    assert(n.source_report_count == 3U && n.timestamp_ms == 201U);

    now = 300U;
    ds5[27] = 5U;
    bridge_on_ds5_input(ds5, sizeof(ds5), NULL);
    now = 301U;
    ds5[6] = 1U;
    bridge_on_ds5_input(ds5, sizeof(ds5), NULL);
    now = 500U;
    bridge_on_ds5_input(ds5, sizeof(ds5), NULL);
    assert(bridge_take_ds5_input(&d));
    assert(d.source_report_count == 2U && d.timestamp_ms == 301U);
    ds5_input_counter.reports = UINT32_MAX;
    ds5[6] = 2U;
    now = 501U;
    bridge_on_ds5_input(ds5, sizeof(ds5), NULL);
    assert(bridge_take_ds5_input(&d));
    assert(d.source_report_count == 0U && d.timestamp_ms == 501U);
    puts("OK actual input callbacks: coalesced fresh count, duplicate expiry, epoch and wrap");
    return 0;
}
