/* ========================================================================
 *  app_bb_overlap_host.c — 共有 BB と CPL=3 アプリの私有領域が重ならないこと
 *
 *  実行: python3 -B tools/tests/test_app_bb_overlap.py [--mutate]
 *
 *  2026-09-30 の後退 (8MB + PEGC、PM が NP21/W で観測): CPL=3 アプリを 1 本
 *  起動して終了するたびに pgalloc の used_pages が 74 ずつ増え、4 本目で
 *  NOMEM・v86 -t は 159 ページの連続が取れずに失敗した。原因は exec が私有
 *  領域 (スタック / exec_heap) を帯の上端 0x800000 まで張ってから、PEGC の
 *  BB [0x7B5000, 0x800000) を恒等で重ねて写していたこと — 私有ページ 74 枚
 *  の PTE が BB の物理で上書きされ、teardown で戻らない。
 *
 *  修正 (exec/exec.c ring3_band_set): 私有領域の上端を帯の上端と
 *  sys_usable_mem_end() の低い方にする。BB は従来どおり恒等のまま丸ごと写す
 *  (gfx_get_framebuffer / pegc_init が同じ番地を使う) ので、カーネル・アプリ
 *  の BB ポインタは変わらない。
 *
 *  実物の kernel/paging.c + kernel/pgalloc.c + kernel/physmem.c を ILP32 で
 *  そのまま組み、exec/exec.c から切り出した本物の ring3_band_set /
 *  app_map_region / exec_bb_overlaps_user / exec_map_shared_bb /
 *  exec_teardown_app (test_app_bb_overlap.py が生成する exec_bb_overlap.inc)
 *  で「起動 → 終了」をまわす。
 *
 *  見るもの:
 *    (a) 8MB + PEGC: 起動・終了 10 回で used_pages が毎回戻り、V86 の backing
 *        (V86_BACKING_PAGES = 159 の連続) が取れる
 *    (b) 私有 PTE (スタック / exec_heap) が BB の物理を指さない・恒等でない
 *    (c) BB の仮想番地の PTE が BB の物理を恒等 + USER で指す (カーネル・
 *        アプリの BB ポインタが BB に届く)
 *    (d) 私有領域の上端 (band_top = RING3_USTACK_TOP) が BB の下にあり、
 *        PDE の所有範囲 (帯 1 枚) は変わらない
 *    (e) teardown が BB の物理を pgalloc へ返そうとしない (paging.c の
 *        pgalloc_free_n_owner 呼び出しを数える)。T1b 以後は teardown の最後の
 *        ledger_reclaim_owner(AS) が取り残しを回収するので、used_pages が戻る
 *        だけでは漏れが無い証拠にならない — exec_as_leftover_pages (回収で
 *        返った取り残し) と ledger_bad_free (他 owner のページ = BB を返そうと
 *        して断られた回数) が増えないことも見る
 *    (f) BB が私有領域と重なる形 (上端が下がっていない) は起動を断り、
 *        私有 PTE を触らない
 *    (g) 17MB (BB が帯の上) / 12MB (帯の上、PDE 2) / 9801 planar (0x6A000、
 *        帯の下) / Cirrus (デバイス窓、PCD 付き) では私有領域の上端が従来の
 *        0x800000 のままで、BB は共有 PT に USER で写り、PCD を保つ
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3;
/* (e): paging.c が返す物理を数える口。pgalloc.h の宣言も同じ名に変わるので、
 * paging.c の呼び出しは全部この関数へ来る (本物へ転送する)。 */
#define pgalloc_free_n_owner host_free_hook
#include "paging_host_source.c"
#undef pgalloc_free_n_owner
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

/* 試験が決める「機械の姿」: 割り当ててよい物理の上限と、いまの BB。 */
static u32 t_usable_end;
static u32 t_bb_base, t_bb_size;
static u32 bb_free_attempts;     /* (e) BB の物理を返そうとした回数 */

u32 sys_usable_mem_end(void);
u32 sys_usable_mem_end(void) { return t_usable_end; }
void gfx_bb_phys_range(u32 *base, u32 *size);
void gfx_bb_phys_range(u32 *base, u32 *size)
{
    if (base) *base = t_bb_base;
    if (size) *size = t_bb_size;
}
int host_free_hook(u32 owner, u32 pfn, int n)
{
    u32 phys = pfn * PAGE_SIZE;
    if (t_bb_size && phys < t_bb_base + t_bb_size &&
        phys + (u32)n * PAGE_SIZE > t_bb_base)
        bb_free_attempts++;
    return pgalloc_free_n_owner(owner, pfn, n);
}

/* exec_teardown_app が引く。この試験は shlib を載せない。 */
#include "appslot.h"
void shlib_addrspace_detach(struct addrspace *as);
void shlib_addrspace_detach(struct addrspace *as) { (void)as; }

/* exec/exec.c から切り出した本物のテキスト (test_app_bb_overlap.py が生成)。 */
#include "exec_bb_overlap.inc"

#include "v86_mem.h"         /* V86_BACKING_PAGES */

/* 8MB + PEGC の実測値 (PM 2026-09-30): BB = 0x4B000 (75 ページ) を低位 RAM の上端から */
#define T_RAM_KB      8192UL
#define T_RAM_END     0x800000UL
#define T_BB_SIZE     0x4B000UL
#define T_BB_BASE     (T_RAM_END - T_BB_SIZE)       /* 0x7B5000 */
#define T_CODE_END    0x510000UL
#define T_SBRK_END    (T_CODE_END + MEM_EXEC_SBRK_MIN)
#define T_ROUNDS      10
#define T_BB_PAGES    (T_BB_SIZE / PAGE_SIZE)       /* 75 */
#define T_PTE_ADDR    0xFFFFF000UL

static u32 app_pte(const struct addrspace *as, u32 va)
{
    u32 pdi = va >> 22;
    const u32 *pt;
    if (pdi < as->app_pde || pdi >= as->app_pde + as->app_pde_count) return 0;
    pt = (const u32 *)as->app_pt_phys[pdi - as->app_pde];
    return pt[(va >> PAGE_SHIFT) % PTE_COUNT];
}

/* 共有 PT (master と同一) の PTE。 */
static u32 *shared_pte(u32 va)
{
    return &page_tables[va >> 22][(va >> PAGE_SHIFT) % PTE_COUNT];
}

/* exec_launch の CPL=3 の起動 (exec.c の同じ順・同じ式) を 1 本ぶん。
 * heap_size 無指定 (二段構えの段 1 = avail / 2)。戻り値は exec_map_shared_bb。 */
static int launch(AppSlot *a)
{
    u32 heap_top, avail, owner;

    ring3_band_set(paging_app_band_pdes(T_CODE_END, 0, sys_usable_mem_end()));
    heap_top = RING3_HEAP_TOP;
    CHECK(heap_top > T_CODE_END);
    CHECK(heap_top - T_CODE_END >= MEM_EXEC_SBRK_MIN + PAGE_SIZE + MEM_EXEC_HEAP_MIN);
    avail = heap_top - T_CODE_END - MEM_EXEC_SBRK_MIN - PAGE_SIZE;

    a->load_addr = MEM_EXEC_LOAD_ADDR;
    a->sbrk_heap_limit = T_SBRK_END;
    a->exec_heap_size = (avail / 2) & ~(PAGE_SIZE - 1);
    a->exec_heap_base = heap_top - a->exec_heap_size;
    a->guard_a = a->exec_heap_base - PAGE_SIZE;
    a->guard_b = RING3_GUARD_BASE;
    a->stack_top = RING3_USTACK_TOP;
    a->band_top = g_ring3_band_top;
    a->band_pdes = g_ring3_band_pdes;
    /* AS owner は AS 作成の直前に取る (exec_launch と同じ、T1b) */
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "app", &owner));
    CHECK(paging_addrspace_create_n(&a->as, owner, g_ring3_band_pdes) == 0);
    a->cpl3 = 1;
    CHECK(paging_addrspace_clear_app_band(&a->as) == 0);
    CHECK(app_map_region(&a->as, MEM_EXEC_LOAD_ADDR, T_SBRK_END) == 0);
    CHECK(app_map_region(&a->as, a->exec_heap_base,
                         a->exec_heap_base + a->exec_heap_size) == 0);
    CHECK(app_map_region(&a->as, RING3_STACK_BOTTOM, RING3_USTACK_TOP) == 0);
    return exec_map_shared_bb(&a->as, a->band_top);
}

/* (b) 私有 PTE: present、恒等でない、BB の物理でない。 */
static void check_private(const AppSlot *a, u32 lo, u32 hi)
{
    u32 va;
    for (va = lo; va < hi; va += PAGE_SIZE) {
        u32 e = app_pte(&a->as, va);
        u32 phys = e & T_PTE_ADDR;
        CHECK(e & PTE_PRESENT);
        CHECK(e & PTE_USER);
        CHECK(phys != va);
        CHECK(phys < t_bb_base || phys - t_bb_base >= t_bb_size);
    }
}

/* 私有領域 3 つ + ガード 2 つ。 */
static void check_layout(const AppSlot *a)
{
    check_private(a, a->load_addr, a->sbrk_heap_limit);
    check_private(a, a->exec_heap_base, a->exec_heap_base + a->exec_heap_size);
    check_private(a, a->band_top - RING3_USTACK_SIZE, a->band_top);
    CHECK(!(app_pte(&a->as, a->guard_a) & PTE_PRESENT));
    CHECK(!(app_pte(&a->as, a->guard_b) & PTE_PRESENT));
    CHECK(a->stack_top == a->band_top);
    CHECK(a->as.app_pde_count == a->band_pdes);
}

static void teardown(AppSlot *a, u32 base_used)
{
    u32 owner = a->as.owner;
    exec_teardown_app(a);
    CHECK(a->as.pd_phys == 0);
    CHECK(used_pages == base_used);        /* (a) 1 枚も漏れない */
    CHECK(bb_free_attempts == 0);          /* (e) BB の物理を返そうとしない */
    /* (a)(e) T1b: 回収で掃除された取り残しも、断られた解放も無い */
    CHECK(exec_as_leftover_pages == 0);
    CHECK(ledger_bad_free == 0);
    CHECK(a->as.owner == 0 && ledger_owners[owner].kind == 0);   /* 番号は返った */
}

/* (g) 帯の外の BB: 共有 PT に恒等 + USER で写り、既存の属性 (PCD) を保つ。
 * 起動前に PTE を preset (デバイス窓は supervisor + PCD で張ってある) し、
 * 終了後に元へ戻す。 */
static u32 saved_pte[T_BB_PAGES];

static void run_outside_band(u32 usable_end, u32 bb_base, u32 preset,
                             u32 base_used)
{
    static AppSlot slot;
    u32 i, va, pdi = bb_base >> 22;
    u32 saved_pde = page_directory[pdi];

    t_usable_end = usable_end;
    t_bb_base = bb_base;
    t_bb_size = T_BB_SIZE;
    for (i = 0; i < T_BB_PAGES; i++) {
        va = bb_base + i * PAGE_SIZE;
        saved_pte[i] = *shared_pte(va);
        *shared_pte(va) = preset ? (va | preset) : saved_pte[i];
    }
    CHECK(launch(&slot) == 0);
    /* 私有領域の上端は従来の帯の上端のまま */
    CHECK(slot.band_top == MEM_APP_BAND_TOP);
    CHECK(slot.band_pdes == 1);
    check_layout(&slot);
    /* (c) BB は共有 PT に恒等 + USER、preset の属性 (PCD) はそのまま */
    for (i = 0; i < T_BB_PAGES; i++) {
        va = bb_base + i * PAGE_SIZE;
        CHECK(*shared_pte(va) == (va | PAGE_RW | PTE_USER | (preset & PTE_PCD)));
    }
    /* USER はこのアプリの PDE にだけ伝播し、master の PDE は触らない */
    CHECK(((u32 *)slot.as.pd_phys)[pdi] & PTE_USER);
    CHECK(page_directory[pdi] == saved_pde);
    teardown(&slot, base_used);
    for (i = 0; i < T_BB_PAGES; i++)
        *shared_pte(bb_base + i * PAGE_SIZE) = saved_pte[i];
    page_directory[pdi] = saved_pde;
}

void _start(void)
{
    /* 恒等で読み書きできる実メモリを 0x400000 から 12MB 張る */
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, base_used, round, va, v86;
    static AppSlot slot;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);

    host_map_fixed_paging();
    paging_init(T_RAM_KB);
    host_pool_boot(T_RAM_KB);
    /* 起動時の姿: shlib 帯を押さえ、PEGC の BB を ⑥ と同じく池の CPL=0 子の
     * アリーナ内の上端から owner = boot で取って、アリーナの上端を凍結する
     * (T1e、TASK_T1_LEDGER §3-6・§3-8。旧 sys_reserve_top と同じ区間)。 */
    CHECK(ledger_claim_fixed(LEDGER_OWNER_SHLIB, MEM_SHLIB_BASE / PAGE_SIZE,
                             MEM_EXEC_LOAD_ADDR / PAGE_SIZE));
    CHECK(pgalloc_arena_end() == T_RAM_END / PAGE_SIZE);
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, (int)T_BB_PAGES,
                                MEM_EXEC_LOAD_ADDR / PAGE_SIZE, pgalloc_arena_end(),
                                LEDGER_TOP_DOWN, &va));
    CHECK(va == T_BB_BASE / PAGE_SIZE);
    ledger_arena_freeze();
    /* 私有領域の上端を決める値 = sys_usable_mem_end() = min(凍結した exec
     * 上端, ledger_arena_top()) (kernel/sys.c)。8MB (FIXED 型) の exec 上端は
     * 0x800000 なので、アリーナの上端 = BB の下端が効く。 */
    CHECK(ledger_arena_top() * PAGE_SIZE == T_BB_BASE);
    base_used = used_pages;

    /* ---- (a)〜(e): 8MB + PEGC、10 回の起動と終了 ------------------------ */
    t_usable_end = ledger_arena_top() * PAGE_SIZE;   /* = T_BB_BASE */
    t_bb_base = T_BB_BASE;
    t_bb_size = T_BB_SIZE;
    for (round = 0; round < T_ROUNDS; round++) {
        CHECK(launch(&slot) == 0);
        /* (d) 私有領域の上端は BB の下 (= sys_usable_mem_end)、PDE は帯 1 枚 */
        CHECK(slot.band_top == T_BB_BASE);
        CHECK(slot.band_top < MEM_APP_BAND_TOP);
        CHECK(slot.band_pdes == 1);
        CHECK(RING3_USTACK_TOP == T_BB_BASE);
        CHECK(RING3_HEAP_TOP < T_BB_BASE);
        /* (b) 私有 PTE は BB を指さない */
        check_layout(&slot);
        /* (c) BB の仮想番地 (帯の中、アプリ固有 PT) は BB の物理を恒等で指す */
        for (va = T_BB_BASE; va < T_RAM_END; va += PAGE_SIZE)
            CHECK(app_pte(&slot.as, va) == (va | PAGE_RW | PTE_USER));
        CHECK(((u32 *)slot.as.pd_phys)[MEM_APP_BAND_BASE >> 22] & PTE_USER);
        CHECK(used_pages > base_used);
        teardown(&slot, base_used);
    }

    /* V86 の backing (159 の連続) が取れる */
    v86 = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, V86_BACKING_PAGES);
    CHECK(v86 != 0);
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, v86 / PAGE_SIZE, V86_BACKING_PAGES));
    CHECK(used_pages == base_used);

    /* ---- (f): 上端が下がっていないのに BB が帯の中 → 起動を断る --------- */
    t_usable_end = T_RAM_END;
    CHECK(launch(&slot) == -1);
    CHECK(slot.band_top == MEM_APP_BAND_TOP);
    /* 断った起動は私有 PTE を 1 枚も BB で上書きしていない */
    check_layout(&slot);
    for (va = T_BB_BASE; va < T_RAM_END; va += PAGE_SIZE)
        CHECK((app_pte(&slot.as, va) & T_PTE_ADDR) != va);
    teardown(&slot, base_used);

    /* ---- (g): BB が帯の外にある構成 ------------------------------------ */
    /* 17MB + PEGC: BB = 0xEB3000 (PDE 3、共有 PT)、上限も帯の上 */
    run_outside_band(0xEB3000UL, 0xEB3000UL, 0, base_used);
    /* 12MB + PEGC: BB = 0xBB5000 (PDE 2、共有 PT) */
    run_outside_band(0xBB5000UL, 0xBB5000UL, 0, base_used);
    /* 9801 planar (8MB): 主記憶 BB 0x6A000 (PDE 0)、上限は 0x800000 のまま */
    run_outside_band(T_RAM_END, MEM_GFX_BB_BASE, 0, base_used);
    /* Cirrus: クライアント面はデバイス窓 (supervisor + PCD で張ってある)。
     * _keep で USER に昇格しても PCD が落ちない */
    run_outside_band(T_RAM_END, MEM_DEVICE_APERTURE_BASE + 0x100000UL,
                     PAGE_RW | PTE_PCD, base_used);

    SAY("app_bb_overlap: PASS");
    die(0);
}
