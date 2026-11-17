/* Kboot stream registry, independent of the GUI con_sink API.
 * ESCC protocol: escc.c (NetBSD zs.c:905-923); ordering:
 * arch/ppc/entry.S:86-95. PEM 7.5: real data accesses are cached,
 * so emergency output also requires DR=1 and the I/G aperture. */
#include "kboot_log.h"
#include "ofw.h"
static const struct pm_log_sink *sinks[PM_LOG_SINK_MAX];
static struct pm_log_stats stats[PM_LOG_SINK_MAX];
int pm_log_stream_only;
static u32 sink_count;
static struct pm_escc serial;
static void add_sat(u32 *v, u32 n)
{
    *v = n > (u32)-1 - *v ? (u32)-1 : *v + n;
}
int pm_log_add_sink(const struct pm_log_sink *sink)
{
    if (!sink || !sink->write || sink_count >= PM_LOG_SINK_MAX) return -1;
    for (u32 i = 0; i < sink_count; i++)
        if (sinks[i] == sink) return -1;
    sinks[sink_count++] = sink;
    return 0;
}
void pm_log_write(const char *bytes, u32 n)
{
    if (!n) return;
    if (!sink_count) {
        add_sat(&stats[0].dropped, n);
        return;
    }
    for (u32 i = 0; i < sink_count; i++) {
        if (!bytes || sinks[i]->write(sinks[i]->ctx, bytes, n)) {
            add_sat(&stats[i].failures, 1);
            add_sat(&stats[i].dropped, n);
        }
    }
}
int pm_log_get_stats(u32 slot, struct pm_log_stats *out)
{
    if (slot >= PM_LOG_SINK_MAX || !out) return -1;
    *out = stats[slot];
    return 0;
}
static int serial_write(void *ctx, const char *bytes, u32 n)
{
    return pm_escc_write(ctx, bytes, n);
}
static const struct pm_log_sink serial_sink = { serial_write, &serial };
int pm_log_escc_init(u32 csr_alias, u32 data_alias)
{
    if (!csr_alias || !data_alias) return -1;
    /* Do not change an already registered sink on a rejected second init. */
    if (pm_log_add_sink(&serial_sink)) return -1;
    serial.csr = csr_alias;
    serial.data = data_alias;
    return 0;
}
int pm_log_fatal_hex(u32 csr_alias, u32 data_alias, const u32 *words, u32 n)
{
    struct pm_escc emergency = { csr_alias, data_alias, 0 };
    if (!csr_alias || !data_alias || (!words && n)) return -1;
    if (n > PM_LOG_FATAL_WORDS) n = PM_LOG_FATAL_WORDS;
    if (pm_escc_write(&emergency, "FATAL", 5)) return -1;
    for (u32 i = 0; i < n; i++) {
        char b[9];
        b[0] = ' ';
        for (u32 j = 0; j < 8; j++)
            b[j + 1] = "0123456789abcdef"[(words[i] >> (28 - 4*j)) & 15];
        if (pm_escc_write(&emergency, b, sizeof(b))) return -1;
    }
    if (pm_escc_write(&emergency, "\r\n", 2)) return -1;
    return pm_escc_drain(&emergency);
}
