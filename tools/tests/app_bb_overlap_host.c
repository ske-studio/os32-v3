/* ========================================================================
 *  app_bb_overlap_host.c — 共有 BB の写像がアプリ帯の私有ページを潰さないこと
 *
 *  実行: python3 -B tools/tests/test_app_bb_overlap.py [--mutate]
 *
 *  2026-09-30 の後退 (8MB + PEGC、PM が NP21/W で観測): CPL=3 アプリを 1 本
 *  起動して終了するたびに pgalloc の used_pages が 74 ずつ増え、3 本目で
 *  NOMEM・v86 -t は 159 ページの連続が取れずに失敗した。原因は exec が PEGC
 *  の BB [0x7B5000, 0x800000) をアプリ PD へ恒等で写すとき、アプリ帯の中の
 *  私有ページ (スタック 64 + exec_heap の上端 10 = 74) の PTE を上書きして
 *  いたこと (exec/exec.c exec_map_shared_bb の注釈)。
 *
 *  実物の kernel/paging.c + kernel/pgalloc.c + kernel/physmem.c を ILP32 で
 *  そのまま組み、exec/exec.c から切り出した本物の app_map_region /
 *  exec_map_shared_bb / exec_teardown_app (test_app_bb_overlap.py が生成する
 *  exec_bb_overlap.inc) で「起動 → 終了」を 10 回まわす。
 *
 *  見るもの:
 *    A. 8MB + PEGC の BB: 起動中のスタック / exec_heap の PTE が BB の物理を
 *       指さない。帯と重なった 75 ページは写さない (exec_bb_clipped_pages)
 *    B. 10 回の起動と終了で used_pages が起動前の値へ毎回戻る
 *    C. その後で V86 の backing (V86_BACKING_PAGES = 159 の連続) が取れる
 *    D. 帯の外の BB (9801 の 0x6A000 / 12MB 機の帯より上) は従来どおり
 *       USER で写り、切り詰めない
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3;
#include "paging_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *text, u32 len)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL: " #x); die(1); } } while (0)
#include "pgalloc_host_source.c"
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* exec_teardown_app が引く。この試験は shlib を載せない。 */
#include "appslot.h"
void shlib_addrspace_detach(struct addrspace *as);
void shlib_addrspace_detach(struct addrspace *as) { (void)as; }

/* exec/exec.c から切り出した本物のテキスト (test_app_bb_overlap.py が生成)。 */
#include "exec_bb_overlap.inc"

#include "v86_mem.h"         /* V86_BACKING_PAGES */

/* 8MB + PEGC の実測値 (PM 2026-09-30): sys_top_reserved = 0x4B000 */
#define T_RAM_KB      8192UL
#define T_RAM_END     0x800000UL
#define T_BB_SIZE     0x4B000UL
#define T_BB_BASE     (T_RAM_END - T_BB_SIZE)       /* 0x7B5000 */
#define T_BAND_TOP    MEM_APP_BAND_TOP              /* 1 枚 = 0x800000 */
#define T_CODE_END    0x510000UL
#define T_SBRK_END    (T_CODE_END + MEM_EXEC_SBRK_MIN)
#define T_STACK_BOT   (T_BAND_TOP - RING3_USTACK_SIZE)
#define T_HEAP_TOP    (T_STACK_BOT - PAGE_SIZE)     /* ガード直下 */
#define T_HEAP_SIZE   0x100000UL
#define T_HEAP_BASE   (T_HEAP_TOP - T_HEAP_SIZE)
#define T_ROUNDS      10

static u32 app_pte(const struct addrspace *as, u32 va)
{
    u32 pdi = va >> 22;
    const u32 *pt;
    if (pdi < as->app_pde || pdi >= as->app_pde + as->app_pde_count) return 0;
    pt = (const u32 *)as->app_pt_phys[pdi - as->app_pde];
    return pt[(va >> PAGE_SHIFT) % PTE_COUNT];
}

/* exec_run の CPL=3 の起動 (exec.c の同じ順) を 1 本ぶん。 */
static void launch(AppSlot *a, u32 bb_base, u32 bb_size)
{
    a->load_addr = MEM_EXEC_LOAD_ADDR;
    a->sbrk_heap_limit = T_SBRK_END;
    a->exec_heap_base = T_HEAP_BASE;
    a->exec_heap_size = T_HEAP_SIZE;
    a->band_top = T_BAND_TOP;
    CHECK(paging_addrspace_create_n(&a->as, 1) == 0);
    a->cpl3 = 1;
    CHECK(paging_addrspace_clear_app_band(&a->as) == 0);
    CHECK(app_map_region(&a->as, MEM_EXEC_LOAD_ADDR, T_SBRK_END) == 0);
    CHECK(app_map_region(&a->as, T_HEAP_BASE, T_HEAP_TOP) == 0);
    CHECK(app_map_region(&a->as, T_STACK_BOT, T_BAND_TOP) == 0);
    exec_map_shared_bb(&a->as, bb_base, bb_size, a->band_top);
}

void _start(void)
{
    /* 恒等で読み書きできる実メモリを 0x400000 から 12MB 張る */
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, base_used, round, va, v86;
    static AppSlot slot;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);

    paging_init(T_RAM_KB);
    host_pool_boot(T_RAM_KB);
    /* 起動時の姿: shlib 帯を押さえ、PEGC の BB を池の末尾から切る
     * (sys_reserve_top → pgalloc_reserve_pfn と同じ)。 */
    pgalloc_mark_used(MEM_SHLIB_BASE, (int)(MEM_EXEC_LOAD_ADDR - MEM_SHLIB_BASE) / (int)PAGE_SIZE);
    CHECK(pgalloc_reserve_pfn(T_BB_BASE / PAGE_SIZE, T_RAM_END / PAGE_SIZE));
    base_used = used_pages;

    /* ---- A + B: 10 回の起動と終了 --------------------------------------- */
    for (round = 0; round < T_ROUNDS; round++) {
        launch(&slot, T_BB_BASE, T_BB_SIZE);
        /* 帯と重なる BB は写さない (75 ページ) */
        CHECK(exec_bb_clipped_pages == T_BB_SIZE / PAGE_SIZE);
        /* スタックと exec_heap の上端は私有の物理のまま (BB の恒等でない) */
        for (va = T_STACK_BOT; va < T_BAND_TOP; va += PAGE_SIZE) {
            u32 e = app_pte(&slot.as, va);
            CHECK(e & PTE_PRESENT);
            CHECK((e & 0xFFFFF000UL) != va);
            CHECK((e & 0xFFFFF000UL) < T_BB_BASE ||
                  (e & 0xFFFFF000UL) >= T_RAM_END);
        }
        for (va = T_BB_BASE; va < T_HEAP_TOP; va += PAGE_SIZE) {
            u32 e = app_pte(&slot.as, va);
            CHECK(e & PTE_PRESENT);
            CHECK((e & 0xFFFFF000UL) < T_BB_BASE);
        }
        /* ガードは非 present のまま */
        CHECK(!(app_pte(&slot.as, T_HEAP_TOP) & PTE_PRESENT));
        CHECK(used_pages > base_used);
        exec_teardown_app(&slot);
        CHECK(slot.as.pd_phys == 0);
        CHECK(used_pages == base_used);        /* 1 枚も漏れない */
    }

    /* ---- C: V86 の backing (159 の連続) が取れる ----------------------- */
    v86 = pgalloc_alloc_n(V86_BACKING_PAGES);
    CHECK(v86 != 0);
    pgalloc_free_n(v86, V86_BACKING_PAGES);
    CHECK(used_pages == base_used);

    /* ---- D: 帯の外の BB は従来どおり写る ------------------------------- */
    {
        /* 9801 の主記憶 BB (0x6A000、PDE 0 = 共有 PT) */
        u32 saved = page_tables[0][MEM_GFX_BB_BASE >> PAGE_SHIFT];
        launch(&slot, MEM_GFX_BB_BASE, PAGE_SIZE);
        CHECK(exec_bb_clipped_pages == 0);
        CHECK(page_tables[0][MEM_GFX_BB_BASE >> PAGE_SHIFT] & PTE_USER);
        exec_teardown_app(&slot);
        CHECK(used_pages == base_used);
        page_tables[0][MEM_GFX_BB_BASE >> PAGE_SHIFT] = saved;
    }
    {
        /* 帯 (1 枚) より上 (12MB 機の PEGC BB に当たる 0xBB5000、PDE 2 = 共有) */
        u32 hi = 0xBB5000UL;
        u32 idx = (hi >> PAGE_SHIFT) % PTE_COUNT;
        u32 saved = page_tables[hi >> 22][idx];
        u32 saved_pde = page_directory[hi >> 22];
        launch(&slot, hi, PAGE_SIZE);
        CHECK(exec_bb_clipped_pages == 0);
        CHECK(page_tables[hi >> 22][idx] == (hi | PAGE_RW | PTE_USER));
        exec_teardown_app(&slot);
        CHECK(used_pages == base_used);
        page_tables[hi >> 22][idx] = saved;
        page_directory[hi >> 22] = saved_pde;
    }
    {
        /* 帯を跨ぐ BB: 下と上だけ写り、帯の中 (1 枚 = 1024 ページ) は写さない */
        u32 lo = MEM_APP_BAND_BASE - PAGE_SIZE;
        u32 idx_lo = (lo >> PAGE_SHIFT) % PTE_COUNT;
        u32 saved_lo = page_tables[0][idx_lo];
        u32 saved_hi = page_tables[2][0];
        u32 saved_pde2 = page_directory[2];
        launch(&slot, lo, T_BAND_TOP + PAGE_SIZE - lo);
        CHECK(exec_bb_clipped_pages == (T_BAND_TOP - MEM_APP_BAND_BASE) / PAGE_SIZE);
        CHECK(page_tables[0][idx_lo] & PTE_USER);
        CHECK(page_tables[2][0] & PTE_USER);
        CHECK((app_pte(&slot.as, T_BAND_TOP - PAGE_SIZE) & 0xFFFFF000UL) !=
              T_BAND_TOP - PAGE_SIZE);
        exec_teardown_app(&slot);
        CHECK(used_pages == base_used);
        page_tables[0][idx_lo] = saved_lo;
        page_tables[2][0] = saved_hi;
        page_directory[2] = saved_pde2;
    }

    SAY("app_bb_overlap: PASS");
    die(0);
}
