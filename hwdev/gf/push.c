/*
 * Copyright (C) 2007 Ben Skeggs.
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
 */
/* GF_FEAT2_DESIGN sections 3/6 and the INIT reference addendum.
 * Adapted from nouveau c03c1a1, nouveau_dma.c:41-147 and nouveau_dma.h:82-96.
 * The lifecycle owner initializes the 32 NOPs and free=max-cur before use
 * (nouveau_chan.c:439-478); the ring and channel have one foreground owner. */
#include "command.h"

#define PUSH_USER (GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE)

static int push_poll(struct gf2_chan *ch, u32 deadline)
{
    struct gf_io *io = &ch->f->io;
    u32 now;
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (io->timing.clock_us(io->opaque, &now)) return GF2_EIO;
    return (i32)(now - deadline) >= 0 ? GF2_ETIME : GF2_OK;
}

static int push_wait(struct gf2_chan *ch)
{
    struct gf_io *io = &ch->f->io;
    return io->timing.wait_us(io->opaque, GF2_WAIT_MIN_US) ? GF2_EIO : GF2_OK;
}

/* nouveau_dma.c:41-66,70-91: ignore transient GETs outside the ring/skips.
 * Unlike the reference timeout, the operation's absolute deadline is fixed. */
static int push_get(struct gf2_chan *ch, u32 deadline, bool after_skips, u32 *get)
{
    struct gf_io *io = &ch->f->io;
    for (;;) {
        u32 raw, off;
        int rc = push_poll(ch, deadline);
        if (rc) return rc;
        if (io->read(io->opaque, GF_MODE_REGISTER_BAR,
                     PUSH_USER + GF2_USER_GET, sizeof(raw), &raw)) return GF2_EIO;
        off = raw - ch->B;
        if (raw >= ch->B && off <= ch->max * sizeof(u32) &&
            !(off & (sizeof(u32) - 1U))) {
            *get = off / sizeof(u32);
            if (*get >= GF2_PUSH_SKIPS &&
                (!after_skips || *get > GF2_PUSH_SKIPS)) return GF2_OK;
        }
        rc = push_wait(ch);
        if (rc) return rc;
    }
}

/* nouveau_dma.h:82-87 WRITE_PUT, user aperture from nouveau_chan.c:440-443.
 * Both the ordinary kick and the wrap publish use this exact drain order. */
static int push_put(struct gf2_chan *ch, u32 put)
{
    struct gf_io *io = &ch->f->io;
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    __asm__ volatile ("" ::: "memory");
    __sync_synchronize();
    (void)ch->va[0];
    return io->write(io->opaque, GF_MODE_REGISTER_BAR,
                     PUSH_USER + GF2_USER_PUT, sizeof(u32),
                     ch->B + put * sizeof(u32)) ? GF2_EIO : GF2_OK;
}

int gf2_kick(struct gf2_chan *ch)
{
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (ch->state != GF2_INIT && ch->state != GF2_RUN && ch->state != GF2_DRAIN)
        return GF2_ESTATE;
    if (ch->cur == ch->put) return GF2_OK;
    rc = push_put(ch, ch->cur);
    if (!rc) ch->put = ch->cur;
    return rc;
}

/* nouveau_dma.c:70-147: reserve a whole packet; leave a word before GET.
 * max excludes the spare tail words, so even a full tail can hold a jump. */
static int push_reserve(struct gf2_chan *ch, u32 words, u32 deadline)
{
    int rc = push_poll(ch, deadline);
    if (rc) return rc;
    if (ch->free >= words) return GF2_OK;
    /* GF_FEAT2_DESIGN section 3: publish pending packets before GET waits. */
    rc = gf2_kick(ch);
    if (rc) return rc;
    while (ch->free < words) {
        u32 get;
        rc = push_get(ch, deadline, false, &get);
        if (rc) return rc;
        if (get <= ch->cur) {
            ch->free = ch->max - ch->cur;
            if (ch->free >= words) break;
            /* nouveau_dma.c:110-132: legacy absolute jump, then skip NOPs. */
            ch->va[ch->cur++] = ch->B | GF2_PUSH_JUMP;
            rc = push_get(ch, deadline, true, &get);
            if (rc) return rc;
            rc = push_put(ch, GF2_PUSH_SKIPS);
            if (rc) return rc;
            ch->cur = ch->put = GF2_PUSH_SKIPS;
        }
        ch->free = get - ch->cur - 1U;
        if (ch->free < words) {
            rc = push_wait(ch);
            if (rc) return rc;
        }
    }
    return GF2_OK;
}

/* include/nvif/push006c.h:50-65: count/header and contiguous method data. */
static int push_packet(struct gf2_chan *ch, u8 subc, u16 method, bool noninc,
                       const u32 *data, u16 count, u32 deadline)
{
    u32 words = (u32)count + 1U;
    int rc = push_reserve(ch, words, deadline);
    if (rc) return rc;
    ch->va[ch->cur++] = (noninc ? GF2_PUSH_NONINC : 0U) |
                        ((u32)count << GF2_METHOD_COUNT_SHIFT) |
                        ((u32)subc << GF2_METHOD_SUBCHANNEL_SHIFT) | method;
    for (u32 i = 0; i < count; ++i) ch->va[ch->cur++] = data[i];
    ch->free -= words;
    return GF2_OK;
}

int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc,
             const u32 *data, u16 count, u32 deadline_us)
{
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (ch->state != GF2_RUN) return GF2_ESTATE;
    if (!count || count > GF2_METHOD_COUNT_MAX ||
        subc > GF2_SUBCHANNEL_MAX || (method & (sizeof(u32) - 1U)) ||
        method > GF2_METHOD_MAX ||
        (!noninc && (u32)method + ((u32)count - 1U) * sizeof(u32) > GF2_METHOD_MAX))
        return GF2_EINVAL;
    return push_packet(ch, subc, method, noninc, data, count, deadline_us);
}

/* INIT/DRAIN emit only this fixed packet (nv10_fence.c:33-41). */
int gf2_push_reference(struct gf2_chan *ch, u32 seq, u32 deadline_us)
{
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    if (ch->state != GF2_INIT && ch->state != GF2_RUN && ch->state != GF2_DRAIN)
        return GF2_ESTATE;
    return push_packet(ch, GF2_REFERENCE_SUBCHANNEL, GF2_REFERENCE_METHOD,
                       false, &seq, 1, deadline_us);
}
