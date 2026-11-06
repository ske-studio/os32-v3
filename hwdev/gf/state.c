/*
 * Copyright 2009, 2012, 2013 Red Hat Inc.
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
 * References (nouveau c03c1a1): nouveau_drm.c:944-991,1007-1028;
 * dispnv04/disp.c:57-95,99-159. OS32 uses retained reservations and CPU
 * copies instead of TTM eviction, and drains before borrowing CPU windows.
 * No PCI power transition or OS suspend is performed here.
 */
#include "feature5.h"
#include "vram_view.h"
#include "../../kdebug/gfinit_pll.h"

static i32 state_result(struct gf5 *g, i32 rc)
{
    if (!g) return gf5_rc_map(rc);
    /* Nested F5 operations have already retained their original lower rc. */
    if (!rc) g->source_rc=0;
    else if (g->last_rc!=rc || !g->source_rc) g->source_rc=rc;
    return g->last_rc=gf5_rc_map(rc);
}

static i32 state_reject(struct gf5 *g, i32 rc)
{
    if (g) g->source_rc=rc;
    return g ? (g->last_rc=gf5_rc_map(rc)) : gf5_rc_map(rc);
}

static bool state_links(const struct gf5 *g)
{
    return g && g->ch && g->accel && g->display && g->links.owner &&
        g->links.admission && g->links.drain && g->links.display_save &&
        g->links.display_restore && g->links.stop && g->links.restart &&
        g->links.copy_extent && g->links.alloc && g->links.free && g->links.clock64;
}

static i32 state_deadline(struct gf5 *g)
{
    u64 now;
    i32 rc=g->links.clock64(g->links.owner,&now);
    if (!rc) g->deadline=(u32)now+GF2_TIMEOUT_US;
    return rc;
}

static i32 state_fault(struct gf5 *g, i32 rc)
{
    g->phase=GF5_FAULT; g->accepting=false; g->busy=false;
    return state_result(g,rc);
}

static i32 state_cancel(struct gf5 *g, i32 rc, bool was_accepting)
{
    i32 result=state_result(g,rc), source=g->source_rc;
    i32 open=g->links.admission(g->links.owner,was_accepting);
    g->busy=false; g->phase=open ? GF5_FAULT : GF5_RUN;
    g->accepting=open ? false : was_accepting;
    g->source_rc=source; g->last_rc=result;
    return result;
}

/* The live heat policy is not part of the image. In particular an image
 * captured before downclock must not raise any domain on resume (§6). */
/* All snapshot storage is one allocation: metadata, extent values, deep
 * trip values, then byte images. No serialized map/node/GPU pointers. */
i32 gf5_state_save(struct gf5 *g, struct gf5_image **out)
{
    if (!state_links(g) || !out) return state_reject(g,GF5_EINVAL);
    if (g->busy || g->phase==GF5_SAVING || g->phase==GF5_RESTORING)
        return state_reject(g,GF5_EBUSY);
    if (g->phase!=GF5_RUN) return state_reject(g,GF5_ESTATE);
    if (g->heat.critical_paused || gf5_stopping(g))
        return state_reject(g,GF5_ESTATE);
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        const struct gf5_tile_slot *s=&g->tile_slot[i];
        if ((s->state!=GF_TILE_FREE && s->state!=GF_TILE_OWNED) ||
            g->tile_live[i]!=(s->state==GF_TILE_OWNED))
            return state_reject(g,GF5_ESTATE);
    }
    g->last_rc=g->source_rc=0;
    i32 rc=state_deadline(g);
    if (rc) return state_result(g,rc);
    struct gf5_clock clock;
    u32 uv;
    u8 vid=0;
    /* Read-only capture precedes the closed transaction. */
    rc=gf5_clock_read(g,&clock);
    if (!rc) rc=gf5_volt_get(g,&uv);
    if (!rc && g->vid.get) rc=g->vid.get(g->vid.owner,&vid);
    if (rc) return state_result(g,rc);
    if (g->fan.ntrip && !g->fan.trip) return state_reject(g,GF5_ESTATE);
    g->busy=true; g->phase=GF5_SAVING;
    bool was_accepting=g->accepting;
    g->accepting=false;
    rc=g->links.admission(g->links.owner,false);
    if (rc) return state_fault(g,rc);

    u32 cursor=0, count=0;
    u64 total=sizeof(struct gf5_image)+(u64)g->fan.ntrip*sizeof(struct gf5_fan_trip);
    struct gf_vram_reservation key;
    while (!rc) {
        i32 next=gf_vram_next(g->ch,&cursor,&key);
        if (next<0) { rc=next; break; }
        if (!next) break;
        if (!key.bytes || ((key.off|key.bytes)&3U)) { rc=GF5_ERANGE; break; }
        total+=sizeof(struct gf5_extent)+(u64)key.bytes;
        if (total>U32_MAX) { rc=GF5_ENOMEM; break; }
        ++count;
    }
    if (!rc && total>U32_MAX) rc=GF5_ENOMEM;
    void *allocation=0;
    if (!rc) rc=g->links.alloc(g->links.owner,(u32)total,&allocation);
    if (rc) {
        /* No display/VRAM/stop operation has occurred. Restore admission;
         * preserve the first error if reopening itself fails. */
        return state_cancel(g,rc,was_accepting);
    }
    struct gf5_image *image=allocation;
    *image=(struct gf5_image){.version=GF5_IMAGE_VERSION,.bytes=(u32)total,
        .nextent=count,.saved_epoch=g->ch->epoch,.clock=clock,
        .requested_uv=uv,.vid=vid,.fan=g->fan};
    image->extent=(void *)(image+1);
    image->fan.trip=(void *)(image->extent+count);
    if (!image->fan.ntrip) image->fan.trip=0;
    for (u32 i=0;i<image->fan.ntrip;++i) image->fan.trip[i]=g->fan.trip[i];
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        image->tile[i]=g->tile[i]; image->tile_live[i]=g->tile_live[i];
        const struct gf5_tile_slot *s=&g->tile_slot[i];
        image->tile_saved[i]=(struct gf5_tile_saved){
            s->state,s->raw_addr,s->raw_limit,s->raw_pitch};
    }
    u8 *copy=(u8 *)(image->extent+count)+image->fan.ntrip*sizeof(struct gf5_fan_trip);
    cursor=0;
    for (u32 i=0;i<count;++i) {
        rc=gf_vram_next(g->ch,&cursor,&key);
        if (rc!=1) {
            if (!rc) rc=GF5_ESTATE;
            g->links.free(g->links.owner,image);
            return state_cancel(g,rc,was_accepting);
        }
        image->extent[i]=(struct gf5_extent){key,copy}; copy+=key.bytes;
    }
    /* Ownership transfers before the first operation that changes display.
     * After this point even an incomplete/failed image belongs to caller. */
    *out=image; g->image=image;
    rc=g->links.drain(g->links.owner,g->deadline);
    if (!rc) rc=g->links.display_save(g->links.owner,image);
    for (u32 i=0;!rc && i<count;++i)
        rc=g->links.copy_extent(g->links.owner,&image->extent[i].key,
                                image->extent[i].host_copy,false);
    if (rc) return state_fault(g,rc);
    image->complete=true;
    rc=g->links.stop(g->links.owner,g->deadline);
    if (rc) return state_fault(g,rc);
    if (g->ch->state!=GF2_OFF) return state_fault(g,GF5_ESTATE);
    g->phase=GF5_SAVED; g->busy=false;
    return state_result(g,0);
}

/* nvkm/subdev/clk/base.c:159-215: RAM, fan up, voltage up, core,
 * voltage down, fan down. The outer deadline and closed RESTORING phase
 * survive nested RAM/tile operations; B2 owns its own pause interval. */
static i32 state_performance(struct gf5 *g, const struct gf5_image *image)
{
    struct gf5_plan plan;
    u32 previous=0;
    i32 rc=gf5_clock_calc(g,&image->clock,&plan);
    if (!rc) rc=gf5_plan_ceiling(g,&plan);
    if (!rc) rc=gf5_ram_prog(g,&plan);
    if (!rc) rc=gf5_fan_cstate(g,(u8)image->fan.cstate,1);
    if (!rc) rc=gf5_volt_get(g,&previous);
    if (!rc && image->requested_uv>=previous) rc=gf5_volt_set(g,image->requested_uv);
    if (!rc) rc=gf5_clock_prog(g,&plan);
    if (!rc && image->requested_uv<previous) rc=gf5_volt_set(g,image->requested_uv);
    if (!rc) rc=gf5_fan_cstate(g,(u8)image->fan.cstate,-1);
    if (!rc) rc=gf5_therm_resume(g,&image->fan);
    return rc;
}

i32 gf5_state_restore(struct gf5 *g, const struct gf5_image *image)
{
    if (!state_links(g) || !image) return state_reject(g,GF5_EINVAL);
    if (g->busy || g->phase==GF5_SAVING || g->phase==GF5_RESTORING)
        return state_reject(g,GF5_EBUSY);
    if (g->phase!=GF5_SAVED || g->image!=image || !image->complete ||
        image->version!=GF5_IMAGE_VERSION) return state_reject(g,GF5_ESTATE);
    if (g->heat.critical_paused || gf5_stopping(g))
        return state_reject(g,GF5_ESTATE);
    if (gf5_above_ceiling(g,&image->clock)) return state_reject(g,GF5_EBUSY);
    g->last_rc=g->source_rc=0;
    i32 rc=state_deadline(g);
    if (rc) return state_result(g,rc);
    g->busy=true; g->phase=GF5_RESTORING; g->accepting=false;
    /* Match all retained keys before any restart or VRAM write. */
    for (u32 i=0;!rc && i<image->nextent;++i) {
        const struct gf_vram_reservation *key=&image->extent[i].key;
        struct gf_vram_reservation live;
        rc=gf_vram_match(g->ch,key->owner,key->reservation_id,key->generation,
                         key->off,key->bytes,&live);
        if (rc==GF3_ERANGE) {
            g->source_rc=rc; g->last_rc=GF5_ESTATE; rc=GF5_ESTATE;
        }
        if (!rc && (live.off!=key->off || live.bytes!=key->bytes)) rc=GF5_ESTATE;
    }
    /* The image owns values, not loans. OFF/recover must leave the resident
     * nodes linked until the replacement descriptor has been programmed.
     * Reject a retired/reused slot instead of resurrecting its reservation. */
    for (u32 i=0;!rc && i<GF5_TILE_COUNT;++i) {
        const struct gf5_tile_saved *saved=&image->tile_saved[i];
        const struct gf5_tile_slot *s=&g->tile_slot[i];
        if ((saved->state!=GF_TILE_FREE && saved->state!=GF_TILE_OWNED) ||
            s->state!=saved->state || s->raw_addr!=saved->raw_addr ||
            s->raw_limit!=saved->raw_limit || s->raw_pitch!=saved->raw_pitch ||
            image->tile_live[i]!=(saved->state==GF_TILE_OWNED) ||
            g->tile_live[i]!=image->tile_live[i]) { rc=GF5_ESTATE; break; }
        if (saved->state==GF_TILE_OWNED) {
            if (s->current>1U) { rc=GF5_ESTATE; break; }
            const struct gf_vram_borrow *loan=&s->loan[s->current];
            const struct gf_vram_reservation *key=&loan->key;
            const struct gf5_tile *tile=&image->tile[i];
            struct gf_vram_reservation live;
            if (!key->reservation_id || loan->kind!=GF_BORROW_TILE ||
                key->reservation_id!=tile->reservation_id ||
                key->generation!=tile->generation || loan->off!=tile->base ||
                loan->bytes!=tile->bytes) { rc=GF5_ESTATE; break; }
            rc=gf_vram_match(g->ch,key->owner,key->reservation_id,key->generation,
                             key->off,key->bytes,&live);
            if (rc==GF3_ERANGE) {
                g->source_rc=rc; g->last_rc=GF5_ESTATE; rc=GF5_ESTATE;
            }
            if (!rc && (live.off!=key->off || live.bytes!=key->bytes)) rc=GF5_ESTATE;
        }
    }
    if (!rc) rc=g->links.restart(g->links.owner,g->deadline);
    if (!rc && (g->ch->state!=GF2_RUN || g->ch->epoch==image->saved_epoch)) rc=GF5_ESTATE;
    if (!rc) rc=state_performance(g,image);
    for (u32 i=0;!rc && i<GF5_TILE_COUNT;++i) {
        if (image->tile_saved[i].state==GF_TILE_OWNED) rc=gf5_tile_set(g,i,&image->tile[i]);
        else rc=gf5_tile_clear(g,i);
        const struct gf5_tile_saved *saved=&image->tile_saved[i];
        const struct gf5_tile_slot *s=&g->tile_slot[i];
        if (!rc && (s->state!=saved->state || s->raw_addr!=saved->raw_addr ||
                    s->raw_limit!=saved->raw_limit || s->raw_pitch!=saved->raw_pitch))
            rc=GF5_ESTATE;
    }
    for (u32 i=0;!rc && i<image->nextent;++i)
        rc=g->links.copy_extent(g->links.owner,&image->extent[i].key,
                                image->extent[i].host_copy,true);
    if (!rc) rc=g->links.display_restore(g->links.owner,image);
    if (rc) return state_fault(g,rc);
    /* F4 restore reinstates IRQ wishes but deliberately leaves slots shut. */
    g->display->saved_closed=false;
    g->phase=GF5_RUN;
    rc=g->links.admission(g->links.owner,true);
    if (rc) { g->display->saved_closed=true; return state_fault(g,rc); }
    /* Restart advances the channel epoch, not the thermal policy's lifetime. */
    if (g->heat.down_active) g->heat.epoch=g->ch->epoch;
    g->accepting=true; g->busy=false;
    return state_result(g,0);
}

void gf5_state_free(struct gf5 *g, struct gf5_image *image)
{
    if (!g || !image || !g->links.free) return;
    if (g->image==image) g->image=0;
    /* A single allocation owns all extent copies and deep trip storage.
     * FAULT is no reason to retain a caller's discarded snapshot. */
    g->links.free(g->links.owner,image);
}
