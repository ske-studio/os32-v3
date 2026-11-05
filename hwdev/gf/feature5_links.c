/*
 * Copyright 2007 Stephane Marchesin
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/* MIT nouveau c03c1a1, nvkm/engine/gr/nv04.c:1211-1226, regs.h:160:
 * NV40 idle ignores SYNC_STALL and waits at most two seconds. The other
 * adapters below join existing OS32 F2/F4 ownership boundaries. */
#include "feature5.h"
#include "display/stage.h"
#include "internal.h"
#include "../../mm/pgalloc.h"
#include "../../include/sys.h"

enum { LINKS_GR_STATUS=0x400700U, LINKS_GR_SYNC_STALL=0x4000U };

i32 gf5_rc_map(i32 rc)
{
    switch (rc) {
    case 0: return GF5_OK;
    case GF5_EINVAL: case GF5_ENOTSUP: case GF5_EBIOS: case GF5_ERANGE:
    case GF5_EBUSY: case GF5_ETIME: case GF5_EIO: case GF5_ENOMEM:
    case GF5_ESTATE: return rc;
    case GF1_EINVAL: case GF2_EINVAL: case GF3_EINVAL: case GF4_EINVAL:
        return GF5_EINVAL;
    case GF1_ENOSPC: case GF2_ENOSPC: case GF4_ENOSPC: return GF5_ENOMEM;
    case GF3_ERANGE: case GF4_ERANGE: return GF5_ERANGE;
    case GF2_ESTATE: case GF3_ESTATE: case GF4_ESTATE: return GF5_ESTATE;
    case GF3_EUNSUPPORTED: case GF4_EUNSUPPORTED: case GF4_ENODEV:
        return GF5_ENOTSUP;
    case GF4_EBUSY: return GF5_EBUSY;
    case GF2_ETIME: case GF4_ETIME: case GF_SCRIPT_ETIME: return GF5_ETIME;
    case GF1_EPCI: case GF1_EMAP: case GF1_EIRQ: case GF1_ESTOP: case GF1_EIO:
    case GF2_EMAP: case GF2_EIO: case GF2_EFAULT: case GF2_EEXIST:
    case GF2_ECLASS: case GF2_ESTOP: case GF3_EFORMAT: case GF4_EFORMAT:
    case GF4_EPLL: case GF4_EDDC: case GF4_EEDID: case GF_SCRIPT_EIO:
    default: return GF5_EIO;
    }
}
/* Links return lower rc unchanged; only the public F5 exit classifies it. */
static i32 links_result(struct gf5 *g,i32 rc)
{
    if (g && rc) { g->source_rc=rc; g->last_rc=gf5_rc_map(rc); }
    return rc;
}

i32 gf5_link_admission(void *owner,bool open)
{
    struct gf5 *g=owner;
    if (!g) return GF5_EINVAL;
    if (!open) g->accepting=false;
    if (!g->display || !g->display->links || !g->display->links->admission)
        return links_result(g,GF5_EINVAL);
    const struct gf4_links *l=g->display->links;
    i32 rc=l->admission(l->owner,open);
    if (!rc) g->accepting=open;
    return links_result(g,rc);
}
i32 gf5_link_drain(void *owner,u32 dl)
{
    struct gf5 *g=owner;
    if (!g || !g->ch) return links_result(g,GF5_EINVAL);
    struct gf2_fence fence;
    i32 rc=gf2_fence_emit(g->ch,&fence,dl);
    if (!rc) rc=gf2_fence_wait(g->ch,fence,dl);
    return links_result(g,rc);
}
static i32 links_idle(struct gf5 *g)
{
    struct gf_io *io=&g->ch->f->io;
    u32 now,status;
    i32 rc=io->timing.clock_us(io->opaque,&now);
    if (rc) return rc;
    u32 limit=now+GF2_TIMEOUT_US;
    for (;;) {
        rc=io->timing.clock_us(io->opaque,&now);
        if (rc) return rc;
        if ((i32)(now-g->deadline)>=0 || (i32)(now-limit)>=0) return GF2_ETIME;
        rc=io->read(io->opaque,GF_MODE_REGISTER_BAR,LINKS_GR_STATUS,4,&status);
        if (rc) return rc;
        if (!(status&~LINKS_GR_SYNC_STALL)) return 0;
        u32 delay=g->deadline-now;
        if (delay>limit-now) delay=limit-now;
        if (delay>GF_INIT_WAIT_CHUNK_US) delay=GF_INIT_WAIT_CHUNK_US;
        rc=io->timing.wait_us(io->opaque,delay);
        if (rc) return rc;
    }
}
i32 gf5_link_drain_pause(void *owner)
{
    struct gf5 *g=owner;
    if (!g || !g->ch || !g->ch->f) return links_result(g,GF5_EINVAL);
    struct gf_io *io=&g->ch->f->io;
    if (!io->read || !io->timing.clock_us || !io->timing.wait_us)
        return links_result(g,GF5_EINVAL);
    i32 rc=gf5_link_admission(g,false);
    if (!rc) rc=gf5_link_drain(g,g->deadline);
    if (!rc) rc=gf2_fifo_pause(g->ch);
    if (!rc) rc=links_idle(g);
    return links_result(g,rc);
}
i32 gf5_link_unpause(void *owner)
{
    struct gf5 *g=owner;
    if (!g || !g->ch || !g->ch->f) return links_result(g,GF5_EINVAL);
    struct gf2_chan *ch=g->ch;
    i32 old_rc=ch->last_rc;
    ch->last_rc=0;
    gf2_fifo_resume(ch);
    i32 rc=ch->last_rc;
    if (!rc && ch->fault.latched) rc=GF2_EFAULT;
    if (!rc && ch->state!=GF2_RUN) rc=GF2_ESTATE;
    if (!rc) { ch->last_rc=old_rc; return 0; }
    ch->last_rc=rc;
    g->phase=GF5_FAULT;
    (void)gf5_link_admission(g,false);
    return links_result(g,rc);
}
i32 gf5_link_display_save(void *owner,struct gf5_image *image)
{
    struct gf5 *g=owner;
    if (!g || !g->display || !image) return links_result(g,GF5_EINVAL);
    return links_result(g,gf4_display_save(g->display,&image->display,g->deadline));
}
i32 gf5_link_display_restore(void *owner,const struct gf5_image *image)
{
    struct gf5 *g=owner;
    if (!g || !g->display || !image) return links_result(g,GF5_EINVAL);
    i32 rc=gf4_stage_deadline(g->display,g->ch ? g->ch->f : 0,&g->deadline);
    if (!rc) rc=gf4_display_restore(g->display,&image->display,g->deadline);
    return links_result(g,rc);
}
i32 gf5_link_stop(void *owner,u32 dl)
{
    struct gf5 *g=owner;
    if (!g || !g->display) return links_result(g,GF5_EINVAL);
    return links_result(g,gf4_service_suspend(g->display,dl));
}
i32 gf5_link_restart(void *owner,u32 dl)
{
    struct gf5 *g=owner;
    if (!g || !g->display) return links_result(g,GF5_EINVAL);
    i32 rc=gf4_service_restart(g->display,dl);
    /* F5 performance restoration begins after devinit/F1/F2/F3 restart. */
    if (!rc) rc=gf4_stage_deadline(g->display,g->ch ? g->ch->f : 0,&g->deadline);
    return links_result(g,rc);
}
/* Retain failed transaction saves along with the adapter-owned index/lock
 * state. In particular no V0/list or cleanup loan points at stack storage. */
static struct gf_init_context links_script;
i32 gf5_link_memory_script(void *owner,u32 pc)
{
    struct gf5 *g=owner;
    if (!g) return GF5_EINVAL;
    if (!g->rom.bytes) return links_result(g,GF5_ENOTSUP);
    if (!g->rom.size) return links_result(g,GF5_EBIOS);
    if (!g->rom.memory_program && !g->rom.memory_pc && !pc) return 0;
    if (!g->rom.memory_program || !pc || pc!=g->rom.memory_pc || pc>=g->rom.size)
        return links_result(g,GF5_EBIOS);
    struct gf_display_context *l=g->display ? g->display->legacy : 0;
    if (!l || !l->io.read || !l->io.write || !l->io.point || !l->io.restore ||
        !l->io.timing.clock_us || !l->io.timing.wait_us)
        return links_result(g,GF5_EINVAL);
    /* A prepare loan, when present, retains its start and consumed budget.
     * Later standalone runs borrow only legacy I/O, never devinit scratch. */
    if (links_script.saved_count || (l->init && l->init->saved_count))
        return links_result(g,GF5_ESTATE);
    links_script=(struct gf_init_context){.io=l->io,.rom=g->rom.bytes,
                                         .rom_size=g->rom.size,.last_rc=GF_SCRIPT_EIO};
    struct gf_init_context *ctx=&links_script;
    if (!l->init) {
        i32 rc=ctx->io.timing.clock_us(ctx->io.opaque,&ctx->started_us);
        if (rc) return links_result(g,rc);
    } else {
        ctx->started_us=l->init->started_us;
        ctx->steps=l->init->steps; ctx->wait_us=l->init->wait_us;
        ctx->elapsed_us=l->init->elapsed_us;
    }
    i32 rc=gf_init_script_execute(ctx,g->rom.memory_program,pc,0,0,g->deadline);
    /* On failure the runner already restored once, preserving its rc. */
    if (!rc) rc=ctx->io.restore(ctx->io.opaque,ctx);
    /* Preserve the same-operation budget without lending ROM/saves back
     * into the devinit scratch (whose original ROM may be a distinct copy). */
    if (l->init) {
        l->init->steps=ctx->steps; l->init->wait_us=ctx->wait_us;
        l->init->elapsed_us=ctx->elapsed_us;
    }
    return links_result(g,rc);
}

/* One resident window: a failed unmap must not leave a pointer to a local
 * view in V0's list. The view/adapter survive until explicit successful close.
 * nouveau_bo.c:669-689 is the map lifetime model; no push-map borrowing. */
static struct gf_vram_view links_view;
static i32 links_view_close(void *owner,struct gf_vram_view *view)
{
    (void)owner;
    /* UC write drain, as in the existing CPU VRAM paths. */
    if (view->mapped && view->bytes) {
        volatile u8 value=view->va[view->bytes-1U];
        (void)value;
    }
    return gf_vram_view_unmap(view);
}
i32 gf5_link_copy_extent(void *owner,const struct gf_vram_reservation *key,
                         u8 *ram,bool to_vram)
{
    struct gf5 *g=owner;
    if (!g || !g->ch || !g->display || !g->display->legacy || !key || !ram || !key->owner)
        return links_result(g,GF5_EINVAL);
    if (!key->bytes || ((key->off|key->bytes)&3U) || key->off>U32_MAX-key->bytes)
        return links_result(g,GF5_ERANGE);
    if (links_view.mapped) return links_result(g,GF5_EBUSY);
    struct gf_vram_reservation live;
    i32 rc=gf_vram_match(g->ch,key->owner,key->reservation_id,key->generation,
                         key->off,key->bytes,&live);
    if (rc) return links_result(g,rc);
    u32 off=key->off,left=key->bytes;
    while (left) {
        u32 bytes=left;
        volatile u8 *va;
        bool boot=off<GF_MODE_VRAM_BYTES;
        if (boot) {
            if (bytes>GF_MODE_VRAM_BYTES-off) bytes=GF_MODE_VRAM_BYTES-off;
            if (!g->display->legacy->bar1_map) return links_result(g,GF2_EMAP);
            va=(volatile u8 *)g->display->legacy->bar1_map+off;
        } else {
            links_view.close=links_view_close; links_view.close_owner=g;
            rc=gf_vram_view_map(&links_view,g->ch,off,bytes,&va);
            if (rc) return links_result(g,rc);
        }
        for (u32 i=0;i<bytes;++i) {
            if (to_vram) va[i]=ram[i];
            else ram[i]=va[i];
        }
        if (to_vram) { volatile u8 value=va[bytes-1U]; (void)value; }
        if (!boot) {
            rc=links_view_close(g,&links_view);
            if (rc) return links_result(g,rc);
        }
        off+=bytes; left-=bytes; ram+=bytes;
    }
    return 0;
}

/* Size metadata belongs to this successful page allocation, not a global
 * fixed-capacity table. Align the payload for every F5/F4 saved type. */
struct links_allocation { u32 pages,reserved[3]; };
i32 gf5_link_alloc(void *owner,u32 bytes,void **out)
{
    struct gf5 *g=owner;
    u32 overhead=sizeof(struct links_allocation)+MEM_PAGE_SIZE-1U;
    if (!g || !out || !bytes) return links_result(g,GF5_EINVAL);
    if (bytes>U32_MAX-overhead) return links_result(g,GF5_ENOMEM);
    u32 pages=(bytes+overhead)/MEM_PAGE_SIZE;
    u32 phys=pgalloc_alloc_phys(LEDGER_OWNER_KERNEL,(int)pages);
    if (!phys) return links_result(g,GF5_ENOMEM);
    struct links_allocation *a=P2V(phys);
    *a=(struct links_allocation){.pages=pages};
    *out=a+1;
    return 0;
}
void gf5_link_free(void *owner,void *allocation)
{
    struct gf5 *g=owner;
    if (!g || !allocation) { (void)links_result(g,GF5_EINVAL); return; }
    struct links_allocation *a=(struct links_allocation *)allocation-1;
    if (!pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,V2P(a)/MEM_PAGE_SIZE,(int)a->pages))
        (void)links_result(g,GF5_EIO);
}
i32 gf5_link_clock64(void *owner,u64 *us)
{
    struct gf5 *g=owner;
    u32 lo,hi;
    if (!g || !us) return links_result(g,GF5_EINVAL);
    i32 rc=sys_time_now(&lo,&hi);
    if (rc) return links_result(g,rc);
    *us=((u64)hi<<32)|lo;
    return 0;
}
static struct gf5_links links={
    .drain_pause=gf5_link_drain_pause,.unpause=gf5_link_unpause,
    .display_save=gf5_link_display_save,.display_restore=gf5_link_display_restore,
    .memory_script=gf5_link_memory_script,.admission=gf5_link_admission,
    .drain=gf5_link_drain,.stop=gf5_link_stop,.restart=gf5_link_restart,
    .copy_extent=gf5_link_copy_extent,.alloc=gf5_link_alloc,.free=gf5_link_free,
    .clock64=gf5_link_clock64
};
struct gf5_links *gf5_links_table(void) { return &links; }
