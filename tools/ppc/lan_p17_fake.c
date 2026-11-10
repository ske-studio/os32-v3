#include "gem.h"

struct fake { u32 config, frame, regs[32], port, addr, id1, id2, pending, reads, no_ta, stuck, latch; unsigned long long tb; u32 delay, zeros, reset_reads, writes, order, bad_order, freeze_tb; u32 log_reg[16], log_val[16], first_frame, tb_calls; };
static u32 read32(void *p, u32 reg)
{
    struct fake *f = p;
    if (reg == 0x6210) return f->config;
    if (reg != 0x620c) return 0;
    if (f->no_ta) return f->frame;
    if (++f->reads < (f->delay ? f->delay : 3)) return f->frame;
    u32 addr = (f->frame >> 23) & 31, r = (f->frame >> 18) & 31;
    u32 value = (f->zeros && addr == 0) ? 0 : 0xffff;
    if (addr == 0 && r == 3 && !f->zeros) value = 0x5678;
    if (((f->config & 0x1) != 0) == f->port && addr == f->addr) {
        if ((f->frame & 0xf0000000) == 0x50000000) {
            f->regs[r] = f->frame & 0xffff;
            if (f->writes < 16) { f->log_reg[f->writes] = r; f->log_val[f->writes] = f->regs[r]; }
            f->writes++;
            if (r == 0 && f->regs[r] == 0x8000) { f->reset_reads = 0; f->order = 0; }
            else if (r == 4 && f->regs[r] == 0x101 && f->order == 0) f->order = 1;
            else if (r == 9 && f->regs[r] == 0 && f->order == 1) f->order = 2;
            else if (r == 0 && f->regs[r] == 0x1200 && f->order == 2) f->order = 3;
            else f->bad_order = 1;
        }
        if (r == 0 && f->regs[0] == 0x8000 && !f->stuck && ++f->reset_reads >= 3) f->regs[0] = 0;
        value = r == 2 ? f->id1 : r == 3 ? f->id2 : f->regs[r];
        if (r == 1 && f->order == 3 && !f->bad_order && !f->no_ta && f->regs[1] & 0x20) value |= 0x24;
        else if (r == 1) value &= ~0x24U;
        if (r == 1 && f->latch == 1) { f->latch = 0; value &= ~4U; }
        else if (r == 1 && f->latch == 2) { f->latch = 3; }
        else if (r == 1 && f->latch == 3) { f->latch = 0; value &= ~4U; }
    }
    return f->frame | 0x00010000 | value;
}
static void write32(void *p, u32 reg, u32 v)
{
    struct fake *f = p;
    if (reg == 0x6210) f->config = v;
    if (reg == 0x620c) { if (!f->first_frame) f->first_frame = v; f->frame = v; f->reads = 0; }
}
static unsigned long long tb(void *p) { struct fake *f = p; f->tb_calls++; return f->freeze_tb ? f->tb : ++f->tb; }
static void say(const char *s) { unsigned n=0; while (s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n)); }
static void fail(const char *s) { say(s); say("\n"); __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable(); }
static int main_test(void)
{
    int c = CASE;
    struct fake f = {0}; struct gem g = {0};
    f.config = 0x4 | 0x2 | 0x200;
    f.port = c == 1 ? 0 : 1; f.addr = 5; f.id1 = 0x0040; f.id2 = 0x6210;
    f.regs[1] = 0x124; f.regs[5] = 0x101; f.regs[9] = 0xffff;
    g.io.ctx = &f; g.io.read32 = read32; g.io.write32 = write32; g.io.tb = tb; g.io.tb_hz = 1000000;
    say("REACHED host\n");
    if (c == 9) f.zeros = 1;
    if (c == 10) f.delay = 99;
    if (c == 14) f.delay = 100;
    if (c == 15) f.freeze_tb = 1;
    if (c == 16) { f.id1 = 0x1234; f.id2 = 0x5678; }
    if (c == 2) f.no_ta = 1;
    int r = gem_mif_init(&g);
    if (f.first_frame != (0x60020000U | (0U << 23) | (2U << 18))) fail("H:frame");
    if (c == 15 && f.tb_calls > 3000000U) fail("H:spin");
    if (c == 16) { if (r != GEM_EUNSUPPORTED || g.phy_id1 != f.id1 || g.phy_id2 != f.id2) fail("H:unknown-init"); if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED || f.writes) fail("H:unknown-write"); return 0; }
    if (c == 2) { if (r != GEM_ETIME) fail("H:A6 phy-invalid"); return 0; }
    if (r != GEM_OK || g.phy_addr != 5 || g.phy_port != f.port || g.phy_id1 != f.id1 || g.phy_id2 != f.id2) fail("H:A6 phy-invalid");
    if (f.config & (0x4 | 0x2)) fail("H:config");
    if (c == 3) { g.phy_id1 = 0; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); g.phy_id1 = 0xffff; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); return 0; }
    if (c == 4 || c == 11) f.stuck = 1;
    if (c == 5 || c == 8) f.regs[1] = 0x100;
    if (c == 6) f.regs[5] = 0x81;
    if (c == 12) f.regs[5] = 0x41;
    if (c == 13) f.regs[5] = 1;
    if (c == 7) f.latch = 1;
    if (c == 17) f.latch = 2;
    r = gem_phy_step(&g, 0); if (r != GEM_AGAIN) fail("H:start");
    r = gem_phy_step(&g, 1);
    r = gem_phy_step(&g, c == 4 ? 500000 : (c == 11 ? 0 : 1));
    if (c == 11) { for (u32 i = 1; i < 1000000 && r == GEM_AGAIN; ++i) r = gem_phy_step(&g, 0); if (r != GEM_ETIME) fail("H:reset-poll-limit"); return 0; }
    if (c == 4) { if (r != GEM_ETIME) fail("H:reset-timeout"); u32 n = f.writes; if (gem_phy_step(&g, 0) != GEM_AGAIN || f.writes != n + 1 || f.log_val[n] != 0x8000) fail("H:retry"); return 0; }
    if (r != GEM_AGAIN || f.writes != 4 || f.log_reg[0] != 0 || f.log_val[0] != 0x8000 || f.log_reg[1] != 4 || f.log_val[1] != 0x101 || f.log_reg[2] != 9 || f.log_val[2] != 0 || f.log_reg[3] != 0 || f.log_val[3] != 0x1200) fail("H:advertise");
    if (c == 7) f.latch = 1;
    if (c == 17) f.latch = 2;
    r = gem_phy_step(&g, c == 5 ? 5000001 : 2);
    if (c == 8) { for (u32 i = 1; i < 1000000 && r == GEM_AGAIN; ++i) r = gem_phy_step(&g, 2); }
    if (c == 17) { if (r != GEM_AGAIN) fail("H:current-status"); return 0; }
    if (c == 5 || c == 8) { if (r != GEM_ETIME) fail("H:an-timeout"); }
    else if (c == 6 || c == 12 || c == 13) { if (r != GEM_EIO) fail("H:partner"); }
    else if (r != GEM_OK) fail("H:link");
    if (r == GEM_EIO || r == GEM_ETIME) { u32 n = f.writes; if (gem_phy_step(&g, 3) != GEM_AGAIN || f.writes != n + 1 || f.log_reg[n] != 0 || f.log_val[n] != 0x8000) fail("H:retry"); }
    return 0;
}

void _start(void) { int r = main_test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r)); __builtin_unreachable(); }
