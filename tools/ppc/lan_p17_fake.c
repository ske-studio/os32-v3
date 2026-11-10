#include "gem.h"

struct fake { u32 config, frame, regs[32], port, addr, id1, id2, pending, reads, no_ta, stuck, latch; unsigned long long tb; u32 delay, zeros; };
static u32 read32(void *p, u32 reg)
{
    struct fake *f = p;
    if (reg == GEM_MIF_CONFIG) return f->config;
    if (reg != GEM_MIF_FRAME) return 0;
    if (f->no_ta) return f->frame;
    if (++f->reads < (f->delay ? f->delay : 3)) return f->frame;
    u32 addr = (f->frame >> GEM_MIF_PHY_SHIFT) & 31, r = (f->frame >> GEM_MIF_REG_SHIFT) & 31;
    u32 value = (f->zeros && addr == 0) ? 0 : 0xffff;
    if (addr == 0 && r == 3 && !f->zeros) value = 0x5678;
    if (((f->config & GEM_MIF_CONFIG_PHY_SEL) != 0) == f->port && addr == f->addr) {
        if ((f->frame & 0xf0000000) == 0x50000000) {
            f->regs[r] = f->frame & 0xffff;
            if (r == 0 && f->regs[r] == 0x8000 && !f->stuck) f->regs[r] = 0;
        }
        value = r == 2 ? f->id1 : r == 3 ? f->id2 : f->regs[r];
        if (r == 1 && f->latch) { f->latch = 0; value &= ~4U; }
    }
    return f->frame | GEM_MIF_FRAME_TA0 | value;
}
static void write32(void *p, u32 reg, u32 v)
{
    struct fake *f = p;
    if (reg == GEM_MIF_CONFIG) f->config = v;
    if (reg == GEM_MIF_FRAME) { f->frame = v; f->reads = 0; }
}
static unsigned long long tb(void *p) { return ++((struct fake *)p)->tb; }
static void say(const char *s) { unsigned n=0; while (s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
static void fail(const char *s) { say(s); say("\n"); __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable(); }
static int main_test(void)
{
    int c = CASE;
    struct fake f = {0}; struct gem g = {0};
    f.config = GEM_MIF_CONFIG_BB_ENA | GEM_MIF_CONFIG_POLL_ENA | GEM_MIF_CONFIG_MDI1;
    f.port = c == 1 ? 0 : 1; f.addr = 5; f.id1 = 0x1234; f.id2 = 0x5678;
    f.regs[1] = 0x124; f.regs[5] = 0x101;
    g.io.ctx = &f; g.io.read32 = read32; g.io.write32 = write32; g.io.tb = tb; g.io.tb_hz = 1000000;
    say("REACHED host\n");
    if (c == 9) f.zeros = 1;
    if (c == 10) f.delay = 99;
    if (c == 2) f.no_ta = 1;
    int r = gem_mif_init(&g);
    if (c == 2) { if (r != GEM_ETIME) fail("H:A6 phy-invalid"); return 0; }
    if (r != GEM_OK || g.phy_addr != 5 || g.phy_port != f.port || g.phy_id1 != f.id1 || g.phy_id2 != f.id2) fail("H:A6 phy-invalid");
    if (f.config & (GEM_MIF_CONFIG_BB_ENA | GEM_MIF_CONFIG_POLL_ENA)) fail("H:config");
    if (c == 3) { g.phy_id1 = 0; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); g.phy_id1 = 0xffff; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); return 0; }
    if (c == 4 || c == 11) f.stuck = 1;
    if (c == 5 || c == 8) f.regs[1] = 0x100;
    if (c == 6) f.regs[5] = 0x81;
    if (c == 12) f.regs[5] = 0x41;
    if (c == 13) f.regs[5] = 1;
    if (c == 7) f.latch = 1;
    r = gem_phy_step(&g, 0); if (r != GEM_AGAIN) fail("H:start");
    r = gem_phy_step(&g, c == 4 ? 500000 : (c == 11 ? 0 : 1));
    if (c == 11) { for (u32 i = 1; i < 1000000 && r == GEM_AGAIN; ++i) r = gem_phy_step(&g, 0); if (r != GEM_ETIME) fail("H:reset-poll-limit"); return 0; }
    if (c == 4) { if (r != GEM_ETIME) fail("H:reset-timeout"); return 0; }
    if (r != GEM_AGAIN || f.regs[4] != 0x101 || f.regs[9] != 0) fail("H:advertise");
    if (c == 7) f.latch = 1;
    r = gem_phy_step(&g, c == 5 ? 5000001 : 2);
    if (c == 8) { for (u32 i = 1; i < 1000000 && r == GEM_AGAIN; ++i) r = gem_phy_step(&g, 2); }
    if (c == 5 || c == 8) { if (r != GEM_ETIME) fail("H:an-timeout"); }
    else if (c == 6 || c == 12 || c == 13) { if (r != GEM_EIO) fail("H:partner"); }
    else if (r != GEM_OK) fail("H:link");
    return 0;
}

void _start(void) { int r = main_test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r)); __builtin_unreachable(); }
