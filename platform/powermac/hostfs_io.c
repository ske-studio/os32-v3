/* Single-task cooperative transport. EE stays clear across USER syscalls;
 * the timebase, rather than DEC ticks, supplies the RPC's 10ms clock. */
#include "hostfs.h"
#include "fs/serialfs/sfs_lan.h"
#include "kernel/irq/irq.h"
#include "os32_kapi_shared.h"

struct pm_hfs_transport pm_hostfs_transport;

#ifndef PM_HFS_FAKE_TB
/* MPC7450UM 4.6.9: coherent upper/lower/upper, as terminal_input.c. */
static u64 pm_hfs_tb(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((u64)hi << 32) | lo;
}
#else
u64 pm_hfs_tb(void);
#endif

int pm_hfs_set_clock(u32 tb_hz)
{
    /* Same admission range as the PowerMac clock provider. */
    if (tb_hz < 1000000u || tb_hz > 1000000000u) return -1;
    pm_hostfs_transport.tb_hz = tb_hz;
    return 0;
}

static u32 pm_hfs_now(void *ctx)
{
    struct pm_hfs_transport *t = ctx;
    /* lib/divdi3.c supplies the PPC division; never link libgcc. */
    return (u32)(pm_hfs_tb() / (t->tb_hz / 100u));
}

static int pm_hfs_can_wait(void *ctx)
{
    (void)ctx;
    return kctx_irq_depth == 0;
}

static void pm_hfs_idle(void *ctx)
{
    if (pm_hfs_can_wait(ctx)) pm_lan_poll(pm_hfs_tb());
}

int pm_hfs_rx_frame(const u8 *frame, u32 len)
{
    struct pm_hfs_transport *t = &pm_hostfs_transport;
    u32 i, payload;
    if (t->rx_pos != t->rx_end || len < SFS_HDR_LEN + SFS_CRC_LEN ||
        len > sizeof(t->rx)) return -1;
    if (frame[0] != SFS_ENQ || frame[SFS_OFF_MAGIC1] != SFS_MAGIC1 ||
        frame[SFS_OFF_MAGIC2] != SFS_MAGIC2) return -1;
    payload = sfs_get16(frame + SFS_OFF_LEN);
    if (payload > SFS_LAN_MAX_PAYLOAD ||
        len != SFS_HDR_LEN + payload + SFS_CRC_LEN) return -1;
    if (sfs_crc32(frame + SFS_OFF_TYPE, len - SFS_PREAMBLE_LEN - SFS_CRC_LEN) !=
        sfs_get32(frame + len - SFS_CRC_LEN)) return -1;
    for (i = 0; i < len; ++i) t->rx[i] = frame[i];
    t->rx_pos = 0;
    t->rx_end = len;
    return 0;
}

static int pm_hfs_get(void *ctx)
{
    struct pm_hfs_transport *t = ctx;
    return t->rx_pos < t->rx_end ? t->rx[t->rx_pos++] : -1;
}

static int pm_hfs_put_until(void *ctx, const u8 *buf, u32 n, u32 deadline)
{
    int r;
    if (!pm_hfs_can_wait(ctx)) return -1;
    for (;;) {
        if (!pm_lan_fs_running()) return -1;
        if ((i32)(pm_hfs_now(ctx) - deadline) >= 0) return OS32_ERR_TIMEOUT;
        r = pm_lan_fs_send(buf, n);
        if (r == LAN_OK) return 0;
        if (r != LAN_EBUSY) return -1;
        pm_hfs_idle(ctx);
    }
}

static int pm_hfs_put(void *ctx, const u8 *buf, u32 n)
{
    u32 deadline = pm_hfs_now(ctx) + sfs_ms_ticks(SFS_LAN_PUT_MS);
    return pm_hfs_put_until(ctx, buf, n, deadline) == 0 ? 0 : -1;
}

const SfsIo pm_hostfs_io = {
    pm_hfs_put, pm_hfs_get, pm_hfs_now, pm_hfs_idle,
    pm_hfs_can_wait, pm_hfs_put_until
};
