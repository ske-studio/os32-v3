/* Run unchanged on the B17 1kHz image in CUI and from the GUI terminal.
 * Values use milliseconds for timeGetTime and microseconds for QPC/Sleep.
 * Timing thresholds belong to guest acceptance; this program reports samples. */
#include "os32api.h"
#include "mmtime.h"
#include <stdio.h>

#define SAMPLES 1000
static u32 sleep_us[SAMPLES];
static u32 event_count, first_lo, first_hi, last_lo, last_hi;

static unsigned long long qpc(void)
{
    u32 lo, hi;
    mmt_now_us(&lo, &hi);
    return ((unsigned long long)hi << 32) | lo;
}

static void timer_cb(u32 id, u32 msg, u32 user, u32 dw1, u32 dw2)
{
    (void)id; (void)msg; (void)user; (void)dw1; (void)dw2;
    mmt_now_us(&last_lo, &last_hi);
    if (!event_count) { first_lo = last_lo; first_hi = last_hi; }
    ++event_count;
}

/* Insertion sort keeps the diagnostic independent of allocator/qsort costs. */
static void sort_samples(void)
{
    for (u32 i = 1; i < SAMPLES; ++i) {
        u32 value = sleep_us[i], j = i;
        while (j && sleep_us[j - 1] > value) {
            sleep_us[j] = sleep_us[j - 1];
            --j;
        }
        sleep_us[j] = value;
    }
}

int main(int argc, char **argv, KernelAPI *api)
{
    u32 lo, hi, id, ms_start, ms_end;
    unsigned long long start, end;
    int rc;
    (void)argc; (void)argv;
    /* Direct status makes a fallback-only run visible in the transcript. */
    rc = api->sys_time_now(&lo, &hi);
    printf("sys_time_now_rc %d\n", rc);
    if (rc) return 1;
    printf("qpf_hz %lu\n", MMT_QPF);
    printf("qpc_lo %lu\nqpc_hi %lu\n", lo, hi);
    ms_start = mmt_now_ms();
    start = qpc();
    mmt_sleep_ms(100);
    end = qpc();
    ms_end = mmt_now_ms();
    printf("timeGetTime_start_ms %lu\n", ms_start);
    printf("timeGetTime_elapsed_ms %lu\n", ms_end - ms_start);
    printf("qpc_elapsed_us %lu\n", (u32)(end - start));

    id = mmt_set_event(10, 0, timer_cb, 0, MMT_PERIODIC);
    printf("periodic_delay_ms %u\n", 10u);
    if (!id) return 1;
    start = qpc();
    mmt_sleep_ms(1000);
    end = qpc();
    rc = mmt_kill_event(id);
    mmt_deliver();
    printf("periodic_elapsed_us %lu\n", (u32)(end - start));
    printf("periodic_count %lu\n", event_count);
    printf("periodic_first_lo %lu\nperiodic_first_hi %lu\n", first_lo, first_hi);
    printf("periodic_last_lo %lu\nperiodic_last_hi %lu\n", last_lo, last_hi);
    printf("periodic_kill_rc %d\n", rc);
    if (rc) return 1;

    for (u32 i = 0; i < SAMPLES; ++i) {
        start = qpc();
        mmt_sleep_ms(1);
        end = qpc();
        sleep_us[i] = (u32)(end - start);
    }
    sort_samples();
    printf("sleep1_samples %u\n", SAMPLES);
    printf("sleep1_min_us %lu\n", sleep_us[0]);
    printf("sleep1_p50_us %lu\n", sleep_us[(SAMPLES + 1) / 2 - 1]);
    printf("sleep1_p99_us %lu\n", sleep_us[(SAMPLES * 99 + 99) / 100 - 1]);
    printf("sleep1_max_us %lu\n", sleep_us[SAMPLES - 1]);
    return 0;
}
