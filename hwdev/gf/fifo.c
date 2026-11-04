/*
 * Copyright 2012 Red Hat Inc.
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
/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/* Adapted from nouveau (c03c1a1), nvkm/engine/fifo/nv04.c and nv40.c,
 * nvkm/subdev/fb/nv44.c; Ben Skeggs and Francisco Jerez.
 * OS32 sequencing: GF_FEAT2_DESIGN section 2 and B0 final addendum. */
#include "command.h"
#include "wait_diag.h"

/* Component aliases of the design's NV40/NV44 register definitions. */
enum {
    FIFO_DELAY = 0x2040, FIFO_TIMESLICE = 0x2044, FIFO_FETCH_HI = 0x2058,
    FIFO_RAMHT = 0x2210, FIFO_RAMRO = 0x2218, FIFO_RAMFC = 0x2220,
    FIFO_RAMFC_SELECT = 0x2230, FIFO_CACHES = 0x2500, FIFO_MODE = 0x2504,
    CACHE_PUSH0 = 0x3200, CACHE_PUSH1 = 0x3204, CACHE_PUT = 0x3210,
    CACHE_DMA_PUSH = 0x3220, CACHE_PULL0 = 0x3250, CACHE_HASH = 0x3258,
    CACHE_GET = 0x3270, CACHE_GRCTX = 0x32e0,
    FB_PCI_ENABLE = 0x100850, FB_PCI_CONTROL = 0x100800,
    HASH_FAILED = 0x10, HASH_BUSY = 0x1000,
    RAMFC_WINDOW_BYTES = 0x80000, RAMFC_CONFIG = 0x30000,
    RAMFC_FETCH = 0x30086078, RAMFC_TIMESLICE = 0x1ffff,
    RAMFC_GRCTX = 0x38
};

static int rd(struct gf2_chan *ch, u32 reg, u32 *value)
{
    struct gf_io *io = &ch->f->io;
    return io->read(io->opaque, 0, reg, 4, value) ? GF2_EIO : GF2_OK;
}

static int wr(struct gf2_chan *ch, u32 reg, u32 value)
{
    struct gf_io *io = &ch->f->io;
    return io->write(io->opaque, 0, reg, 4, value) ? GF2_EIO : GF2_OK;
}

static int mask(struct gf2_chan *ch, u32 reg, u32 bits, u32 value)
{
    u32 old;
    int rc = rd(ch, reg, &old);
    return rc ? rc : wr(ch, reg, (old & ~bits) | value);
}

static int fc_write(struct gf2_chan *ch, u32 off, u32 value)
{
    return gf_inst_write32(ch->f, &ch->f->imem.ramfc,
                          GF2_CHID * GF2_RAMFC_BYTES + off, value)
           ? GF2_EIO : GF2_OK;
}

#define TRY(call) do { int rc_ = (call); if (rc_) return rc_; } while (0)

int gf2_fb_init(struct gf2_chan *ch)
{
    TRY(gf2_fault_check(ch));
    /* fb/nv44.c:48-53; caller has closed the FIFO gates. */
    TRY(wr(ch, FB_PCI_ENABLE, 0x80000000U));
    return wr(ch, FB_PCI_CONTROL, 1);
}

int gf2_fifo_init(struct gf2_chan *ch)
{
    struct gf_imem *im = &ch->f->imem;
    TRY(gf2_fault_check(ch));
    /* fifo/nv40.c:39-58; slot zero, aperture-relative push DMA instance. */
    for (u32 off = 0; off < GF2_RAMFC_BYTES; off += sizeof(u32))
        TRY(fc_write(ch, off, 0));
    TRY(fc_write(ch, 0x00, ch->B));
    TRY(fc_write(ch, 0x04, ch->B));
    TRY(fc_write(ch, 0x0c, ch->push_dma.inst.off >> 4));
    TRY(fc_write(ch, 0x18, RAMFC_FETCH));
    TRY(fc_write(ch, 0x3c, RAMFC_TIMESLICE));

    /* fifo/nv40.c:190-228, NV4a branch. F1 alone owns 2100/2140. */
    TRY(wr(ch, FIFO_DELAY, 0xff));
    TRY(wr(ch, FIFO_TIMESLICE, 0x2101ffff));
    TRY(wr(ch, FIFO_FETCH_HI, 1));
    TRY(wr(ch, FIFO_RAMHT, (3U << 24) | ((GF2_RAMHT_BITS - 9U) << 16) |
                         (im->ramht.off >> 8)));
    TRY(wr(ch, FIFO_RAMRO, im->ramro.off >> 8));
    TRY(wr(ch, FIFO_RAMFC_SELECT, 0));
    TRY(wr(ch, FIFO_RAMFC, ((im->vram_bytes - RAMFC_WINDOW_BYTES +
                            im->ramfc.off) >> 16) | RAMFC_CONFIG));
    TRY(wr(ch, CACHE_PUSH1, GF2_CHID_INVALID));
    TRY(wr(ch, CACHE_PUSH0, 1));
    TRY(wr(ch, CACHE_PULL0, 1));
    return wr(ch, FIFO_CACHES, 1);
}

int gf2_fifo_bind_gr(struct gf2_chan *ch, u32 ctx_off)
{
    u32 active;
    TRY(gf2_fault_check(ch));
    /* fifo/nv40.c:125-164; bind before enabling MODE. */
    TRY(mask(ch, FIFO_CACHES, 1, 0));
    TRY(rd(ch, CACHE_PUSH1, &active));
    if ((active & GF2_CHID_INVALID) == GF2_CHID)
        TRY(wr(ch, CACHE_GRCTX, ctx_off >> 4));
    TRY(fc_write(ch, RAMFC_GRCTX, ctx_off >> 4));
    return mask(ch, FIFO_CACHES, 1, 1);
}

int gf2_fifo_start(struct gf2_chan *ch)
{
    TRY(gf2_fault_check(ch));
    /* fifo/nv04.c:92-99; no DMA_PUSH/PULL0 writes here. */
    return mask(ch, FIFO_MODE, 1U << GF2_CHID, 1U << GF2_CHID);
}

/* fifo/nv40.c:64-87: mask preserves the other bits of shared RAMFC words.
 * In particular +18 combines DMA_FETCH[27:0] and 2058[29:28]. */
static const struct { u32 reg, off, bits; } ramfc_layout[] = {
    {0x3240, 0x00, U32_MAX}, {0x3244, 0x04, U32_MAX},
    {0x3248, 0x08, U32_MAX}, {0x322c, 0x0c, U32_MAX},
    {0x32a0, 0x10, U32_MAX}, {0x3228, 0x14, U32_MAX},
    {0x3224, 0x18, 0x0fffffffU}, {0x2058, 0x18, 0x30000000U},
    {0x3280, 0x1c, U32_MAX}, {0x3254, 0x20, U32_MAX},
    {0x3268, 0x24, U32_MAX}, {0x3264, 0x28, U32_MAX},
    {0x3260, 0x2c, U32_MAX}, {0x326c, 0x30, U32_MAX},
    {0x324c, 0x34, U32_MAX}, {0x32e0, 0x38, U32_MAX},
    {0x2044, 0x3c, 0x1ffffU}, {0x32e4, 0x40, U32_MAX},
    {0x32e8, 0x44, U32_MAX}, {0x2088, 0x4c, U32_MAX},
    {0x3300, 0x50, U32_MAX}, {0x330c, 0x54, U32_MAX}
};

static int stop(struct gf2_chan *ch)
{
    u32 active, rv, cv;
    /* fifo/nv04.c:41-88. Cleanup continues even for a faulted channel. */
    TRY(wr(ch, FIFO_CACHES, 0));
    TRY(rd(ch, CACHE_PUSH1, &active));
    if ((active & GF2_CHID_INVALID) == GF2_CHID) {
        TRY(mask(ch, CACHE_DMA_PUSH, 1, 0));
        TRY(wr(ch, CACHE_PUSH0, 0));
        TRY(mask(ch, CACHE_PULL0, 1, 0));
        for (u32 i = 0; i < sizeof(ramfc_layout) / sizeof(ramfc_layout[0]); ++i) {
            TRY(rd(ch, ramfc_layout[i].reg, &rv));
            if (gf_inst_read32(ch->f, &ch->f->imem.ramfc,
                               GF2_CHID * GF2_RAMFC_BYTES + ramfc_layout[i].off,
                               &cv)) return GF2_EIO;
            TRY(fc_write(ch, ramfc_layout[i].off,
                         (cv & ~ramfc_layout[i].bits) | (rv & ramfc_layout[i].bits)));
        }
        for (u32 i = 0; i < sizeof(ramfc_layout) / sizeof(ramfc_layout[0]); ++i)
            TRY(wr(ch, ramfc_layout[i].reg, 0));
        TRY(wr(ch, CACHE_GET, 0));
        TRY(wr(ch, CACHE_PUT, 0));
        TRY(wr(ch, CACHE_PUSH1, GF2_CHID_INVALID));
        TRY(wr(ch, CACHE_PUSH0, 1));
        TRY(wr(ch, CACHE_PULL0, 1));
    }
    TRY(mask(ch, FIFO_MODE, 1U << GF2_CHID, 0));
    return wr(ch, FIFO_CACHES, 1);
}

void gf2_fifo_stop(struct gf2_chan *ch)
{
    (void)gf2_fault_check(ch);
    int rc = stop(ch);
    if (rc) ch->last_rc = rc;
}

int gf2_fifo_pause(struct gf2_chan *ch)
{
    struct gf_io *io = &ch->f->io;
    u32 now, deadline, pull;
    TRY(gf2_fault_check(ch));
    /* fifo/nv04.c:212-243, with section 6's absolute deadline and no ACK. */
    TRY(wr(ch, FIFO_CACHES, 0));
    TRY(mask(ch, CACHE_PULL0, 1, 0));
    if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
    deadline = now + GF2_TIMEOUT_US;
    for (;;) {
        TRY(gf2_fault_check(ch));
        if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
        if ((i32)(now - deadline) >= 0) {
            if (gf_wait_timeout(io, "fifo_hash_busy", CACHE_PULL0,
                                "HASH_BUSY&00001000==0", " pull=", deadline, now))
                serial_puts_polled("\r\n");
            return GF2_ETIME;
        }
        TRY(rd(ch, CACHE_PULL0, &pull));
        if (!(pull & HASH_BUSY)) break;
        u32 delay = deadline - now;
        if (delay > GF2_WAIT_MAX_US) delay = GF2_WAIT_MAX_US;
        if (io->timing.wait_us(io->opaque, delay)) return GF2_EIO;
    }
    TRY(rd(ch, CACHE_PULL0, &pull));
    if (pull & HASH_FAILED) return GF2_EFAULT;
    return wr(ch, CACHE_HASH, 0);
}

void gf2_fifo_resume(struct gf2_chan *ch)
{
    int rc = gf2_fault_check(ch);
    /* fifo/nv04.c:247-254. Never reopen after a foreground fault. */
    if (!rc) rc = mask(ch, CACHE_PULL0, 1, 1);
    if (!rc) rc = wr(ch, FIFO_CACHES, 1);
    if (rc) ch->last_rc = rc;
}

#undef TRY
