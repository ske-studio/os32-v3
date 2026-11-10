/* Fixed diagnostic snapshot for the early LAN monitor. */
#include "lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "kboot_info.h"
#include "kernel_boot.h"
#include "kernel_tick.h"
#include "clock_source.h"
#include "arch_io.h"
#include "arch_cpu.h"
#include "ppc_pd.h"
#include "ppc_memmap.h"
#include "platform_memory.h"

static u32 status[17], memory[8];
static u8 bytes[256];
static struct pm_lan_range ranges[2];
static const struct gem *clock_g;

static void clock_read(u32 *tick, u32 *hz, u64 *tb, u64 *us)
{
    u32 lo, hi;
    unsigned int flags = irq_save();
    *tick = tick_count;
    *hz = ppc_boot_info()->tb_hz;
    *tb = clock_g->io.tb(clock_g->io.ctx);
    platform_clock_read_us(&lo, &hi);
    *us = ((u64)hi << 32) | lo;
    irq_restore(flags);
}

static u32 read_word(u32 pa, void *ctx)
{
    (void)ctx;
    return *(const u32 *)pm_p2v_ram(pa);
}

static int translate(u32 va, u32 *pa)
{
    struct ppc_translation t;
    if (!ppc_pd_lookup_read(ppc_root_current(), va, 0, 0, &t, read_word, 0))
        return 1;
    *pa = t.pa;
    return 0;
}

static void put32(u32 offset, u32 value)
{
    bytes[offset] = (u8)(value >> 24);
    bytes[offset + 1] = (u8)(value >> 16);
    bytes[offset + 2] = (u8)(value >> 8);
    bytes[offset + 3] = (u8)value;
}

static void pair(u32 offset, u64 value)
{
    status[offset] = (u32)(value >> 32);
    status[offset + 1] = (u32)value;
}

const struct pm_lan_diag *pm_lan_diag_snapshot(const struct pm_gmac_boot *boot,
    const struct gem *g, u32 state, u64 request_drops)
{
    const struct pm_kboot_info *info = ppc_boot_info();
    static const struct pm_lan_diag result = {
        status, 17, memory, 8, bytes, ranges, 2,
        clock_read, translate, pm_lan_copy8
    };
    u32 tick, hz, i;
    u64 tb, us;
    clock_g = g;
    status[0] = state; status[1] = g->phy_id1; status[2] = g->phy_id2;
    pair(3, g->tx_packets); pair(5, g->rx_packets); pair(7, g->rx_drops);
    pair(9, g->uncertain_tx); pair(11, request_drops);
    status[13] = g->error_status; status[14] = g->tx_status;
    status[15] = g->rx_status; status[16] = g->tx_pending;
    memory[0] = 0; memory[1] = info->admitted_end;
    memory[2] = info->image_first; memory[3] = info->image_end - info->image_first;
    memory[4] = info->hpt_pa; memory[5] = info->hpt_bytes;
    memory[6] = g->dma.pa; memory[7] = g->dma.bytes;
    ranges[0] = (struct pm_lan_range){0, g->dma.pa + GEM_POOL_RX_BUF};
    ranges[1] = (struct pm_lan_range){g->dma.pa + GEM_POOL_SIZE,
        PM_MEMORY_LEGACY_END - (g->dma.pa + GEM_POOL_SIZE)};
    for (i = 0; i < sizeof(bytes); ++i) bytes[i] = 0;
    for (i = 0; i < 17; ++i) put32(i * 4, status[i]);
    for (i = 0; i < 8; ++i) put32(72 + i * 4, memory[i]);
    clock_read(&tick, &hz, &tb, &us);
    put32(104, tick); put32(108, hz);
    put32(112, (u32)(tb >> 32)); put32(116, (u32)tb);
    put32(120, (u32)(us >> 32)); put32(124, (u32)us);
    for (i = 0; i < 16; ++i) bytes[128 + i] = boot->boot_id[i];
    for (i = 0; i < 6; ++i) { bytes[144 + i] = boot->mac[i]; bytes[150 + i] = boot->peer_mac[i]; }
    return &result;
}
