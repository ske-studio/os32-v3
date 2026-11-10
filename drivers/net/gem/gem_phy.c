#include "gem.h"

/* OpenBSD mii.h: register and IEEE 802.3 bit definitions. */
#define BMCR 0
#define BMSR 1
#define PHYID1 2
#define PHYID2 3
#define ANAR 4
#define ANLPAR 5
#define GTCR 9
#define BMCR_RESET 0x8000
#define BMCR_AUTOEN 0x1000
#define BMCR_STARTNEG 0x0200
#define BMSR_EXTSTAT 0x0100
#define BMSR_ACOMP 0x0020
#define BMSR_LINK 0x0004
#define ANAR_CSMA 0x0001
#define ANAR_TX_FD 0x0100
#define PHY_POLLS 1000000U

enum { PHY_START, PHY_RESET_WAIT, PHY_AN_WAIT, PHY_DONE };

/* OpenBSD miidevs:46,178 and bmtphy.c:67-68; miivar.h:236-238,
 * mii.h:103-104 define the OUI/model extraction. BCM5201 needs only
 * mii_phy_reset (bmtphy.c:236-). Add QuickSilver PHY after hardware identification. */
static const struct { u32 oui, model; const char *name; } supported_phys[] = {
    { 0x001018, 0x21, "BCM5201" },
};

static int supported(u32 id1, u32 id2)
{
    u32 oui = (id1 << 6) | (id2 >> 10);
    u32 model = (id2 & 0x03f0) >> 4;
    for (u32 i = 0; i < sizeof(supported_phys) / sizeof(supported_phys[0]); ++i)
        if (oui == supported_phys[i].oui && model == supported_phys[i].model) return 1;
    return 0;
}

static unsigned long long ticks(u32 hz, u32 us)
{
    unsigned long long n = (unsigned long long)hz * us + 999999U, q = 0;
    for (int i = 63; i >= 0; --i) {
        if ((n >> i) >= 1000000U) {
            n -= (unsigned long long)1000000U << i;
            q |= 1ULL << i;
        }
    }
    return q;
}

/* OpenBSD gem_mii_readreg/writereg :1286-1344: TA0 and 100 x 1us. */
static int frame(struct gem *g, u32 addr, u32 reg, u32 op, u32 *data)
{
    u32 v = op | (addr << GEM_MIF_PHY_SHIFT) | (reg << GEM_MIF_REG_SHIFT);
    if (op == GEM_MIF_FRAME_WRITE) v |= *data;
    g->io.write32(g->io.ctx, GEM_MIF_FRAME, v);
    for (u32 n = 0; n < 100; ++n) {
        unsigned long long start = g->io.tb(g->io.ctx);
        for (u32 spin = 0; spin < 10000; ++spin)
            if (g->io.tb(g->io.ctx) - start >= ticks(g->io.tb_hz, 1)) break;
        v = g->io.read32(g->io.ctx, GEM_MIF_FRAME);
        if (v & GEM_MIF_FRAME_TA0) { *data = v & GEM_MIF_FRAME_DATA; return GEM_OK; }
    }
    return GEM_ETIME;
}

static int rd(struct gem *g, u32 addr, u32 reg, u32 *v)
{ return frame(g, addr, reg, GEM_MIF_FRAME_READ, v); }
static int wr(struct gem *g, u32 reg, u32 v)
{ return frame(g, g->phy_addr, reg, GEM_MIF_FRAME_WRITE, &v); }

int gem_mif_init(struct gem *g)
{
    u32 config, a, b;
    if (!g || !g->io.read32 || !g->io.write32 || !g->io.tb || !g->io.tb_hz)
        return GEM_EINVAL;
    /* OpenBSD gem.c:1260-1268 gem_mifinit; clear POLL_ENA too to stop firmware polling. */
    config = g->io.read32(g->io.ctx, GEM_MIF_CONFIG);
    config &= ~(GEM_MIF_CONFIG_BB_ENA | GEM_MIF_CONFIG_POLL_ENA);
    g->io.write32(g->io.ctx, GEM_MIF_CONFIG, config);
    g->phy_id1 = g->phy_id2 = 0;
    /* OpenBSD gem.c:249-293 gem_config: external first, internal fallback. */
    for (u32 port = 1; ; --port) {
        if (!port || (config & GEM_MIF_CONFIG_MDI1)) {
            u32 selected = port ? config | GEM_MIF_CONFIG_PHY_SEL : config & ~GEM_MIF_CONFIG_PHY_SEL;
            g->io.write32(g->io.ctx, GEM_MIF_CONFIG, selected);
            for (u32 addr = 0; addr < 32; ++addr) {
                int r = rd(g, addr, PHYID1, &a);
                if (r != GEM_OK) return r;
                r = rd(g, addr, PHYID2, &b);
                if (r != GEM_OK) return r;
                if (a && a != 0xffff && b && b != 0xffff) {
                    g->phy_addr = addr; g->phy_port = port;
                    g->phy_id1 = a; g->phy_id2 = b; g->phy_step = PHY_START;
                    return supported(a, b) ? GEM_OK : GEM_EUNSUPPORTED;
                }
            }
        }
        if (!port) break;
    }
    return GEM_EUNSUPPORTED;
}

static void next(struct gem *g, u32 step, unsigned long long now)
{ g->phy_step = step; g->phy_started = now; g->phy_polls = 0; }

int gem_phy_step(struct gem *g, unsigned long long now)
{
    u32 v, status;
    int r;
    if (!g || !supported(g->phy_id1, g->phy_id2))
        return GEM_EUNSUPPORTED;
    switch (g->phy_step) {
    case PHY_START:
        /* OpenBSD mii_physubr.c:282-307 mii_phy_reset; NetBSD mii_physubr.c:396-415.
         * Use steps to avoid blocking; NetBSD permits 500ms, OpenBSD waits 100ms. */
        r = wr(g, BMCR, BMCR_RESET); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        next(g, PHY_RESET_WAIT, now); return GEM_AGAIN;
    case PHY_RESET_WAIT:
        r = rd(g, g->phy_addr, BMCR, &v); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        if (v & BMCR_RESET) {
            if (now - g->phy_started >= ticks(g->io.tb_hz, 500000) || ++g->phy_polls >= PHY_POLLS) { g->phy_step = PHY_START; return GEM_ETIME; }
            return GEM_AGAIN;
        }
        /* OpenBSD mii_physubr.c:148-176 and NetBSD mii_physubr.c:198-264 mii_phy_auto.
         * OS32 advertises no pause or gigabit: only 100TX full is supported. */
        r = wr(g, ANAR, ANAR_CSMA | ANAR_TX_FD); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        if (v & BMSR_EXTSTAT) { r = wr(g, GTCR, 0); if (r != GEM_OK) { g->phy_step = PHY_START; return r; } }
        r = wr(g, BMCR, BMCR_AUTOEN | BMCR_STARTNEG); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        next(g, PHY_AN_WAIT, now); return GEM_AGAIN;
    case PHY_AN_WAIT:
        /* OpenBSD ukphy_subr.c:64-94 and NetBSD ukphy_subr.c:57-119 ukphy_status.
         * NetBSD uses the second read; OpenBSD ORs the latched first read.
         * Steps bound the wait without blocking; only 100TX full is accepted. */
        r = rd(g, g->phy_addr, BMSR, &status); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
        status = v;
        if ((status & (BMSR_LINK | BMSR_ACOMP)) == (BMSR_LINK | BMSR_ACOMP)) {
            r = rd(g, g->phy_addr, ANLPAR, &v); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
            /* OpenBSD ukphy_subr.c:90,104 intersects advertisements; QEMU BCM5201 reports ANLPAR 0x0100 without selector. */
            r = rd(g, g->phy_addr, ANAR, &status); if (r != GEM_OK) { g->phy_step = PHY_START; return r; }
            if (!(v & status & ANAR_TX_FD)) { g->phy_step = PHY_START; return GEM_EIO; }
            g->phy_step = PHY_DONE; return GEM_OK;
        }
        if (now - g->phy_started >= ticks(g->io.tb_hz, 5000000) || ++g->phy_polls >= PHY_POLLS) { g->phy_step = PHY_START; return GEM_ETIME; }
        return GEM_AGAIN;
    case PHY_DONE:
        /* BMSR_LINK latches low until read (OpenBSD mii.h, ukphy_subr.c). */
        r = rd(g, g->phy_addr, BMSR, &v); if (r != GEM_OK) return r;
        if (!(v & BMSR_LINK)) { g->phy_step = PHY_START; return GEM_EIO; }
        return GEM_OK;
    default: return GEM_EINVAL;
    }
}
