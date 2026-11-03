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
 * Authors: Ben Skeggs
 * NV44A adaptation: GF_FEAT2_DESIGN section 5. References below are relative
 * to nouveau/drivers/gpu/drm/nouveau at c03c1a14507dfaf8b856c2608f9ac216021fc222.
 * Bare filenames below are in nvkm/engine/gr/.
 */
#include "command.h"

/* ctxnv40.c:671-689; gr/regs.h; nv44.c:31-46. */
#define GR_PROGRAM_INDEX 0x400324U
#define GR_PROGRAM_DATA 0x400328U
#define GR_CURRENT 0x40032cU
#define GR_UNITS 0x1540U
#define GR_UNIT_SELECT 0x405000U
#define GR_FIFO 0x400720U
#define FB_TILE 0x100600U
#define GR_TILE 0x400900U
#define TILE_STRIDE 0x10U
#define TILE_LIMIT 4U
#define TILE_PITCH 8U
#define TILE_VALID 1U

static int rd(struct gf_foundation *f, u32 off, u32 *v)
{ return f->io.read(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(*v), v); }
static int wr(struct gf_foundation *f, u32 off, u32 v)
{ return f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(v), v); }

/* F1 section 4: compiler barrier and UC aperture readback publish the object.
 * write32 also drains each individual write; this is the final handoff. */
static int publish(struct gf_foundation *f, const struct gf_inst *inst)
{
    u32 value;
    __asm__ volatile("" ::: "memory");
    return gf_inst_read32(f, inst, 0, &value) ? GF2_EIO : GF2_OK;
}

static int copy_ram_config(struct gf2_chan *ch)
{
    /* nv40.c:388-423: resource length, independently of detected VRAM size. */
    static const u32 dst[] = {0x4009f0U, 0x4009f4U, 0x4069f0U, 0x4069f4U};
    static const u32 src[] = {0x100200U, 0x100204U, 0x100200U, 0x100204U};
    struct gf_foundation *f = ch->f;
    u32 value;
    for (unsigned i = 0; i < sizeof(dst) / sizeof(dst[0]); i++)
        if (rd(f, src[i], &value) || wr(f, dst[i], value)) return GF2_EIO;
    if (wr(f, 0x400840U, 0) || wr(f, 0x400844U, 0) ||
        wr(f, 0x4008a0U, ch->bar1.length - 1) ||
        wr(f, 0x4008a4U, ch->bar1.length - 1)) return GF2_EIO;
    return GF2_OK;
}

static int copy_tiles(struct gf2_chan *ch)
{
    struct { u32 addr, limit, pitch; } tiles[GF2_GR_TILE_COUNT];
    struct gf_foundation *f = ch->f;
    /* Snapshot while no work is submitted; fb/nv44.c:30-45 encodes bit 0
     * as the VRAM tile enable. Reject overlap with the linear push range. */
    for (unsigned i = 0; i < GF2_GR_TILE_COUNT; i++) {
        u32 off = FB_TILE + i * TILE_STRIDE;
        if (rd(f, off, &tiles[i].addr) ||
            rd(f, off + TILE_LIMIT, &tiles[i].limit) ||
            rd(f, off + TILE_PITCH, &tiles[i].pitch)) return GF2_EIO;
        if ((tiles[i].addr & TILE_VALID) &&
            (tiles[i].addr & ~TILE_VALID) < ch->B + GF2_PUSH_BYTES &&
            tiles[i].limit >= ch->B) return GF2_ESTATE;
    }
    /* nv44.c:31-46: pitch, inclusive limit, then address. Do not alter PFB. */
    for (unsigned i = 0; i < GF2_GR_TILE_COUNT; i++) {
        u32 off = GR_TILE + i * TILE_STRIDE;
        if (wr(f, off + TILE_PITCH, tiles[i].pitch) ||
            wr(f, off + TILE_LIMIT, tiles[i].limit) ||
            wr(f, off, tiles[i].addr)) return GF2_EIO;
    }
    return GF2_OK;
}

int gf2_gr_init(struct gf2_chan *ch)
{
    /* nv40.c:305-313,315-327,346-365,367-373: NV4A branch only. */
    static const struct { u32 off, value; } debug[] = {
        {0x400080U, 0xffffffffU}, {0x400080U, 0},
        {0x400084U, 0x401287c0U}, {0x40008cU, 0xe0de8055U},
        {0x400090U, 0x00008000U}, {0x400610U, 0x00be3c5fU},
        {0x400144U, 0x10010100U}, {0x400714U, 0xffffffffU}
    }, config[] = {
        {0x400820U, 0x83280effU}, {0x400824U, 0x000000a0U},
        {0x400860U, 0}, {0x400864U, 0},
        {0x400b38U, 0x2ffff800U}, {0x400b3cU, 0x00006000U},
        {0x400bc4U, 0x1003d888U}, {0x400bbcU, 0xb7a7b500U}
    };
    struct gf_foundation *f = ch->f;
    u32 code[GF2_GRCTX_PROGRAM_WORDS], words, bytes, units, unit = 0;
    int rc = gf2_grctx_program(code, &words, &bytes);
    if (rc || words > GF2_GRCTX_PROGRAM_WORDS) return GF2_EIO;
    if (wr(f, GR_PROGRAM_INDEX, 0)) return GF2_EIO;
    for (u32 i = 0; i < words; i++)
        if (wr(f, GR_PROGRAM_DATA, code[i])) return GF2_EIO;
    if (wr(f, GR_CURRENT, 0)) return GF2_EIO;
    /* nv40.c:299-303: ACK/enable belong to F1, with delivery opened by C0
     * only after initialization and binding (design sections 2,5). */
    for (unsigned i = 0; i < sizeof(debug) / sizeof(debug[0]); i++)
        if (wr(f, debug[i].off, debug[i].value)) return GF2_EIO;
    if (rd(f, GR_UNITS, &units)) return GF2_EIO;
    units &= 0xffU;
    if (units) {
        while (!(units & 1U)) { unit++; units >>= 1; }
        if (wr(f, GR_UNIT_SELECT, unit)) return GF2_EIO;
    }
    for (unsigned i = 0; i < sizeof(config) / sizeof(config[0]); i++)
        if (wr(f, config[i].off, config[i].value)) return GF2_EIO;
    rc = copy_ram_config(ch);
    if (!rc) rc = copy_tiles(ch);
    if (rc) return rc;
    /* nv40.c:74-86: allocate a stable caller-owned handle, fill, self pointer,
     * publish, then FIFO's RAMFC binding (fifo/nv40.c:125-164). */
    rc = gf_inst_alloc(f, bytes, GF2_OBJECT_ALIGN, true, &ch->grctx);
    if (rc) return rc == GF1_ENOSPC ? GF2_ENOSPC : GF2_EIO;
    rc = gf2_grctx_fill(f, &ch->grctx);
    if (rc) { rc = GF2_EIO; goto free_ctx; }
    if (gf_inst_write32(f, &ch->grctx, 0, ch->grctx.off >> 4) ||
        publish(f, &ch->grctx)) { rc = GF2_EIO; goto free_ctx; }
    rc = gf2_fifo_bind_gr(ch, ch->grctx.off);
    if (rc) goto free_ctx;
    /* nv40.c:268; xf86-video-nv/src/nv_hw.c:1173. Init only, not repair. */
    if (wr(f, GR_FIFO, 1)) { rc = GF2_EIO; goto free_ctx; }
    return GF2_OK;
free_ctx:
    gf_inst_free(f, &ch->grctx);
    ch->grctx = (struct gf_inst){0};
    return rc;
}

int gf2_gr_object_new(struct gf2_chan *ch, u32 handle, u16 class_id,
                      struct gf2_object *obj)
{
    /* nv44.c:83-100: keep the complete class list, including later 3D use. */
    static const u16 classes[] = {
        GF2_GR_BETA1, GF2_GR_CLIP, GF2_GR_NULL, GF2_GR_M2MF,
        GF2_GR_ROP, GF2_GR_PATTERN, GF2_GR_GDI, GF2_GR_SURFACE2D,
        GF2_GR_BETA4, GF2_GR_SIFM, GF2_GR_IFC, GF2_GR_IMAGEBLIT,
        GF2_GR_SURFACE2D_NV30, GF2_GR_SIFM_NV30, GF2_GR_SWZSURF, GF2_GR_CURIE
    };
    unsigned i;
    int rc;
    for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++)
        if (classes[i] == class_id) break;
    if (i == sizeof(classes) / sizeof(classes[0])) return GF2_ECLASS;
    /* nv40.c:44-59: 20 bytes, 16-byte aligned, class then four zero words. */
    rc = gf_inst_alloc(ch->f, GF2_GR_OBJECT_BYTES, GF2_OBJECT_ALIGN, false, &obj->inst);
    if (rc) return rc == GF1_ENOSPC ? GF2_ENOSPC : GF2_EIO;
    for (u32 off = 0; off < GF2_GR_OBJECT_BYTES; off += sizeof(u32))
        if (gf_inst_write32(ch->f, &obj->inst, off, off ? 0 : class_id)) {
            rc = GF2_EIO; goto free_obj;
        }
    rc = publish(ch->f, &obj->inst);
    if (rc) goto free_obj;
    rc = gf2_ramht_add(ch, handle, GF2_RAMHT_ENGINE_GR, obj->inst.off, &obj->cookie);
    if (rc) goto free_obj;
    obj->handle = handle;
    obj->class_id = class_id;
    return GF2_OK;
free_obj:
    gf_inst_free(ch->f, &obj->inst);
    obj->inst = (struct gf_inst){0};
    return rc;
}
