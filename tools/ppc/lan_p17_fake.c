#include "gem.h"

struct fake { u32 config, frame, regs[32], port, addr, id1, id2, pending, reads, no_ta, stuck, latch; unsigned long long tb; u32 delay, zeros, reset_reads, writes, order, bad_order, freeze_tb; u32 log_reg[16], log_val[16], first_frame, tb_calls, bmsr_reads; };
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
    if (reg == 0x620c) { if (!f->first_frame) f->first_frame = v; if ((v & 0xf0000000U) == 0x60000000U && ((v >> 18) & 31) == 1) f->bmsr_reads++; f->frame = v; f->reads = 0; }
}
static unsigned long long tb(void *p) { struct fake *f = p; f->tb_calls++; return f->freeze_tb ? f->tb : ++f->tb; }
static __attribute__((noinline)) void say(const char *s) { unsigned n=0; while (s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
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
    if (c == 16 || c >= 23) { f.id1 = 0x1234; f.id2 = 0x5678; }
    if (c == 2) f.no_ta = 1;
    int r = gem_mif_init(&g);
    if (f.first_frame != (0x60020000U | (0U << 23) | (2U << 18))) fail("H:frame");
    if (c == 15 && f.tb_calls > 3000000U) fail("H:spin");
    if (c >= 30) {
        if (r != GEM_OK) fail("H:unknown-init");
        f.regs[9] = 0x1b00; /* master configuration plus both gigabit modes */
        if (c == 31) f.regs[1] &= ~0x100U;
        if (c == 32) f.regs[9] = 0xffff;
        r = gem_phy_step(&g, 0);
        if (r != GEM_AGAIN) fail("H:gigabit-start");
        if (c == 31 || c == 32) {
            if (f.writes) fail("H:gigabit-unavailable");
            return 0;
        }
        if (f.writes != 3 || f.log_reg[0] != 9 || f.log_val[0] != 0x1800)
            fail("H:gigabit-advertise");
        if (f.log_reg[1] != 4 || f.log_val[1] != 0x1e1 ||
            f.log_reg[2] != 0 || f.log_val[2] != 0x1200) fail("H:gigabit-restart");
        /* Negotiation completes at 100FD. Further polls must not restart it. */
        f.order = 3; f.bad_order = 0; f.regs[0] = 0x1000;
        for (u32 n = 0; n < 3; ++n)
            if (gem_phy_step(&g, n + 1) != GEM_OK || f.writes != 3)
                fail("H:gigabit-complete");
        return 0;
    }
    if (c >= 23) {
        if (r != GEM_OK) fail("H:unknown-init");
        g.phy_step = 2; /* sample the already negotiated standard registers */
        f.order = 3; f.regs[0] = 0x1000; f.regs[4] = 0x101;
        f.regs[9] = f.regs[10] = 0;
        if (c == 24) f.latch = 1;
        if (c == 25) f.regs[4] = f.regs[5] = 0x41;
        if (c == 26) { f.regs[9] = 0x200; f.regs[10] = 0x800; }
        if (c == 27) f.regs[1] = 0x100;
        if (c == 28) f.regs[0] = 0x2100;
        if (c == 29) f.regs[9] = f.regs[10] = 0xffff;
        struct gem_phy_sample sample;
        if (gem_phy_sample(&g, &sample) || sample.id1 != f.id1 || sample.id2 != f.id2 ||
            sample.port != f.port || sample.addr != f.addr || f.writes) fail("H:sample");
        u32 speed = c == 25 ? 10 : c == 26 ? 1000 : c == 27 ? 0 : 100;
        if (sample.link != (c != 27) || sample.speed != speed ||
            sample.full_duplex != (c != 27) || sample.gigabit_valid != (c != 29)) fail("H:standard-link");
        r = gem_phy_step(&g, 0);
        if (r != ((speed == 100) ? GEM_OK : GEM_AGAIN) || f.writes) fail("H:unknown-write");
        return 0;
    }
    if (c == 16) { if (r != GEM_OK || g.phy_id1 != f.id1 || g.phy_id2 != f.id2) fail("H:unknown-init"); if (gem_phy_step(&g, 0) != GEM_AGAIN || f.writes) fail("H:unknown-write"); return 0; }
    if (c == 2) { if (r != GEM_ETIME) fail("H:A6 phy-invalid"); return 0; }
    if (r != GEM_OK || g.phy_addr != 5 || g.phy_port != f.port || g.phy_id1 != f.id1 || g.phy_id2 != f.id2) fail("H:A6 phy-invalid");
    if (f.config & (0x4 | 0x2)) fail("H:config");
    if (c == 3) { g.phy_id1 = 0; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); g.phy_id1 = 0xffff; if (gem_phy_step(&g, 0) != GEM_EUNSUPPORTED) fail("H:unknown"); return 0; }
    if (c == 4 || c == 11) f.stuck = 1;
    if (c == 5 || c == 8) f.regs[1] = 0x100;
    if (c == 6) f.regs[5] = 0x80;
    if (c == 21) f.regs[5] = 0x100;
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
    if (c == 22) f.regs[4] = 1;
    r = gem_phy_step(&g, c == 5 ? 5000001 : 2);
    if (c == 8) { for (u32 i = 1; i < 1000000 && r == GEM_AGAIN; ++i) r = gem_phy_step(&g, 2); }
    if (c == 17) { if (r != GEM_AGAIN) fail("H:current-status"); return 0; }
    if (c == 5 || c == 8) { if (r != GEM_ETIME) fail("H:an-timeout"); }
    else if (c == 6 || c == 12 || c == 13 || c == 22) { if (r != GEM_EIO) fail("H:partner"); }
    else if (r != GEM_OK) fail("H:link");
    if (r == GEM_EIO || r == GEM_ETIME) { u32 n = f.writes; if (gem_phy_step(&g, 3) != GEM_AGAIN || f.writes != n + 1 || f.log_reg[n] != 0 || f.log_val[n] != 0x8000) fail("H:retry"); }
    if (c >= 18 && c <= 20) {
        u32 reads = f.bmsr_reads, writes = f.writes;
        if (c == 19) f.latch = 1;
        if (c == 20) f.no_ta = 1;
        r = gem_phy_step(&g, 3);
        if (c == 18 && (r != GEM_OK || f.bmsr_reads != reads + 1)) fail("H:done-link");
        if (c == 19) {
            if (r != GEM_EIO || f.bmsr_reads != reads + 1) fail("H:done-down");
            if (gem_phy_step(&g, 4) != GEM_AGAIN || f.writes != writes + 1 || f.log_reg[writes] != 0 || f.log_val[writes] != 0x8000) fail("H:done-restart");
        }
        if (c == 20 && (r != GEM_ETIME || f.bmsr_reads != reads + 1)) fail("H:done-mif");
    }
    return 0;
}

void _start(void) { int r = main_test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r)); __builtin_unreachable(); }
