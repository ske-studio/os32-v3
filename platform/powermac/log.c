/* Kboot stream registry, independent of the GUI con_sink API.
 * ESCC protocol: escc.c (NetBSD zs.c:905-923); ordering:
 * arch/ppc/entry.S:86-95. PEM 7.5: real data accesses are cached,
 * so emergency output also requires DR=1 and the I/G aperture. */
#include "kboot_log.h"
#include "ofw.h"
#if KBOOT_SHELL || KBOOT_SH
#include "port_service.h"
#endif
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
#if KBOOT_SHELL || KBOOT_SH
/* Zilog UM0109 §3, BRG time constant with the MacIO 3.6864 MHz RTxC
 * and x16 async clock (NetBSD macppc zs.c:303,321,669-673). */
enum {
    ESCC_RTXC_HZ = 3686400,
    ESCC_BRG_RATE = ESCC_RTXC_HZ / (2 * 16),
    ESCC_WR4 = 4,
    ESCC_WR4_ASYNC_1STOP_X16 = 0x44,
    ESCC_WR11 = 11,
    ESCC_WR11_TX_RX_BRG = 0x50,
    ESCC_WR12 = 12,
    ESCC_WR13 = 13,
    ESCC_WR14 = 14,
    ESCC_WR14_BRG_RTXC = 0x00,
    ESCC_WR14_BRG_ENABLE = 0x01,
};
static u32 escc_baud;

static void escc_write_register(unsigned reg, unsigned value)
{
    ppc_store8(serial.csr, reg);
    ppc_store8(serial.csr, value);
}

static int escc_port_init_vfast(u32 baud)
{
    if (!baud || baud > ESCC_BRG_RATE / 2 || ESCC_BRG_RATE % baud)
        return -2;
    if (baud == escc_baud)
        return -1;

    unsigned time_constant = ESCC_BRG_RATE / baud - 2;
    if (time_constant > 0xffff || pm_escc_drain(&serial))
        return -2;

    escc_write_register(ESCC_WR14, ESCC_WR14_BRG_RTXC);
    escc_write_register(ESCC_WR4, ESCC_WR4_ASYNC_1STOP_X16);
    escc_write_register(ESCC_WR11, ESCC_WR11_TX_RX_BRG);
    escc_write_register(ESCC_WR12, time_constant & 0xff);
    escc_write_register(ESCC_WR13, time_constant >> 8);
    escc_write_register(ESCC_WR14, ESCC_WR14_BRG_RTXC | ESCC_WR14_BRG_ENABLE);
    escc_baud = baud;
    return -1; /* The exact rate runs on ESCC's normal clock path. */
}

static void escc_port_init(u32 baud) { (void)escc_port_init_vfast(baud); }
static int escc_port_putchar(char c) { return pm_escc_write(&serial, &c, 1); }
static int escc_port_initialized(void) { return escc_baud != 0; }

static int escc_port_status(u32 *mode, u32 *baud, u32 *fifo)
{
    if (mode) *mode = 0;
    if (baud) *baud = escc_baud;
    if (fifo) *fifo = 1; /* ESCC hardware receive FIFO. */
    return escc_baud ? 0 : -1;
}

/* PortOps requires a complete table. Keep the absent slots' default behavior. */
static void escc_port_empty_diag(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{
    if (oe) *oe = 0;
    if (fe) *fe = 0;
    if (pe) *pe = 0;
    if (overflow) *overflow = 0;
}

static const PortOps escc_port = {
    .init = escc_port_init,
    .getchar_kapi = pm_escc_getchar_kapi,
    .putchar_kapi = escc_port_putchar,
    .trygetchar = pm_escc_trygetchar,
    .is_initialized = escc_port_initialized,
    .init_vfast = escc_port_init_vfast,
    .get_status = escc_port_status,
    .diag = escc_port_empty_diag,
};
#endif

int pm_log_escc_init(u32 csr_alias, u32 data_alias)
{
    if (!csr_alias || !data_alias) return -1;
    /* Do not change an already registered sink on a rejected second init. */
    if (pm_log_add_sink(&serial_sink)) return -1;
    serial.csr = csr_alias;
    serial.data = data_alias;
    pm_escc_rx_bind(&serial);
#if KBOOT_SHELL || KBOOT_SH
    port_register(&escc_port);
#endif
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
