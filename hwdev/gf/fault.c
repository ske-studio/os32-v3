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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Register sequences derived from MIT nouveau, Ben Skeggs,
 * c03c1a14507dfaf8b856c2608f9ac216021fc222, nvkm/engine/ paths below.
 * OS32 policy: foreground capture, no ACK, repair, restart or replay.
 */
#include "command.h"
#include "../../include/io.h"
#include "../../lib/serial_diag.h"

/* GF_FEAT2_DESIGN sections 2/6; fifo/regsnv04.h:20-22,35-124. */
#define FAULT_CACHES 0x002500U
#define FAULT_PUSH0 0x003200U
#define FAULT_PUSH1 0x003204U
#define FAULT_DMA_PUSH 0x003220U
#define FAULT_DMA_FETCH 0x003224U
#define FAULT_DMA_STATE 0x003228U
#define FAULT_DMA_PUT 0x003240U
#define FAULT_DMA_GET 0x003244U
#define FAULT_PULL0 0x003250U
#define FAULT_CACHE_GET 0x003270U
#define FAULT_DMA_DCOUNT 0x0032a0U
#define FAULT_CACHE_METHOD 0x090000U
#define FAULT_CACHE_GET_MASK 0x7ffU
#define FAULT_CACHE_STRIDE 8U
#define FAULT_GRAPH_CTX 0x40032cU
#define FAULT_GRAPH_ADDR 0x400704U
#define FAULT_GRAPH_DATA 0x400708U
#define FAULT_GRAPH_CLASS 0x400160U
#define FAULT_GRAPH_FIFO 0x400720U

static int rd(struct gf_foundation *f, u32 off, u32 *v)
{ return f->io.read(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(*v), v); }
static int wr(struct gf_foundation *f, u32 off, u32 v)
{ return f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(v), v); }
static int close_gate(struct gf_foundation *f, u32 off)
{
    u32 v;
    if (rd(f, off, &v)) return -1;
    return wr(f, off, v & ~1U);
}

static void merge(struct gf_irq_stats *latest, const struct gf_irq_stats *now)
{
    latest->entries = now->entries;
    latest->fifo = now->fifo;
    latest->graph = now->graph;
    latest->vblank[0] = now->vblank[0];
    latest->vblank[1] = now->vblank[1];
    latest->pmc |= now->pmc;
    latest->pfifo |= now->pfifo;
    latest->pgraph |= now->pgraph;
    latest->nsource |= now->nsource;
    latest->nstatus |= now->nstatus;
    latest->unknown |= now->unknown;
}

static int capture(struct gf2_chan *ch)
{
    struct gf_foundation *f = ch->f;
    struct gf2_fault *fault = &ch->fault;
    /* fifo/nv04.c:415-419,303-329,354-362. Do not restore CACHES. */
    int rc = rd(f, FAULT_CACHES, &fault->caches);
    rc |= wr(f, FAULT_CACHES, 0);
    rc |= rd(f, FAULT_PUSH1, &fault->push1);
    fault->chid = fault->push1 & GF2_CHID_INVALID;
    rc |= rd(f, FAULT_CACHE_GET, &fault->cache_get);
    rc |= rd(f, FAULT_PULL0, &fault->pull0);
    u32 off = FAULT_CACHE_METHOD +
              ((fault->cache_get & FAULT_CACHE_GET_MASK) / sizeof(u32)) * FAULT_CACHE_STRIDE;
    rc |= rd(f, off, &fault->method);
    rc |= rd(f, off + sizeof(u32), &fault->data);
    rc |= rd(f, FAULT_DMA_PUT, &fault->put);
    rc |= rd(f, FAULT_DMA_GET, &fault->get);
    rc |= rd(f, FAULT_DMA_PUSH, &fault->dma_push);
    rc |= rd(f, FAULT_DMA_STATE, &fault->dma_state);
    rc |= rd(f, FAULT_DMA_FETCH, &fault->dma_fetch);
    rc |= rd(f, FAULT_DMA_DCOUNT, &fault->dma_dcount);
    rc |= rd(f, GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE + GF2_USER_REFERENCE,
             &fault->reference);
    /* gr/nv40.c:241-246. gr_method retains trapped addr, including subc. */
    rc |= rd(f, FAULT_GRAPH_CTX, &fault->gr_trap);
    rc |= rd(f, FAULT_GRAPH_ADDR, &fault->gr_method);
    rc |= rd(f, FAULT_GRAPH_DATA, &fault->gr_data);
    u32 subc = (fault->gr_method >> 16) & GF2_SUBCHANNEL_MAX;
    rc |= rd(f, FAULT_GRAPH_CLASS + subc * sizeof(u32), &fault->gr_class);
    fault->gr_class &= 0xffffU;
    return rc;
}

static void log_fault(const struct gf2_fault *f)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[gf2] fault epoch="); serial_diag_u32(f->epoch);
    serial_puts_polled(" chid="); serial_diag_u32(f->chid);
    serial_puts_polled(" PUT="); serial_diag_hex(f->put, 8);
    serial_puts_polled(" GET="); serial_diag_hex(f->get, 8);
    serial_puts_polled(" reference="); serial_diag_hex(f->reference, 8);
    serial_puts_polled(" pfifo="); serial_diag_hex(f->first.pfifo, 8);
    serial_puts_polled(" pgraph="); serial_diag_hex(f->first.pgraph, 8);
    serial_puts_polled(" nsource="); serial_diag_hex(f->first.nsource, 8);
    serial_puts_polled(" nstatus="); serial_diag_hex(f->first.nstatus, 8);
    serial_puts_polled(" subc="); serial_diag_u32((f->method >> 13) & GF2_SUBCHANNEL_MAX);
    serial_puts_polled(" method="); serial_diag_hex(f->method & GF2_METHOD_MAX, 4);
    serial_puts_polled(" data="); serial_diag_hex(f->data, 8);
    serial_puts_polled(" gr_class="); serial_diag_hex(f->gr_class, 4);
    serial_puts_polled(" gr_subc="); serial_diag_u32((f->gr_method >> 16) & GF2_SUBCHANNEL_MAX);
    serial_puts_polled(" gr_method="); serial_diag_hex(f->gr_method & GF2_METHOD_MAX, 4);
    serial_puts_polled(" gr_data="); serial_diag_hex(f->gr_data, 8);
    serial_puts_polled("\r\n");
}

int gf2_fault_check(struct gf2_chan *ch)
{
    struct gf_irq_stats now;
    gf_irq_snapshot(ch->f, &now);
    u32 fifo_delta = now.fifo - ch->seen.fifo;
    u32 graph_delta = now.graph - ch->seen.graph;
    ch->seen = now;
    if (ch->fault.latched) {
        merge(&ch->fault.latest, &now);
        return GF2_EFAULT;
    }
    if (!fifo_delta && !graph_delta)
        return ch->state == GF2_FAULT ? GF2_EFAULT : GF2_OK;
    ch->fault = (struct gf2_fault){0};
    ch->fault.latched = true;
    ch->fault.epoch = ch->epoch;
    ch->fault.first = ch->fault.latest = now;
    int rc = capture(ch);
    /* F1 owns ACK/masks; its enable contract requires IRQ save/restore. */
    unsigned saved = irq_save();
    rc |= gf_irq_enable(ch->f, false, false, ch->heads);
    irq_restore(saved);
    /* fifo/nv04.c:53-60,340: close, omitting :345-349/:397-398 restart.
     * gr/nv40.c:268 supplies FIFO bit0; OS32 closes it instead of resuming. */
    rc |= close_gate(ch->f, FAULT_DMA_PUSH);
    rc |= close_gate(ch->f, FAULT_PUSH0);
    rc |= close_gate(ch->f, FAULT_PULL0);
    rc |= close_gate(ch->f, FAULT_GRAPH_FIFO);
    ch->state = GF2_FAULT;
    ch->last_rc = rc ? GF2_EIO : GF2_EFAULT;
    log_fault(&ch->fault);
    return GF2_EFAULT;
}
