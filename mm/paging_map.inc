/* Master page mappings, shared permissions and V86 session mappings. */

/* 動的な master の PT は pgalloc の workspace からだけ取る。T1a で全構成が
 * モデル経路 (workspace 持ち) になったので、旧 legacy の「アプリ帯の最大上端
 * より上の恒等 RW ページを 1 枚ずつ探す」分岐は撤去した (TASK_T1_LEDGER
 * §4-1)。workspace の下端の不変条件 (ARENA_TOP: MEM_APP_BAND_MAX_TOP 以上、
 * FIXED: PDE 0 の中) は pgalloc_init_layout が見る。 */
static u32 *reserve_table(void)
{
    return (u32 *)P2V(pgalloc_alloc_pt());
}

/* 未公開 PT 自身を一時リストに使う。成功まで master は一切変更しない。 */
static int prepare_tables(u32 first, u32 count)
{
    u32 pdi, last, i;
    u32 *pending = 0, *table, *next;

    if (!count) return 0;
    last = (first + count - 1) / PTE_COUNT;
    first /= PTE_COUNT;
    for (pdi = first; pdi <= last; pdi++) {
        if (!page_tables[pdi] && (!pg_enabled || live_addrspaces ||
            paging_current_cr3() != paging_kernel_pd_phys())) return -1;
    }
    for (pdi = first; pdi <= last; pdi++) {
        if (page_tables[pdi]) continue;
        table = reserve_table();
        if (!table) {
            while (pending) {
                next = (u32 *)P2V(pending[0]);
                pgalloc_free_pt(V2P(pending));
                pending = next;
            }
            return -1;
        }
        table[0] = V2P(pending);
        table[1] = pdi;
        pending = table;
    }
    while (pending) {
        table = pending;
        pending = (u32 *)P2V(table[0]);
        pdi = table[1];
        for (i = 0; i < PTE_COUNT; i++) table[i] = 0;
        page_tables[pdi] = table;
        page_directory[pdi] = V2P(table) | PAGE_RW;
    }
    return 0;
}

static int set_page_noflush(u32 virt_addr, u32 phys_addr, u32 flags)
{
    u32 pdi = PDE_INDEX(virt_addr);
    u32 pti = PTE_INDEX(virt_addr);

    /* マッピング範囲外。無言 no-op にすると「ガードページを置いたつもり」の
     * まま保護なしで走る (fail-open) ので、必ず失敗を返す。 */
    if (pdi >= PAGING_PT_COUNT) return -1;

    page_tables[pdi][pti] = (phys_addr & PTE_FRAME_MASK) | flags;

    /* 実効権限は PDE と PTE の論理積になる (i386 の仕様)。PTE だけ USER に
     * しても、その PDE に USER が無ければユーザ (V86 ゲスト) からは
     * アクセスできず #PF になる。PTE に USER を付けたら PDE にも伝播させる。
     *
     * PDE を USER にしても、同じ PDE 配下の他のページは PTE 側が
     * supervisor のままなので保護は保たれる。カーネル帯を USER に
     * しないこと (docs/archive/v21/v86v2/02_np21w_paging_analysis.md)。 */
    if (flags & PTE_USER) {
        page_directory[pdi] |= PTE_USER;
    }
    return 0;
}

int paging_set_page(u32 virt_addr, u32 phys_addr, u32 flags)
{
    int rc;
    if (live_addrspaces && (flags & PTE_USER)) return -1;
    rc = prepare_tables(virt_addr >> PAGE_SHIFT, 1);
    if (rc == 0) rc = set_page_noflush(virt_addr, phys_addr, flags);
    if (rc == 0 && pg_enabled) arch_mmu_flush_tlb();
    return rc;
}

/* ======================================================================== */
/*  paging_map_range — 範囲マップ (end は exclusive)                        */
/*                                                                          */
/*  [virt_start, virt_end) を phys_start からの連続物理へ flags でマップし、 */
/*  TLB フラッシュを最後に 1 回だけ行う。かつてはページごとに               */
/*  paging_set_page → CR3 全リロードが走り、V86 setup だけで 300 回超の      */
/*  フラッシュが発生していた (R9)。                                          */
/* ======================================================================== */
int paging_map_range(u32 virt_start, u32 virt_end, u32 phys_start, u32 flags)
{
    u32 count;
    if (live_addrspaces && (flags & PTE_USER)) return -1;
    if (virt_start > virt_end) { paging_range_reject_count++; return -1; }
    if (virt_start == virt_end) return 0;
    count = ((virt_end - 1) >> PAGE_SHIFT) - (virt_start >> PAGE_SHIFT) + 1;
    return paging_map_phys(virt_start, phys_start, count, flags);
}

/* ======================================================================== */
/*  paging_map_phys — デバイス窓マップ (ページ数指定)                        */
/*                                                                          */
/*  paging_map_range のページ数版。ドライバ (gfx/ など) が「この物理窓を     */
/*  この仮想番地へ npages 分」と書けるようにするための入口で、               */
/*  ページテーブルはカーネル側 (本ファイル) だけが触る、という分担を保つ。   */
/* ======================================================================== */
int paging_map_phys(u32 virt_addr, u32 phys_addr, u32 npages, u32 flags)
{
    u32 v = virt_addr >> PAGE_SHIFT;
    u32 p = phys_addr >> PAGE_SHIFT;
    u32 i;
    if (live_addrspaces && (flags & PTE_USER)) return -1;
    if (npages > PAGING_PFN_COUNT - v || npages > PAGING_PFN_COUNT - p)
        return -1;
    if (prepare_tables(v, npages) != 0) return -1;
    for (i = 0; i < npages; i++)
        set_page_noflush((v + i) << PAGE_SHIFT, (p + i) << PAGE_SHIFT, flags);
    if (npages && pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

/* 最初の AS より前、shm_init の後だけ。tramp は exec の静的 BSS。 */
int paging_boot_user_shared(u32 tramp)
{
    if (boot_user_shared_done || live_addrspaces || !pg_enabled ||
        (tramp & (PAGE_SIZE - 1)) || tramp < KERNEL_LOAD_ADDR ||
        tramp >= KHEAP_BASE) return -1;
    if (paging_map_range(MEM_SHM_BASE, MEM_SHM_BASE + MEM_SHM_SIZE,
                         MEM_SHM_BASE, PAGE_RW | PTE_USER) != 0 ||
        paging_set_page(tramp, tramp, PAGE_RO | PTE_USER) != 0) return -1;
    boot_user_shared_done = 1;
    return 0;
}

/* SHM は既存共有 USER の RW だけを切り替える。全範囲を検査してから書く。 */
int paging_shm_set_rw(u32 base, u32 end, int writable)
{
    u32 a, pdi, entry;
    u32 mask = ~(u32)(PAGE_SIZE - 1);
    if (base < MEM_SHM_BASE || end > MEM_SHM_BASE + MEM_SHM_SIZE ||
        base >= end || ((base | end) & (PAGE_SIZE - 1))) return -1;
    for (a = base; a < end; a += PAGE_SIZE) {
        pdi = PDE_INDEX(a);
        if (!page_tables[pdi] ||
            (page_directory[pdi] & (mask | PTE_PRESENT | PTE_PS)) !=
            (V2P(page_tables[pdi]) | PTE_PRESENT)) return -1;
        entry = page_tables[pdi][(a >> PAGE_SHIFT) % PTE_COUNT];
        if ((entry & (mask | PTE_PRESENT)) != (a | PTE_PRESENT)) return -1;
        if (!(entry & PTE_USER)) {
            paging_shm_user_missing_count++;
            return -1;
        }
    }
    for (a = base; a < end; a += PAGE_SIZE) {
        u32 page = (a - MEM_SHM_BASE) / PAGE_SIZE;
        set_page_noflush(a, a, (writable ? PAGE_RW : PAGE_RO) | PTE_USER);
        if (page >= MEM_SHM_SIZE / PAGE_SIZE) continue;
        if (writable) shm_audit_ro[page / BITS_PER_U32] &= ~(1UL << (page % BITS_PER_U32));
        else shm_audit_ro[page / BITS_PER_U32] |= 1UL << (page % BITS_PER_U32);
    }
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

static struct v86_session_state *v86_map_session;
int paging_v86_session_open(void) { return v86_map_session != 0; }

int paging_v86_snapshot(struct v86_session_state *s)
{
    if (v86_map_session || !pg_enabled || !page_tables[0]) return -1;
    s->active_cr3 = paging_current_cr3();
    s->master_pde = page_directory[0];
    s->active_pde = ((u32 *)P2V(s->active_cr3))[0];
    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)
        s->low_pte[i] = page_tables[0][i];
    v86_map_session = s;
    return 0;
}

/* Restore is allocation-free and cannot leave a live backing alias behind.
 * Count a missing session, then restore the shared PT directly. No PDE USER
 * propagation to the current CR3 (which may differ from the captured CR3),
 * and only one TLB flush after all saved PTEs/PDEs have been written. */
int paging_v86_restore(struct v86_session_state *s)
{
    u32 bad = 0;
    s->restore_pte_bad = s->restore_pde_bad = 0;
    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++) {
        u32 saved = s->low_pte[i];
        if (v86_map_session != s) bad++;
        page_tables[0][i] = saved;
    }
    page_directory[0] = s->master_pde;
    ((u32 *)P2V(s->active_cr3))[0] = s->active_pde;
    arch_mmu_flush_tlb();
    for (u32 i = 0; i < V86_GUEST_MAP_END / PAGE_SIZE; i++)
        if (page_tables[0][i] != s->low_pte[i]) bad++;
    s->restore_pte_bad = bad; /* Includes a rejected session/PTE restore. */
    if (page_directory[0] != s->master_pde ||
        ((u32 *)P2V(s->active_cr3))[0] != s->active_pde) {
        s->restore_pde_bad++;
        bad++;
    }
    v86_map_session = 0;
    return (int)bad;
}

/* Open session only: never promote outside the low 1MB. */
int paging_v86_map_range(u32 base, u32 end, u32 phys, u32 flags)
{
    u32 a;
    if (!v86_map_session || base >= end || end > platform_memory_attributes.bios_rom.end ||
        ((base | end | phys) & (PAGE_SIZE - 1)) ||
        phys > ~(u32)0 - (end - base - 1) || !page_tables[0]) return -1;
    for (a = base; a < end; a += PAGE_SIZE, phys += PAGE_SIZE)
        set_page_noflush(a, phys, flags);
    if (flags & PTE_USER)
        ((u32 *)P2V(paging_current_cr3()))[0] |= PTE_USER;
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

/* 指定範囲を覆う PDE から USER を落とす。
 * paging_set_page() は USER を立てる方向にしか伝播させないので、
 * V86 セッション終了時にカーネル側が明示的に戻すために使う。 */
int paging_pde_clear_user(u32 start, u32 end)
{
    u32 pdi;
    u32 first = PDE_INDEX(start);
    u32 last  = PDE_INDEX(end);

    if (start > end) { paging_range_reject_count++; return -1; }

    for (pdi = first; pdi <= last && pdi < PAGING_PT_COUNT; pdi++) {
        page_directory[pdi] &= ~(u32)PTE_USER;
    }

    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

/* ======================================================================== */
/*  paging_set_readonly — 範囲内の全ページをRead-Onlyに                     */
/* ======================================================================== */
int paging_set_readonly(u32 start, u32 end)
{
    u32 pfn, last;
    if (start > end) { paging_range_reject_count++; return -1; }
    last = end >> PAGE_SHIFT;
    for (pfn = start >> PAGE_SHIFT; pfn <= last; pfn++) {
        if (page_tables[pfn / PTE_COUNT])
            page_tables[pfn / PTE_COUNT][pfn % PTE_COUNT] &= ~(u32)PTE_RW;
    }
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

/* ======================================================================== */
/*  paging_set_not_present — 範囲内の全ページをアクセス不可に               */
/* ======================================================================== */
int paging_set_not_present(u32 start, u32 end)
{
    u32 pfn, last;
    if (start > end) { paging_range_reject_count++; return -1; }
    last = end >> PAGE_SHIFT;
    for (pfn = start >> PAGE_SHIFT; pfn <= last; pfn++) {
        if (page_tables[pfn / PTE_COUNT])
            page_tables[pfn / PTE_COUNT][pfn % PTE_COUNT] &= ~(u32)PTE_PRESENT;
    }
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

/* ======================================================================== */
/*  paging_enabled — ページングが有効かどうか                               */
/* ======================================================================== */
int paging_enabled(void) { return pg_enabled; }

/* ======================================================================== */
/*  paging_is_present — 指定アドレスのページがPresentかどうか                */
/*                                                                          */
/*  メモリダンプの安全チェック用。ページング無効時は常に1を返す。            */
/* ======================================================================== */
int paging_is_present(u32 virt_addr)
{
    u32 pdi, pti;
    if (!pg_enabled) return 1;
    pdi = PDE_INDEX(virt_addr);
    if (!page_tables[pdi]) return 0;
    pti = PTE_INDEX(virt_addr);
    return (page_tables[pdi][pti] & PTE_PRESENT) ? 1 : 0;
}

u32 paging_pte_flags(u32 virt_addr)
{
    u32 pdi, pti;
    if (!pg_enabled) return 0;
    pdi = PDE_INDEX(virt_addr);
    if (!page_tables[pdi]) return 0;
    pti = PTE_INDEX(virt_addr);
    return page_tables[pdi][pti] & ~PTE_FRAME_MASK;
}

/* PTE の生値 (番地 + フラグ)。ページング前・PT が無ければ 0。kdebug の peek が窓の PTE を
 * 張り替える前に退避する (TASK_KPEEK §2-4 4)。読むだけ。 */
u32 paging_pte_raw(u32 virt_addr)
{
    u32 pdi = PDE_INDEX(virt_addr);
    u32 pti = PTE_INDEX(virt_addr);
    if (!pg_enabled) return 0;   /* paging_pte_flags と同じ扱い */
    if (!page_tables[pdi]) return 0;
    return page_tables[pdi][pti];
}

/* paging_pte_raw で退避した生値をそのまま書き戻す。PT が無ければ -1。
 * PDE は触らない (窓の PDE は paging_init で present、live AS も同じ PT)。 */
int paging_pte_restore(u32 virt_addr, u32 raw)
{
    u32 pdi = PDE_INDEX(virt_addr);
    u32 pti = PTE_INDEX(virt_addr);
    if (!page_tables[pdi]) return -1;
    page_tables[pdi][pti] = raw;
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

