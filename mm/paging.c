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
/*    0x01000 - 0x9FFFF : R/W  (フォント/KCG scratch/GFX, ブート後に再利用)      */
/*    0xA0000 - 0xEFFFF : R/W。TVRAM / B,R,G / E は PCD (UC)。             */
/*    CG 窓と ROM は WB (PCD なし)。ROM の書込み保護は下記。                 */
/*    0xF0000 - 0xFFFFF : R/O  (BIOS ROM)                                   */
/*                                                                          */
/*  [拡張メモリ] 番地の正典は include/memmap.h、実値は build/out/MEMMAP.md   */
/*    0x100000 -          : R/W  (カーネル code+data+BSS → heap+KAPI+SHM → SLACK */
/*                                SQLite owner の stack 直下は動的 NP guard。 */
/*                                __bss_end 由来で浮く)                      */
/*    0x3E0000 - 0x3E0FFF : NP   (固定 PD/PT 枠の guard)                     */
/*    0x3E1000 - 0x3FAFFF : R/W  (固定 PD/PT 26 枚: 使用 11 + 予備 15)       */
/*    0x3FB000 - 0x3FBFFF : NP   (カーネルスタックガード)                    */
/*    0x3FC000 - 0x3FFFFF : R/W  (カーネルスタック, 16KB)                    */
/*    0x400000 - 0x4FFFFF : R/W  (シェル常駐帯域, ガード付き)                */
/*    0x500000 - mem_end  : R/W  (物理ページの池。DMA プール 0x7F0000 を含む) */
/*    mem_end  - 0xFFFFFF : NP   (未実装メモリ)                             */
/*                                                                          */
/*  [16MB 超のデバイス窓帯 (H3b)]                                            */
/*    0x1000000-0x1FFFFFF : NP   (実 RAM 無し。既定は全ページ Not-Present)   */
/*      paging_map_phys() でドライバが窓だけを張る。現在の利用者は Xe10      */
/*      内蔵 Cirrus のリニア窓 0x1000000〜0x11FFFFF (2MB)。                  */
/* ======================================================================== */

#include "paging.h"

#include "io.h"
#include "../kernel/v86/v86_mem.h"
#include "cpu.h"      /* arch_mmu_* (実装は arch/$(ARCH)/arch_cpu.h) */
#include "memmap.h"
#include "pgalloc.h"
#include "../include/platform_memory.h"

STATIC_ASSERT(MEM_BIOS_ROM_END + 1 == KERNEL_LOAD_ADDR, bios_rom_ends_at_kernel);

static int platform_span_has(const struct platform_memory_span *s, u32 a)
{
    return a >= s->first && a < s->end;
}

static u32 native_cache_bits(u32 a)
{
    for (u32 i = 0; i < PLATFORM_MEMORY_NATIVE_UC_COUNT; i++)
        if (platform_span_has(&platform_memory_attributes.native_uc[i], a))
            return PTE_PCD;
    return 0;
}

u32 paging_sqlite_stack_guard;

/* カーネルスタック帯のレイアウト不変条件。
 * ガードページはスタック直下に隣接する。ずれると paging_init の R/W 強制や
 * ガード設定が意図しないページに掛かる。
 * スタックは浮動番地のカーネル帯域の外 (固定 PD/PT 枠の後) に置く。
 * 同じ帯に置くと KHEAP_BASE 以降が育ったぶん必ずぶつかる (決裁 D1)。 */
STATIC_ASSERT(MEM_STACK_GUARD_END + 1 == MEM_KSTACK_BASE,
              kstack_guard_adjacent);
STATIC_ASSERT((MEM_STACK_GUARD & (PAGE_SIZE - 1)) == 0,
              kstack_guard_page_aligned);
/* 浮動番地の帯 (カーネル帯域) の外に居ること = D1 の要点 */
STATIC_ASSERT(MEM_STACK_GUARD > MEM_KERNEL_BAND_END, kstack_outside_kernel_band);
/* シェル常駐帯域には食い込まない */
STATIC_ASSERT(MEM_KSTACK_TOP < MEM_SHELL_LOAD_ADDR, kstack_below_shell_band);

/* ------------------------------------------------------------------------ */
/*  帯どうしの重なりと範囲の逆転 (票 TASK_KSTACK_USER §4 の 1、決裁 D2)      */
/*                                                                          */
/*  **実値 (KHEAP_BASE) では書けない。** KHEAP_BASE は `(u32)&__bss_end`     */
/*  由来で C の整数定数式ではなく、GCC はファイルスコープの可変長配列と見て  */
/*  "variably modified at file scope" で落とす — **条件が真でも落ちる**。    */
/*  (同じ理由で 2026-09-17 まで mm/shm.c:29,33 の表明 2 本が黙って       */
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
STATIC_ASSERT(APP_BAND_PDE == PDE_INDEX(MEM_APP_BAND_BASE), app_band_pde_matches);
STATIC_ASSERT(APP_BAND_PDE < PAGING_PT_COUNT, app_band_pde_in_range);
STATIC_ASSERT((MEM_SHLIB_BASE >> PDE_SHIFT) == APP_BAND_PDE, shlib_base_in_app_band);
STATIC_ASSERT((MEM_EXEC_LOAD_ADDR >> PDE_SHIFT) == APP_BAND_PDE, exec_load_in_app_band);
STATIC_ASSERT(((MEM_APP_BAND_TOP - 1) >> PDE_SHIFT) == APP_BAND_PDE, app_band_top_in_pde);
STATIC_ASSERT(MEM_SHLIB_END <= MEM_EXEC_LOAD_ADDR, shlib_band_below_exec);

/* 可変 PDE 化 (票 docs/archive/v3/APP_BAND_PDE.md)。帯は先頭 PDE から
 * 連続 MEM_APP_BAND_MAX_PDES 枚まで伸びうる。次の 5 つが崩れると、
 * アプリ PD がカーネル帯やデバイス窓を差し替えて黙って壊れる。 */
STATIC_ASSERT(MEM_APP_BAND_MAX_PDES >= 1, app_band_at_least_one_pde);
STATIC_ASSERT(MEM_APP_BAND_PDE_SIZE == (u32)PTE_COUNT * PAGE_SIZE,
              app_band_pde_size_is_one_pde);
STATIC_ASSERT(APP_BAND_PDE + MEM_APP_BAND_MAX_PDES <= LEASE_PDE, app_band_before_lease);
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
              MEM_FIXED_PAGING_END - MEM_FIXED_PAGING_BASE == 11 * PAGE_SIZE,
              fixed_paging_eleven_pages);
STATIC_ASSERT(((MEM_FIXED_PAGING_BASE | MEM_FIXED_PD_BASE |
                MEM_FIXED_BOOT_PT_BASE | MEM_FIXED_APERTURE_PT_BASE |
                MEM_FIXED_MMIO_TOP_PT_BASE |
                MEM_FIXED_PAGING_END | MEM_SHELL_HEAP_BASE | MEM_SHELL_HEAP_END |
                (MEM_SHELL_BAND_END + 1)) & (PAGE_SIZE - 1)) == 0,
              fixed_paging_page_aligned);
STATIC_ASSERT(MEM_FIXED_PD_BASE == MEM_FIXED_PAGING_BASE &&
              MEM_FIXED_BOOT_PT_BASE == MEM_FIXED_PD_BASE + PAGE_SIZE &&
              MEM_FIXED_APERTURE_PT_BASE == MEM_FIXED_BOOT_PT_BASE +
                  PAGING_BOOT_PT_COUNT * PAGE_SIZE &&
              MEM_FIXED_MMIO_TOP_PT_BASE == MEM_FIXED_APERTURE_PT_BASE + PAGE_SIZE &&
              MEM_FIXED_PAGING_END == MEM_FIXED_MMIO_TOP_PT_BASE + PAGE_SIZE,
              fixed_paging_contiguous);
/* シンボル由来の metadata の境界は os32.ld が見る。 */
STATIC_ASSERT(MEM_FIXED_PAGING_GUARD_BASE + PAGE_SIZE == MEM_FIXED_PAGING_BASE &&
              MEM_FIXED_PAGING_FRAME_END - MEM_FIXED_PAGING_BASE == 26 * PAGE_SIZE &&
              MEM_FIXED_PAGING_FRAME_END - MEM_FIXED_PAGING_USED_END == 15 * PAGE_SIZE &&
              MEM_FIXED_PAGING_FRAME_END == MEM_STACK_GUARD,
              fixed_paging_frame);
STATIC_ASSERT(MEM_KSTACK_BASE + MEM_KSTACK_SIZE == MEM_SHELL_LOAD_ADDR &&
              MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE &&
              MEM_SHELL_HEAP_END == MEM_SHELL_GUARD &&
              MEM_SHELL_HEAP_SIZE == MEM_SHELL_EXEC_HEAP_SIZE &&
              MEM_SHELL_GUARD + MEM_GUARD_SIZE + MEM_SHELL_STACK_SIZE == MEM_SHELL_STACK_TOP,
              shell_band_boundaries);

/* OS が割り当てるデバイス窓の帯 (memmap.h MEM_DEVICE_APERTURE_*) の先頭 4MB を
 * 覆う PT。**静的に 1 枚** 持つ (2026-09-29、Cirrus のリニア窓 FE000000h)。
 * 新しい PDE を足せるのは live AS が 0 の間だけ (prepare_tables) だが、窓を
 * 張る gfx の init は exec の後にも走る。paging_init で PDE を present にして
 * おけば、以後のアプリ PD は PDE ごとこの PT を写し、paging_map_phys は PTE を
 * 書くだけで済む (PDE 4-7 の 16MB〜32MB と同じやり方)。legacy 経路 (8MB) には
 * 動的 PT の置き場が無いので、動的確保にはしない。 */
#define PAGING_MMIO_TOP_PDI (MEM_MMIO_TOP_PT_BASE / MEM_DEVICE_APERTURE_PDE_SIZE)
#define PAGING_APERTURE_PDI (MEM_DEVICE_APERTURE_BASE / MEM_DEVICE_APERTURE_PDE_SIZE)
STATIC_ASSERT(MEM_DEVICE_APERTURE_PDE_SIZE == (u32)PTE_COUNT * PAGE_SIZE,
              aperture_pt_covers_one_pde);
STATIC_ASSERT((MEM_DEVICE_APERTURE_BASE % MEM_DEVICE_APERTURE_PDE_SIZE) == 0,
              aperture_base_pde_aligned);
STATIC_ASSERT(MEM_DEVICE_APERTURE_BASE + MEM_DEVICE_APERTURE_PDE_SIZE <=
              MEM_DEVICE_APERTURE_END, aperture_pt_inside_band);
STATIC_ASSERT(PAGING_APERTURE_PDI >= PAGING_BOOT_PT_COUNT &&
              PAGING_APERTURE_PDI < PAGING_PT_COUNT, aperture_pdi_static_free);


STATIC_ASSERT((MEM_MMIO_TOP_PT_BASE % MEM_DEVICE_APERTURE_PDE_SIZE) == 0 &&
              PAGING_MMIO_TOP_PDI == PAGING_PT_COUNT - 1, mmio_top_last_pde);

static u32 *page_directory;          /* アライン済みポインタ */
static u32 *page_tables[PAGING_PT_COUNT];

static int pg_enabled = 0;
static u32 live_addrspaces;
u32 paging_live_addrspaces(void) { return live_addrspaces; }
static int boot_user_shared_done;
/* Intended SHM permissions, recorded by the only post-boot RW-changing API.
 * Auditing must not infer the expected RW bit from the PTE being checked. */
static u32 shm_audit_ro[BITMAP_WORDS(MEM_SHM_SIZE / PAGE_SIZE)];
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

STATIC_ASSERT((MEM_SHELL_BAND_END >> PDE_SHIFT) < APP_BAND_PDE, shell_outside_private_pt);

#include "paging_boot.inc"

#include "paging_map.inc"

#include "paging_addrspace.inc"

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
    u32 pdi = PDE_INDEX(va), pti = (va >> PAGE_SHIFT) % PTE_COUNT;
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
/*  KCG scratch / GFX BB の恒等 alias は CPL=3 起動後も supervisor のまま。      */
/*  CLIENT は私有 lease VA に写し、共有 PT は昇格させない。      */
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
    if (paging_sqlite_stack_guard && a == paging_sqlite_stack_guard) return MM_NP;
    if (tramp && a == tramp) return MM_ROU;     /* exec.c の KAPI 踏み台 */

    /* コンベンショナルメモリ */
    if (a <= MEM_NULL_GUARD_END) return MM_NP;
    if (a < MEM_CONV_END) return MM_RW;
    if (a < platform_memory_attributes.bios_rom.first) return MM_RW;   /* VRAM */
    if (a < platform_memory_attributes.bios_rom.end) return MM_RO;

    if (a >= MEM_FIXED_PAGING_GUARD_BASE && a < MEM_FIXED_PAGING_BASE) return MM_NP;
    if (a >= MEM_FIXED_PAGING_BASE && a < MEM_FIXED_PAGING_FRAME_END) return MM_RW;

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
            return shm_audit_ro[page / BITS_PER_U32] & (1UL << (page % BITS_PER_U32)) ? MM_ROU : MM_RWU;
        }
        if (a >= MEM_SHM_GUARD_HI && a < MEM_SHM_GUARD_HI + PAGE_SIZE)
            return MM_NP;
        if (a < MEM_SHM_GUARD_LO) return MM_RW; /* 本体 + ヒープ + KAPI */
        return MM_RW;                           /* SLACK */
    }

    /* 固定 PD/PT 枠の後: カーネルスタックとそのガード。 */
    if (a >= MEM_STACK_GUARD && a <= MEM_STACK_GUARD_END) return MM_NP;
    /* 台帳の backing は present / supervisor / R/W (T1-U1: USER は立たない)。 */
    if (a >= MEM_LEDGER_META_BASE && a < MEM_LEDGER_META_END)
        return MM_RW;
    if (a >= MEM_KSTACK_BASE && a < MEM_SHELL_LOAD_ADDR) return MM_RW;
    if (a < MEM_SHELL_LOAD_ADDR) return MM_RW;

    /* DMA を含め、実 RAM の上端より先は not-present。 */
    if (a / PAGE_SIZE >= boot_identity_end) return MM_NP;
    /* DMA プールは USER なしの RW (票 §1-3)。 */
    if (a >= MEM_DMA_POOL_BASE && a <= MEM_DMA_POOL_END) return MM_RW;

    /* シェル常駐帯域 */
    if (a < MEM_SHELL_GUARD) return MM_RW;
    if (a < MEM_SHELL_GUARD + PAGE_SIZE) return MM_NP;
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
u32 paging_memmap_bad[MM_BAD_MAX * MM_BAD_WORDS];
u32 paging_memmap_bad_count;

/* 自己診断の変異専用 (paging.h の註)。PDE には触らない。 */
int paging_poke_user_bit(u32 virt, int set_user)
{
    u32 pdi = virt / (PTE_COUNT * PAGE_SIZE);
    u32 idx = (virt / PAGE_SIZE) % PTE_COUNT;

    if (!pg_enabled || pdi >= PAGING_PT_COUNT || !page_tables[pdi]) return -1;
    if (set_user) page_tables[pdi][idx] |= (u32)PTE_USER;
    else          page_tables[pdi][idx] &= ~(u32)PTE_USER;
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
    while (i < (MEM_DMA_POOL_END + 1) / PAGE_SIZE) {
        a = i * PAGE_SIZE;
        want = memmap_want_at(a, tramp_page);
        seen = memmap_seen_at(page_tables[i / PTE_COUNT][i % PTE_COUNT]);
        if (want == seen) { i++; continue; }
        /* 同じ (期待, 実物) の組が続くあいだを 1 本の区間にまとめる */
        for (j = i + 1; j < (MEM_DMA_POOL_END + 1) / PAGE_SIZE; j++) {
            a = j * PAGE_SIZE;
            if (memmap_want_at(a, tramp_page) != want) break;
            if (memmap_seen_at(page_tables[j / PTE_COUNT][j % PTE_COUNT]) != seen) break;
        }
        if (runs < MM_BAD_MAX) {
            paging_memmap_bad[runs * MM_BAD_WORDS + MM_BAD_START] = i * PAGE_SIZE;
            paging_memmap_bad[runs * MM_BAD_WORDS + MM_BAD_END] = j * PAGE_SIZE - 1;
            paging_memmap_bad[runs * MM_BAD_WORDS + MM_BAD_KINDS] = ((u32)want << MM_BAD_KIND_SHIFT) | (u32)seen;
        }
        runs++;
        i = j;
    }
    paging_memmap_bad_count = (u32)runs;
    return runs;
}

#include "paging_lease.inc"

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
        if (di >= APP_BAND_PDE && di < (MEM_LEASE_END >> PDE_SHIFT)) {
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
            if (a <= MEM_SHELL_BAND_END) {
                u8 kind = memmap_want_at(a, tramp);
                if (kind != MM_NP) want = PTE_PRESENT;
                if (kind == MM_RW || kind == MM_RWU) want |= PTE_RW;
                if (kind == MM_RWU || kind == MM_ROU) want |= PTE_USER;
                want |= native_cache_bits(a);
            } else if ((p < boot_identity_end || pgalloc_audit_ram(p)) &&
                       !(a >= platform_memory_geometry.system_first &&
                         a < platform_memory_geometry.system_end)) want = PAGE_RW;
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
            if (paging_sqlite_stack_guard && a == paging_sqlite_stack_guard) want = 0;
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
    if (!fixed_paging_valid() || !boot_layout_valid()) return 1;
    pd = P2V(as->pd_phys);
    for (u32 di = 0; di < PDE_COUNT; di++) {
        u32 d = pd[di];
        if (di < APP_BAND_PDE || di >= (MEM_LEASE_END >> PDE_SHIFT)) {
            if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (page_directory[di] & ~(PTE_ACCESSED | PTE_DIRTY))) return 1;
        } else if (di < LEASE_PDE) {
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
