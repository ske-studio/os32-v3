#include "gem.h"

#define GEM_WAIT_POLLS 1000000U

/* OpenBSD gem_init_regs :865-952, PCI Apple GEM. DMA and MAC are stopped. */
int gem_mac_init(struct gem *g, const u8 mac[6])
{
    u32 v;
    if (!g || !mac || !g->io.read32 || !g->io.write32) return GEM_EINVAL;
#define W(reg, val) g->io.write32(g->io.ctx, (reg), (val))
    W(GEM_MAC_IPG0, 0); W(GEM_MAC_IPG1, 8); W(GEM_MAC_IPG2, 4);
    W(GEM_MAC_MAC_MIN_FRAME, 64); W(GEM_MAC_MAC_MAX_FRAME, 1522 | (0x2000U << 16));
    W(GEM_MAC_PREAMBLE_LEN, 7); W(GEM_MAC_JAM_SIZE, 4);
    W(GEM_MAC_ATTEMPT_LIMIT, 16); W(GEM_MAC_CONTROL_TYPE, 0x8088);
    W(GEM_MAC_RANDOM_SEED, ((mac[5] << 8) | mac[4]) & 0x3ff);
    W(GEM_MAC_ADDR3, 0); W(GEM_MAC_ADDR4, 0); W(GEM_MAC_ADDR5, 0);
    W(GEM_MAC_ADDR6, 1); W(GEM_MAC_ADDR7, 0xc200); W(GEM_MAC_ADDR8, 0x180);
    W(GEM_MAC_ADDR_FILTER0, 0); W(GEM_MAC_ADDR_FILTER1, 0); W(GEM_MAC_ADDR_FILTER2, 0);
    W(GEM_MAC_ADR_FLT_MASK1_2, 0); W(GEM_MAC_ADR_FLT_MASK0, 0);
    W(GEM_MAC_NORM_COLL_CNT, 0); W(GEM_MAC_FIRST_COLL_CNT, 0);
    W(GEM_MAC_EXCESS_COLL_CNT, 0); W(GEM_MAC_LATE_COLL_CNT, 0);
    W(GEM_MAC_DEFER_TMR_CNT, 0); W(GEM_MAC_PEAK_ATTEMPTS, 0);
    W(GEM_MAC_RX_FRAME_COUNT, 0); W(GEM_MAC_RX_LEN_ERR_CNT, 0);
    W(GEM_MAC_RX_ALIGN_ERR, 0); W(GEM_MAC_RX_CRC_ERR_CNT, 0); W(GEM_MAC_RX_CODE_VIOL, 0);
    W(GEM_MAC_SEND_PAUSE_CMD, 0x1bf0);
    W(GEM_CONFIG, GEM_CONFIG_TXDMA_LIMIT | GEM_CONFIG_RXDMA_LIMIT | GEM_CONFIG_BURST_INF |
      GEM_CONFIG_RONPAULBIT | GEM_CONFIG_BUG2FIX);
    W(GEM_MAC_ADDR0, (mac[4] << 8) | mac[5]);
    W(GEM_MAC_ADDR1, (mac[2] << 8) | mac[3]);
    W(GEM_MAC_ADDR2, (mac[0] << 8) | mac[1]);
    /* OpenBSD gem_iff :1590-1639: empty multicast list. */
    v = g->io.read32(g->io.ctx, GEM_MAC_RX_CONFIG);
    v &= ~(GEM_MAC_RX_HASH_FILTER | GEM_MAC_RX_PROMISCUOUS | GEM_MAC_RX_PROMISC_GRP);
    v |= GEM_MAC_RX_HASH_FILTER;
    for (u32 i = 0; i < 16; i++) W(GEM_MAC_HASH0 + i * 4, 0);
    W(GEM_MAC_RX_CONFIG, v);
#undef W
    return GEM_OK;
}

/* OpenBSD gem_mii_statchg :1364-1404 and gem_init :839-851. */
int gem_mac_enable_step(struct gem *g, unsigned long long now)
{
    u32 v;
    unsigned long long ticks;
    if (!g || !g->io.read32 || !g->io.write32 || !g->io.tb_hz) return GEM_EINVAL;
    if (g->step == 0) {
        g->io.write32(g->io.ctx, GEM_MAC_TX_CONFIG, 0);
        g->started = now; g->polls = 0; g->step = 1;
        return GEM_AGAIN;
    }
    if (g->step == 2) return GEM_OK;
    if (g->step != 1) return GEM_EINVAL;
    /* ceil(hz / 100), calculated in 32 bits without a 64-bit helper. */
    ticks = g->io.tb_hz / 100U + (g->io.tb_hz % 100U != 0);
    if (now - g->started < ticks) {
        if (++g->polls >= GEM_WAIT_POLLS) { g->state = GEM_STATE_QUARANTINED; return GEM_ETIME; }
        return GEM_AGAIN;
    }
    g->io.write32(g->io.ctx, GEM_MAC_TX_CONFIG, GEM_MAC_TX_ENA_IPG0 | GEM_MAC_TX_NGU |
                  GEM_MAC_TX_NGU_LIMIT | GEM_MAC_TX_ENABLE | GEM_MAC_TX_IGN_CARRIER | GEM_MAC_TX_IGN_COLLIS);
    g->io.write32(g->io.ctx, GEM_MAC_XIF_CONFIG, GEM_MAC_XIF_TX_MII_ENA | GEM_MAC_XIF_LINK_LED | GEM_MAC_XIF_FDPLX_LED);
    v = g->io.read32(g->io.ctx, GEM_MAC_CONTROL_CONFIG);
    g->io.write32(g->io.ctx, GEM_MAC_CONTROL_CONFIG, v & ~(GEM_MAC_CC_RX_PAUSE | GEM_MAC_CC_TX_PAUSE));
    v = g->io.read32(g->io.ctx, GEM_MAC_RX_CONFIG);
    g->io.write32(g->io.ctx, GEM_MAC_RX_CONFIG, v | GEM_MAC_RX_ENABLE | GEM_MAC_RX_STRIP_CRC);
    g->io.write32(g->io.ctx, GEM_RX_KICK, GEM_RX_PUBLISHED);
    g->step = 2;
    return GEM_OK;
}
