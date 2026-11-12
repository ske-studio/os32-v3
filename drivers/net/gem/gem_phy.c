#include "gem.h"

/* OpenBSD mii.h: register and IEEE 802.3 bit definitions. */
#define BMCR 0
#define BMSR 1
#define PHYID1 2
#define PHYID2 3
#define ANAR 4
#define ANLPAR 5
#define GTCR 9
#define GTSR 10
#define BMCR_RESET 0x8000
#define BMCR_AUTOEN 0x1000
#define BMCR_STARTNEG 0x0200
#define BMSR_EXTSTAT 0x0100
#define BMSR_ACOMP 0x0020
#define BMSR_LINK 0x0004
#define ANAR_CSMA 0x0001
#define ANAR_TX_FD 0x0100
#define ANAR_10_100 0x01e0
#define GTCR_1000_ADV 0x0300
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
/* BCM54xx profile slot: add ID/revision and the measured chip's fixups only
 * after RH2's first sample. OpenBSD sys/dev/mii/brgphy.c:764-782,1002;
 * FreeBSD sys/dev/mii/brgphy.c (brgphy_reset). No guessed BCM54xx writes. */
static int valid_id(const struct gem *g)
{ return g && g->phy_id1 && g->phy_id1 != 0xffff && g->phy_id2 && g->phy_id2 != 0xffff; }

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
                    return GEM_OK; /* RH_PHY_ACCEPT: unknown IDs remain readable. */
                }
            }
        }
        if (!port) break;
    }
    return GEM_EUNSUPPORTED;
}

/* NetBSD sys/dev/mii/ukphy_subr.c:68-119: current BMSR, BMCR and
 * intersection of advertisements. Sampling performs no MII writes. */
int gem_phy_sample(struct gem *g, struct gem_phy_sample *out)
{
    if (!out || !valid_id(g)) return GEM_EUNSUPPORTED;
    struct gem_phy_sample v = {.port=g->phy_port, .addr=g->phy_addr,
                              .id1=g->phy_id1, .id2=g->phy_id2};
    int r;
    r = rd(g, g->phy_addr, BMCR, &v.bmcr); if (r) goto done;
    r = rd(g, g->phy_addr, BMSR, &v.bmsr); if (r) goto done;
    r = rd(g, g->phy_addr, BMSR, &v.bmsr); if (r) goto done; /* RH_PHY_SECOND */
    r = rd(g, g->phy_addr, ANAR, &v.anar); if (r) goto done;
    r = rd(g, g->phy_addr, ANLPAR, &v.anlpar); if (r) goto done;
    if (v.bmsr & BMSR_EXTSTAT) {
        if (rd(g, g->phy_addr, GTCR, &v.gtcr) == GEM_OK &&
            rd(g, g->phy_addr, GTSR, &v.gtsr) == GEM_OK &&
            v.gtcr != 0xffff && v.gtsr != 0xffff) v.gigabit_valid = 1;
    }
    v.link = !!(v.bmsr & BMSR_LINK) && !(v.bmcr & (BMCR_RESET | 0x0c00));
    if (!v.link) goto done;
    if (v.bmcr & BMCR_AUTOEN) {
        if (!(v.bmsr & BMSR_ACOMP)) goto done;
        u32 common = v.anar & v.anlpar;
        if (v.gigabit_valid && (v.gtcr & 0x200) && (v.gtsr & 0x800)) {
            v.speed = 1000; v.full_duplex = 1;
        } else if (v.gigabit_valid && (v.gtcr & 0x100) && (v.gtsr & 0x400)) v.speed = 1000;
        else if (common & ANAR_TX_FD) { v.speed = 100; v.full_duplex = 1; }
        else if (common & (0x200 | 0x80)) v.speed = 100;
        else if (common & 0x40) { v.speed = 10; v.full_duplex = 1; }
        else if (common & 0x20) v.speed = 10;
    } else {
        v.speed = (v.bmcr & 0x40) ? 1000 : (v.bmcr & 0x2000) ? 100 : 10;
        v.full_duplex = !!(v.bmcr & 0x100);
    }
done:
    g->phy_sample = *out = v;
    return r;
}

static void next(struct gem *g, u32 step, unsigned long long now)
{ g->phy_step = step; g->phy_started = now; g->phy_polls = 0; }

int gem_phy_step(struct gem *g, unsigned long long now)
{
    u32 v, status;
    int r;
    if (!valid_id(g)) return GEM_EUNSUPPORTED;
    if (!supported(g->phy_id1, g->phy_id2)) {
        /* OpenBSD mii_physubr.c:80-123,148-178 and brgphy.c:726-743:
         * keep the standard advertisement within the MAC's 10/100 limit.
         * Preserve GTCR's unrelated bits; no vendor-specific PHY writes. */
        if (g->phy_step == PHY_START) {
            r = rd(g, g->phy_addr, BMSR, &status); if (r) return r;
            if ((status & BMSR_EXTSTAT) &&
                rd(g, g->phy_addr, GTCR, &v) == GEM_OK && v != 0xffff) {
                r = wr(g, GTCR, v & ~GTCR_1000_ADV); if (r) return r; /* RH_PHY_LIMIT_GIGABIT */
                r = wr(g, ANAR, ANAR_CSMA | ANAR_10_100); if (r) return r;
                r = wr(g, BMCR, (BMCR_AUTOEN | BMCR_STARTNEG)); if (r) return r;
                next(g, PHY_AN_WAIT, now);
                return GEM_AGAIN;
            }
            next(g, PHY_AN_WAIT, now);
        }
        struct gem_phy_sample sample;
        r = gem_phy_sample(g, &sample);
        if (r) return r;
        /* The existing MAC contract is 100TX full; other speeds remain diagnostic. */
        return sample.link && sample.speed == 100 && sample.full_duplex ? GEM_OK : GEM_AGAIN;
    }
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
