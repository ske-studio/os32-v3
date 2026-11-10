#include "drivers/net/gem/gem.h"
#define RESET_REG (GEM_PCI_BANK2_OFFSET + GEM_RESET)
static u32 regs[0x10000 / 4], writes, last_rx_dma, last_tx_dma, tick;
static u32 pending[0x10000 / 4], stuck, target[0x10000 / 4];
static u32 log_reg[16], log_val[16];
static int failure;
static void say(const char *s) { unsigned n = 0; while (s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
static void fail(const char *s) { if (!failure) { say(s); failure = 1; } }
static u32 rd(void *ctx, u32 reg) {
    (void)ctx;
    if (pending[reg/4] && reg != stuck && --pending[reg/4] == 0) {
        regs[reg/4] = target[reg/4];
        if (reg == GEM_RX_CONFIG) last_rx_dma = tick;
        if (reg == GEM_TX_CONFIG) last_tx_dma = tick;
    }
    return regs[reg/4];
}
static void wr(void *ctx, u32 reg, u32 val) {
    (void)ctx; if (writes < 16) { log_reg[writes] = reg; log_val[writes] = val; } writes++;
    if (pending[RESET_REG/4]) fail("H:reset-pending\n");
    if (reg == GEM_RX_CONFIG && (regs[GEM_MAC_RX_CONFIG/4] & 1)) fail("H:A5 reset-active\n");
    if (reg == GEM_TX_CONFIG && (regs[GEM_MAC_TX_CONFIG/4] & 1)) fail("H:A5 reset-active\n");
    if (reg == RESET_REG) {
        if ((val & GEM_RESET_RX) && (regs[GEM_RX_CONFIG/4] & 1)) fail("H:dma-active\n");
        if ((val & GEM_RESET_TX) && (regs[GEM_TX_CONFIG/4] & 1)) fail("H:dma-active\n");
        if (val == GEM_RESET_RX && tick - last_rx_dma < 5) fail("H:delay\n");
        if (val == GEM_RESET_TX && tick - last_tx_dma < 5) fail("H:delay\n");
        regs[reg/4] = val; target[reg/4] = 0; pending[reg/4] = 2; return;
    }
    if (reg == GEM_MAC_RX_CONFIG || reg == GEM_MAC_TX_CONFIG || reg == GEM_RX_CONFIG || reg == GEM_TX_CONFIG) {
        u32 old = regs[reg/4];
        regs[reg/4] = val;
        pending[reg/4] = 0;
        if ((old & 1) && !(val & 1)) {
            regs[reg/4] |= 1; target[reg/4] = val; pending[reg/4] = 2;
        }
        return;
    }
    regs[reg/4] = val;
}
static unsigned long long tb(void *ctx) { (void)ctx; return tick; }
static void init(struct gem *g) { *g = (struct gem){0}; g->io.read32=rd; g->io.write32=wr; g->io.tb=tb; g->io.tb_hz=1000; regs[GEM_MAC_RX_CONFIG/4]=1; regs[GEM_MAC_TX_CONFIG/4]=1; regs[GEM_RX_CONFIG/4]=1; regs[GEM_TX_CONFIG/4]=1; }
static int main_test(void) {
    struct gem g; int r __attribute__((unused)); init(&g); say("REACHED host\n");
#if CASE == 0
    for (tick=0; tick<100; tick++) { r=gem_reset_step(&g,tick); if (r==GEM_OK) break; }
    if (tick==100 || writes!=9) fail("H:sequence\n");
    { static const u32 expected_reg[] = { GEM_INTMASK, GEM_MAC_RX_CONFIG, GEM_RX_CONFIG, RESET_REG, GEM_MAC_TX_CONFIG, GEM_TX_CONFIG, RESET_REG, RESET_REG, GEM_INTMASK };
      static const u32 expected_val[] = { ~0U, 0, 0, GEM_RESET_RX, 0, 0, GEM_RESET_TX, GEM_RESET_RX | GEM_RESET_TX, ~0U };
      for (u32 i=0; i<9; i++) if (log_reg[i]!=expected_reg[i] || log_val[i]!=expected_val[i]) fail("H:sequence\n"); }
    regs[GEM_STATUS_ALIAS/4]=GEM_INTR_BERR; regs[(GEM_PCI_BANK2_OFFSET+GEM_ERROR_STATUS)/4]=7;
    gem_poll(&g,tick,0); if (g.state!=GEM_STATE_READY || g.error_status!=7) fail("H:ready-monitor\n");
#elif CASE == 6
    regs[GEM_STATUS_ALIAS/4]=GEM_INTR_TX_MAC | GEM_INTR_RX_MAC;
    regs[GEM_MAC_TX_STATUS/4]=GEM_MAC_TX_UNDERRUN; regs[GEM_MAC_RX_STATUS/4]=GEM_MAC_RX_DONE;
    gem_poll(&g,0,0); if (g.state!=GEM_STATE_RESETTING || g.rx_status!=GEM_MAC_RX_DONE) fail("H:simultaneous\n");
#elif CASE == 7
    wr(0,GEM_MAC_RX_CONFIG, GEM_MAC_RX_ENABLE | GEM_MAC_RX_STRIP_CRC);
    rd(0,GEM_MAC_RX_CONFIG); rd(0,GEM_MAC_RX_CONFIG);
    if (regs[GEM_MAC_RX_CONFIG/4] != (GEM_MAC_RX_ENABLE | GEM_MAC_RX_STRIP_CRC)) fail("H:retain\n");
    wr(0,GEM_MAC_RX_CONFIG,GEM_MAC_RX_STRIP_CRC); rd(0,GEM_MAC_RX_CONFIG); rd(0,GEM_MAC_RX_CONFIG);
    if (regs[GEM_MAC_RX_CONFIG/4] != GEM_MAC_RX_STRIP_CRC) fail("H:retain\n");
#elif CASE == 1
    stuck=GEM_MAC_RX_CONFIG;
    for (tick=0; tick<1000100; tick++) { r=gem_reset_step(&g,tick); if (r==GEM_ETIME) break; }
    if (tick>1001) fail("H:timeout\n");
    { u32 before=writes; gem_reset_step(&g,tick); if (writes!=before) fail("H:quarantine\n"); }
#elif CASE == 2
    regs[GEM_STATUS_ALIAS/4]=GEM_INTR_RX_MAC; regs[GEM_MAC_RX_STATUS/4]=GEM_MAC_RX_OVERFLOW;
    regs[GEM_RX_FIFO_WR_PTR/4]=1; regs[GEM_RX_FIFO_RD_PTR/4]=2; regs[GEM_MAC_MAC_STATE/4]=GEM_MAC_STATE_OVERFLOW;
    gem_poll(&g,0,0); gem_poll(&g,300,0); gem_poll(&g,400,0);
    if (g.state!=GEM_STATE_RESETTING) fail("H:watch-deadline\n");
    if (g.rx_watch_started!=0) fail("H:watch-extend\n");
#elif CASE == 3
    regs[GEM_STATUS_ALIAS/4]=GEM_INTR_RX_MAC; regs[GEM_MAC_RX_STATUS/4]=GEM_MAC_RX_OVERFLOW;
    regs[GEM_RX_FIFO_WR_PTR/4]=1; regs[GEM_RX_FIFO_RD_PTR/4]=2; regs[GEM_MAC_MAC_STATE/4]=GEM_MAC_STATE_OVERFLOW;
    gem_poll(&g,0,0); if (g.state==GEM_STATE_RESETTING) fail("H:watch-immediate\n");
    regs[GEM_RX_FIFO_WR_PTR/4]=3; gem_poll(&g,400,0); if (!g.rx_watching || g.state==GEM_STATE_RESETTING) fail("H:progress\n");
    regs[GEM_MAC_MAC_STATE/4]=0; regs[GEM_STATUS_ALIAS/4]=0; gem_poll(&g,800,0); if (g.rx_watching) fail("H:resolved\n");
#elif CASE == 5
    { static const u32 steps[] = {1,2,4,5,6,8,9};
      static const u32 reg[] = {GEM_MAC_RX_CONFIG,GEM_RX_CONFIG,RESET_REG,GEM_MAC_TX_CONFIG,GEM_TX_CONFIG,RESET_REG,RESET_REG};
      for (u32 i=0; i<7; i++) { init(&g); g.step=steps[i]; stuck=reg[i]; regs[stuck/4]=3; pending[stuck/4]=2; r=gem_reset_step(&g,1000); if (r!=GEM_ETIME || g.state!=GEM_STATE_QUARANTINED) fail("H:bit-timeout\n"); } }
#elif CASE == 4
    regs[GEM_STATUS_ALIAS/4]=GEM_INTR_BERR; regs[(GEM_PCI_BANK2_OFFSET+GEM_ERROR_STATUS)/4]=7; gem_poll(&g,0,0); if (g.error_status!=7) fail("H:berr\n");
#endif
    return failure;
}
void _start(void) { int code=main_test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }

/* P14 isolates reset monitoring from the P18 completion implementation. */
int gem_tx_reclaim(struct gem *g, unsigned long long now, u32 budget)
{ (void)g; (void)now; (void)budget; return GEM_OK; }
