#include "gem.h"

static void put64(u8 *p, unsigned long long v)
{
    for (u32 i = 0; i < 8; i++) p[i] = (u8)(v >> (i * 8));
}

int gem_tx(struct gem *g, const void *frame, u32 bytes)
{
    u8 *base, *buf, *desc;
    const u8 *src = frame;
    u32 slot, length, next;
    if (!g || g->state != GEM_STATE_READY || g->ring_publish_step != 3)
        return GEM_EBUSY;
    if (!frame || bytes < 14 || bytes > 1514) return GEM_EINVAL;
    /* OpenBSD gem_start:1725-1738: reserve one descriptor. */
    if (g->tx_pending >= 31) return GEM_AGAIN;
    slot = g->tx_head;
    base = g->dma.cpu;
    buf = base + GEM_POOL_TX_BUF + slot * GEM_BUF_SIZE;
    desc = base + GEM_POOL_TX_RING + slot * GEM_DESC_SIZE;
    length = bytes < 60 ? 60 : bytes;
    for (u32 i = 0; i < bytes; i++) buf[i] = src[i];
    for (u32 i = bytes; i < length; i++) buf[i] = 0;
    /* gem_start:1759-1780: publish buffer then LE address and flags. */
    if (g->io.sync(buf, length, DMA_PREWRITE)) return GEM_EIO;
    put64(desc + GEM_DESC_ADDR_OFFSET, g->dma.bus + GEM_POOL_TX_BUF + slot * GEM_BUF_SIZE);
    put64(desc + GEM_DESC_FLAGS_OFFSET, length | GEM_TD_START_OF_PACKET | GEM_TD_END_OF_PACKET);
    /* NetBSD gem_start:1589: PREWRITE makes descriptors visible before kick. */
    if (g->io.sync(desc, GEM_DESC_SIZE, DMA_PREWRITE)) return GEM_EIO;
    next = (slot + 1) % GEM_RING_COUNT;
    /* OpenBSD gem_start:1795-1802: commit and kick the next index. */
    g->io.write32(g->io.ctx, GEM_TX_KICK, next);
    g->tx_head = next;
    if (g->tx_pending++ == 0) g->tx_started = g->io.tb(g->io.ctx);
    return GEM_OK;
}

int gem_tx_reclaim(struct gem *g, unsigned long long now, u32 budget)
{
    u32 completion, count, limit;
    u8 *base = g->dma.cpu;
    /* gem_tint:1646-1668 uses IRQ status; direct completion supports polling. */
    completion = g->io.read32(g->io.ctx, GEM_TX_COMPLETION) & GEM_TX_COMPLETION_MASK;
    if (completion >= GEM_RING_COUNT) return GEM_EIO;
    count = (completion + GEM_RING_COUNT - g->tx_tail) % GEM_RING_COUNT;
    if (count > g->tx_pending) return GEM_EIO;
    limit = budget < 8 ? budget : 8;
    if (count > limit) count = limit;
    for (u32 i = 0; i < count; i++) {
        /* gem_tint:1656-1668: POSTWRITE precedes ownership return. */
        if (g->io.sync(base + GEM_POOL_TX_BUF + g->tx_tail * GEM_BUF_SIZE,
                       GEM_BUF_SIZE, DMA_POSTWRITE)) return GEM_EIO;
        g->tx_tail = (g->tx_tail + 1) % GEM_RING_COUNT;
        g->tx_pending--;
        g->tx_packets++;
        g->tx_started = now;
    }
    /* gem_start:1802: five-second watchdog; retain uncertain packets on reset. */
    if (g->tx_pending && now - g->tx_started >= (unsigned long long)g->io.tb_hz * 5) {
        gem_enter_reset(g);
        return GEM_AGAIN;
    }
    return GEM_OK;
}
