/* ======================================================================== */
/*  PAGING.C — x86ページング (メモリ保護)                                   */
/*                                                                          */
/*  アイデンティティマッピング方式:                                         */
/*    仮想アドレス = 物理アドレス の1:1対応を維持し、                        */
/*    ページ属性(R/W, Present)でメモリ保護のみを追加する。                   */
/*    既存コード(VRAMアクセス等)を一切変更せずに保護が得られる。             */
/*                                                                          */
/*  構造:                                                                   */
/*    page_directory[1024]  — ページディレクトリ (4KB)                      */
/*    page_tables[1024] — sparse pointers; eight bootstrap PTs (32KB)       */
/*    Other PTs are allocated on demand, never a static 4MiB RAM map.       */
/*                                                                          */
/*  保護マップ (ブートアーキテクチャ改善後):                               */
/*                                                                          */
/*  [コンベンショナルメモリ]                                                */
/*    0x00000 - 0x00FFF : NP   (NULLポインタ検出, paging_reclaim後)          */
/*    0x01000 - 0x9FFFF : R/W  (フォント/Unicode/GFX, ブート後に再利用)      */
/*    0xA0000 - 0xEFFFF : R/W。TVRAM / B,R,G / E は PCD (UC)。             */
/*    CG 窓と ROM は WB (PCD なし)。ROM の書込み保護は下記。                 */
/*    0xF0000 - 0xFFFFF : R/O  (BIOS ROM)                                   */
/*                                                                          */
/*  [拡張メモリ]                                                            */
/*    0x100000 - 0x1FFFFF : R/W  (カーネル帯域: code+heap+KAPI+SHM。末尾の  */
/*                                SHM 後方予約だけ NP。番地は __bss_end 由来 */
/*                                で浮くので、実値は docs/02_memory.md §2-1) */
/*    0x200000 -          : R/W  (SQLite帯域: code+BSS+代替スタック)      */
/*             - 0x2FAFFF : NP   (カーネル予約)                              */
/*    0x2FB000 - 0x2FBFFF : NP   (カーネルスタックガード)                    */
/*    0x2FC000 - 0x2FFFFF : R/W  (カーネルスタック, 16KB。2026-09-17 に      */
/*                                0x1FC000 から移設 — 決裁 D1)               */
/*    0x300000 - 0x3FFFFF : R/W  (シェル常駐帯域, ガード付き)             */
/*    0x400000 - 0x4FFFFF : R/O+U(共有ライブラリ帯域: .text/.rodata。      */
/*                                .data/.bss はアプリ PD ごとに差し替え)   */
/*    0x500000 - mem_end  : R/W  (プログラム空間, ガードページ付き)         */
/*    mem_end  - 0xFFFFFF : NP   (未実装メモリ)                             */
/*                                                                          */
/*  [16MB 超のデバイス窓帯 (H3b)]                                            */
/*    0x1000000-0x1FFFFFF : NP   (実 RAM 無し。既定は全ページ Not-Present)   */
/*      paging_map_phys() でドライバが窓だけを張る。現在の利用者は Xe10      */
/*      内蔵 Cirrus のリニア窓 0x1000000〜0x11FFFFF (2MB)。                  */
/* ======================================================================== */

#include "paging.h"

#include "io.h"
#include "v86_mem.h"
#include "cpu.h"      /* arch_mmu_* (実装は arch/$(ARCH)/arch_cpu.h) */
#include "memmap.h"
#include "pgalloc.h"
#include "pc98.h"

/* カーネルスタック帯のレイアウト不変条件。
 * ガードページはスタック直下に隣接する。ずれると paging_init の R/W 強制や
 * ガード設定が意図しないページに掛かる。
 * 2026-09-17 (決裁 D1): スタックはカーネル帯域の末尾から **SQLite 帯域の
 * 末尾** へ移った。カーネル帯域は KHEAP_BASE 以降が浮くので、固定番地の
 * スタックを同じ帯に置くと育ったぶんが必ずぶつかる。 */
STATIC_ASSERT(MEM_STACK_GUARD_END + 1 == MEM_KSTACK_BASE,
              kstack_guard_adjacent);
STATIC_ASSERT((MEM_STACK_GUARD & (PAGE_SIZE - 1)) == 0,
              kstack_guard_page_aligned);
/* 浮動番地の帯 (カーネル帯域) の外に居ること = D1 の要点 */
STATIC_ASSERT(MEM_STACK_GUARD > MEM_KERNEL_BAND_END, kstack_outside_kernel_band);
/* シェル常駐帯域には食い込まない */
STATIC_ASSERT(MEM_KSTACK_TOP < MEM_SHELL_LOAD_ADDR, kstack_below_shell_band);
/* SQLite 帯域の予約域 (NP) はスタックガードの手前で終わる */
STATIC_ASSERT(MEM_KERNEL_RESV_END < MEM_STACK_GUARD, kernel_resv_below_kstack);

/* ------------------------------------------------------------------------ */
/*  帯どうしの重なりと範囲の逆転 (票 TASK_KSTACK_USER §4 の 1、決裁 D2)      */
/*                                                                          */
/*  **実値 (KHEAP_BASE) では書けない。** KHEAP_BASE は `(u32)&__bss_end`     */
/*  由来で C の整数定数式ではなく、GCC はファイルスコープの可変長配列と見て  */
/*  "variably modified at file scope" で落とす — **条件が真でも落ちる**。    */
/*  (同じ理由で 2026-09-17 まで kernel/shm.c:29,33 の表明 2 本が黙って       */
/*   死んでいた。今は下の MEM_SHM_GUI_OFFSET で生き返っている。)             */
/*                                                                          */
/*  そこで **「予算いっぱいまで育った場合の配置」** を検査する。上限は       */
/*  build/os32.ld の ASSERT がリンク時に保証するので、最悪配置で重ならない   */
/*  なら実配置でも重ならない。最悪配置の番地は全部定数式なので表明できる。   */
/*                                                                          */
/*  実値のほうは 2 つで見る: tools/gen_memmap.py --check (make check) と、    */
/*  ブート自己診断 paging_memmap_selftest (PDE 0 の PTE 1024 本)。           */
/* ------------------------------------------------------------------------ */

/* 予算そのものが正気か (帯のサイズを増やす変更がここを食い潰したら落ちる) */
STATIC_ASSERT(MEM_KERNEL_IMAGE_MAX >= 0x40000UL, kernel_image_budget_sane);

/* 最悪配置: SHM 帯 (前方ガード〜後方ガードの末尾) がカーネル帯域に収まる */
STATIC_ASSERT(MEM_SHM_GUARD_HI_MAX + MEM_GUARD_SIZE - 1 <= MEM_KERNEL_BAND_END,
              shm_band_within_kernel_band);
/* 最悪配置: SHM 後方予約が逆転しない (空 = START == END + 1 は許す) */
STATIC_ASSERT(MEM_SHM_RESV_START_MAX <= MEM_SHM_RESV_END + 1,
              shm_resv_not_reversed);
/* 最悪配置: SHM 帯はカーネルスタックガードより下で終わる */
STATIC_ASSERT(MEM_SHM_GUARD_HI_MAX + MEM_GUARD_SIZE <= MEM_STACK_GUARD,
              shm_band_below_kstack_guard);
/* 最悪配置: カーネルスタックの全 16KB が SHM 帯の外 */
STATIC_ASSERT(MEM_KSTACK_BASE > MEM_SHM_GUARD_HI_MAX + MEM_GUARD_SIZE - 1,
              kstack_outside_shm_band);
/* 最悪配置でもヒープと KAPI がカーネル帯域に収まる */
STATIC_ASSERT(MEM_SHM_GUARD_LO_MAX < MEM_KERNEL_BAND_END, kheap_kapi_within_band);

/* リング3 アプリ帯 PDE (M1b) はアプリ帯域を覆う PDE と一致し、かつ静的
 * page_tables[] の範囲内でなければならない。ここがずれるとアプリ PD が
 * カーネル帯域を差し替えたり範囲外 PT を読んだりして黙って壊れる。
 * K3 以降、この 1 枚の PDE が「共有ライブラリ帯域 + プログラム帯 +
 * ユーザスタック」を丸ごと覆う。3 つとも同じ PDE の内側であること
 * (別 PDE に出ると、PD ごとの差し替えから外れて共有されてしまう)。 */
STATIC_ASSERT(APP_BAND_PDE == (MEM_APP_BAND_BASE >> 22), app_band_pde_matches);
STATIC_ASSERT(APP_BAND_PDE < PAGING_PT_COUNT, app_band_pde_in_range);
STATIC_ASSERT((MEM_SHLIB_BASE >> 22) == APP_BAND_PDE, shlib_base_in_app_band);
STATIC_ASSERT((MEM_EXEC_LOAD_ADDR >> 22) == APP_BAND_PDE, exec_load_in_app_band);
STATIC_ASSERT(((MEM_APP_BAND_TOP - 1) >> 22) == APP_BAND_PDE, app_band_top_in_pde);
STATIC_ASSERT(MEM_SHLIB_END <= MEM_EXEC_LOAD_ADDR, shlib_band_below_exec);

/* 可変 PDE 化 (票 docs/tasks/memory/APP_BAND_PDE.md)。帯は先頭 PDE から
 * 連続 MEM_APP_BAND_MAX_PDES 枚まで伸びうる。次の 5 つが崩れると、
 * アプリ PD がカーネル帯やデバイス窓を差し替えて黙って壊れる。 */
STATIC_ASSERT(MEM_APP_BAND_MAX_PDES >= 1, app_band_at_least_one_pde);
STATIC_ASSERT(MEM_APP_BAND_PDE_SIZE == (u32)PTE_COUNT * PAGE_SIZE,
              app_band_pde_size_is_one_pde);
STATIC_ASSERT(APP_BAND_PDE + MEM_APP_BAND_MAX_PDES <= (MEM_LEASE_BASE >> 22), app_band_before_lease);
STATIC_ASSERT(MEM_APP_BAND_MAX_TOP == MEM_APP_STACK_TOP, app_stack_at_band_top);

/* K6-RAM (2026-09-11): 「実 RAM の上限」という概念は OS 側に持たない。
 * 静的 bootstrap PT が覆う守備範囲 (PAGING_BOOT_MAP_SIZE) は
 * **paging_init が恒等マップを張れる範囲**でしかなく、それを超える RAM は
 * pgalloc_stage_online が paging_map_phys で動的 PT を足して張る。
 * 守備範囲は 4MB (= 1 PDE) の倍数であること。 */
STATIC_ASSERT(PAGING_PFN_COUNT == PDE_COUNT * PTE_COUNT, full_pfn_space);
STATIC_ASSERT((PAGING_BOOT_MAP_SIZE % (PTE_COUNT * PAGE_SIZE)) == 0,
              map_size_pde_aligned);
/* T2a′: PD/PT backing は画像外の恒久 FIXED。ポインタ表だけを BSS に置く。 */
STATIC_ASSERT(MEM_FIXED_BOOT_PT_COUNT == 8 &&
              MEM_FIXED_PAGING_END - MEM_FIXED_PAGING_BASE == 10 * PAGE_SIZE,
              fixed_paging_ten_pages);
STATIC_ASSERT(((MEM_FIXED_PAGING_BASE | MEM_FIXED_PD_BASE |
                MEM_FIXED_BOOT_PT_BASE | MEM_FIXED_APERTURE_PT_BASE |
                MEM_FIXED_PAGING_END | MEM_SHELL_HEAP_BASE | MEM_SHELL_HEAP_END |
                (MEM_SHELL_BAND_END + 1)) & (PAGE_SIZE - 1)) == 0,
              fixed_paging_page_aligned);
STATIC_ASSERT(MEM_FIXED_PD_BASE == MEM_FIXED_PAGING_BASE &&
              MEM_FIXED_BOOT_PT_BASE == MEM_FIXED_PD_BASE + PAGE_SIZE &&
              MEM_FIXED_APERTURE_PT_BASE == MEM_FIXED_BOOT_PT_BASE +
                  PAGING_BOOT_PT_COUNT * PAGE_SIZE &&
              MEM_FIXED_PAGING_END == MEM_FIXED_APERTURE_PT_BASE + PAGE_SIZE,
              fixed_paging_contiguous);
STATIC_ASSERT(MEM_SHELL_STACK_TOP <= MEM_SHELL_HEAP_BASE &&
              MEM_SHELL_HEAP_BASE < MEM_SHELL_HEAP_END &&
              MEM_SHELL_HEAP_BASE + MEM_SHELL_HEAP_SIZE == MEM_SHELL_HEAP_END &&
              MEM_SHELL_HEAP_END == MEM_FIXED_PAGING_BASE,
              shell_heap_below_fixed_paging);
STATIC_ASSERT(MEM_FIXED_PAGING_END <= MEM_SHELL_BAND_END + 1 &&
              MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE &&
              MEM_POOL_BASE - MEM_FIXED_PAGING_END == 5 * PAGE_SIZE,
              fixed_paging_tail_reserved);
STATIC_ASSERT(MEM_KERNEL_BAND_END < MEM_SHELL_LOAD_ADDR &&
              MEM_DMA_POOL_END < MEM_SHELL_LOAD_ADDR &&
              MEM_LEDGER_META_END <= MEM_STACK_GUARD &&
              MEM_KSTACK_TOP < MEM_SHELL_LOAD_ADDR &&
              MEM_SHELL_LOAD_ADDR <= MEM_SHELL_HEAP_BASE,
              fixed_paging_above_kernel_regions);

/* OS が割り当てるデバイス窓の帯 (memmap.h MEM_DEVICE_APERTURE_*) の先頭 4MB を
 * 覆う PT。**静的に 1 枚** 持つ (2026-09-29、Cirrus のリニア窓 FE000000h)。
 * 新しい PDE を足せるのは live AS が 0 の間だけ (prepare_tables) だが、窓を
 * 張る gfx の init は exec の後にも走る。paging_init で PDE を present にして
 * おけば、以後のアプリ PD は PDE ごとこの PT を写し、paging_map_phys は PTE を
 * 書くだけで済む (PDE 4-7 の 16MB〜32MB と同じやり方)。legacy 経路 (8MB) には
 * 動的 PT の置き場が無いので、動的確保にはしない。 */
#define PAGING_APERTURE_PDI (MEM_DEVICE_APERTURE_BASE / MEM_DEVICE_APERTURE_PDE_SIZE)
STATIC_ASSERT(MEM_DEVICE_APERTURE_PDE_SIZE == (u32)PTE_COUNT * PAGE_SIZE,
              aperture_pt_covers_one_pde);
STATIC_ASSERT((MEM_DEVICE_APERTURE_BASE % MEM_DEVICE_APERTURE_PDE_SIZE) == 0,
              aperture_base_pde_aligned);
STATIC_ASSERT(MEM_DEVICE_APERTURE_BASE + MEM_DEVICE_APERTURE_PDE_SIZE <=
              MEM_DEVICE_APERTURE_END, aperture_pt_inside_band);
STATIC_ASSERT(PAGING_APERTURE_PDI >= PAGING_BOOT_PT_COUNT &&
              PAGING_APERTURE_PDI < PAGING_PT_COUNT, aperture_pdi_static_free);


static u32 *page_directory;          /* アライン済みポインタ */
static u32 *page_tables[PAGING_PT_COUNT];

static int pg_enabled = 0;
static u32 live_addrspaces;
static int boot_user_shared_done;
/* Intended SHM permissions, recorded by the only post-boot RW-changing API.
 * Auditing must not infer the expected RW bit from the PTE being checked. */
static u32 shm_audit_ro[(MEM_SHM_SIZE / PAGE_SIZE + 31) / 32];
u32 paging_shm_user_missing_count;
/* 逆転した範囲 (start > end) を渡されて撥ねた回数。
 *
 * 範囲 API は前から -1 を返していたが、**呼び側が戻り値を見ていない**ので
 * 空振りが成功に見えていた (paging_init の
 * paging_set_not_present(MEM_SHM_RESV_START, MEM_SHM_RESV_END) がまさにそれ)。
 * 静的な検査 (STATIC_ASSERT / gen_memmap.py) が届かない実行時の呼び出しを
 * 拾うため、撥ねた回数をカーネルシンボルとして公開し、ブート自己診断で見る。
 * static にしないのは kselftest_pass と同じ理由 (ホストから emu_read_mem する)。 */
u32 paging_range_reject_count = 0;
/* paging_init が実際に恒等マップした範囲の上端 PFN (exclusive)。 */
static u32 boot_identity_end;

/* CR3/PDE frame と固定10枚の恒等 sup/RW/WB を照合。A/D bit は無視する。
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
    entry = page_directory[PAGING_APERTURE_PDI];
    if (entry & PTE_USER) return 0;
    if (V2P(page_tables[PAGING_APERTURE_PDI]) != MEM_FIXED_APERTURE_PT_BASE ||
        (entry & (mask | PAGE_RW | PTE_PS | PTE_PCD | PTE_PWT)) !=
        (MEM_FIXED_APERTURE_PT_BASE | PAGE_RW)) return 0;
    for (a = MEM_FIXED_PAGING_BASE; a < MEM_FIXED_PAGING_END; a += PAGE_SIZE) {
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

/* ======================================================================== */
/*  paging_init — ページテーブル構築 + ページング有効化                      */
/* ======================================================================== */
void paging_init(u32 mem_kb)
{
    extern u32 __sqlite_start;
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
    if (mem_kb > PAGING_BOOT_MAP_SIZE / 1024) mem_kb = PAGING_BOOT_MAP_SIZE / 1024;
    max_mem_bytes = mem_kb * 1024;
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
                !(phys >= MEM_SYSTEM_SPACE_BASE && phys < MEM_SYSTEM_SPACE_END)) {
                /* コンベンショナルメモリ(0-1MB)またはプローブ範囲内 */
                page_tables[i][j] = phys | PAGE_RW |
                    (PC98_NATIVE_VRAM(phys) ? PTE_PCD : 0);
            } else {
                /* 未実装領域 */
                page_tables[i][j] = PAGE_NOT_PRESENT;
            }
        }
        /* ページディレクトリにテーブルを登録 */
        page_directory[i] = V2P(page_tables[i]) | PAGE_RW;
    }

    /* デバイス窓の帯の PT (静的 1 枚)。中身は全 Not-Present、PDE は present。
     * 窓を張るのは gfx バックエンドの paging_map_phys (supervisor + PCD)。 */
    page_tables[PAGING_APERTURE_PDI] = (u32 *)P2V_BOOT(MEM_FIXED_APERTURE_PT_BASE);
    for (j = 0; j < PTE_COUNT; j++)
        page_tables[PAGING_APERTURE_PDI][j] = PAGE_NOT_PRESENT;
    page_directory[PAGING_APERTURE_PDI] =
        V2P(page_tables[PAGING_APERTURE_PDI]) | PAGE_RW;

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

    /* カーネル帯域内 SHM 後方予約: Not-Present (カーネル帯域の終端まで)。
     * カーネルが予算いっぱいまで育つとこの帯は **空** になる
     * (START == END + 1)。空のときに呼ぶと逆転として撥ねられるので呼ばない。
     * 逆転 (START > END + 1) は上の STATIC_ASSERT が最悪配置で禁じている。 */
    if (MEM_SHM_RESV_START <= MEM_SHM_RESV_END)
        paging_set_not_present(MEM_SHM_RESV_START, MEM_SHM_RESV_END);
    else if (MEM_SHM_RESV_START > MEM_SHM_RESV_END + 1)
        paging_range_reject_count++;   /* 逆転 = 設計が壊れている。空とは別 */

    /* カーネル予約域 (SQLite帯域後 〜 シェル帯域前): Not-Present */
    paging_set_not_present(MEM_KERNEL_RESV_START, MEM_KERNEL_RESV_END);

    /* DMA プール (票 TASK_HAL_WIRING §1-3): 予約域の中に開ける 64KB の穴。
     * present / supervisor / R/W。上下は予約域のまま NP がガードになる。
     *
     * **NP 化の範囲から外すのではなく、張り直す。** 除外で済ませると、
     * ここが「上のメモリ量ループが決めた属性のまま」になり、プローブ量が
     * 小さい機械では Not-Present のまま残る (= 割り込み文脈の DMA 設定で
     * 三重フォルト)。順序も大事で、**NP の後**に張らないと消される。
     *
     * USER は立てない — CPL=3 から装置の記述子を書けてはいけない。
     * 立っていないことは kselftest の MM 検査 (MM_RW vs MM_RWU) が見る。 */
    paging_map_range(MEM_DMA_POOL_BASE, MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE,
                     MEM_DMA_POOL_BASE, PAGE_RW);

    /* シェルスタックガード: Not-Present */
    paging_set_not_present(MEM_SHELL_GUARD, MEM_SHELL_GUARD + PAGE_SIZE - 1);

    /* heap の末尾を一旦 NP にし、固定10枚だけを最後に張り直す。 */
    paging_set_not_present(MEM_SHELL_HEAP_END, MEM_SHELL_BAND_END);

    /* SQLite帯域 + 代替スタック (0x200000〜): 強制R/W
     * ブートローダーが sqlite.bin を 0x200000 にロード済み。
     * 代替スタックも含めメモリプローブ結果に関係なく R/W を保証する。 */
    {
        u32 sq_start = PAGE_ALIGN_DOWN((u32)&__sqlite_start);
        u32 sq_end   = PAGE_ALIGN_DOWN(MEM_SQLITE_STACK_TOP + PAGE_SIZE);
        paging_map_range(sq_start, sq_end, sq_start, PAGE_RW);
    }

    /* BIOS ROM: Read-Only */
    paging_set_readonly(MEM_BIOS_ROM_START, MEM_BIOS_ROM_END);

    /* ========================================================
     *  変換表の根 (CR3) にページディレクトリをセット → 変換を有効化 (CR0.PG)
     * ======================================================== */
    if (paging_map_range(MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_END,
                         MEM_FIXED_PAGING_BASE, PAGE_RW) != 0 ||
        !fixed_paging_valid() ||
        !boot_range_valid(PAGE_ALIGN_DOWN((u32)&__sqlite_start),
                          PAGE_ALIGN_DOWN(MEM_SQLITE_STACK_TOP + PAGE_SIZE), 1) ||
        !boot_range_valid(MEM_DMA_POOL_BASE, MEM_DMA_POOL_END + 1, 1) ||
        !boot_range_valid(MEM_LEDGER_META_BASE, MEM_LEDGER_META_END, 0) ||
        !boot_range_valid(MEM_STACK_GUARD, MEM_KSTACK_BASE, 0) ||
        !boot_range_valid(MEM_KSTACK_BASE, MEM_SHELL_LOAD_ADDR, 1) ||
        !boot_range_valid(MEM_SHELL_GUARD, MEM_SHELL_GUARD + PAGE_SIZE, 0) ||
        !boot_range_valid(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_END, 1) ||
        !boot_range_valid(MEM_FIXED_PAGING_END, MEM_POOL_BASE, 0)) {
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
    /* ページ0: Read-Only (BIOS DATA AREA アクセスを許可しつつ書き込み検出)
     * [DEBUG] NOT PRESENT → R/O に変更: LZ4展開中にBDA参照でクラッシュする
     * 問題を調査中。元は paging_set_not_present(0x0, MEM_NULL_GUARD_END); */
    paging_set_readonly(0x0, MEM_NULL_GUARD_END);

    /* 0x1000-0x9FFFF: R/W (フォント/Unicode/GFX用) */
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

/* 台帳の backing (FIXED 型) を present / supervisor / R/W にする
 * (TASK_T1_LEDGER §3-3 ②、§4-1)。paging_init は予約域として NP にしている。
 * memory_boot_init が FIXED を選んだときだけ、モデルの初期化の直前に 1 回
 * 呼ぶ。下の 0x2F8000 (DMA プールの上側ガード) は NP のまま。
 * ブート文脈 (master CR3、live AS 0) の外では何もしない。0 = 失敗。 */
static int ledger_backing_mapped;

int paging_map_ledger_backing(void)
{
    if (!paging_boot_context()) return 0;
    if (paging_map_range(MEM_LEDGER_META_BASE, MEM_LEDGER_META_END,
                         MEM_LEDGER_META_BASE, PAGE_RW) != 0) return 0;
    ledger_backing_mapped = 1;
    return 1;
}

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
    u32 pdi = virt_addr >> 22;
    u32 pti = (virt_addr >> 12) & 0x3FF;

    /* マッピング範囲外。無言 no-op にすると「ガードページを置いたつもり」の
     * まま保護なしで走る (fail-open) ので、必ず失敗を返す。 */
    if (pdi >= PAGING_PT_COUNT) return -1;

    page_tables[pdi][pti] = (phys_addr & 0xFFFFF000UL) | flags;

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
        pdi = a >> 22;
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
        if (writable) shm_audit_ro[page / 32] &= ~(1UL << (page % 32));
        else shm_audit_ro[page / 32] |= 1UL << (page % 32);
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
    if (page_directory[0] != s->master_pde ||
        ((u32 *)P2V(s->active_cr3))[0] != s->active_pde) bad++;
    v86_map_session = 0;
    return (int)bad;
}

/* Open session only: never promote outside the low 1MB. */
int paging_v86_map_range(u32 base, u32 end, u32 phys, u32 flags)
{
    u32 a;
    if (!v86_map_session || base >= end || end > MEM_BIOS_ROM_END + 1 ||
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
    u32 first = start >> 22;
    u32 last  = end >> 22;

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
    pdi = virt_addr >> 22;
    if (!page_tables[pdi]) return 0;
    pti = (virt_addr >> 12) & 0x3FF;
    return (page_tables[pdi][pti] & PTE_PRESENT) ? 1 : 0;
}

u32 paging_pte_flags(u32 virt_addr)
{
    u32 pdi, pti;
    if (!pg_enabled) return 0;
    pdi = virt_addr >> 22;
    if (!page_tables[pdi]) return 0;
    pti = (virt_addr >> 12) & 0x3FF;
    return page_tables[pdi][pti] & 0xFFFu;
}

/* ======================================================================== */
/*  リング3 アドレス空間 (M1b: PD 複製)                                     */
/*                                                                          */
/*  カーネル全体は identity マッピング (仮想=物理) なので、pgalloc が返す     */
/*  物理アドレスはそのまま仮想アドレスとして読み書きできる。新 PD / アプリ    */
/*  PT のバッキングは 0x400000 帯 (APP_BAND_PDE の範囲) から取られるが、      */
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
    pde = ((const volatile u32 *)P2V_IO(pd_phys))[va >> 22];
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
    return V2P(page_tables[va >> 22]);
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

/* アプリ帯に必要な PDE 枚数 (票 §4-1)。純粋な算術なのでホスト試験から
 * 直接呼べる (tools/tests/app_band_pde_host.c)。 */
u32 paging_app_band_pdes(u32 code_end, u32 heap_req, u32 ram_top)
{
    u32 need_top, n, by_ram;

    /* heap_size 無指定 (0) は従来どおり 1 枚。指定しないプログラムの
     * レイアウトを 1 バイトも動かさないための線引き (回帰ゼロ)。 */
    if (heap_req == 0) return 1;

    /* 要求を満たすのに帯の上端が最低どこまで要るか。
     * exec/exec.c の子プロセス帯レイアウトと同じ並び:
     *   本体 | sbrk (最低分) | guard_a | exec_heap | guard | ユーザスタック */
    heap_req = PAGE_ALIGN_UP(heap_req);
    if (heap_req < MEM_EXEC_HEAP_MIN) heap_req = MEM_EXEC_HEAP_MIN;
    if (code_end < MEM_LEGACY_APP_BASE) code_end = MEM_LEGACY_APP_BASE;

    need_top = MEM_EXEC_SBRK_MIN + PAGE_SIZE + PAGE_SIZE + MEM_EXEC_STACK_SIZE;
    /* 桁あふれは「伸ばせない」に倒す (大きい枚数を返さない) */
    if (heap_req > (u32)0xFFFFFFFFUL - need_top) return 1;
    need_top += heap_req;
    if (code_end > (u32)0xFFFFFFFFUL - need_top) return 1;
    need_top += code_end;

    if (need_top <= (MEM_LEGACY_APP_BASE + MEM_APP_BAND_PDE_SIZE)) return 1;
    n = (need_top - MEM_LEGACY_APP_BASE + MEM_APP_BAND_PDE_SIZE - 1) /
        MEM_APP_BAND_PDE_SIZE;
    if (n > MEM_LEGACY_APP_PDES) n = MEM_LEGACY_APP_PDES;

    /* 空き RAM (= 子が予約済みの範囲) を超えては伸ばさない。足りなければ
     * 1 枚のまま返し、要求が入らなければ exec が EXEC_ERR_NOMEM で拒否する
     * (切り詰めて「渡せたことにする」のは 2026-09-10 方針で禁止)。 */
    by_ram = (ram_top > MEM_LEGACY_APP_BASE) ?
             (ram_top - MEM_LEGACY_APP_BASE) / MEM_APP_BAND_PDE_SIZE : 0;
    if (by_ram < 1) by_ram = 1;
    if (n > by_ram) n = by_ram;
    if (n < 1) n = 1;
    return n;
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
    for (i = 0; i < PDE_COUNT; i++) pd[i] = page_directory[i];
    /* Never inherit a high user/device mapping into unused private bands. */
    for (i = APP_BAND_PDE; i < (MEM_LEASE_END >> 22); i++) pd[i] = 0;
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
    u32 pdi = virt >> 22;
    u32 pti = (virt >> 12) & 0x3FF;
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
    pt[pti] = (phys & 0xFFFFF000UL) | flags;

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
    if ((vstart >> 22) < as->app_pde ||
        ((vend - 1) >> 22) >= as->app_pde + as->app_pde_count) return 0;
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

/* 自己診断の AS は試験用の AS owner で作る (TASK_T1_LEDGER §4-8、B12)。
 * PD / PT も試験で張るページもその owner で確保・解放する — kernel で確保
 * すると free_user_range (as->owner で返す) が拒否され、空きページ数の比較が
 * 落ちる。解放の owner 検査は緩めない。 */
static int selftest_as_begin(struct addrspace *as, u32 pdes)
{
    u32 owner;
    if (!ledger_owner_new(LEDGER_KIND_AS, 0, "pgtest", &owner)) return -1;
    if (paging_addrspace_create_n(as, owner, pdes) != 0) {
        (void)ledger_owner_retire(owner);
        return -1;
    }
    return 0;
}

/* 破棄して、owner のページが 0 なら番号を返す。残っていれば (失敗枝) 回収で
 * 掃除してから返し、1 (= 呼び手が rc のビットを立てる) を返す。 */
static int selftest_as_end(struct addrspace *as)
{
    u32 owner = as->owner, left = 0;
    int bad;
    paging_addrspace_destroy(as);
    bad = ledger_owner_pages(owner) != 0;
    (void)ledger_reclaim_owner(owner, &left);
    if (!ledger_owner_retire(owner)) bad = 1;
    return bad;
}

/* V1 自己診断用のプローブ。カーネル .bss (0x100000-0x1FFFFF, PDE 0) に置かれ、
 * 全 PD で共有される領域。SHM 等の実データを触らずに共有を検証できる。 */
static volatile u32 pd_selftest_probe;

int paging_pd_clone_selftest(void)
{
    struct addrspace as;
    u32 saved_cr3;
    u32 seen;
    int rc = 0;

    if (!pg_enabled) return 0; /* ページング無効なら検証対象外 */

    if (selftest_as_begin(&as, 1) != 0) return 1;

    /* live AS 中の汎用 USER 要求は、既存共有 USER 宛てでも拒否する。 */
    if (paging_set_page(MEM_SHM_BASE, MEM_SHM_BASE, PAGE_RW | PTE_USER) != -1 ||
        paging_map_range(MEM_SHM_BASE, MEM_SHM_BASE + PAGE_SIZE,
                         MEM_SHM_BASE, PAGE_RW | PTE_USER) != -1 ||
        paging_map_phys(MEM_SHM_BASE, MEM_SHM_BASE, 1, PAGE_RW | PTE_USER) != -1)
        rc |= 16;
    saved_cr3 = paging_current_cr3();

    /* master 経由で既知値を書く。 */
    pd_selftest_probe = 0x12345678UL;

    /* CR3 を新 PD に載せる。この行以降が実行できている時点で、カーネルの
     * コード (PDE 0) とスタック (PDE 0) が新 PD でも共有されている証拠。 */
    paging_load_cr3(as.pd_phys);

    /* 新 PD からカーネル帯域の同じ番地を読む — master が書いた値が見えるなら
     * 同一物理を指している (共有 OK)。 */
    seen = pd_selftest_probe;

    /* 新 PD 側から書き換える。 */
    pd_selftest_probe = 0xA5A5F00DUL;

    /* master に戻す。 */
    paging_load_cr3(saved_cr3);

    if (seen != 0x12345678UL) rc |= 2;                 /* 新 PD から共有が見えない */
    if (pd_selftest_probe != 0xA5A5F00DUL) rc |= 4;    /* 新 PD の書込が master に反映されない */

    if (selftest_as_end(&as)) rc |= 8;                 /* 試験用 owner のページが残った */
    return rc;
}

/* Shared device aliases cannot be promoted, rewritten or freed by an AS.
 * Exercise ordinary/keep/physical range APIs as well as the single-page API.
 * Preserve complete entries, including PFN and cache bits, on rejection.
 * rc bits: 1=missing PT, 2=AS creation, 4=keep, 64=single page,
 * 128=identity range, 256=physical range, 8=free, 16=entry changed,
 * 32=AS cleanup. */
int paging_map_user_keep_selftest(void)
{
    struct addrspace as;
    u32 va = PAGING_BOOT_MAP_SIZE - PAGE_SIZE;
    u32 pdi = va >> 22, pti = (va >> PAGE_SHIFT) % PTE_COUNT;
    u32 saved, master, app, expected;
    int rc = 0;
    if (!pg_enabled) return 0;
    if (pdi >= PAGING_PT_COUNT || !page_tables[pdi]) return 1;
    saved = page_tables[pdi][pti];
    master = page_directory[pdi];
    expected = va | PAGE_RW | PTE_PCD;
    page_tables[pdi][pti] = expected;
    if (selftest_as_begin(&as, 1)) {
        page_tables[pdi][pti] = saved;
        return 2;
    }
    app = ((u32 *)P2V(as.pd_phys))[pdi];
    if (paging_addrspace_map_user_keep(&as, va, va + PAGE_SIZE, PAGE_RW | PTE_USER) != -1) rc |= 4;
    if (paging_addrspace_map_user(&as, va, va, PAGE_RW | PTE_USER) != -1) rc |= 64;
    if (paging_addrspace_map_user_range(&as, va, va + PAGE_SIZE, PAGE_RW) != -1) rc |= 128;
    if (paging_addrspace_map_user_range_phys(&as, va, va + PAGE_SIZE, PAGE_SIZE, PAGE_RW) != -1) rc |= 256;
    if (paging_addrspace_free_user_range(&as, va, va + PAGE_SIZE)) rc |= 8;
    if (page_tables[pdi][pti] != expected || page_directory[pdi] != master ||
        ((u32 *)P2V(as.pd_phys))[pdi] != app) rc |= 16;
    if (selftest_as_end(&as)) rc |= 32;
    page_tables[pdi][pti] = saved;
    page_directory[pdi] = master;
    arch_mmu_flush_tlb();
    return rc;
}


/* ======================================================================== */
/*  paging_app_band_selftest — T2c の疎な高位アプリ帯                       */
/*  (票 docs/tasks/v3/TASK_T2_APPBAND.md §4、§5-1)                           */
/*                                                                          */
/*  1. 未使用の全 APP PDE/PT は空、master の高位 APP PDE も空                */
/*  2. 高位 stack ページの写像時にだけ私有 PT を確保する                     */
/*  3. ページ・PT・PD の破棄で owner と池の空きが元へ戻る                    */
/*  CR3 は載せ替えない。戻り値: 0=全通過、非0 はビットフラグ。              */
/* ======================================================================== */
int paging_app_band_selftest(void)
{
    struct addrspace as;
    u32 before = pgalloc_free_pages(), pa, k;
    int rc = 0;
    if (!pg_enabled) return 0;
    if (selftest_as_begin(&as, MEM_APP_BAND_MAX_PDES)) return 1;
    for (k = 0; k < MEM_APP_BAND_MAX_PDES; k++)
        if (as.app_pt_phys[k] || page_directory[APP_BAND_PDE + k]) rc |= 2;
    pa = pgalloc_alloc_phys(as.owner, 1);
    if (!pa) rc |= 4;
    else {
        u32 va = MEM_APP_STACK_TOP - PAGE_SIZE;
        if (paging_addrspace_map_user(&as, va, pa, PAGE_RW | PTE_USER)) {
            pgalloc_free_n_owner(as.owner, pa / PAGE_SIZE, 1);
            rc |= 8;
        } else if (paging_addrspace_free_user_range(&as, va, va + PAGE_SIZE) != 1) rc |= 16;
    }
    if (selftest_as_end(&as) || before != pgalloc_free_pages()) rc |= 32;
    return rc;
}

/* ======================================================================== */
/*  paging_memmap_selftest — 地図 (memmap.h) と実物 (PDE 0 の PTE) の照合     */
/*  (票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 3)                       */
/*                                                                          */
/*  静的検査 (STATIC_ASSERT / tools/gen_memmap.py) が見るのは「設計が矛盾    */
/*  していないか」で、ここが見るのは「実装が設計どおりか」。**別のこと**を   */
/*  見ている。2026-09-17 の穴は前者が無かったので設計に矛盾が入り、後者が    */
/*  無かったので実装のずれが残った。                                         */
/*                                                                          */
/*  期待値は全て memmap.h から引く ([C4])。番地は 1 つも直書きしない。        */
/*                                                                          */
/*  ブート直後 (kselftest_run_post_exec) に呼ぶ。旧 VRAM / フォント /        */
/*  Unicode / GFX BB の恒等 alias は CPL=3 起動後も supervisor のまま。      */
/*  CLIENT と Unicode は私有 lease VA に写し、共有 PT は昇格させない。      */
/* ======================================================================== */

#define MM_NP   0   /* not-present */
#define MM_RW   1   /* present + RW, supervisor */
#define MM_RO   2   /* present + RO, supervisor */
#define MM_ROU  3   /* present + RO + USER (KAPI 踏み台だけ) */
/* present + RW + **USER**。MM_RW と分けるまで、この検査は RW を見た時点で
 * MM_RW を返していて **USER の混入を見分けられなかった** (票 §1-3、
 * Codex 往復 3 B6)。DMA プールに USER が立つと CPL=3 のアプリが装置の
 * 記述子を書ける — 見えない差なので、ここで名前を分ける。 */
#define MM_RWU  4

/* 期待値。**固定番地 (スタックとそのガード) を浮動番地 (SHM) より先に見る。**
 * 逆にすると、SHM がスタックを飲んでいる今の状態を「期待どおり」と読んで
 * しまい、この自己診断そのものが穴を隠す。 */
static u8 memmap_want_at(u32 a, u32 tramp)
{
    if (tramp && a == tramp) return MM_ROU;     /* exec.c の KAPI 踏み台 */

    /* コンベンショナルメモリ */
    if (a <= MEM_NULL_GUARD_END) return MM_RO;  /* BDA 参照のため R/O */
    if (a < MEM_CONV_END) return MM_RW;
    if (a < MEM_BIOS_ROM_START) return MM_RW;   /* VRAM */
    if (a <= MEM_BIOS_ROM_END) return MM_RO;

    /* カーネル帯域 — ここは KHEAP_BASE 以降が全部浮動番地。
     * **浮動番地の規則は帯の中にだけ適用する。** 帯の境界 MEM_KERNEL_BAND_END
     * は固定番地なので、育ちすぎた SHM が境界を越えて「期待どおり」を
     * 主張してはいけない。そうしないと、予算を超えた配置で SHM 後方ガードが
     * SQLite 帯の先頭ページを not-present にしても、期待値が同じ壊れた定数から
     * 作られているせいで一致してしまう (= 自己診断が穴を隠す)。 */
    if (a >= KERNEL_LOAD_ADDR && a <= MEM_KERNEL_BAND_END) {
        if (a >= MEM_SHM_GUARD_LO && a < MEM_SHM_BASE) return MM_NP;
        if (a >= MEM_SHM_BASE && a <= MEM_SHM_END) {
            u32 page = (a - MEM_SHM_BASE) / PAGE_SIZE;
            return shm_audit_ro[page / 32] & (1UL << (page % 32)) ? MM_ROU : MM_RWU;
        }
        if (a >= MEM_SHM_GUARD_HI && a < MEM_SHM_GUARD_HI + PAGE_SIZE)
            return MM_NP;
        if (a < MEM_SHM_GUARD_LO) return MM_RW; /* 本体 + ヒープ + KAPI */
        return MM_NP;                           /* SHM 後方予約 */
    }

    /* SQLite 帯域 — 固定番地 (スタックとそのガード) を浮動番地 (SQLite の
     * 代替スタック末尾から決まる予約域) より **先に** 見る。逆にすると、
     * 予約域がスタックを飲んでいる状態を「期待どおり」と読んでしまう。 */
    if (a >= MEM_STACK_GUARD && a <= MEM_STACK_GUARD_END) return MM_NP;
    /* 台帳の backing は FIXED 型のときだけ present / supervisor / R/W
     * (T1-U1: USER は立たない)。それ以外は予約域 (NP) のまま。予約域の
     * 分岐より先に見る (DMA プールと同じ理由)。 */
    if (a >= MEM_LEDGER_META_BASE && a < MEM_LEDGER_META_END)
        return ledger_backing_mapped ? MM_RW : MM_NP;
    if (a >= MEM_KSTACK_BASE && a < MEM_SHELL_LOAD_ADDR) return MM_RW;
    /* **予約域を NP とする分岐より先に** DMA プールを見る (票 §1-3)。
     * 逆にすると、張り忘れて NP のままの池を「期待どおり」と読む。 */
    if (a >= MEM_DMA_POOL_BASE && a <= MEM_DMA_POOL_END) return MM_RW;
    if (a >= MEM_KERNEL_RESV_START && a <= MEM_KERNEL_RESV_END) return MM_NP;
    if (a < MEM_SHELL_LOAD_ADDR) return MM_RW;  /* SQLite code+BSS+代替スタック */

    /* シェル常駐帯域 */
    if (a < MEM_SHELL_GUARD) return MM_RW;
    if (a < MEM_SHELL_GUARD + PAGE_SIZE) return MM_NP;
    if (a >= MEM_FIXED_PAGING_END) return MM_NP;
    return MM_RW;                               /* スタック + heap + 固定PD/PT */
}

static u8 memmap_seen_at(u32 pte)
{
    if (!(pte & PTE_PRESENT)) return MM_NP;
    /* **USER を先に見る。** RW を見た時点で MM_RW を返していたころは、
     * 「present + RW + USER」が「present + RW」と同じに見えていた。 */
    if (pte & PTE_RW) return (pte & PTE_USER) ? MM_RWU : MM_RW;
    return (pte & PTE_USER) ? MM_ROU : MM_RO;
}

/* 食い違った区間 (start, end, want<<4|seen) を先頭から最大 MM_BAD_MAX 本。
 * static にしないのは kselftest_pass と同じ理由 (ホストから読む)。 */
u32 paging_memmap_bad[MM_BAD_MAX * 3];
u32 paging_memmap_bad_count;

/* 自己診断の変異専用 (paging.h の註)。PDE には触らない。 */
int paging_poke_user_bit(u32 virt, int set_user)
{
    u32 idx;

    if (!pg_enabled || !page_tables[0]) return -1;
    if (virt >= (u32)PTE_COUNT * PAGE_SIZE) return -1;
    idx = virt / PAGE_SIZE;
    if (set_user) page_tables[0][idx] |= (u32)PTE_USER;
    else          page_tables[0][idx] &= ~(u32)PTE_USER;
    arch_mmu_flush_tlb();
    return 0;
}

int paging_memmap_selftest(u32 tramp_page)
{
    u32 i, j, a;
    u8 want, seen;
    int runs = 0;

    if (paging_v86_session_open()) return -1;
    paging_memmap_bad_count = 0;
    if (!pg_enabled || !page_tables[0] || !fixed_paging_valid() ||
        V2P(page_directory) != MEM_FIXED_PD_BASE) return -1;

    i = 0;
    while (i < PTE_COUNT) {
        a = i * PAGE_SIZE;
        want = memmap_want_at(a, tramp_page);
        seen = memmap_seen_at(page_tables[0][i]);
        if (want == seen) { i++; continue; }
        /* 同じ (期待, 実物) の組が続くあいだを 1 本の区間にまとめる */
        for (j = i + 1; j < PTE_COUNT; j++) {
            a = j * PAGE_SIZE;
            if (memmap_want_at(a, tramp_page) != want) break;
            if (memmap_seen_at(page_tables[0][j]) != seen) break;
        }
        if (runs < MM_BAD_MAX) {
            paging_memmap_bad[runs * 3 + 0] = i * PAGE_SIZE;
            paging_memmap_bad[runs * 3 + 1] = j * PAGE_SIZE - 1;
            paging_memmap_bad[runs * 3 + 2] = ((u32)want << 4) | (u32)seen;
        }
        runs++;
        i = j;
    }
    paging_memmap_bad_count = (u32)runs;
    return runs;
}

/* T2b: page-table transactions confined to the private lease window. */
/* Root predicate mirrors exec/lease.c:context(); lease_context adds PT checks. */
static int lease_root_context(const struct addrspace *as)
{
    return as && as->pd_phys && !(as->pd_phys % PAGE_SIZE) && as->lease_pt_phys[0] &&
        pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner) &&
        (paging_current_cr3() == paging_kernel_pd_phys() ||
         paging_current_cr3() == as->pd_phys) && !kctx_irq_depth && !kctx_exc_depth;
}

static int lease_context(const struct addrspace *as)
{
    u32 i, *pd;
    if (!lease_root_context(as)) return 0;
    /* T2c supervisor identity aliases: never install the master to walk.
     * Validate managed frames and exact PDE rights before dereferencing PTs. */
    pd = P2V(as->pd_phys);
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) {
        u32 phys = as->lease_pt_phys[i], d = pd[(MEM_LEASE_BASE >> 22) + i];
        if (!phys) { if (d) return 0; continue; }
        if (phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||
            (d & ~(PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW | PTE_USER))
            return 0;
    }
    return 1;
}

int paging_addrspace_create_lease(struct addrspace *as, u32 owner)
{
    u32 phys, i;
    u32 *pd;
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth) return -1;
    if (paging_addrspace_create(as, owner)) return -1;
    phys = pgalloc_alloc_phys(owner, 1);
    if (!phys) { paging_addrspace_destroy(as); return -1; }
    for (i = 0; i < PTE_COUNT; i++) ((u32 *)P2V(phys))[i] = 0;
    pd = P2V(as->pd_phys);
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) pd[(MEM_LEASE_BASE >> 22) + i] = 0;
    as->lease_pt_phys[0] = phys;
    pd[MEM_LEASE_BASE >> 22] = phys | PAGE_RW | PTE_USER;
    return 0;
}

u32 paging_lease_pte(const struct addrspace *as, u32 va)
{
    u32 phys, d, index;
    if (!lease_root_context(as) || va < MEM_LEASE_BASE || va >= MEM_LEASE_END) return 0;
    index = (va - MEM_LEASE_BASE) >> 22;
    phys = as->lease_pt_phys[index];
    if (!phys || phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner)) return 0;
    d = ((u32 *)P2V(as->pd_phys))[(MEM_LEASE_BASE >> 22) + index];
    if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW | PTE_USER)) return 0;
    return ((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT];
}

int paging_lease_map(struct addrspace *as, const struct lease_mapping *maps, u32 n)
{
    u32 pending[MEM_LEASE_MAX_PDES] = {0};
    u32 i, j, k, va, phys, *pd;
    unsigned int saved;
    int rc = -1;
    if (!lease_context(as) || !maps || !n || n > MEM_LEASE_MAX) return -1;
    pd = P2V(as->pd_phys);
    for (i = 0; i < n; i++) {
        const struct lease_mapping *m = &maps[i];
        if (m->slot >= MEM_LEASE_MAX || as->leases[m->slot].token || !m->token ||
            m->sid >= LEDGER_MAX_SURFACES ||
            ledger_surfaces[m->sid].gen != m->generation ||
            ledger_surfaces[m->sid].first * PAGE_SIZE != m->phys ||
            ledger_surfaces[m->sid].npages != m->npages ||
            ledger_surfaces[m->sid].closing ||
            (m->flags & (PTE_PCD | PTE_PWT)) !=
                (ledger_surfaces[m->sid].cache == LEDGER_CACHE_UC ? PTE_PCD : 0) ||
            ledger_surfaces[m->sid].perm_max == LEDGER_PERM_NONE ||
            ((m->flags & PTE_RW) && ledger_surfaces[m->sid].perm_max != LEDGER_PERM_RW)) goto fail;
        if (m->base < MEM_LEASE_BASE || m->base >= MEM_LEASE_END ||
            (m->base % PAGE_SIZE) || (m->phys % PAGE_SIZE) || !m->npages ||
            m->npages > (MEM_LEASE_END - m->base) / PAGE_SIZE ||
            m->npages > PHYSMEM_MAX_PFN - m->phys / PAGE_SIZE ||
            (m->flags & ~(PTE_PRESENT | PTE_USER | PTE_RW | PTE_PCD | PTE_PWT)) ||
            (m->flags & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) goto fail;
        for (j = 0; j < i; j++)
            if (m->slot == maps[j].slot ||
                (m->base < maps[j].base + maps[j].npages * PAGE_SIZE &&
                 maps[j].base < m->base + m->npages * PAGE_SIZE)) goto fail;
        for (j = 0; j < m->npages; j++) {
            va = m->base + j * PAGE_SIZE;
            k = (va - MEM_LEASE_BASE) >> 22;
            phys = as->lease_pt_phys[k];
            if (phys) {
                if (!pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||
                    (pd[(MEM_LEASE_BASE >> 22) + k] & ~0xfffUL) != phys ||
                    (((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] & PTE_PRESENT)) goto fail;
            } else if (!pending[k]) {
                pending[k] = pgalloc_alloc_phys(as->owner, 1);
                if (!pending[k]) { rc = -2; goto fail; }
                for (u32 z = 0; z < PTE_COUNT; z++) ((u32 *)P2V(pending[k]))[z] = 0;
            }
        }
    }
    saved = irq_save();
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k]) {
        as->lease_pt_phys[k] = pending[k];
        pd[(MEM_LEASE_BASE >> 22) + k] = pending[k] | PAGE_RW | PTE_USER;
    }
    for (i = 0; i < n; i++) for (j = 0; j < maps[i].npages; j++) {
        va = maps[i].base + j * PAGE_SIZE;
        phys = as->lease_pt_phys[(va - MEM_LEASE_BASE) >> 22];
        ((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] =
            (maps[i].phys + j * PAGE_SIZE) | maps[i].flags;
    }
    for (i = 0; i < n; i++) {
        struct as_lease *l = &as->leases[maps[i].slot];
        l->token = maps[i].token;
        l->sid = maps[i].sid;
        l->generation = maps[i].generation;
        l->base = maps[i].base;
        l->npages = maps[i].npages;
        l->flags = maps[i].flags;
        ledger_surfaces[l->sid].lease_count++;
    }
    /* No callback/AS switch during preparation; inactive AS reloads on resume. */
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    irq_restore(saved);
    return 0;
fail:
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k])
        pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);
    return rc;
}

int paging_lease_unmap(struct addrspace *as, u32 base, u32 npages)
{
    u32 j, k, *pt, *pd;
    unsigned int saved;
    if (!lease_context(as) || base < MEM_LEASE_BASE || base >= MEM_LEASE_END ||
        base % PAGE_SIZE || !npages || npages > (MEM_LEASE_END - base) / PAGE_SIZE) return -1;
    for (j = 0; j < npages; j++) {
        u32 va = base + j * PAGE_SIZE;
        k = (va - MEM_LEASE_BASE) >> 22;
        if (!as->lease_pt_phys[k] ||
            !pgalloc_page_owned(as->lease_pt_phys[k] / PAGE_SIZE, as->owner) ||
            (((u32 *)P2V(as->pd_phys))[(MEM_LEASE_BASE >> 22) + k] & ~0xfffUL) !=
            as->lease_pt_phys[k]) return -1;
    }
    saved = irq_save();
    for (j = 0; j < npages; j++) {
        u32 va = base + j * PAGE_SIZE;
        k = (va - MEM_LEASE_BASE) >> 22;
        if (as->lease_pt_phys[k])
            ((u32 *)P2V(as->lease_pt_phys[k]))[(va >> PAGE_SHIFT) % PTE_COUNT] = 0;
    }
    pd = P2V(as->pd_phys);
    /* Remove empty additional PDEs before TLB synchronization and free. */
    for (k = 1; k < MEM_LEASE_MAX_PDES; k++) if (as->lease_pt_phys[k]) {
        pt = P2V(as->lease_pt_phys[k]);
        for (j = 0; j < PTE_COUNT && !pt[j]; j++) {}
        if (j == PTE_COUNT) pd[(MEM_LEASE_BASE >> 22) + k] = 0;
    }
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    for (k = 1; k < MEM_LEASE_MAX_PDES; k++)
        if (!as->appmem_poisoned && as->lease_pt_phys[k] &&
            !pd[(MEM_LEASE_BASE >> 22) + k]) {
            if (!pgalloc_free_n_owner(as->owner, as->lease_pt_phys[k] / PAGE_SIZE, 1)) {
                paging_addrspace_poison(as);
                continue;
            }
            as->lease_pt_phys[k] = 0;
        }
    irq_restore(saved);
    return 0;
}

/* T2e e10c (a). Return -1 only for the explicit V86 skip. Counters are
 * accumulated by the lifecycle caller, so deliberate test corruptions do not
 * pollute guest observation. A/D bits are hardware-owned, never compared. */
int paging_master_audit(u32 tramp)
{
    int bad = 0;
    if (paging_v86_session_open()) return -1;
    if (!pg_enabled || !fixed_paging_valid()) return 1;
    for (u32 di = 0; di < PDE_COUNT; di++) {
        u32 d = page_directory[di];
        if (di >= APP_BAND_PDE && di < (MEM_LEASE_END >> 22)) {
            if (d) bad++;
            continue;
        }
        /* Registered pointers guard the dereference even for a corrupt PDE. */
        if (d & PTE_PRESENT) {
            if (!page_tables[di] || (d & ~(u32)(PAGE_SIZE - 1)) != V2P(page_tables[di]) ||
                (d & (PAGE_RW | PTE_PS | PTE_PCD | PTE_PWT)) != PAGE_RW) {
                bad++; continue;
            }
        } else if (!page_tables[di] && di * PTE_COUNT >= pgalloc_limit_pfn()) continue;
        for (u32 ti = 0; ti < PTE_COUNT; ti++) {
            u32 p = di * PTE_COUNT + ti, a = p * PAGE_SIZE;
            u32 e = (d & PTE_PRESENT) ? page_tables[di][ti] : 0;
            u32 want = 0;
            if (di == 0) {
                u8 kind = memmap_want_at(a, tramp);
                if (kind != MM_NP) want = PTE_PRESENT;
                if (kind == MM_RW || kind == MM_RWU) want |= PTE_RW;
                if (kind == MM_RWU || kind == MM_ROU) want |= PTE_USER;
                if (PC98_NATIVE_VRAM(a)) want |= PTE_PCD;
            } else if ((p < boot_identity_end || pgalloc_audit_ram(p)) &&
                       !(a >= MEM_SYSTEM_SPACE_BASE && a < MEM_SYSTEM_SPACE_END)) want = PAGE_RW;
            for (u32 ri = 0; ri < ledger_region_count; ri++) {
                const struct ledger_region *r = &ledger_regions[ri];
                if (r->type != LEDGER_R_DEVICE || p < r->first || p >= r->end) continue;
                /* A decode reservation can exceed the actual mapped window. */
                want = 0;
                for (u32 k = 0; k < LEDGER_MAX_RESOURCES; k++) {
                    const struct ledger_resource *res = &ledger_resources[k];
                    if ((r->res_mask & (1U << k)) && p >= res->map_first && p < res->map_end)
                        want = PAGE_RW | PTE_PCD;
                }
            }
            if (!(want & PTE_PRESENT)) { if (e & PTE_PRESENT) bad++; continue; }
            if ((e & (~(u32)(PAGE_SIZE - 1) | PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT)) !=
                (a | want)) bad++;
            if ((want & PTE_USER) && !(d & PTE_USER)) bad++;
        }
    }
    return bad;
}

/* (b), private application and shared PDEs; lease pages are checked by lease.c. */
int paging_as_audit(const struct addrspace *as, int (*shared_ro)(u32, u32))
{
    u32 *pd;
    if (!as || !as->pd_phys || as->pd_phys % PAGE_SIZE ||
        !pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner)) return 1;
    pd = P2V(as->pd_phys);
    for (u32 di = 0; di < PDE_COUNT; di++) {
        u32 d = pd[di];
        if (di < APP_BAND_PDE || di >= (MEM_LEASE_END >> 22)) {
            if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (page_directory[di] & ~(PTE_ACCESSED | PTE_DIRTY))) return 1;
        } else if (di < (MEM_LEASE_BASE >> 22)) {
            if (di >= APP_BAND_PDE + MEM_APP_BAND_MAX_PDES) {
                if (d) return 1;
                continue;
            }
            u32 pt_phys = as->app_pt_phys[di - APP_BAND_PDE];
            if (!pt_phys) { if (d) return 1; continue; }
            if (pt_phys % PAGE_SIZE || !pgalloc_page_owned(pt_phys / PAGE_SIZE, as->owner) ||
                (d & ~(PTE_ACCESSED | PTE_DIRTY)) != (pt_phys | PAGE_RW | PTE_USER)) return 1;
            u32 *pt = P2V(pt_phys);
            for (u32 ti = 0; ti < PTE_COUNT; ti++) {
                u32 e = pt[ti], va = (di * PTE_COUNT + ti) * PAGE_SIZE;
                if (!(e & PTE_PRESENT)) continue;
                if (!(e & PTE_USER) || (e & (PTE_PCD | PTE_PWT))) return 1;
                if (!pgalloc_page_owned(e >> PAGE_SHIFT, as->owner) &&
                    !(va >= MEM_SHLIB_BASE && va < MEM_SHLIB_END && !(e & PTE_RW) &&
                      shared_ro && shared_ro(va, e & ~(u32)(PAGE_SIZE - 1)))) return 1;
            }
        }
    }
    return 0;
}
