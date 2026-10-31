/* Bootstrap page tables, fixed layout validation and boot backing. */

/* CR3/PDE frame と固定枠全体 (使用11枚 + 予備) の恒等 sup/RW/WB を照合。A/D bit は無視する。
 * PDE0 の USER 単独は SHM/trampoline のため許可し、実効権限は PTE で拒否。 */
static int fixed_paging_valid(void)
{
    u32 i, a, entry;
    u32 mask = ~(u32)(PAGE_SIZE - 1);
    if (V2P(page_directory) != MEM_FIXED_PD_BASE) return 0;
    for (i = 0; i < PAGING_BOOT_PT_COUNT; i++) {
        a = MEM_FIXED_BOOT_PT_BASE + i * PAGE_SIZE;
        entry = page_directory[i];
        if (V2P(page_tables[i]) != a ||
            (entry & (mask | PAGE_RW | PTE_PS | PTE_PCD | PTE_PWT)) !=
            (a | PAGE_RW)) return 0;
    }
    for (i = PAGING_APERTURE_PDI; (u32)i <= PAGING_MMIO_TOP_PDI;
         i += PAGING_MMIO_TOP_PDI - PAGING_APERTURE_PDI) {
        a = i == PAGING_APERTURE_PDI ? MEM_FIXED_APERTURE_PT_BASE :
                                     MEM_FIXED_MMIO_TOP_PT_BASE;
        entry = page_directory[i];
        if (entry & PTE_USER) return 0;
        if (V2P(page_tables[i]) != a ||
            (entry & (mask | PAGE_RW | PTE_PS | PTE_PCD | PTE_PWT)) !=
            (a | PAGE_RW)) return 0;
    }
    for (a = MEM_FIXED_PAGING_BASE; a < MEM_FIXED_PAGING_FRAME_END; a += PAGE_SIZE) {
        entry = page_tables[0][a / PAGE_SIZE];
        if ((entry & (mask | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) !=
            (a | PAGE_RW)) return 0;
    }
    return 1;
}

/* PG を立てる前に現在の stack/heap と予約境界の PTE を確認する。 */
static int boot_range_valid(u32 first, u32 end, int present)
{
    u32 a, entry;
    u32 mask = ~(u32)(PAGE_SIZE - 1);
    for (a = first; a < end; a += PAGE_SIZE) {
        entry = page_tables[a / (PTE_COUNT * PAGE_SIZE)][(a / PAGE_SIZE) % PTE_COUNT];
        if (present) {
            if ((entry & (mask | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) !=
                (a | PAGE_RW)) return 0;
        } else if (entry & PTE_PRESENT) return 0;
    }
    return 1;
}

/* SLACK は module owner の頁も含む。SQLite owner が登録した stack guard
 * だけ NP を要求し、残りは未割当・body/pool/stack とも恒等 sup/RW/WB。
 * 帯を常駐末尾まで縮めると、想定外の NP / 写像漏れを見逃す。 */
static int boot_slack_valid(void)
{
    u32 guard = paging_sqlite_stack_guard;
    if (guard < MEM_SHM_RESV_START || guard >= MEM_FIXED_PAGING_GUARD_BASE)
        return boot_range_valid(MEM_SHM_RESV_START, MEM_FIXED_PAGING_GUARD_BASE, 1);
    return !(guard % PAGE_SIZE) &&
        boot_range_valid(MEM_SHM_RESV_START, guard, 1) &&
        boot_range_valid(guard, guard + PAGE_SIZE, 0) &&
        boot_range_valid(guard + PAGE_SIZE, MEM_FIXED_PAGING_GUARD_BASE, 1);
}

/* 台帳の backing の型によらない supervisor 範囲。
 * SHM の属性は後で shm_init / boot_user_shared が張る。 */
static int boot_layout_valid(void)
{
    return MEM_SHM_RESV_START <= MEM_FIXED_PAGING_GUARD_BASE &&
        boot_slack_valid() &&
        boot_range_valid(MEM_LEDGER_META_BASE, MEM_LEDGER_META_END, 1) &&
        boot_range_valid(MEM_FIXED_PAGING_GUARD_BASE, MEM_FIXED_PAGING_BASE, 0) &&
        boot_range_valid(MEM_STACK_GUARD, MEM_KSTACK_BASE, 0) &&
        boot_range_valid(MEM_KSTACK_BASE, MEM_SHELL_LOAD_ADDR, 1) &&
        boot_range_valid(MEM_SHELL_LOAD_ADDR, MEM_SHELL_GUARD, 1) &&
        boot_range_valid(MEM_SHELL_GUARD, MEM_SHELL_GUARD + PAGE_SIZE, 0) &&
        boot_range_valid(MEM_SHELL_GUARD + PAGE_SIZE, MEM_SHELL_BAND_END + 1, 1);
}

/* ======================================================================== */
/*  paging_init — ページテーブル構築 + ページング有効化                      */
/* ======================================================================== */
void paging_init(u32 mem_kb)
{
    int i, j;
    u32 phys;
    u32 pd_phys;
    u32 max_mem_bytes;
    u32 *pt_base;
    unsigned int flags = irq_save();

    /* 一度だけ初期化する。動的 PT / live AS / 現在 CR3 を破壊しない。 */
    if (pg_enabled) { irq_restore(flags); return; }

    /* ここで頭打ちにするのは **静的 bootstrap PT が覆う範囲** であって
     * 「OS32 が RAM として面倒を見る上限」ではない (K6-RAM)。これより上の
     * RAM は pgalloc_stage_online が paging_map_phys で張る。
     * mem_kb * 1024 の桁あふれ (mem_kb > 4194303) もこれで防げる。 */
    if (mem_kb > PAGING_BOOT_MAP_SIZE / KIB) mem_kb = PAGING_BOOT_MAP_SIZE / KIB;
    max_mem_bytes = mem_kb * KIB;
    boot_identity_end = max_mem_bytes / PAGE_SIZE;

    /* アライン済みポインタを取得 (先頭だけ上げれば以降は 4KB 刻みで乗る) */
    page_directory = (u32 *)P2V_BOOT(MEM_FIXED_PD_BASE);
    pt_base = (u32 *)P2V_BOOT(MEM_FIXED_BOOT_PT_BASE);
    for (i = 0; i < PAGING_PT_COUNT; i++) {
        page_tables[i] = i < PAGING_BOOT_PT_COUNT ?
            pt_base + (u32)i * PTE_COUNT : 0;
    }

    /* ページディレクトリ初期化: 全エントリをNot-Presentに */
    for (i = 0; i < PDE_COUNT; i++) {
        page_directory[i] = 0;
    }

    /* ページテーブル構築: 実装されている範囲のみR/W、超えた範囲はNot-Present。
     * 16MB〜32MB を覆う PT (H3b で足した 4 枚) は全ページ Not-Present のまま
     * になるが、**PDE は present で登録しておく** — こうしておけば
     * paging_map_phys() がデバイス窓を張るときに PTE を書くだけで済み、
     * PDE の張り替え (= 他 PD との整合) を考えなくてよい。 */
    for (i = 0; i < PAGING_BOOT_PT_COUNT; i++) {
        for (j = 0; j < PTE_COUNT; j++) {
            phys = (u32)(i * PTE_COUNT + j) * PAGE_SIZE;
            if ((phys < max_mem_bytes || phys < MEM_1MB) &&
                !(phys >= platform_memory_geometry.system_first &&
                  phys < platform_memory_geometry.system_end)) {
                /* コンベンショナルメモリ(0-1MB)またはプローブ範囲内 */
                page_tables[i][j] = phys | PAGE_RW |
                    native_cache_bits(phys);
            } else {
                /* 未実装領域 */
                page_tables[i][j] = PAGE_NOT_PRESENT;
            }
        }
        /* ページディレクトリにテーブルを登録 */
        page_directory[i] = V2P(page_tables[i]) | PAGE_RW;
    }

    /* デバイス窓と最上位 MMIO の PT (各静的 1 枚)。中身は全 Not-Present、PDE は present。
     * 窓を張るのは gfx バックエンドの paging_map_phys (supervisor + PCD)。 */
    for (i = PAGING_APERTURE_PDI; (u32)i <= PAGING_MMIO_TOP_PDI;
         i += PAGING_MMIO_TOP_PDI - PAGING_APERTURE_PDI) {
        phys = i == PAGING_APERTURE_PDI ? MEM_FIXED_APERTURE_PT_BASE :
                                        MEM_FIXED_MMIO_TOP_PT_BASE;
        page_tables[i] = (u32 *)P2V_BOOT(phys);
        for (j = 0; j < PTE_COUNT; j++)
            page_tables[i][j] = PAGE_NOT_PRESENT;
        page_directory[i] = phys | PAGE_RW;
    }

    /* ========================================================
     *  保護属性の設定
     * ======================================================== */

    /* カーネルスタック: R/W を強制する。
     *
     * 上のループはプローブしたメモリ量 (max_mem_bytes) を超える範囲を
     * Not-Present にするので、極端に小さいメモリ量が報告された場合でも
     * **自分が今立っているスタックだけは必ず生かしておく**。
     * ここを NP にした瞬間に次の push で三重フォルトになる。 */
    paging_map_range(MEM_KSTACK_BASE,
                     PAGE_ALIGN_DOWN(MEM_KSTACK_TOP) + PAGE_SIZE,
                     MEM_KSTACK_BASE, PAGE_RW);

    /* スタックガードページ: Not-Present */
    paging_set_not_present(MEM_STACK_GUARD, MEM_STACK_GUARD_END);

    /* SLACK と常駐 metadata は初期の恒等 sup/RW/WB のまま。guard だけ NP。 */
    paging_set_not_present(MEM_FIXED_PAGING_GUARD_BASE,
                           MEM_FIXED_PAGING_BASE - 1);

    /* シェルスタックガード: Not-Present */
    paging_set_not_present(MEM_SHELL_GUARD, MEM_SHELL_GUARD + PAGE_SIZE - 1);

    /* BIOS ROM: Read-Only */
    paging_set_readonly(platform_memory_attributes.bios_rom.first,
                        platform_memory_attributes.bios_rom.end - 1);

    /* ========================================================
     *  変換表の根 (CR3) にページディレクトリをセット → 変換を有効化 (CR0.PG)
     * ======================================================== */
    if (paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_FRAME_END,
                         MEM_FIXED_PAGING_BASE, PAGE_RW) != 0 ||
        !fixed_paging_valid() ||
        !boot_layout_valid()) {
        irq_restore(flags);
        return; /* PG は立てない。後段の memory_boot_init が fail-stop。 */
    }
    pd_phys = V2P(page_directory);
    arch_mmu_load_root(pd_phys);
    arch_mmu_enable();

    pg_enabled = 1;
    irq_restore(flags);
}

/* ======================================================================== */
/*  paging_reclaim_conventional — ブート後のコンベンショナルメモリ属性変更    */
/*  ページ0: NOT PRESENT (NULLポインタ検出)                                 */
/*  0x1000-0x9FFFF: R/W (旧R/O/ローダー領域とブート時スタックを解放)         */
/* ======================================================================== */
void paging_reclaim_conventional(void)
{
    /* Boot consumers now use the immutable real_lowmem copy. */
    paging_set_not_present(0, MEM_NULL_GUARD_END);

    /* 0x1000-0x9FFFF: R/W (フォント/KCG scratch/GFX用) */
    paging_map_range(MEM_CONV_RECLAIM_START, MEM_CONV_RECLAIM_END + 1,
                     MEM_CONV_RECLAIM_START, PAGE_RW);
}

/* ======================================================================== */
/*  paging_set_page — 1ページの属性を変更                                   */
/* ======================================================================== */
/* 1 ページ設定の共通部 (TLB フラッシュなし)。
 * paging_set_page と paging_map_range から使う。 */
u32 paging_boot_identity_end(void)
{
    return boot_identity_end;
}

/* APP 帯を避け、master から安全に書ける RAM だけを PT に使う。 */
int paging_boot_context(void)
{
    return pg_enabled && !live_addrspaces &&
           paging_current_cr3() == paging_kernel_pd_phys();
}

int paging_verify_identity(u32 first, u32 count, void *identity)
{
    u32 p, entry, index;
    u32 mask = ~(u32)(PAGE_SIZE - 1);
    u32 *table;
    if (!pg_enabled || !count || first >= PAGING_PFN_COUNT ||
        count > PAGING_PFN_COUNT - first || identity != P2V(first * PAGE_SIZE))
        return 0;
    for (p = first; p < first + count; p++) {
        index = p / PTE_COUNT;
        table = page_tables[index];
        if (!table) return 0;
        entry = page_directory[index];
        if ((entry & (mask | PAGE_RW | PTE_PS | PTE_PCD | PTE_PWT)) !=
            (V2P(table) | PAGE_RW)) return 0;
        entry = table[p % PTE_COUNT];
        if ((entry & (mask | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) !=
            (p * PAGE_SIZE | PAGE_RW)) return 0;
    }
    return 1;
}

/* 台帳の backing (FIXED 型) が present / supervisor / R/W か確かめる
 * (TASK_T1_LEDGER §3-3 ②、§4-1)。backing は core BSS の中で、paging_init が
 * 張り済み。memory_boot_init が FIXED を選んだときだけ、モデルの初期化の
 * 直前に 1 回呼ぶ。ブート文脈 (master CR3、live AS 0) の外では何もしない。0 = 失敗。 */
static int ledger_backing_mapped;

int paging_map_ledger_backing(void)
{
    if (!paging_boot_context()) return 0;
    /* 常駐 BSS の中なので張り済み。壊れた写像は直さず失敗にする。 */
    if (!paging_verify_identity(MEM_LEDGER_META_BASE / PAGE_SIZE,
                                MEM_LEDGER_META_SIZE / PAGE_SIZE,
                                P2V(MEM_LEDGER_META_BASE))) return 0;
    ledger_backing_mapped = 1;
    return 1;
}

