#include "gem.h"

#define RX_RING 0x1000u
#define RX_BUFFERS 0x20000u
#define BUFFER_SIZE 2048u
#define POOL_SIZE 0x30000u

/* gemvar.h:213-216,260-271: PCI descriptors are little-endian. */
static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}
static void put64(u8 *p, u32 lo, u32 hi)
{
    put32(p, lo); put32(p + 4, hi);
}

int gem_ring_prepare(struct gem *g, struct gem_dma *d)
{
    u8 *base, *desc;
    u32 i;
    if (!g || !d || !d->cpu || !g->io.sync || d->bytes < POOL_SIZE ||
        (d->bus & 0xfffu) || d->bus > 0xffffffffu - (POOL_SIZE - 1))
        return GEM_EINVAL;
    if (g->ring_prepare_step && (g->dma.cpu != d->cpu || g->dma.bus != d->bus))
        return GEM_EINVAL;
    base = d->cpu;
    if (!g->ring_prepare_step) {
        g->dma = *d;
        /* OpenBSD gem_meminit:685-709: zero and sync TX and RX rings. */
        for (i = 0; i < GEM_RING_COUNT * GEM_DESC_SIZE; i++) {
            base[i] = 0; base[RX_RING + i] = 0;
        }
        g->ring_prepare_step = 1;
    }
    if (g->ring_prepare_step == 1) {
        if (g->io.sync(base, GEM_RING_COUNT * GEM_DESC_SIZE, DMA_PREWRITE)) return GEM_EIO;
        g->ring_prepare_step = 2;
    }
    /* gem_meminit:704-709; gemvar.h:260-271: buffer, address, OWN, sync. */
    while (g->ring_prepare_step == 2 && g->ring_cursor < GEM_RX_PUBLISHED) {
        i = g->ring_cursor;
        if (g->io.sync(base + RX_BUFFERS + i * BUFFER_SIZE, BUFFER_SIZE, DMA_PREREAD)) return GEM_EIO;
        desc = base + RX_RING + i * GEM_DESC_SIZE;
        put64(desc + GEM_DESC_ADDR_OFFSET, d->bus + RX_BUFFERS + i * BUFFER_SIZE, 0);
        put64(desc + GEM_DESC_FLAGS_OFFSET, (BUFFER_SIZE << GEM_RD_BUFSHIFT) | (u32)GEM_RD_OWN, 0);
        g->ring_cursor++;
    }
    if (g->ring_prepare_step == 2) g->ring_prepare_step = 3;
    if (g->ring_prepare_step == 3) {
        if (g->io.sync(base + RX_RING, GEM_RING_COUNT * GEM_DESC_SIZE, DMA_PREWRITE)) return GEM_EIO;
        g->ring_prepare_step = 4;
    }
    return GEM_OK;
}

int gem_ring_publish(struct gem *g)
{
    u32 fifo;
    if (!g || !g->io.write32 || !g->io.read32 || !g->io.master_enable || g->ring_prepare_step != 4) return GEM_EINVAL;
    /* OpenBSD gem_init:787-809: base addresses and polling masks. */
    if (g->ring_publish_step == 0) {
        g->io.write32(g->io.ctx, GEM_TX_RING_PTR_HI, 0);
        g->io.write32(g->io.ctx, GEM_TX_RING_PTR_LO, g->dma.bus);
        g->io.write32(g->io.ctx, GEM_RX_RING_PTR_HI, 0);
        g->io.write32(g->io.ctx, GEM_RX_RING_PTR_LO, g->dma.bus + RX_RING);
        g->io.write32(g->io.ctx, GEM_INTMASK, ~0u);
        g->io.write32(g->io.ctx, GEM_MAC_RX_MASK, GEM_MAC_RX_DONE | GEM_MAC_RX_FRAME_CNT);
        g->io.write32(g->io.ctx, GEM_MAC_TX_MASK, 0xffff);
        g->io.write32(g->io.ctx, GEM_MAC_CONTROL_MASK, 0);
        g->ring_publish_step = 1;
    }
    if (g->ring_publish_step == 1) {
        if (g->io.master_enable(g->io.ctx)) return GEM_EIO;
        g->ring_publish_step = 2;
    }
    /* gem_init:811-834: DMA enable follows master readback; MAC stays off. */
    if (g->ring_publish_step == 2) {
        g->io.write32(g->io.ctx, GEM_TX_CONFIG, GEM_RING_SZ_32 | ((0x04ffu << 10) & GEM_TX_CONFIG_TXFIFO_TH) | GEM_TX_CONFIG_TXDMA_EN);
        g->io.write32(g->io.ctx, GEM_TX_KICK, 0);
        g->io.write32(g->io.ctx, GEM_RX_CONFIG, GEM_RING_SZ_32 | (GEM_THRSH_1024 << GEM_RX_CONFIG_FIFO_THRS_SHIFT) | (2u << GEM_RX_CONFIG_FBOFF_SHFT) | GEM_RX_CONFIG_RXDMA_EN);
        fifo = g->io.read32(g->io.ctx, GEM_RX_FIFO_SIZE) * 64u;
        g->io.write32(g->io.ctx, GEM_RX_PAUSE_THRESH, (3 * fifo / 256) | ((fifo / 256) << 12));
        g->io.write32(g->io.ctx, GEM_RX_BLANKING, (6 << 12) | 6);
        g->ring_publish_step = 3;
    }
    /* FreeBSD gem_rint:1581-1589: RX kick is deferred to MAC enable. */
    return GEM_OK;
}
