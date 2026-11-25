/* ------------------------------------------------------------------------ */
/*  gfx の予約・BB・SURFACE (票 docs/archive/v3/TASK_T1_LEDGER.md §4-5、T1e)    */
/*                                                                          */
/*  ⑥ (gfx_boot_reserve) の後・⑦ (probe) の前に走るので、選択中の backend  */
/*  はまだ決まっていない — 候補ごとに見る。                                  */
/*  (1) 3 地点のうち boot / gfx の ledger_selfcheck が落ちていない。         */
/*  (2) planar は pool 内の連続 32 頁、RAM、owner = boot (移譲後 gshell)。 */
/*      ledger_surface_validate で全頁の所有と面の整合も確かめる。          */
/*  (3) sys_usable_mem_end() <= ledger_arena_top() (§3-6)。                 */
/*  (4) PEGC 候補なら BB は池の RAM で owner = boot、アリーナの上端より上、   */
/*      PEGC の窓が gfx の DEVICE 区間に入っている。                        */
/*  (5) Cirrus 候補なら CLIENT + DISPLAY の 2 本 (MMIO、UC、表示面は kernel)、 */
/*      どちらも gfx の DEVICE 区間 (Xe10-linear) の中。                     */
/* ------------------------------------------------------------------------ */
static int gfx_dev_covers(u32 pfn)
{
    const struct ledger_region *r;
    u32 i;
    for (i = 0; i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (r->type == LEDGER_R_DEVICE && r->owner == LEDGER_OWNER_GFX &&
            r->first <= pfn && pfn < r->end) return 1;
    }
    return 0;
}

static void __attribute__((cold)) test_gfx_ledger(void)
{
    const struct ledger_surface *p, *e, *c, *d;
    u32 top = ledger_arena_top() * PAGE_SIZE;
    p = ledger_surface_find(LEDGER_SF_PC98, LEDGER_ROLE_CLIENT);
    e = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_CLIENT);
    c = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_CLIENT);
    d = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_DISPLAY);
    check(ledger_check_fail == 0, "gfx:selfcheck boot+gfx");
    check(p && p->first >= MEM_POOL_BASE / PAGE_SIZE &&
          p->npages == MEM_GFX_BB_SIZE / PAGE_SIZE &&
          p->backing == LEDGER_SB_RAM &&
          (p->owner == LEDGER_OWNER_BOOT || p->owner == LEDGER_OWNER_GSHELL) &&
          ledger_surface_validate(p),
          "gfx:planar surface");
    {
        /* probe より前の値 (kernel.c の post-exec)。probe が FFF00000h に付け替えた後は
         * pegc_window_base() と比べること (R7FIX2 段 3b、Fable R2)。 */
        const struct ledger_surface *pd = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_DISPLAY);
        check(!pd || (pd->first == PEGC_LINEAR_BASE / PAGE_SIZE &&
              pd->npages == PEGC_FB_SIZE_480 / PAGE_SIZE &&
              pd->width == MEM_GFX_BB8_WIDTH && pd->height == MEM_GFX_BB8_HEIGHT &&
              pd->pitch == MEM_GFX_BB8_PITCH && pd->planes == 1 && !pd->plane_offset[0] &&
              pd->format == GFX_BB_PACKED8 && pd->owner == LEDGER_OWNER_KERNEL &&
              pd->backing == LEDGER_SB_VRAM && pd->cache == LEDGER_CACHE_UC &&
              pd->perm_max == LEDGER_PERM_RW && ledger_surface_validate(pd)),
              "gfx:pegc display");
    }
    {
        u32 gen = p ? p->gen : 0, first = p ? p->first : 0;
        check(p && ledger_surface_regen((u32)(p - ledger_surfaces), 0) &&
              p->gen == gen + 1 && p->first == first &&
              p->npages == MEM_GFX_BB_SIZE / PAGE_SIZE,
              "gfx:regen keeps backing");
    }
    check(sys_usable_mem_end() <= top, "gfx:usable <= arena top");
    check(!e || (e->owner == LEDGER_OWNER_BOOT && e->backing == LEDGER_SB_RAM &&
                 e->first * PAGE_SIZE >= top &&
                 gfx_dev_covers(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE)), "gfx:pegc bb");
    check(!c == !d && (!c || (c->backing == LEDGER_SB_MMIO &&
                              d->backing == LEDGER_SB_MMIO &&
                              c->cache == LEDGER_CACHE_UC && d->cache == LEDGER_CACHE_UC &&
                              d->owner == LEDGER_OWNER_KERNEL &&
                              gfx_dev_covers(c->first) && gfx_dev_covers(d->first))),
          "gfx:cirrus client+display");
    {
        const u32 planes[4] = {GVRAM_PLANE_B, GVRAM_PLANE_R, GVRAM_PLANE_G, GVRAM_PLANE_I};
        u32 i, n = 0, valid = 1, cache_ok = 1;
        u32 *pd = P2V(paging_kernel_pd_phys());
        u32 *pt = P2V(pd[0] & ~0xfffUL);
        for (i = 0; i < LEDGER_MAX_SURFACES; i++) {
            const struct ledger_surface *sf = &ledger_surfaces[i];
            if (!sf->npages || sf->backend != LEDGER_SF_PC98 ||
                sf->role != LEDGER_ROLE_DISPLAY) continue;
            if (n >= 4 || sf->first * PAGE_SIZE != planes[n] ||
                sf->npages != GVRAM_PLANE_SIZE / PAGE_SIZE ||
                sf->cache != LEDGER_CACHE_UC || sf->planes != 1 ||
                sf->plane_offset[0] || !ledger_surface_validate(sf)) valid = 0;
            n++;
        }
        for (i = 0; i < MEM_1MB / PAGE_SIZE; i++)
            if ((pt[i] & (PTE_PCD | PTE_PWT)) !=
                (PC98_NATIVE_VRAM(i * PAGE_SIZE) ? PTE_PCD : 0)) cache_ok = 0;
        check(valid && n == 4, "gfx:planar display bundle");
        check(cache_ok, "gfx:native aliases UC, CG/ROM WB");
    }
}
