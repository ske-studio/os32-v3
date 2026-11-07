/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* NV4a single-partition RAM reclocking, nouveau c03c1a1
 * nvkm/subdev/fb/ramnv40.c:78-175. GF_FEAT5_DESIGN section 3 adds
 * one rollback attempt; waits retain the reference budgets. BIT M is prepared. */
#include "feature5.h"

/* Addresses/masks/waits: ramnv40.c:78-175 (all accesses BAR0). */
#define RAM_BAR 0U
#define RAM_WORD 4U
#define RAM_BYTE 1U
#define RAM_HEADS 2U
#define RAM_STRIDE 0x2000U
#define RAM_VBLANK 0x600808U
#define RAM_VBLANK_BIT 0x00010000U
#define RAM_SR_INDEX 0x0c03c4U
#define RAM_SR_DATA 0x0c03c5U
#define RAM_SR1 1U
#define RAM_BLANK 0x20U
#define RAM_PROBE_LAST 32U
#define RAM_VBLANK_US 2000000U
#define RAM_PRECHARGE 0x1002d4U
#define RAM_REFRESH 0x1002d0U
#define RAM_AUTO 0x100210U
#define RAM_AUTO_BIT 0x80000000U
#define RAM_SELF 0x1002dcU
#define RAM_MUX 0x00c040U
#define RAM_MUX_MASK 0x0000c000U
#define RAM_CTRL 0x004020U
#define RAM_COEF 0x004024U
#define RAM_CTRL_MASK 0xc0771100U
#define RAM_SETTLE_US 100U

static i32 rd(struct gf_io *io, u32 off, u32 width, u32 *v)
{
    return io->read(io->opaque, RAM_BAR, off, width, v);
}
static i32 wr(struct gf_io *io, u32 off, u32 width, u32 v)
{
    return io->write(io->opaque, RAM_BAR, off, width, v);
}
static i32 mask(struct gf_io *io, u32 off, u32 bits, u32 v)
{
    u32 old;
    i32 rc = rd(io, off, RAM_WORD, &old);
    return rc ? rc : wr(io, off, RAM_WORD, (old & ~bits) | v);
}
static void first(i32 *rc, i32 next)
{
    if (!*rc) *rc = next;
}
static i32 clock_now(struct gf_io *io, u32 *now)
{
    return io->timing.clock_us(io->opaque, now);
}
/* ramnv40.c:90,146,152: fixed delays have no outer deadline. */
static i32 delay(struct gf_io *io, u32 us)
{
    while (us) {
        u32 n = us > GF2_WAIT_MAX_US ? GF2_WAIT_MAX_US : us;
        i32 rc = io->timing.wait_us(io->opaque, n);
        if (rc) return rc;
        us -= n;
    }
    return 0;
}
/* ramnv40.c:99-109,165-169: each edge has its own full 2s budget. */
static i32 vblank(struct gf_io *io, u32 head, bool high)
{
    u32 start, now, v;
    i32 rc = clock_now(io, &start);
    if (rc) return rc;
    for (;;) {
        rc = clock_now(io, &now);
        if (rc) return rc;
        if (now - start >= RAM_VBLANK_US)
            return GF5_ETIME;
        rc = rd(io, RAM_VBLANK + head * RAM_STRIDE, RAM_WORD, &v);
        if (rc) return rc;
        if (!!(v & RAM_VBLANK_BIT) == high) return 0;
        u32 n = RAM_VBLANK_US - (now - start);
        if (n > GF2_WAIT_MAX_US) n = GF2_WAIT_MAX_US;
        rc = delay(io, n);
        if (rc) return rc;
    }
}
static i32 sr_write(struct gf_io *io, u32 head, u32 v)
{
    i32 rc = wr(io, RAM_SR_INDEX + head * RAM_STRIDE, RAM_BYTE, RAM_SR1);
    return rc ? rc : wr(io, RAM_SR_DATA + head * RAM_STRIDE, RAM_BYTE, v);
}
/* One best-effort old-PLL pass. No deadline may suppress self/auto cleanup.
 * This is the design's adapter-error cleanup, not a retry of the script. */
static i32 cleanup_delay(struct gf_io *io)
{
    for (u32 left = RAM_SETTLE_US; left;) {
        u32 n = left > GF2_WAIT_MAX_US ? GF2_WAIT_MAX_US : left;
        i32 rc = io->timing.wait_us(io->opaque, n);
        if (rc) return rc;
        left -= n;
    }
    return 0;
}
static i32 rollback(struct gf_io *io, u32 ctrl, u32 coef, u32 mux)
{
    /* ramnv40.c:115-120: prepare again after normal refresh / BIT M. */
    i32 rc = wr(io, RAM_PRECHARGE, RAM_WORD, 1);
    first(&rc, wr(io, RAM_REFRESH, RAM_WORD, 1)); /* rollback refresh first */
    first(&rc, wr(io, RAM_REFRESH, RAM_WORD, 1)); /* rollback refresh second */
    first(&rc, mask(io, RAM_AUTO, RAM_AUTO_BIT, 0));
    first(&rc, wr(io, RAM_SELF, RAM_WORD, 1));
    first(&rc, mask(io, RAM_MUX, RAM_MUX_MASK, 0));
    first(&rc, wr(io, RAM_CTRL, RAM_WORD, ctrl));
    first(&rc, wr(io, RAM_COEF, RAM_WORD, coef));
    first(&rc, cleanup_delay(io));
    first(&rc, mask(io, RAM_MUX, RAM_MUX_MASK, mux & RAM_MUX_MASK));
    first(&rc, wr(io, RAM_SELF, RAM_WORD, 0));
    first(&rc, mask(io, RAM_AUTO, RAM_AUTO_BIT, RAM_AUTO_BIT));
    first(&rc, cleanup_delay(io));
    return rc;
}
static i32 finish(struct gf5 *g, i32 rc)
{
    if (g) { g->source_rc = rc; g->last_rc = gf5_rc_map(rc); }
    return gf5_rc_map(rc);
}

i32 gf5_ram_prog(struct gf5 *g, const struct gf5_plan *plan)
{
    struct gf_io *io;
    u32 ctrl, coef, mux, sr[RAM_HEADS] = {0}, active = 0, blanked = 0;
    bool touched = false, rolled_back = false, reopen;
    i32 rc;
    if (!g || !plan) return finish(g, GF5_EINVAL);
    if (!g->ch || !g->ch->f || !g->links.owner || !g->links.drain_pause ||
        !g->links.unpause || !g->links.admission) return finish(g, GF5_EINVAL);
    if (g->phase != GF5_RUN && g->phase != GF5_RESTORING)
        return finish(g, GF5_ESTATE);
    if (!g->rom.bytes || !g->rom.size) return finish(g, GF5_ENOTSUP);
    if (!!g->rom.memory_pc != !!g->rom.memory_program ||
        (g->rom.memory_program && !g->links.memory_script))
        return finish(g, GF5_EBIOS);
    io = &g->ch->f->io;
    if (!io->read || !io->write || !io->timing.clock_us || !io->timing.wait_us)
        return finish(g, GF5_EINVAL);
    rc = gf5_plan_check(g, plan, true);
    if (rc) return rc;
    rc=gf5_plan_domain_ceiling(g,plan,true);
    if (rc) return finish(g,rc);
    /* ramnv40.c:115-147 uses private coefficients; consume before any MMIO. */
    g->plan_guard.ram_used = true;
    /* Snapshot before any destructive writes. B1 already calculated mc/mn. */
    rc = rd(io, RAM_CTRL, RAM_WORD, &ctrl);
    if (!rc) rc = rd(io, RAM_COEF, RAM_WORD, &coef);
    if (!rc) rc = rd(io, RAM_MUX, RAM_WORD, &mux);
    if (rc) return finish(g, rc);
    reopen = g->accepting;
    rc = g->links.drain_pause(g->links.owner);
    g->accepting = false;
    if (rc) goto failed;
    /* ramnv40.c:78-96: exactly cnt++ < 32, including its last delay. */
    for (u32 h = 0; h < RAM_HEADS; ++h) {
        u32 initial, current;
        rc = rd(io, RAM_VBLANK + h * RAM_STRIDE, RAM_WORD, &initial);
        if (rc) goto cleanup;
        for (u32 cnt = 0; cnt <= RAM_PROBE_LAST; ++cnt) {
            rc = rd(io, RAM_VBLANK + h * RAM_STRIDE, RAM_WORD, &current);
            if (rc) goto cleanup;
            if (current != initial) {
                rc = wr(io, RAM_SR_INDEX + h * RAM_STRIDE, RAM_BYTE, RAM_SR1);
                if (!rc) rc = rd(io, RAM_SR_DATA + h * RAM_STRIDE, RAM_BYTE, &sr[h]);
                if (rc) goto cleanup;
                if (!(sr[h] & RAM_BLANK)) active |= 1U << h;
                break;
            }
            rc = delay(io, 1);
            if (rc) goto cleanup;
        }
    }
    for (u32 h = 0; h < RAM_HEADS; ++h) {
        if (!(active & (1U << h))) continue;
        rc = vblank(io, h, false);
        if (!rc) rc = vblank(io, h, true);
        if (rc) goto cleanup;
        blanked |= 1U << h; /* A failing adapter write may have applied. */
        rc = sr_write(io, h, sr[h] | RAM_BLANK);
        if (rc) goto cleanup;
    }
    /* ramnv40.c:115-147: NV4a uses only the default memory partition. */
    rc = wr(io, RAM_PRECHARGE, RAM_WORD, 1);
    if (!rc) rc = wr(io, RAM_REFRESH, RAM_WORD, 1); /* refresh first */
    if (!rc) rc = wr(io, RAM_REFRESH, RAM_WORD, 1); /* refresh second */
    if (rc) goto cleanup;
    touched = true; /* Include applied-but-failed auto-refresh writes. */
    rc = mask(io, RAM_AUTO, RAM_AUTO_BIT, 0);
    if (!rc) rc = wr(io, RAM_SELF, RAM_WORD, 1);
    if (!rc) rc = mask(io, RAM_MUX, RAM_MUX_MASK, 0);
    if (!rc) rc = mask(io, RAM_CTRL, RAM_CTRL_MASK, plan->mc);
    if (!rc) rc = wr(io, RAM_COEF, RAM_WORD, plan->mn);
    if (!rc) rc = delay(io, RAM_SETTLE_US);
    if (!rc) rc = mask(io, RAM_MUX, RAM_MUX_MASK, RAM_MUX_MASK);
    if (!rc) rc = wr(io, RAM_SELF, RAM_WORD, 0);
    if (!rc) rc = mask(io, RAM_AUTO, RAM_AUTO_BIT, RAM_AUTO_BIT);
    if (!rc) rc = delay(io, RAM_SETTLE_US);
    if (!rc && g->rom.memory_program)
        rc = g->links.memory_script(g->links.owner, g->rom.memory_pc);
cleanup:
    if (rc && touched) {
        (void)rollback(io, ctrl, coef, mux);
        rolled_back = true;
    }
    /* ramnv40.c:161-175: even timeout must attempt every saved SR1. */
    for (u32 h = 0; h < RAM_HEADS; ++h) {
        if (!(blanked & (1U << h))) continue;
        if (!rc) first(&rc, vblank(io, h, true));
        first(&rc, sr_write(io, h, sr[h]));
    }
    /* SR1/vblank can fail after the first rollback decision. Preserve rc,
     * and include this late failure in the same single old-PLL attempt. */
    if (rc && touched && !rolled_back) {
        /* ramnv40.c:98-120: disable head memory access before reclocking,
         * including heads already reopened by the SR1 cleanup above. */
        for (u32 h = 0; h < RAM_HEADS; ++h) {
            if (blanked & (1U << h))
                (void)sr_write(io, h, sr[h] | RAM_BLANK);
        }
        (void)rollback(io, ctrl, coef, mux);
        /* ramnv40.c:161-175: restore every saved head after reclocking. */
        for (u32 h = 0; h < RAM_HEADS; ++h) {
            if (blanked & (1U << h)) (void)sr_write(io, h, sr[h]);
        }
    }
    if (rc) goto failed;
    rc = g->links.unpause(g->links.owner);
    if (!rc && reopen) rc = g->links.admission(g->links.owner, true);
    if (rc) goto failed;
    g->accepting = reopen;
    return finish(g, 0);
failed:
    g->phase = GF5_FAULT;
    g->accepting = false;
    return finish(g, rc);
}
