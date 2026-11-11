/* HAL v2 C7: CPU mappings and DIRECT alias staging (§7-4, §7-5). */
#include "gfx2_int.h"
#include "paging.h"
#include "kstring.h"
#include "os32_kapi_shared.h"

#define GFX2_MAP_PAGES ((MEM_GFX2_MAP_END - MEM_GFX2_MAP_BASE) / PAGE_SIZE)
#define GFX2_LOCK_DL_US 1000000UL
static struct gfx2_vaspace gfx2_vaspace[GFX2_MAX_ADAPTERS];

static struct gfx2_vaspace *vaspace(struct addrspace *as, int create)
{
    struct gfx2_vaspace *empty = 0;
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        if (gfx2_vaspace[i].as_gen == as->generation) return &gfx2_vaspace[i];
        if (!gfx2_vaspace[i].as_gen) empty = &gfx2_vaspace[i];
    }
    return create ? empty : 0;
}

static void mark(struct gfx2_vaspace *v, u32 first, u32 n, int used)
{
    for (u32 k = first; k < first + n; k++) {
        if (used) v->bits[k / 32] |= 1UL << (k % 32);
        else v->bits[k / 32] &= ~(1UL << (k % 32));
    }
}

void gfx2_vaspace_drop(struct addrspace *as)
{
    struct gfx2_vaspace *v = vaspace(as, 0);
    if (v) kmemset(v, 0, sizeof(*v));
}

int gfx2_surf_page(const struct gfx2_surf *s, u32 k, u32 *pa, u32 *attr)
{
    const u32 *pfns;
    u32 n, phys, cache = 0;
    struct gfx2_adapter *a;
    if (k >= s->bytes / PAGE_SIZE + !!(s->bytes % PAGE_SIZE)) return OS32_ERR_INVAL;
    if (s->staging) {
        if (k >= s->staging_n) return OS32_ERR_INVAL;
        phys = (s->staging_pfn + k) * PAGE_SIZE;
    } else if (s->pool == GFX2_POOL_SYSMEM) {
        if (k >= s->sys_n) return OS32_ERR_INVAL;
        phys = (s->sys_pfn + k) * PAGE_SIZE;
    } else if (s->pool == GFX2_POOL_GART) {
        if (gfx2_gart_pfns(s, &pfns, &n) || k >= n) return OS32_ERR_INVAL;
        phys = pfns[k] * PAGE_SIZE;
    } else if (s->pool == GFX2_POOL_VRAM) {
        a = gfx2_adapter_get(s->adapter);
        if (!a || s->gpu_off > ~0U - a->vram_phys ||
            k > (~0U - a->vram_phys - s->gpu_off) / PAGE_SIZE) return OS32_ERR_INVAL;
        phys = a->vram_phys + s->gpu_off + k * PAGE_SIZE;
        /* Match the supervisor alias, including effective WC via MTRR. */
        cache = paging_pte_flags((u32)(uptr)P2V_IO(phys)) & (PTE_PCD | PTE_PWT);
    } else return OS32_ERR_INVAL;
    if (phys & (PAGE_SIZE - 1)) return OS32_ERR_INVAL;
    *pa = phys; *attr = cache;
    return 0;
}

int gfx2_cpu_visible(const struct gfx2_surf *s)
{
    return s->kva || (s->map_as && paging_current_cr3() == s->map_as->pd_phys);
}

void gfx2_map_detach(struct gfx2_surf *s)
{
    s->map_as = 0; s->map_va = s->map_npages = 0;
}

int gfx2_map(struct gfx2_surf *s, struct addrspace *as, u32 *va)
{
    u32 n = s->bytes / PAGE_SIZE + !!(s->bytes % PAGE_SIZE);
    u32 first = 0, run = 0, mapped = 0, base, pa, attr;
    struct gfx2_vaspace *v;
    int rc;
    if (!as || !as->generation || !va || !n) return OS32_ERR_INVAL;
    if (s->map_as) {
        if (s->map_as != as) return OS32_ERR_INVAL;
        *va = s->map_va; return 0;
    }
    v = vaspace(as, 1);
    if (!v || n > GFX2_MAP_PAGES) return OS32_ERR_NOSPC;
    for (u32 k = 0; k < GFX2_MAP_PAGES; k++) {
        if (v->bits[k / 32] & (1UL << (k % 32))) run = 0;
        else if (++run == n) { first = k + 1 - n; break; }
    }
    if (run != n) return OS32_ERR_NOSPC;
    base = MEM_GFX2_MAP_BASE + first * PAGE_SIZE;
    while (mapped < n) {
        u32 count = 1, next_pa, next_attr;
        rc = gfx2_surf_page(s, mapped, &pa, &attr);
        if (rc) goto rollback;
        while (mapped + count < n &&
               !gfx2_surf_page(s, mapped + count, &next_pa, &next_attr) &&
               count <= (~0U - pa) / PAGE_SIZE &&
               next_pa == pa + count * PAGE_SIZE && next_attr == attr) count++;
        rc = paging_gfx2_map(as, base + mapped * PAGE_SIZE, pa, count,
                            PAGE_RW | PTE_USER | attr);
        if (rc) { rc = rc == -2 ? OS32_ERR_NOSPC : OS32_ERR_INVAL; goto rollback; }
        mapped += count;
    }
    v->as_gen = as->generation;
    mark(v, first, n, 1);
    s->map_as = as; s->map_va = base; s->map_npages = n;
    *va = base;
    return 0;
rollback:
    if (mapped && paging_lease_unmap(as, base, mapped)) {
        /* A failed rollback must retain the backing and reserve its VA. */
        v->as_gen = as->generation;
        mark(v, first, mapped, 1);
        s->map_as = as; s->map_va = base; s->map_npages = mapped;
        s->flags |= GFX2_SF_KEEP;
        return OS32_ERR_IO;
    }
    return rc;
}

int gfx2_unmap(struct gfx2_surf *s)
{
    struct gfx2_vaspace *v;
    if (!s->map_as) return 0;
    if (paging_lease_unmap(s->map_as, s->map_va, s->map_npages)) return OS32_ERR_IO;
    v = vaspace(s->map_as, 0);
    if (v) mark(v, (s->map_va - MEM_GFX2_MAP_BASE) / PAGE_SIZE, s->map_npages, 0);
    gfx2_map_detach(s);
    return 0;
}

void gfx2_staging_drop(struct gfx2_surf *s)
{
    if (s->staging && !(s->flags & GFX2_SF_KEEP))
        (void)pgalloc_free_n_owner(s->staging_owner, s->staging_pfn, (int)s->staging_n);
    s->staging = 0; s->staging_pitch = 0;
    s->staging_pfn = s->staging_n = s->staging_owner = 0;
}

/* Only transient alias staging is detached on an unmap failure. */
static int staging_unmap(struct gfx2_surf *s)
{
    int rc = gfx2_unmap(s);
    if (rc) { s->flags |= GFX2_SF_KEEP; gfx2_map_detach(s); }
    gfx2_staging_drop(s);
    s->flags &= ~GFX2_SF_KEEP;
    return rc;
}

int gfx2_lock_revoke(struct gfx2_surf *s)
{
    int rc = 0;
    if (!(s->flags & GFX2_SF_ALIAS)) return OS32_ERR_INVAL;
    if (s->staging || s->map_as) rc = staging_unmap(s);
    if (s->state == GFX2_ST_LOCKED_RO || s->state == GFX2_ST_LOCKED_RW)
        (void)gfx2_surf_state_set(s, GFX2_ST_IDLE);
    return rc;
}

int gfx2_lock(u32 sid, u32 flags, void **va, u32 *pitch)
{
    struct gfx2_caller c;
    struct gfx2_surf *s;
    struct gfx2_adapter *a;
    GfxSyncCtx *ctx = 0;
    u32 mode = 0, addr, pfn, n;
    u32 access = flags & GFX2_LOCK_READONLY ? GFX2_CPU_R : (GFX2_CPU_R | GFX2_CPU_W);
    int rc;
    if (!va || !pitch || flags & ~(GFX2_LOCK_READONLY | GFX2_LOCK_DISCARD | GFX2_LOCK_DONOTWAIT))
        return OS32_ERR_INVAL;
    if (gfx2_caller_get(&c)) return OS32_ERR_INVAL;
    s = gfx2_surf_get(sid, c.owner, &rc);
    if (!s) return rc;
    if (s->state == GFX2_ST_LOCKED_RO || s->state == GFX2_ST_LOCKED_RW) return OS32_ERR_BUSY;
    if (s->state != GFX2_ST_IDLE) return OS32_ERR_STALE;
    a = gfx2_adapter_get(s->adapter);
    if (!a) return OS32_ERR_INVAL;
    if (s->flags & GFX2_SF_ALIAS) {
        if (gfx2_sync_get(a, s->head, &ctx, &mode) || mode != GFXSYNC_DIRECT)
            return OS32_ERR_INVAL;
    }
    if ((flags & GFX2_LOCK_DONOTWAIT) &&
        (!gfx2_fence_done(a, s->last_write) ||
         ((access & GFX2_CPU_W) && !gfx2_fence_done(a, s->last_read)))) return OS32_ERR_BUSY;
    rc = gfx2_cpu_begin(s, access, GFX2_LOCK_DL_US); /* DISCARD still waits. */
    if (rc) return rc;
    if (ctx) {
        n = s->bytes / PAGE_SIZE + !!(s->bytes % PAGE_SIZE);
        if (!pgalloc_alloc_n_owner(c.owner, (int)n, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE,
                                  pgalloc_arena_end(), 0, &pfn)) return OS32_ERR_NOSPC;
        s->staging = P2V(pfn * PAGE_SIZE); s->staging_pitch = s->pitch;
        s->staging_pfn = pfn; s->staging_n = n; s->staging_owner = c.owner;
        kmemset(s->staging, 0, n * PAGE_SIZE); /* DISCARD and page tail never expose old data. */
    }
    rc = gfx2_map(s, c.as, &addr);
    if (rc) {
        if (ctx) { (void)staging_unmap(s); }
        return rc;
    }
    if (ctx && !(flags & GFX2_LOCK_DISCARD) &&
        gfxsync_read_rows(ctx, 0, 0, s->w, s->h, s->staging, s->staging_pitch)) {
        (void)staging_unmap(s);
        return OS32_ERR_IO;
    }
    rc = gfx2_surf_state_set(s, flags & GFX2_LOCK_READONLY ? GFX2_ST_LOCKED_RO : GFX2_ST_LOCKED_RW);
    if (rc) { if (ctx) (void)staging_unmap(s); return rc; }
    *va = (void *)(uptr)addr; *pitch = s->pitch;
    return 0;
}

int gfx2_unlock(u32 sid)
{
    struct gfx2_caller c;
    struct gfx2_surf *s;
    GfxSyncCtx *ctx = 0;
    u32 mode = 0, access;
    struct gfx2_rect all;
    int rc = 0, lookup_rc;
    if (gfx2_caller_get(&c)) return OS32_ERR_INVAL;
    s = gfx2_surf_get(sid, c.owner, &lookup_rc);
    if (!s) return lookup_rc;
    if (s->state != GFX2_ST_LOCKED_RO && s->state != GFX2_ST_LOCKED_RW &&
        !(s->state == GFX2_ST_LOST && s->map_as == c.as)) return OS32_ERR_INVAL;
    access = s->state == GFX2_ST_LOCKED_RO ? GFX2_CPU_R : (GFX2_CPU_R | GFX2_CPU_W);
    all = (struct gfx2_rect){0, 0, s->w, s->h};
    if (s->staging) {
        if (gfx2_sync_get(gfx2_adapter_get(s->adapter), s->head, &ctx, &mode) ||
            mode != GFXSYNC_DIRECT ||
            gfxsync_write_rows(ctx, 0, 0, s->w, s->h, s->staging, s->staging_pitch)) rc = OS32_ERR_IO;
    }
    gfx2_cpu_end(s, access, &all);
    if (s->staging && staging_unmap(s)) rc = OS32_ERR_IO;
    (void)gfx2_surf_state_set(s, GFX2_ST_IDLE);
    return rc;
}
