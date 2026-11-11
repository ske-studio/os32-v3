/* HAL v2 surface table and lifetime (HAL_V2_DESIGN §§7, 11-2, 12-1). */
#include "gfx2_int.h"
#include "kstring.h"

#define SURF_SLOT_BITS 10
#define SURF_GEN_MASK ((1UL << (32 - SURF_SLOT_BITS)) - 1)
#define SURF_USAGE (GFX2_USE_RENDER2D | GFX2_USE_SCANOUT | GFX2_USE_CURSOR | \
                    GFX2_USE_STAGING | GFX2_USE_TEXTURE)

struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];

static void surf_generation(struct gfx2_surf *s, u32 gen)
{
    s->gen = gen & SURF_GEN_MASK;
    /* Zero is the absent source/head sentinel, never a live sid. */
    if (!s->gen) s->gen = 1;
    s->sid = (s->gen << SURF_SLOT_BITS) | (u32)(s - gfx2_surfs);
}

static struct gfx2_surf *surf_slot(void)
{
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++)
        if (gfx2_surfs[i].state == GFX2_ST_FREE) return &gfx2_surfs[i];
    return NULL;
}

static void surf_clear(struct gfx2_surf *s)
{
    u32 gen = s->gen + 1;
    kmemset(s, 0, sizeof(*s));
    surf_generation(s, gen);
}

static int surf_done(struct gfx2_surf *s)
{
    struct gfx2_adapter *a = gfx2_adapter_get(s->adapter);
    return a && gfx2_fence_done(a, s->last_write) && gfx2_fence_done(a, s->last_read);
}

struct gfx2_surf *gfx2_surf_get(u32 sid, u32 owner, int *rc)
{
    struct gfx2_surf *s = &gfx2_surfs[sid & (GFX2_MAX_SURFACES - 1)];
    struct gfx2_caller c;
    *rc = OS32_ERR_STALE;
    if (!sid || s->state == GFX2_ST_FREE || s->sid != sid) return NULL;
    *rc = OS32_ERR_INVAL;
    if (s->owner == GFX2_OWNER_SCREEN) {
        if (gfx2_caller_get(&c) || c.owner != owner || c.app_id != gfx_screen_owner()) return NULL;
    } else if (s->owner != owner) return NULL;
    *rc = 0;
    return s;
}

static int surf_backing_free(struct gfx2_adapter *a, struct gfx2_surf *s)
{
    if (s->pool == GFX2_POOL_VRAM) return gfx2_heap_free(&a->heap, s->heap_off);
    if (s->pool == GFX2_POOL_GART) return gfx2_gart_free(a, s);
    return gfx2_sysmem_free(s);
}

int gfx2_surf_new(struct gfx2_adapter *a, u16 w, u16 h, u8 fmt, u8 pool,
                  u8 usage, u32 owner, struct gfx2_surf **out)
{
    struct gfx2_surf *s;
    struct gfx2_caller c;
    struct gfx2_surf prior;
    u32 cpp, row, pitch, bytes, align = a->caps.pitch_align;
    int rc;
    if (!w || !h || w > a->caps.max_w || h > a->caps.max_h ||
        fmt < GFX2_FMT_INDEX8 || fmt > GFX2_FMT_UYVY ||
        !(a->caps.fmt_surface & (1UL << fmt)) ||
        pool < GFX2_POOL_VRAM || pool > GFX2_POOL_SYSMEM || (usage & ~SURF_USAGE) ||
        !align || (align & (align - 1))) return OS32_ERR_INVAL;
    cpp = (fmt == GFX2_FMT_XRGB8888 || fmt == GFX2_FMT_ARGB8888) ? 4 :
          (fmt == GFX2_FMT_INDEX8 || fmt == GFX2_FMT_A8 || fmt == GFX2_FMT_L8) ? 1 : 2;
    row = (u32)w * cpp;
    if (row > (~0UL - (align - 1))) return OS32_ERR_INVAL;
    pitch = (row + align - 1) & ~(align - 1);
    if (pitch > a->caps.max_pitch || pitch > ~0UL / h) return OS32_ERR_INVAL;
    bytes = pitch * h;
    if (pool == GFX2_POOL_GART && (gfx2_caller_get(&c) || c.owner != owner || !c.as))
        return OS32_ERR_INVAL;
    s = surf_slot();
    if (!s) return OS32_ERR_NOSPC;
    prior = *s;
    /* Preserve the free slot's generation until creation commits. */
    s->adapter = a->index; s->owner = owner;
    s->w = w; s->h = h; s->fmt = fmt; s->pool = pool; s->usage = usage;
    s->pitch = pitch; s->bytes = bytes;
    if (pool == GFX2_POOL_VRAM) {
        u32 offset_align = (usage & GFX2_USE_SCANOUT) ? a->caps.scanout_align : a->caps.offset_align;
        rc = gfx2_heap_alloc(&a->heap, bytes, offset_align, &s->heap_off);
        s->gpu_off = s->heap_off;
    } else if (pool == GFX2_POOL_GART) rc = gfx2_gart_alloc(a, s, owner);
    else rc = gfx2_sysmem_alloc(s, owner);
    if (rc) goto empty;
    if (pool != GFX2_POOL_SYSMEM) {
        rc = a->be->alloc(a->ctx, s);
        if (rc) goto backing;
    }
    if (pool == GFX2_POOL_VRAM) {
        rc = a->be->kva(a->ctx, s, &s->kva);
        if (rc == OS32_ERR_NOSYS) s->kva = NULL;
        else if (rc) goto release;
    }
    if (pool == GFX2_POOL_GART) {
        u32 va;
        rc = gfx2_map(s, c.as, &va);
        if (rc) goto release;
    }
    surf_generation(s, s->gen ? s->gen : 1);
    s->state = GFX2_ST_IDLE;
    *out = s;
    return 0;
release:
    if (pool != GFX2_POOL_SYSMEM && a->be->release(a->ctx, s)) goto keep;
backing:
    if (surf_backing_free(a, s)) goto keep; /* creation rollback */
empty:
    *s = prior;
    return OS32_ERR_NOSPC;
keep:
    /* Failed cleanup follows the same KEEP/owner-release path as destroy. */
    surf_generation(s, s->gen ? s->gen : 1);
    s->state = GFX2_ST_IDLE;
    s->flags |= GFX2_SF_KEEP;
    return OS32_ERR_NOSPC;
}

static void surf_alias_shape(struct gfx2_surf *s, const struct gfx2_alias_desc *d)
{
    s->w = d ? d->w : 0; s->h = d ? d->h : 0;
    s->pitch = d ? d->pitch : 0; s->bytes = d ? d->bytes : 0;
    s->gpu_off = d ? d->gpu_off : 0; s->kva = d ? d->kva : NULL;
    if (d) {
        s->adapter = d->adapter; s->fmt = d->fmt; s->pool = d->pool;
        s->head = d->head; s->role = d->role;
    }
    s->last_read = s->last_write = 0;
    s->dirty = (struct gfx2_rect){0};
}

int gfx2_surf_alias(struct gfx2_adapter *a, const struct gfx2_alias_desc *d,
                    struct gfx2_surf **out)
{
    struct gfx2_surf *s;
    if (!d || d->adapter != a->index || d->head >= a->heads ||
        (d->role != GFX2_ROLE_DISPLAY && d->role != GFX2_ROLE_CLIENT) ||
        (d->pool != GFX2_POOL_VRAM && d->pool != GFX2_POOL_SYSMEM)) return OS32_ERR_INVAL;
    s = surf_slot();
    if (!s) return OS32_ERR_FULL;
    s->owner = GFX2_OWNER_SCREEN;
    s->usage = GFX2_USE_SCANOUT | GFX2_USE_V1;
    s->flags = GFX2_SF_ALIAS;
    surf_alias_shape(s, d);
    surf_generation(s, s->gen ? s->gen : 1);
    s->state = GFX2_ST_IDLE;
    *out = s;
    return 0;
}

int gfx2_surf_alias_update(struct gfx2_surf *s, const struct gfx2_alias_desc *d)
{
    u32 old_sid = s->sid;
    if (!(s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;
    if ((s->state != GFX2_ST_IDLE && s->state != GFX2_ST_LOST) || !surf_done(s) ||
        s->map_as) return OS32_ERR_BUSY;
    surf_generation(s, s->gen + 1);
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++)
        for (u32 h = 0; h < 2; h++)
            if (gfx2_adapters[i].head[h].scan_sid == old_sid)
                gfx2_adapters[i].head[h].scan_sid = s->sid;
    surf_alias_shape(s, d);
    s->state = GFX2_ST_IDLE;
    return 0;
}

int gfx2_surf_destroy(struct gfx2_surf *s)
{
    struct gfx2_adapter *a = gfx2_adapter_get(s->adapter);
    if (s->flags & GFX2_SF_ALIAS) return OS32_ERR_INVAL; /* borrowed backing */
    if (s->flags & GFX2_SF_KEEP) return OS32_ERR_BUSY;
    if (s->scan_ref || !surf_done(s) ||
        ((s->pool == GFX2_POOL_VRAM || s->pool == GFX2_POOL_GART) &&
         a->state == GFX2_AD_LOST_HELD)) return OS32_ERR_BUSY;
    if (gfx2_unmap(s)) goto keep;
    if (s->pool != GFX2_POOL_SYSMEM && a->be->release(a->ctx, s)) goto keep;
    if (surf_backing_free(a, s)) goto keep;
    surf_clear(s);
    return 0;
keep:
    s->flags |= GFX2_SF_KEEP;
    /* C7 leaves map_* unchanged on IO; C15 must still find and poison the AS. */
    return OS32_ERR_BUSY;
}

int gfx2_surf_state_set(struct gfx2_surf *s, u32 st)
{
    u32 old = s->state;
    int ok = (old == GFX2_ST_LOST && st == GFX2_ST_CLOSING) ||
        (old == GFX2_ST_IDLE && (st == GFX2_ST_LOCKED_RO || st == GFX2_ST_LOCKED_RW || st == GFX2_ST_CLOSING)) ||
        ((old == GFX2_ST_LOCKED_RO || old == GFX2_ST_LOCKED_RW) && st == GFX2_ST_IDLE) ||
        (old >= GFX2_ST_IDLE && old <= GFX2_ST_CLOSING && st == GFX2_ST_LOST) ||
        (old >= GFX2_ST_IDLE && old <= GFX2_ST_LOST && st == GFX2_ST_QUAR);
    if (!ok) return OS32_ERR_INVAL;
    s->state = st;
    return 0;
}

void gfx2_scan_ref_get(struct gfx2_surf *s) { s->scan_ref++; }
void gfx2_scan_ref_put(struct gfx2_surf *s) { if (s->scan_ref) s->scan_ref--; }

void gfx2_scan_ref_swap(struct gfx2_adapter *a, u32 head, struct gfx2_surf *s)
{
    u32 old = a->head[head].scan_sid;
    if (s) gfx2_scan_ref_get(s);
    if (old) {
        struct gfx2_surf *prev = &gfx2_surfs[old & (GFX2_MAX_SURFACES - 1)];
        if (prev->sid == old && prev->state != GFX2_ST_FREE) gfx2_scan_ref_put(prev);
    }
    a->head[head].scan_sid = s ? s->sid : 0;
}

void gfx2_dirty_add(struct gfx2_surf *s, const struct gfx2_rect *r)
{
    /* Clip in 64 bits so arbitrary signed input cannot overflow. */
    long long x = r->x, y = r->y, right = x + r->w, bottom = y + r->h;
    if (r->w <= 0 || r->h <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > s->w) right = s->w;
    if (bottom > s->h) bottom = s->h;
    if (x >= right || y >= bottom) return;
    if (s->dirty.w && s->dirty.h) {
        if (s->dirty.x < x) x = s->dirty.x;
        if (s->dirty.y < y) y = s->dirty.y;
        if (s->dirty.x + s->dirty.w > right) right = s->dirty.x + s->dirty.w;
        if (s->dirty.y + s->dirty.h > bottom) bottom = s->dirty.y + s->dirty.h;
    }
    s->dirty = (struct gfx2_rect){(i32)x, (i32)y, (i32)(right-x), (i32)(bottom-y)};
}

void gfx2_closing_sweep(struct gfx2_adapter *a)
{
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->adapter == a->index && s->state == GFX2_ST_CLOSING &&
            !s->scan_ref && surf_done(s)) (void)gfx2_surf_destroy(s);
    }
    gfx2_quarantine_drain(a);
}

int gfx2_surface_create(u32 adapter, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 *sid)
{
    struct gfx2_caller c;
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    struct gfx2_surf *s;
    int rc = gfx2_caller_get(&c);
    if (rc) return rc;
    if (!a || !sid) return OS32_ERR_INVAL;
    if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;
    rc = gfx2_surf_new(a, w, h, fmt, pool, usage, c.owner, &s);
    if (!rc) *sid = s->sid;
    return rc;
}

int gfx2_surface_free(u32 sid)
{
    struct gfx2_caller c;
    struct gfx2_surf *s;
    int rc = gfx2_caller_get(&c);
    if (rc) return rc;
    s = gfx2_surf_get(sid, c.owner, &rc);
    if (!s) return rc;
    if (s->flags & GFX2_SF_ALIAS) return OS32_ERR_INVAL;
    if (s->state == GFX2_ST_LOCKED_RO || s->state == GFX2_ST_LOCKED_RW) return OS32_ERR_BUSY;
    if (s->state == GFX2_ST_CLOSING || s->state == GFX2_ST_QUAR) return OS32_ERR_INVAL;
    if (s->scan_ref || !surf_done(s)) {
        (void)gfx2_surf_state_set(s, GFX2_ST_CLOSING);
        return 0;
    }
    return gfx2_surf_destroy(s);
}

int gfx2_surface_desc(u32 sid, GFX2_SurfaceDesc *out)
{
    struct gfx2_caller c;
    struct gfx2_surf *s;
    int rc = gfx2_caller_get(&c);
    if (rc) return rc;
    if (!out) return OS32_ERR_INVAL;
    s = gfx2_surf_get(sid, c.owner, &rc);
    if (!s) return rc;
    *out = (GFX2_SurfaceDesc){s->sid, s->gen, s->w, s->h, s->fmt, s->pool,
        s->usage, s->tiling, s->pitch, s->bytes, s->gpu_off, s->state};
    return 0;
}

int gfx2_screen_surface(u32 adapter, u32 head, u32 role, u32 *sid)
{
    struct gfx2_caller c;
    struct gfx2_adapter *a = gfx2_adapter_get(adapter);
    int rc = gfx2_caller_get(&c);
    if (rc) return rc;
    if (!a || !sid || head >= a->heads ||
        (role != GFX2_ROLE_DISPLAY && role != GFX2_ROLE_CLIENT)) return OS32_ERR_INVAL;
    if (!(a->flags & GFX2_AF_ACTIVE)) return OS32_ERR_NOSYS;
    if (c.app_id != gfx_screen_owner()) return OS32_ERR_BUSY;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        if ((s->flags & GFX2_SF_ALIAS) && s->adapter == adapter && s->head == head && s->role == role) {
            *sid = s->sid;
            return 0;
        }
    }
    return OS32_ERR_INVAL;
}
