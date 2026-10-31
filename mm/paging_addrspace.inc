/* Private application address spaces, page walks and mapping lifetime. */

/* ======================================================================== */
/*  リング3 アドレス空間 (M1b: PD 複製)                                     */
/*                                                                          */
/*  カーネル全体は identity マッピング (仮想=物理) なので、pgalloc が返す     */
/*  物理アドレスはそのまま仮想アドレスとして読み書きできる。新 PD / アプリ    */
/*  PT のバッキングは常駐シェル帯 (0x400000) を除く物理 pool から取られ、      */
/*  アプリ PT を master と同一 identity で初期化するため、CR3 を新 PD に      */
/*  載せた後もそれらのページは自分自身を identity で見られる。               */
/* ======================================================================== */

/* 表の物理ポインタを触る口を paging に集約する (T1f)。
 * exec の旧判定と同じく PDE / PTE の両方に PRESENT・USER・RW が必要。 */
static int as_va_to_pa_flags(u32 pd_phys, u32 va, u32 *pa, u32 need)
{
    u32 pde, pt_phys, pte;
    if (!pd_phys || !paging_is_present((uptr)P2V(pd_phys)))
        return AS_VA_TABLE;
    pde = ((const volatile u32 *)P2V_IO(pd_phys))[PDE_INDEX(va)];
    if ((pde & need) != need || (pde & PTE_PS)) return AS_VA_PDE;
    pt_phys = pde & ~(u32)(PAGE_SIZE - 1);
    if (!paging_is_present((uptr)P2V(pt_phys))) return AS_VA_TABLE;
    pte = ((const volatile u32 *)P2V_IO(pt_phys))[(va >> 12) & (PTE_COUNT - 1)];
    if ((pte & need) != need) return AS_VA_PTE;
    *pa = (pte & ~(u32)(PAGE_SIZE - 1)) | (va & (PAGE_SIZE - 1));
    return 0;
}

int as_va_to_pa(u32 pd_phys, u32 va, u32 *pa)
{
    return as_va_to_pa_flags(pd_phys, va, pa, PTE_PRESENT | PTE_USER | PTE_RW);
}

int as_va_to_pa_read(u32 pd_phys, u32 va, u32 *pa)
{
    return as_va_to_pa_flags(pd_phys, va, pa, PTE_PRESENT | PTE_USER);
}

u32 paging_registered_pt(u32 va)
{
    return V2P(page_tables[PDE_INDEX(va)]);
}

u32 paging_kernel_pd_phys(void)
{
    /* identity マッピングなので page_directory の仮想アドレス = 物理。 */
    return V2P(page_directory);
}

u32 paging_current_cr3(void)
{
    return arch_mmu_current_root();
}

void paging_load_cr3(u32 pd_phys)
{
    arch_mmu_load_root(pd_phys);
}

static u32 as_generation; /* Saturate: never reuse a registered AS identity. */

int paging_addrspace_create_n(struct addrspace *as, u32 owner, u32 pde_count)
{
    u32 pd_phys, i;
    u32 *pd;
    if (paging_v86_session_open()) return -1;
    if (!as || !owner || !pde_count || pde_count > MEM_APP_BAND_MAX_PDES ||
        as_generation == ~(u32)0) return -1;
    for (i = 0; i < sizeof(*as) / sizeof(u32); i++) ((u32 *)as)[i] = 0;
    as->owner = owner;
    pd_phys = pgalloc_alloc_phys(owner, 1);
    if (!pd_phys) return -1;
    pd = (u32 *)P2V(pd_phys);
    /* APP_BAND より下の常駐シェルの PT (PDE 1) も写す。 */
    for (i = 0; i < PDE_COUNT; i++) pd[i] = page_directory[i];
    /* Never inherit a high user/device mapping into unused private bands. */
    for (i = APP_BAND_PDE; i < (MEM_LEASE_END >> PDE_SHIFT); i++) pd[i] = 0;
    as->generation = ++as_generation;
    as->pd_phys = pd_phys;
    as->app_pde = APP_BAND_PDE;
    as->app_pde_count = MEM_APP_BAND_MAX_PDES;
    live_addrspaces++;
    return 0;
}

int paging_addrspace_create(struct addrspace *as, u32 owner)
{
    return paging_addrspace_create_n(as, owner, 1);
}

void paging_addrspace_poison(struct addrspace *as)
{
    if (!as || as->appmem_poisoned || !as->pd_phys) return;
    as->appmem_poisoned = 1;
    /* Retain PD/PT pages, but remove the quarantined AS from the live count.
     * The abort safe points prevent this PD from being scheduled again. */
    live_addrspaces--;
    exec_addrspace_abort(as);
}

void paging_addrspace_destroy(struct addrspace *as)
{
    u32 k;

    if (!as || as->appmem_poisoned || !as->pd_phys) return;
    /* アクティブな PD を破棄してはならない (呼び出し側が master へ戻す責任)。
     * ここでは確認だけして、万一アクティブでも解放は続行しない。 */
    if (paging_current_cr3() == as->pd_phys) {
        return;
    }
    for (k = 0; k < MEM_LEASE_MAX; k++)
        if (as->leases[k].token) return; /* revoke before PD/PT teardown */
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) {
        if (as->lease_pt_phys[k] &&
            !pgalloc_free_n_owner(as->owner, as->lease_pt_phys[k] / PAGE_SIZE, 1)) {
            paging_addrspace_poison(as);
            return;
        }
        as->lease_pt_phys[k] = 0;
    }
    for (k = 0; k < MEM_APP_BAND_MAX_PDES; k++) {
        if (as->app_pt_phys[k] &&
            !pgalloc_free_n_owner(as->owner, as->app_pt_phys[k] / PAGE_SIZE, 1)) {
            paging_addrspace_poison(as);
            return;
        }
        as->app_pt_phys[k] = 0;
    }
    if (!pgalloc_free_n_owner(as->owner, as->pd_phys / PAGE_SIZE, 1)) {
        paging_addrspace_poison(as);
        return;
    }
    live_addrspaces--;
    as->pd_phys = 0;
    as->app_pde = 0;
    as->app_pde_count = 0;
}

/* Private application PT only; keep_cache preserves its existing PCD/PWT. */
static int addrspace_map_user_page(struct addrspace *as, u32 virt, u32 phys,
                                   u32 flags, int keep_cache)
{
    u32 pdi = PDE_INDEX(virt);
    u32 pti = PTE_INDEX(virt);
    u32 *pd;
    u32 *pt;

    if (!as || !as->pd_phys || !as->app_pde_count) return -1;
    pd = (u32 *)P2V(as->pd_phys);

    if (pdi >= as->app_pde && pdi < as->app_pde + as->app_pde_count) {
        /* アプリ固有 PT (このアプリの PD からしか見えない)。
         * 枚数分の連続 PDE のどれに落ちるかで PT を選ぶ。 */
        u32 k = pdi - as->app_pde;
        if (!as->app_pt_phys[k]) {
            u32 frame = pgalloc_alloc_phys(as->owner, 1);
            if (!frame) return -1;
            pt = (u32 *)P2V(frame);
            u32 i;
            for (i = 0; i < PTE_COUNT; i++) pt[i] = 0;
            as->app_pt_phys[k] = frame;
            pd[pdi] = frame | PAGE_RW;
        }
        pt = (u32 *)P2V(as->app_pt_phys[k]);
    } else {
        return -1; /* Shared PTs are immutable through ordinary AS mapping. */
    }

    if (keep_cache) flags |= (pt[pti] & (u32)(PTE_PCD | PTE_PWT));
    pt[pti] = (phys & PTE_FRAME_MASK) | flags;

    /* このアプリ PD の PDE にだけ USER を伝播 (master の PDE は触らない)。 */
    if (flags & PTE_USER) {
        pd[pdi] |= PTE_USER;
    }
    return 0;
}

int paging_addrspace_map_user(struct addrspace *as, u32 virt, u32 phys,
                              u32 flags)
{
    return addrspace_map_user_page(as, virt, phys, flags, 0);
}

/* 範囲版の共通部。末尾の TLB 処理まで含めて 1 か所にまとめる。
 * phys_base != 0 のときは identity ではなく [phys_base, ...) の連続物理を
 * 張る (K5b P1)。identity 版は phys_base に 0 を渡す (物理 0 は張らない)。 */
static int addrspace_map_user_range(struct addrspace *as, u32 vstart,
                                    u32 vend, u32 flags, int keep_cache,
                                    u32 phys_base)
{
    u32 pfn, first, count, pdi;
    u32 *pd;
    if (!as || !as->pd_phys || !as->app_pde_count || vstart > vend) return -1;
    if (vstart == vend) return 0;
    if (phys_base & (u32)(PAGE_SIZE - 1)) return -1;
    first = vstart >> PAGE_SHIFT;
    count = ((vend - 1) >> PAGE_SHIFT) - first + 1;
    pd = (u32 *)P2V(as->pd_phys);
    {
        u32 k, added[MEM_APP_BAND_MAX_PDES], n = 0;
        for (pdi = first / PTE_COUNT; pdi <= (first + count - 1) / PTE_COUNT; pdi++) {
            if (pdi >= as->app_pde && pdi < as->app_pde + as->app_pde_count) {
                k = pdi - as->app_pde;
                if (as->app_pt_phys[k]) continue;
                as->app_pt_phys[k] = pgalloc_alloc_phys(as->owner, 1);
                if (!as->app_pt_phys[k]) goto rollback;
                {
                    u32 i;
                    u32 *pt = P2V(as->app_pt_phys[k]);
                    for (i = 0; i < PTE_COUNT; i++) pt[i] = 0;
                }
                added[n++] = k;
                pd[pdi] = as->app_pt_phys[k] | PAGE_RW;
            } else goto rollback;
        }
        goto ready;
rollback:
        while (n) {
            k = added[--n];
            pd[as->app_pde + k] = 0;
            pgalloc_free_n_owner(as->owner, as->app_pt_phys[k] / PAGE_SIZE, 1);
            as->app_pt_phys[k] = 0;
        }
        return -1;
ready: ;
    }
    for (pfn = first; pfn < first + count; pfn++) {
        u32 phys = phys_base ? (phys_base + ((pfn - first) << PAGE_SHIFT))
                             : (pfn << PAGE_SHIFT);
        addrspace_map_user_page(as, pfn << PAGE_SHIFT, phys,
                                flags, keep_cache);
    }
    /* この PD が既にアクティブなら TLB を捨てる。通常は CR3 に載せる前に
     * 呼ぶので不要だが、載せた後の追加マップにも備える。 */
    if (as && as->pd_phys && paging_current_cr3() == as->pd_phys) {
        paging_load_cr3(as->pd_phys);
    }
    return 0;
}

int paging_addrspace_map_user_range(struct addrspace *as, u32 vstart,
                                    u32 vend, u32 flags)
{
    return addrspace_map_user_range(as, vstart, vend, flags, 0, 0);
}

/* 私有 PT の中だけで既存 PTE の PCD/PWT を保つ。現在 in-tree の呼び手なし
 * (拒否を検査する selftest を除く) — 将来の私有デバイス lease 用。 */
int paging_addrspace_map_user_keep(struct addrspace *as, u32 vstart,
                                   u32 vend, u32 flags)
{
    return addrspace_map_user_range(as, vstart, vend, flags, 1, 0);
}

/* ======================================================================== */
/*  paging_addrspace_map_user_range_phys — per-app 物理を固定仮想へ (P1)     */
/*  票 TASK_K5_multiapp.md D1/I5。同じ 0x500000 に 4 本を載せる土台。         */
/* ======================================================================== */
int paging_addrspace_map_user_range_phys(struct addrspace *as, u32 vstart,
                                         u32 vend, u32 pstart, u32 flags)
{
    if (pstart == 0) return -1;   /* 物理 0 は identity 版の合図に使っている */
    return addrspace_map_user_range(as, vstart, vend, flags, 0, pstart);
}

/* ======================================================================== */
/*  paging_addrspace_clear_app_band — アプリ帯の identity を落とす (P2, I6)  */
/* ======================================================================== */
int paging_addrspace_clear_app_band(struct addrspace *as)
{
    u32 k, i;
    u32 *pd;

    if (!as || !as->pd_phys || !as->app_pde_count) return -1;
    pd = (u32 *)P2V(as->pd_phys);
    for (k = 0; k < as->app_pde_count; k++) {
        u32 pdi = as->app_pde + k;
        u32 *pt;
        if (!as->app_pt_phys[k]) continue;
        pt = (u32 *)P2V(as->app_pt_phys[k]);
        for (i = 0; i < PTE_COUNT; i++) pt[i] = 0;
        /* PDE は PT を指したまま present/RW。USER は張り直しで立てる。 */
        pd[pdi] &= ~(u32)PTE_USER;
    }
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    return 0;
}

/* ======================================================================== */
/*  paging_addrspace_free_user_range — per-app 物理を返す (P6)               */
/*                                                                          */
/*  アプリ固有 PDE の中だけを辿る。共有 PT (VRAM/SHM/フォント/GFX) に        */
/*  掛かる範囲は 1 ビットも触らない — 触ると他の PD ごと巻き添えになる。      */
/* ======================================================================== */
/* Master context only: never restore the app CR3 after poisoning its AS. */
u32 paging_addrspace_free_user_range(struct addrspace *as, u32 vstart,
                                     u32 vend)
{
    u32 pfn, first, count, freed = 0, saved;

    if (!as || as->appmem_poisoned || !as->pd_phys || !as->app_pde_count || vstart >= vend) return 0;
    /* Reject the entire request before freeing any private page. */
    if ((PDE_INDEX(vstart)) < as->app_pde ||
        ((vend - 1) >> PDE_SHIFT) >= as->app_pde + as->app_pde_count) return 0;
    saved = paging_current_cr3();
    if (saved == as->pd_phys) paging_load_cr3(paging_kernel_pd_phys());
    first = vstart >> PAGE_SHIFT;
    count = ((vend - 1) >> PAGE_SHIFT) - first + 1;
    for (pfn = first; pfn < first + count; pfn++) {
        u32 pdi = pfn / PTE_COUNT;
        u32 pti = pfn % PTE_COUNT;
        u32 *pt;
        if (pdi < as->app_pde || pdi >= as->app_pde + as->app_pde_count)
            continue;                       /* 共有帯は触らない */
        if (!as->app_pt_phys[pdi - as->app_pde]) continue;
        pt = (u32 *)P2V(as->app_pt_phys[pdi - as->app_pde]);
        if (pt[pti] & PTE_PRESENT) {
            if (!pgalloc_free_n_owner(as->owner, pt[pti] >> PAGE_SHIFT, 1)) {
                /* Retain the failed frame; never retry a poisoned owner. */
                pt[pti] &= ~PTE_PRESENT;
                paging_addrspace_poison(as);
                break;
            }
            freed++;
        }
        pt[pti] = 0;
    }
    if (saved == as->pd_phys && !as->appmem_poisoned) paging_load_cr3(saved);
    return freed;
}

