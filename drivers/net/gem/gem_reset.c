#include "gem.h"

/* OpenBSD gem_bitwait :470-482: one second, plus a stopped-clock bound. */
#define GEM_WAIT_POLLS 1000000U
#define GEM_RESET_REG (GEM_PCI_BANK2_OFFSET + GEM_RESET)

enum { RESET_START, RX_MAC_WAIT, RX_DMA_WAIT, RX_DELAY, RX_RESET_WAIT,
       TX_MAC_WAIT, TX_DMA_WAIT, TX_DELAY, TX_RESET_WAIT, FULL_WAIT, RESET_DONE };

static unsigned long long ticks(u32 hz, u32 us)
{
    /* Avoid a 64-bit division helper in the kernel. */
    unsigned long long n = (unsigned long long)hz * us + 999999U;
    unsigned long long q = 0;
    for (int i = 63; i >= 0; --i) {
        if ((n >> i) >= 1000000U) {
            n -= (unsigned long long)1000000U << i;
            q |= 1ULL << i;
        }
    }
    return q;
}

static void next(struct gem *g, u32 step, unsigned long long now)
{
    g->step = step;
    g->started = now;
    g->polls = 0;
}

static int wait_clear(struct gem *g, u32 reg, u32 mask, unsigned long long now)
{
    if (!(g->io.read32(g->io.ctx, reg) & mask)) return GEM_OK;
    if (now - g->started >= ticks(g->io.tb_hz, 1000000U) ||
        ++g->polls >= GEM_WAIT_POLLS) {
        g->state = GEM_STATE_QUARANTINED;
        return GEM_ETIME;
    }
    return GEM_AGAIN;
}

int gem_reset_step(struct gem *g, unsigned long long now)
{
    int r;
    u32 v;
    if (g->state == GEM_STATE_QUARANTINED) return GEM_ETIME;
    switch (g->step) {
    case RESET_START:
        g->state = GEM_STATE_RESETTING;
        g->io.write32(g->io.ctx, GEM_INTMASK, ~0U);
        /* gem_disable_rx :647-659, gem_reset_rx :583-607. */
        v = g->io.read32(g->io.ctx, GEM_MAC_RX_CONFIG);
        g->io.write32(g->io.ctx, GEM_MAC_RX_CONFIG, v & ~GEM_MAC_RX_ENABLE);
        next(g, RX_MAC_WAIT, now); return GEM_AGAIN;
    case RX_MAC_WAIT:
        r = wait_clear(g, GEM_MAC_RX_CONFIG, GEM_MAC_RX_ENABLE, now);
        if (r != GEM_OK) return r;
        g->io.write32(g->io.ctx, GEM_RX_CONFIG, 0);
        next(g, RX_DMA_WAIT, now); return GEM_AGAIN;
    case RX_DMA_WAIT:
        r = wait_clear(g, GEM_RX_CONFIG, GEM_RX_CONFIG_RXDMA_EN, now);
        if (r != GEM_OK) return r;
        next(g, RX_DELAY, now); return GEM_AGAIN;
    case RX_DELAY:
        if (now - g->started < ticks(g->io.tb_hz, 5000U)) {
            if (++g->polls >= GEM_WAIT_POLLS) { g->state = GEM_STATE_QUARANTINED; return GEM_ETIME; }
            return GEM_AGAIN;
        }
        g->io.write32(g->io.ctx, GEM_RESET_REG, GEM_RESET_RX);
        next(g, RX_RESET_WAIT, now); return GEM_AGAIN;
    case RX_RESET_WAIT:
        r = wait_clear(g, GEM_RESET_REG, GEM_RESET_RX, now);
        if (r != GEM_OK) return r;
        /* gem_disable_tx :666-678, gem_reset_tx :615-640. */
        v = g->io.read32(g->io.ctx, GEM_MAC_TX_CONFIG);
        g->io.write32(g->io.ctx, GEM_MAC_TX_CONFIG, v & ~GEM_MAC_TX_ENABLE);
        next(g, TX_MAC_WAIT, now); return GEM_AGAIN;
    case TX_MAC_WAIT:
        r = wait_clear(g, GEM_MAC_TX_CONFIG, GEM_MAC_TX_ENABLE, now);
        if (r != GEM_OK) return r;
        g->io.write32(g->io.ctx, GEM_TX_CONFIG, 0);
        next(g, TX_DMA_WAIT, now); return GEM_AGAIN;
    case TX_DMA_WAIT:
        r = wait_clear(g, GEM_TX_CONFIG, GEM_TX_CONFIG_TXDMA_EN, now);
        if (r != GEM_OK) return r;
        next(g, TX_DELAY, now); return GEM_AGAIN;
    case TX_DELAY:
        if (now - g->started < ticks(g->io.tb_hz, 5000U)) {
            if (++g->polls >= GEM_WAIT_POLLS) { g->state = GEM_STATE_QUARANTINED; return GEM_ETIME; }
            return GEM_AGAIN;
        }
        g->io.write32(g->io.ctx, GEM_RESET_REG, GEM_RESET_TX);
        next(g, TX_RESET_WAIT, now); return GEM_AGAIN;
    case TX_RESET_WAIT:
        r = wait_clear(g, GEM_RESET_REG, GEM_RESET_TX, now);
        if (r != GEM_OK) return r;
        /* gem_reset :486-501: full reset follows both halves. */
        g->io.write32(g->io.ctx, GEM_RESET_REG, GEM_RESET_RX | GEM_RESET_TX);
        next(g, FULL_WAIT, now); return GEM_AGAIN;
    case FULL_WAIT:
        r = wait_clear(g, GEM_RESET_REG, GEM_RESET_RX | GEM_RESET_TX, now);
        if (r != GEM_OK) return r;
        g->io.write32(g->io.ctx, GEM_INTMASK, ~0U);
        next(g, RESET_DONE, now); return GEM_OK;
    case RESET_DONE: return GEM_OK;
    default: return GEM_EINVAL;
    }
}

int gem_poll(struct gem *g, unsigned long long now, u32 tx_budget)
{
    u32 status, v, wr, rd;
    (void)tx_budget; /* TX completion and RX delivery are added in later stages. */
    if (g->state == GEM_STATE_QUARANTINED) return GEM_ETIME;
    if (g->state == GEM_STATE_RESETTING) return gem_reset_step(g, now);
    /* gem_eint :1151-1197 and gem_rx_watchdog :1199-1235. */
    status = g->io.read32(g->io.ctx, GEM_STATUS_ALIAS);
    if (status & (GEM_INTR_BERR | GEM_INTR_RX_TAG_ERR))
        g->error_status = g->io.read32(g->io.ctx, GEM_PCI_BANK2_OFFSET + GEM_ERROR_STATUS);
    if (status & GEM_INTR_TX_MAC) {
        g->tx_status = g->io.read32(g->io.ctx, GEM_MAC_TX_STATUS);
        if (g->tx_status & (GEM_MAC_TX_UNDERRUN | GEM_MAC_TX_PKT_TOO_LONG)) {
            g->step = RESET_START; g->state = GEM_STATE_RESETTING;
            return GEM_AGAIN;
        }
    }
    if (status & GEM_INTR_RX_MAC) {
        g->rx_status = g->io.read32(g->io.ctx, GEM_MAC_RX_STATUS);
        if ((g->rx_status & GEM_MAC_RX_OVERFLOW) && !g->rx_watching) {
            g->rx_watch_wr = g->io.read32(g->io.ctx, GEM_RX_FIFO_WR_PTR);
            g->rx_watch_rd = g->io.read32(g->io.ctx, GEM_RX_FIFO_RD_PTR);
            g->rx_watch_started = now; g->rx_watching = 1;
        }
    }
    if (g->rx_watching && now - g->rx_watch_started >= ticks(g->io.tb_hz, 400000U)) {
        wr = g->io.read32(g->io.ctx, GEM_RX_FIFO_WR_PTR);
        rd = g->io.read32(g->io.ctx, GEM_RX_FIFO_RD_PTR);
        v = g->io.read32(g->io.ctx, GEM_MAC_MAC_STATE);
        if ((v & GEM_MAC_STATE_OVERFLOW) != GEM_MAC_STATE_OVERFLOW) g->rx_watching = 0;
        else if (wr == rd || (wr == g->rx_watch_wr && rd == g->rx_watch_rd)) {
            g->rx_watching = 0; g->step = RESET_START; g->state = GEM_STATE_RESETTING;
            return GEM_AGAIN;
        } else {
            g->rx_watch_wr = wr; g->rx_watch_rd = rd; g->rx_watch_started = now;
        }
    }
    return GEM_OK;
}
