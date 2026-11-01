#include "foundation.h"

/* nv40_instmem_oneinit, nvkm/subdev/instmem/nv40.c:158-206. */
enum { VRAM_SIZE_REG = 0x10020c, VS_REG = 0x1540, PAGE_BYTES = 4096,
       DISPLAY_BYTES = 307200, FIXED_END = 0x40000,
       VS_CONTEXT_BYTES = 0x4980, BASE_CONTEXT_BYTES = 0x4000,
       CONTEXT_COUNT = 32, RESERVE_BASE_BYTES = 0x100000,
       VBIOS_BYTES = 0x10000, RAMHT_BYTES = 0x8000,
       RAMRO_BYTES = 0x8000, RAMFC_BYTES = 0x20000 };

static int allocate(struct gf_foundation *f, u32 start, u32 bytes, u32 align,
                    bool zero, struct gf_inst *out)
{
    struct gf_inst **link = &f->imem.used;
    u32 pos = start;
    if (!f->mapped) return GF1_EMAP;
    if (!bytes || (align && (align & (align - 1))) || bytes > 0xfffffffcU)
        return GF1_EINVAL;
    bytes = (bytes + 3) & ~3U;
    if (align < 4) align = 4;
    while (1) {
        if (pos > 0xffffffffU - (align - 1)) return GF1_ENOSPC;
        pos = (pos + align - 1) & ~(align - 1);
        if (pos > f->imem.aperture_bytes || bytes > f->imem.aperture_bytes - pos)
            return GF1_ENOSPC;
        if (!*link || (pos <= (*link)->off && bytes <= (*link)->off - pos)) break;
        if (pos < (*link)->off + (*link)->size) pos = (*link)->off + (*link)->size;
        link = &(*link)->next;
    }
    if (zero) {
        volatile u32 *p = (volatile u32 *)(f->bar2 + pos);
        for (u32 i = 0; i < bytes / 4; i++) p[i] = 0;
        __asm__ volatile("" ::: "memory");
        (void)p[0];
    }
    out->off = pos; out->size = bytes; out->next = *link; *link = out;
    return GF1_OK;
}

int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out)
{
    return allocate(f, FIXED_END, bytes, align, zero, out);
}

void gf_inst_free(struct gf_foundation *f, struct gf_inst *obj)
{
    struct gf_inst **link = &f->imem.used;
    if (obj == &f->imem.vbios || obj == &f->imem.ramht ||
        obj == &f->imem.ramro || obj == &f->imem.ramfc) return;
    while (*link && *link != obj) link = &(*link)->next;
    if (*link) { *link = obj->next; obj->next = 0; }
}

/* nv40_instobj_rd32/wr32, nvkm/subdev/instmem/nv40.c:48-58. */
static int access_check(struct gf_foundation *f, const struct gf_inst *obj, u32 off)
{
    if (!f->mapped) return GF1_EMAP;
    if ((off & 3) || off > obj->size || obj->size - off < 4 ||
        obj->off > f->imem.aperture_bytes ||
        obj->size > f->imem.aperture_bytes - obj->off) return GF1_EINVAL;
    return GF1_OK;
}
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *obj, u32 off, u32 *v)
{
    int rc = access_check(f, obj, off);
    if (rc) return rc;
    *v = *(volatile u32 *)(f->bar2 + obj->off + off);
    return GF1_OK;
}
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj, u32 off, u32 v)
{
    int rc = access_check(f, obj, off);
    if (rc) return rc;
    volatile u32 *p = (volatile u32 *)(f->bar2 + obj->off + off);
    *p = v;
    __asm__ volatile("" ::: "memory");
    (void)*p;
    return GF1_OK;
}

int gf_instmem_init(struct gf_foundation *f)
{
    u32 v, mask, vs = 0, r;
    int rc;
    if (f->io.read(f->io.opaque, 0, VRAM_SIZE_REG, 4, &v) ||
        f->io.read(f->io.opaque, 0, VS_REG, 4, &mask)) return GF1_EIO;
    v &= 0xff000000U;
    mask = (mask >> 8) & 0xff;
    while (mask) { vs += mask & 1; mask >>= 1; }
    r = ((VS_CONTEXT_BYTES * vs + BASE_CONTEXT_BYTES) * CONTEXT_COUNT + RESERVE_BASE_BYTES + PAGE_BYTES - 1) & ~(PAGE_BYTES - 1);
    if (v < r + DISPLAY_BYTES) return GF1_ENOSPC;
    f->imem.vram_bytes = v; f->imem.reserved = r;
    rc = gf_instmem_map(f, &f->instmem_bar, r);
    if (rc) return rc;
    f->imem.used = 0;
    rc = allocate(f, 0, VBIOS_BYTES, 1, false, &f->imem.vbios);
    if (!rc) rc = allocate(f, 0, RAMHT_BYTES, 1, true, &f->imem.ramht);
    if (!rc) rc = allocate(f, 0, RAMRO_BYTES, 1, false, &f->imem.ramro);
    if (!rc) rc = allocate(f, 0, RAMFC_BYTES, 1, true, &f->imem.ramfc);
    return rc;
}
