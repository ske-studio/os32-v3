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
#include "feature5.h"
#include "foundation.h"

/* nouveau nvkm/subdev/fb/nv44.c:30-46,60; engine/gr/nv44.c:31-46. */
#define TILE_ALIGN 65536U /* nouveau_bo.c:178-188 */
#define TILE_PITCH_MIN 1024U /* X nv_accel_common.c:94-100, NV4x */
#define TILE_ROW_FACTOR 64U /* nouveau_bo.c:178-188 */
#define TILE_STRIDE 0x10U
#define FB_ADDR 0x100600U
#define FB_LIMIT 0x100604U
#define FB_PITCH 0x100608U
#define GR_ADDR 0x400900U
#define GR_LIMIT 0x400904U
#define GR_PITCH 0x400908U
#define TILE_ENABLE 1U
#define TILE_BAR 0U
#define TILE_WIDTH 4U
#define TILE_MAX 0xffffffffULL

static i32 tile_result(struct gf5 *g, i32 rc)
{
    if (g) {
        g->source_rc=rc;
        g->last_rc=rc==GF3_EBUSY ? GF5_EBUSY : gf5_rc_map(rc);
        return g->last_rc;
    }
    return gf5_rc_map(rc);
}

/* Remainder by shift/subtract avoids a 64-bit runtime division helper. */
static u64 tile_remainder(u64 value, u64 divisor)
{
    u64 rem=0;
    for (int bit=63; bit>=0; --bit) {
        rem=(rem<<1)|((value>>bit)&1U);
        if (rem>=divisor) rem-=divisor;
    }
    return rem;
}

static i32 tile_fb_program(struct gf5 *g,u32 slot,u32 addr,u32 limit,u32 pitch)
{
    struct gf_io *io=&g->ch->f->io;
    u32 d=slot*TILE_STRIDE, flush;
    i32 rc;
    if ((rc=io->write(io->opaque,TILE_BAR,FB_LIMIT+d,TILE_WIDTH,limit)) ||
        (rc=io->write(io->opaque,TILE_BAR,FB_PITCH+d,TILE_WIDTH,pitch)) ||
        (rc=io->write(io->opaque,TILE_BAR,FB_ADDR+d,TILE_WIDTH,addr)) ||
        (rc=io->read(io->opaque,TILE_BAR,FB_ADDR+d,TILE_WIDTH,&flush)) ||
        0) return rc;
    return 0;
}

static i32 tile_program(struct gf5 *g,u32 slot,u32 addr,u32 limit,u32 pitch)
{
    struct gf_io *io=&g->ch->f->io;
    u32 d=slot*TILE_STRIDE;
    i32 rc=tile_fb_program(g,slot,addr,limit,pitch);
    if (rc) return rc;
    if ((rc=io->write(io->opaque,TILE_BAR,GR_PITCH+d,TILE_WIDTH,pitch)) ||
        (rc=io->write(io->opaque,TILE_BAR,GR_LIMIT+d,TILE_WIDTH,limit)) ||
        (rc=io->write(io->opaque,TILE_BAR,GR_ADDR+d,TILE_WIDTH,addr))) return rc;
    return 0;
}

/* loan nodes never move; a zero key denotes an unused node. */
static i32 tile_put(struct gf5 *g,struct gf_vram_borrow *loan)
{
    return loan->key.reservation_id ? gf_vram_borrow_put(g->ch,loan) : 0;
}

static i32 tile_free(struct gf5 *g,u32 slot)
{
    struct gf5_tile_slot *s=&g->tile_slot[slot];
    i32 rc=tile_put(g,&s->loan[0]);
    if (!rc) rc=tile_put(g,&s->loan[1]);
    if (rc) return rc;
    *s=(struct gf5_tile_slot){0};
    g->tile[slot]=(struct gf5_tile){0};
    g->tile_live[slot]=false;
    return 0;
}

static i32 tile_fault(struct gf5 *g,u32 slot,i32 rc)
{
    g->tile_slot[slot].state=GF_TILE_UNKNOWN;
    g->phase=GF5_FAULT; g->accepting=false;
    return tile_result(g,rc);
}

static i32 tile_apply(struct gf5 *g,u32 slot,const struct gf5_tile *tile,u32 bytes)
{
    struct gf5_tile_slot *s=&g->tile_slot[slot];
    bool reopen=g->accepting;
    i32 rc;
    /* Retain the attempted raw descriptor even when a write applies then fails.
     * Old and new byte ranges/keys remain in their resident loan nodes. */
    s->raw_addr=tile ? tile->base|TILE_ENABLE : 0;
    s->raw_limit=tile ? tile->base+bytes-1U : 0;
    s->raw_pitch=tile ? tile->pitch : 0;
    g->poll_source="tile-pause";
    rc=g->links.drain_pause(g->links.owner);
    if (rc) goto fault;
    g->poll_source="tile-program";
    rc=tile_program(g,slot,s->raw_addr,s->raw_limit,s->raw_pitch);
    if (rc) goto fault;
    g->poll_source="tile-unpause";
    rc=g->links.unpause(g->links.owner);
    if (rc) goto fault;
    if (reopen) {
        g->poll_source="tile-admission";
        rc=g->links.admission(g->links.owner,true);
        if (rc) goto fault;
    }
    g->poll_source="tile-put";
    if (tile) {
        rc=tile_put(g,&s->loan[s->current]);
        if (rc) goto fault;
        s->current^=1U;
        s->state=GF_TILE_OWNED;
        g->tile[slot]=*tile; g->tile[slot].bytes=bytes;
        g->tile_live[slot]=true;
    } else {
        rc=tile_free(g,slot);
        if (rc) goto fault;
    }
    g->poll_source=0;
    return tile_result(g,0);
fault:
    return tile_fault(g,slot,rc);
}

static int tile_io_valid(struct gf5 *g)
{
    return g && g->ch && g->ch->f && g->ch->f->io.read && g->ch->f->io.write;
}

/* X nv_accel_common.c:94-100: accept only fixed points of its generator. */
static int tile_pitch_valid(u32 pitch)
{
    u32 a=1U, q=pitch/4U;
    while (q>=2U) { q>>=1; a<<=1; }
    if (a<TILE_PITCH_MIN) a=TILE_PITCH_MIN;
    return pitch && !(pitch&(a-1U));
}

i32 gf5_tiles_init(struct gf5 *g)
{
    if (!tile_io_valid(g)) return tile_result(g,GF5_EINVAL);
    if (g->accepting || g->ch->state!=GF2_OFF || g->image)
        return tile_result(g,GF5_EBUSY);
    for (u32 i=0;i<GF5_TILE_COUNT;++i)
        if (g->tile_slot[i].loan[0].key.reservation_id ||
            g->tile_slot[i].loan[1].key.reservation_id)
            return tile_result(g,GF5_EBUSY);
    /* No partial initialization may advertise a free slot. */
    for (u32 i=0;i<GF5_TILE_COUNT;++i) g->tile_slot[i].state=GF_TILE_UNKNOWN;
    g->poll_source="tile-init";
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        i32 rc=tile_fb_program(g,i,0,0,0);
        if (rc) return tile_fault(g,i,rc);
    }
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        g->tile_slot[i]=(struct gf5_tile_slot){0};
        g->tile[i]=(struct gf5_tile){0}; g->tile_live[i]=false;
    }
    g->phase=GF5_RUN;
    g->poll_source=0;
    return tile_result(g,0);
}

i32 gf5_tiles_retire(struct gf5 *g)
{
    if (!tile_io_valid(g)) return tile_result(g,GF5_EINVAL);
    if (g->accepting || g->ch->state!=GF2_OFF || g->image)
        return tile_result(g,GF5_EBUSY);
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        struct gf5_tile_slot *s=&g->tile_slot[i];
        if (s->state==GF_TILE_FREE) continue;
        for (u32 j=0;j<2;++j) if (s->loan[j].key.reservation_id) {
            i32 rc=gf_vram_borrow_overlaps(g->ch,s->loan[j].off,
                                          s->loan[j].bytes,GF_BORROW_VIEW);
            if (rc) return tile_result(g,rc>0 ? GF5_EBUSY : rc);
        }
        g->poll_source="tile-retire";
        i32 rc=tile_program(g,i,0,0,0);
        if (!rc) rc=tile_free(g,i);
        if (rc) return tile_fault(g,i,rc);
    }
    g->poll_source=0;
    return tile_result(g,0);
}

i32 gf5_tile_find(struct gf5 *g,u32 *slot)
{
    if (!g || !slot) return tile_result(g,GF5_EINVAL);
    for (u32 i=0;i<GF5_TILE_COUNT;++i)
        if (g->tile_slot[i].state==GF_TILE_FREE) { *slot=i; return tile_result(g,0); }
    return tile_result(g,GF5_EBUSY);
}

i32 gf5_tile_set(struct gf5 *g,u32 slot,const struct gf5_tile *tile)
{
    struct gf_vram_reservation key;
    if (!g || !tile || slot>=GF5_TILE_COUNT || !g->ch || !g->ch->f ||
        !g->ch->f->io.read || !g->ch->f->io.write || !g->links.drain_pause ||
        !g->links.unpause || !g->links.admission || !tile->bytes || !tile->pitch ||
        (tile->base&(TILE_ALIGN-1U))) return tile_result(g,GF5_EINVAL);
    if (g->tile_slot[slot].state==GF_TILE_UNKNOWN ||
        (g->phase!=GF5_RUN && g->phase!=GF5_RESTORING))
        return tile_result(g,GF5_ESTATE);
    if (!tile_pitch_valid(tile->pitch)) return tile_result(g,GF5_EINVAL);
    u64 row=(u64)tile->pitch*TILE_ROW_FACTOR;
    u64 rem=tile_remainder(tile->bytes,row);
    u64 bytes=(u64)tile->bytes+(rem ? row-rem : 0);
    if (bytes>TILE_MAX || (u64)tile->base+bytes>TILE_MAX+1ULL ||
        (u64)tile->base+bytes>g->ch->f->imem.vram_bytes)
        return tile_result(g,GF5_EINVAL);
    i32 rc=gf_vram_match(g->ch,0,tile->reservation_id,tile->generation,
                         tile->base,(u32)bytes,&key);
    if (rc==GF3_ERANGE) { g->source_rc=rc; return g->last_rc=GF5_EINVAL; }
    if (rc) return tile_result(g,rc);
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        if (i==slot || g->tile_slot[i].state==GF_TILE_FREE) continue;
        for (u32 j=0;j<2;++j) {
            const struct gf_vram_borrow *loan=&g->tile_slot[i].loan[j];
            if (loan->key.reservation_id && (u64)tile->base<(u64)loan->off+loan->bytes &&
                (u64)loan->off<(u64)tile->base+bytes)
                return tile_result(g,GF5_EBUSY);
        }
    }
    rc=gf_vram_borrow_overlaps(g->ch,tile->base,(u32)bytes,GF_BORROW_VIEW);
    if (rc) return tile_result(g,rc>0 ? GF5_EBUSY : rc);
    struct gf5_tile_slot *s=&g->tile_slot[slot];
    if (s->state==GF_TILE_OWNED) {
        rc=gf_vram_borrow_overlaps(g->ch,s->loan[s->current].off,
                                  s->loan[s->current].bytes,GF_BORROW_VIEW);
        if (rc) return tile_result(g,rc>0 ? GF5_EBUSY : rc);
    }
    rc=gf_vram_borrow_get(g->ch,&s->loan[s->current^1U],&key,
                          tile->base,(u32)bytes,GF_BORROW_TILE);
    if (rc) return tile_result(g,rc);
    return tile_apply(g,slot,tile,(u32)bytes);
}

i32 gf5_tile_clear(struct gf5 *g,u32 slot)
{
    if (!g || slot>=GF5_TILE_COUNT || !g->ch || !g->ch->f ||
        !g->ch->f->io.read || !g->ch->f->io.write || !g->links.drain_pause ||
        !g->links.unpause || !g->links.admission) return tile_result(g,GF5_EINVAL);
    struct gf5_tile_slot *s=&g->tile_slot[slot];
    if (s->state==GF_TILE_UNKNOWN ||
        (g->phase!=GF5_RUN && g->phase!=GF5_RESTORING))
        return tile_result(g,GF5_ESTATE);
    if (s->state==GF_TILE_FREE) return tile_result(g,0);
    i32 rc=gf_vram_borrow_overlaps(g->ch,s->loan[s->current].off,
                                  s->loan[s->current].bytes,GF_BORROW_VIEW);
    if (rc) return tile_result(g,rc>0 ? GF5_EBUSY : rc);
    return tile_apply(g,slot,0,0);
}
