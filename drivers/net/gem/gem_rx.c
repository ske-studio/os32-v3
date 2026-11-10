#include "gem.h"

/* OpenBSD gemvar.h:260-271: descriptors are little-endian. */
static unsigned long long get64(const u8 *p)
{
    unsigned long long v = 0;
    for (u32 i = 0; i < 8; i++) v |= (unsigned long long)p[i] << (i * 8);
    return v;
}
static void put64(u8 *p, unsigned long long v)
{
    for (u32 i = 0; i < 8; i++) p[i] = (u8)(v >> (i * 8));
}

int gem_rx(struct gem *g, void *frame, u32 cap, u32 *bytes)
{
    u8 *base, *desc, *buf, *target;
    u32 slot, producer, length;
    unsigned long long flags;
    int result;
    if (!g || g->state != GEM_STATE_READY || g->ring_publish_step != 3)
        return GEM_EBUSY;
    if (!frame || !bytes) return GEM_EINVAL;
    base = g->dma.cpu;
    /* gem_rint:974-989: inspect at most eight descriptors per poll. */
    for (u32 seen = 0; seen < 8; seen++) {
        slot = g->rx_head;
        desc = base + GEM_POOL_RX_RING + slot * GEM_DESC_SIZE;
        if (g->io.sync(desc, GEM_DESC_SIZE, DMA_POSTREAD)) return GEM_EIO;
        flags = get64(desc + GEM_DESC_FLAGS_OFFSET);
        if (flags & GEM_RD_OWN) return GEM_AGAIN;
        buf = base + GEM_POOL_RX_BUF + slot * GEM_BUF_SIZE;
        if (g->io.sync(buf, GEM_BUF_SIZE, DMA_POSTREAD)) return GEM_EIO;
        /* gem_rint:998-1022: CRC and length are descriptor fields; FBOFF is 2. */
        length = (u32)((flags & GEM_RD_BUFSIZE) >> GEM_RD_BUFSHIFT);
        result = GEM_AGAIN;
        if ((flags & GEM_RD_BAD_CRC) || length < 14 || length > 1514) {
            g->rx_drops++;
        } else if (cap < length) {
            g->rx_drops++;
            result = GEM_ERANGE;
        } else {
            for (u32 i = 0; i < length; i++) ((u8 *)frame)[i] = buf[i + 2];
            *bytes = length;
            g->rx_packets++;
            result = GEM_OK;
        }
        /* gem_fill_rx_ring/add_rxbuf:1040-1087: refill producer, not consumer;
         * unlike BSD's mbufs, each fixed pool slot owns its own buffer. */
        producer = (slot + GEM_RX_PUBLISHED) % GEM_RING_COUNT;
        target = base + GEM_POOL_RX_BUF + producer * GEM_BUF_SIZE;
        if (g->io.sync(target, GEM_BUF_SIZE, DMA_PREREAD)) return GEM_EIO;
        desc = base + GEM_POOL_RX_RING + producer * GEM_DESC_SIZE;
        /* gemvar.h:260-271: buffer sync, address, OWN flags, descriptor sync. */
        put64(desc + GEM_DESC_ADDR_OFFSET, g->dma.bus + GEM_POOL_RX_BUF + producer * GEM_BUF_SIZE);
        put64(desc + GEM_DESC_FLAGS_OFFSET, (GEM_BUF_SIZE << GEM_RD_BUFSHIFT) | GEM_RD_OWN);
        if (g->io.sync(desc, GEM_DESC_SIZE, DMA_PREWRITE)) return GEM_EIO;
        g->rx_head = (slot + 1) % GEM_RING_COUNT;
        /* FreeBSD if_gem.c:1581-1589: fetches and kicks in groups of four. */
        if (g->rx_head % 4 == 0) {
            if (g->io.sync(base + GEM_POOL_RX_RING, GEM_RING_COUNT * GEM_DESC_SIZE, DMA_PREWRITE)) return GEM_EIO;
            g->io.write32(g->io.ctx, GEM_RX_KICK, (g->rx_head + GEM_RX_PUBLISHED) & 31);
        }
        if (result != GEM_AGAIN) return result;
    }
    return GEM_AGAIN;
}
