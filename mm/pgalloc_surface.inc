/* Surface validation, registration, generation, release, and transfer. */
/* ======================================================================== */
/*  SURFACE (§3-1・§3-8) と CPL=0 子のアリーナの上端 (§3-6)、T1e            */
/*                                                                          */
/*  T1 は型と表と登録・移譲だけ (lease の付け外しは T2、lease_count は 0)。  */
/*  登録・移譲は起動時と GUI の開始にしか走らないので cold (大きさで組ま   */
/*  せる、カーネルの予算 §4-5-R)。                                         */
/* ======================================================================== */
int ledger_surface_validate(const struct ledger_surface *sf)
{
    u32 p, i, bytes, plane_bytes;
    if (!sf || !owner_ok(sf->owner) || !sf->npages ||
        sf->first >= PHYSMEM_MAX_PFN || sf->npages > PHYSMEM_MAX_PFN - sf->first ||
        sf->npages > U32_MAX / PAGE_SIZE ||
        !sf->backing || sf->backing > LEDGER_SB_MMIO ||
        !sf->planes || sf->planes > SURFACE_QUERY_MAX || !sf->width || !sf->height || !sf->pitch ||
        sf->perm_max > LEDGER_PERM_RO) return 0;
    bytes = sf->npages * PAGE_SIZE;
    plane_bytes = (u32)sf->pitch * sf->height;
    for (i = 0; i < sf->planes; i++)
        if (sf->plane_offset[i] > bytes || plane_bytes > bytes - sf->plane_offset[i])
            return 0;
    if (sf->cache != (sf->backing <= LEDGER_SB_FIXED_RAM ?
                      LEDGER_CACHE_WB : LEDGER_CACHE_UC)) return 0;
    for (p = sf->first; p < sf->first + sf->npages; p++) {
        if (sf->backing == LEDGER_SB_RAM) {
            if (p >= limit_pfn || !page_owned(p, sf->owner)) return 0;
        } else {
            for (i = 0; i < ledger_region_count; i++) {
                const struct ledger_region *r = &ledger_regions[i];
                if (r->first <= p && p < r->end && r->cache == sf->cache &&
                    ((sf->backing == LEDGER_SB_FIXED_RAM &&
                      r->type == LEDGER_R_SURFACE_BACKING && r->owner == sf->owner) ||
                     (sf->backing == LEDGER_SB_VRAM && r->type == LEDGER_R_FIXED &&
                      r->owner == sf->owner) ||
                     (sf->backing >= LEDGER_SB_VRAM && r->type == LEDGER_R_DEVICE))) break;
            }
            if (i == ledger_region_count) return 0;
            if (sf->backing >= LEDGER_SB_VRAM && ledger_regions[i].type == LEDGER_R_DEVICE) {
                const struct ledger_region *r = &ledger_regions[i];
                for (i = 0; i < LEDGER_MAX_RESOURCES; i++)
                    if ((r->res_mask & (1U << i)) &&
                        ledger_resources[i].map_first <= p &&
                        p < ledger_resources[i].map_end) break;
                if (i == LEDGER_MAX_RESOURCES) return 0;
            }
        }
    }
    return 1;
}

u32 gfx_surface_unready;

int __attribute__((cold))
ledger_surface_create(const struct ledger_surface *sf, u32 *sid)
{
    u32 i, p, used_bytes;
    unsigned int flags = irq_save();
    int ok = 0;
    /* Padding uses the master identity alias until T2c. Reject before writes. */
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth ||
        !ledger_surface_validate(sf) || sf->lease_count || sf->closing) goto done;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
        const struct ledger_surface *other = &ledger_surfaces[i];
        if (other->npages && sf->first < other->first + other->npages &&
            other->first < sf->first + sf->npages) goto done;
    }
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (!ledger_surfaces[i].npages && ledger_surfaces[i].gen != LEDGER_SURFACE_GEN_MAX) break;
    if (i == LEDGER_MAX_SURFACES) goto done;
    p = ledger_surfaces[i].gen + 1;
    ledger_surfaces[i] = *sf;
    ledger_surfaces[i].gen = p;
    gfx_surface_unready &= ~(1U << i);
    /* RAM padding belongs to this surface, and is never disclosed dirty. */
    if (sf->backing <= LEDGER_SB_FIXED_RAM) {
        used_bytes = (u32)sf->pitch * sf->height;
        for (p = 0; p < sf->npages * PAGE_SIZE; p++) {
            u32 n;
            for (n = 0; n < sf->planes; n++)
                if (p >= sf->plane_offset[n] && p - sf->plane_offset[n] < used_bytes) break;
            if (n == sf->planes) ((u8 *)P2V(sf->first * PAGE_SIZE))[p] = 0;
        }
    }
    if (sid) *sid = i;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

/* Preserve identity/backing and pixels; only validated geometry is replaceable. */
int ledger_surface_regen(u32 sid, const struct ledger_surface *geom)
{
    unsigned int flags = irq_save();
    struct ledger_surface next, *sf;
    int ok = 0;
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth || sid >= LEDGER_MAX_SURFACES) goto done;
    sf = &ledger_surfaces[sid];
    if (!sf->npages || sf->lease_count || sf->closing || sf->gen == LEDGER_SURFACE_GEN_MAX)
        goto done;
    next = *sf;
    if (geom) {
        next.width = geom->width;
        next.height = geom->height;
        next.pitch = geom->pitch;
        next.format = geom->format;
        next.planes = geom->planes;
        for (u32 i = 0; i < SURFACE_QUERY_MAX; i++) next.plane_offset[i] = geom->plane_offset[i];
        if (!ledger_surface_validate(&next)) goto done;
    }
    next.gen++;
    *sf = next;
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

int ledger_surface_release(u32 sid)
{
    struct ledger_surface *sf;
    unsigned int flags = irq_save();
    int ok = 0;
    if (kctx_irq_depth || kctx_exc_depth ||
        sid >= LEDGER_MAX_SURFACES || !ledger_surfaces[sid].npages) goto done;
    sf = &ledger_surfaces[sid];
    sf->closing = 1;
    if (!sf->lease_count) {
        if (sf->backing == LEDGER_SB_RAM &&
            !pgalloc_free_n_owner(sf->owner, sf->first, (int)sf->npages)) goto done;
        sf->npages = 0;
    }
    ok = 1;
done:
    irq_restore(flags);
    return ok;
}

struct ledger_surface *ledger_surface_find(u32 backend, u32 role)
{
    u32 i;
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (ledger_surfaces[i].npages && ledger_surfaces[i].backend == backend &&
            ledger_surfaces[i].role == role) return &ledger_surfaces[i];
    return 0;
}

int __attribute__((cold)) ledger_surface_transfer(u32 sid, u32 to)
{
    struct ledger_surface *sf;
    struct ledger_region *r;
    u32 i, from;
    unsigned int flags;
    int ok;
    flags = irq_save();
    ok = 0;
    if (sid >= LEDGER_MAX_SURFACES || !owner_ram(to) ||
        !ledger_surfaces[sid].npages) goto done;
    sf = &ledger_surfaces[sid];
    from = sf->owner;
    /* 2 回目以降 (GUI → CUI → GUI) は同じ owner なので何もしない (R5 (b))。
     * RAM の面はページの L2 ごと移す (全ページが from のものでなければ拒否)。 */
    ok = from == to || sf->backing != LEDGER_SB_RAM ||
         ledger_transfer(sf->first, (int)sf->npages, from, to);
    if (!ok || from == to) goto done;
    /* 固定 RAM の面は、それを含む同じ owner の SURFACE_BACKING 区間も移す。
     * MMIO の面は SURFACE の owner だけ (装置の DEVICE 区間は gfx のまま)。 */
    for (i = 0; sf->backing == LEDGER_SB_FIXED_RAM && i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (r->type == LEDGER_R_SURFACE_BACKING && r->owner == from &&
            r->first <= sf->first && sf->first + sf->npages <= r->end)
            r->owner = (u8)to;
    }
    sf->owner = (u8)to;
done:
    irq_restore(flags);
    return ok;
}

