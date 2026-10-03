/* ========================================================================
 *  memmap_boot_host.c — ブート順を実物で再生し、地図と PTE を突き合わせる
 *  票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 2・3
 *  記録: tools/tests/memmap_tdd.md
 *
 *  kernel/paging.c と kernel/shm.c を **そのまま** #include し、
 *  paging_init → paging_reclaim_conventional → shm_init → (KAPI 踏み台)
 *  というカーネルの起動順を再生してから paging_memmap_selftest() を呼ぶ。
 *  ゲストを起動せずに「地図と実装が食い違う件数と区間」を確定できる。
 *
 *  __bss_end はリンカが決めるので、ここでは -D で渡した値を .set で作る。
 *  シナリオは 2 つ:
 *    HOST_BSS_END  カーネル本体の大きさを決める。決裁 D1 後の配置で
 *                  「典型」「予算いっぱい」「予算超過」の 3 通りを回す。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3, host_loads, host_enables;
static void *host_boot_paging_ptr(u32 addr);
static void host_load_check(u32 root);
static void host_enable_check(void);
#define HOST_MMU_LOAD_CHECK(root) host_load_check(root)
#define HOST_MMU_ENABLE_CHECK() host_enable_check()
#include "paging_host_source.c"
#include "exec_heap.h"

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x2BC060\n"
        ".globl __bss_end\n.set __bss_end, " QUOTE(HOST_BSS_END) "\n");

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

static void *host_boot_paging_ptr(u32 addr)
{
    CHECK(!_irq_enabled() && !pg_enabled);
    CHECK(addr >= MEM_FIXED_PAGING_BASE && addr < MEM_FIXED_PAGING_END);
    return (void *)addr;
}
static void host_load_check(u32 root)
{
    CHECK(!_irq_enabled());
    CHECK(root == MEM_FIXED_PD_BASE && fixed_paging_valid());
    CHECK(!pg_enabled);
    host_loads++;
}
static void host_enable_check(void)
{
    CHECK(!_irq_enabled() && host_cr3 == MEM_FIXED_PD_BASE);
    CHECK(fixed_paging_valid() && !pg_enabled);
    host_enables++;
}

static u32 fixed_hash(void)
{
    u32 a, hash = 0;
    for (a = MEM_FIXED_PAGING_BASE; a < MEM_POOL_BASE; a += sizeof(u32))
        hash = hash * 33U + *(u32 *)a;
    return hash;
}

static void shell_heap_test(void)
{
    void *parent, *child;
    u32 used, hash = fixed_hash();
    CHECK(MEM_SHELL_HEAP_SIZE == 452UL * 1024);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    parent = exec_heap_alloc(MEM_SHELL_HEAP_SIZE - 8);
    CHECK(parent && (u32)parent + MEM_SHELL_HEAP_SIZE - 8 == MEM_SHELL_HEAP_END);
    *(u32 *)parent = 0x12345678;
    CHECK(!exec_heap_alloc(1));
    CHECK(exec_heap_total() == MEM_SHELL_HEAP_SIZE);
    exec_heap_save_state(&used);
    exec_heap_init_at(0x500000, PAGE_SIZE);
    child = exec_heap_alloc(64);
    CHECK(child);
    exec_heap_free(child);
    exec_heap_restore_state(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, used);
    CHECK(exec_heap_total() == MEM_SHELL_HEAP_SIZE && exec_heap_used() == used);
    CHECK(*(u32 *)parent == 0x12345678 && !exec_heap_alloc(1));
    exec_heap_free(parent);
    CHECK(!exec_heap_used() && exec_heap_alloc(MEM_SHELL_HEAP_SIZE - 8) == parent);
    CHECK(fixed_hash() == hash && fixed_paging_valid());
}

void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
#include "pgalloc_host_source.c"
/* pgalloc_host_source.c は irq_save() を 0 に置き換えてある。 */
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"

/* kernel/shm.c の shm_init() がページ表に対してやることの写し。
 *
 * **本当は shm.c をそのまま #include したい**が、ホストの gcc 15 は
 * `(u32)&__bss_end` を含む定数式を畳まなくなっており (クロスの gcc 13.2 は
 * 畳む)、shm.c の STATIC_ASSERT が "variably modified at file scope" で
 * 落ちる。そこでここだけ写しにして、**写しが本物とずれていないことを
 * tools/tests/test_memmap_boot.py が kernel/shm.c の本文と突き合わせる**。 */
static void shm_init_replay(void)
{
    paging_set_not_present(MEM_SHM_GUARD_LO,
                           MEM_SHM_GUARD_LO + PAGE_SIZE - 1);
    paging_set_not_present(MEM_SHM_GUARD_HI,
                           MEM_SHM_GUARD_HI + PAGE_SIZE - 1);
    paging_map_range(MEM_SHM_BASE, MEM_SHM_BASE + MEM_SHM_SIZE,
                     MEM_SHM_BASE, PAGE_RW);
}

/* 16 進 8 桁を書き出す (ホスト側に libc が無いので自前) */
static void hex8(char *out, u32 v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = "0123456789ABCDEF"[v & 0xF];
        v >>= 4;
    }
}
static void say_run(u32 start, u32 end, u32 code)
{
    char line[40];
    int i;
    for (i = 0; i < 40; i++) line[i] = ' ';
    hex8(line, start);
    line[8] = '-';
    hex8(line + 9, end);
    line[17] = ' ';
    line[18] = 'w';
    line[19] = '=';
    line[20] = (char)('0' + (code >> 4));
    line[21] = ' ';
    line[22] = 's';
    line[23] = '=';
    line[24] = (char)('0' + (code & 0xF));
    line[25] = '\n';
    report(line, 26);
}

void _start(void)
{
    u32 args[6] = {0x380000, 0xC80000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, i, tramp;
    int bad;

    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x380000);

    /* ---- カーネルの起動順そのもの (kernel/kernel.c) ---- */
    host_map_fixed_paging();
    host_arch_if = HOST_INITIAL_IF;
    paging_init(HOST_RAM_KB);
    CHECK(host_arch_if == HOST_INITIAL_IF);
    if (HOST_EXPECT_INIT_FAIL) {
        CHECK(!host_loads && !host_enables && !pg_enabled && !host_cr3);
        CHECK(!live_addrspaces);
        SAY("PASS: pre-PG failure restores IF");
        die(0);
    }
    CHECK(host_loads == 1 && host_enables == 1 && pg_enabled);
    CHECK(fixed_paging_valid() && host_cr3 == MEM_FIXED_PD_BASE);
    {
        u32 i, hash = fixed_hash();
        for (i = MEM_FIXED_PAGING_BASE / PAGE_SIZE; i < MEM_FIXED_PAGING_END / PAGE_SIZE; i++)
            CHECK((page_tables[0][i] & (PTE_USER | PTE_PCD | PTE_PWT)) == 0);
        for (i = MEM_FIXED_PAGING_END / PAGE_SIZE; i < MEM_POOL_BASE / PAGE_SIZE; i++)
            CHECK(!(page_tables[0][i] & PTE_PRESENT));
        /* live AS + 非master CR3 の再呼出しは全状態を保つ。 */
        live_addrspaces = 1;
        host_cr3 = 0x123000;
        paging_init(1024);
        CHECK(host_arch_if == HOST_INITIAL_IF && fixed_hash() == hash);
        CHECK(live_addrspaces == 1 && host_cr3 == 0x123000);
        CHECK(host_loads == 1 && host_enables == 1);
        live_addrspaces = 0;
        host_cr3 = MEM_FIXED_PD_BASE;
    }
    shell_heap_test();
    host_pool_boot(16384);
    paging_reclaim_conventional();
    shm_init_replay();

    /* shm_init 後の boot 口が張る KAPI 踏み台ページ (RO+USER)。番地は .bss 由来なので
     * カーネルでは実行時にしか分からない。ここではカーネル帯の中の 1 ページ
     * を選んで同じ属性で張る。 */
    tramp = KERNEL_LOAD_ADDR + 0x60000UL;
    CHECK(paging_boot_user_shared(tramp) == 0);


    {
        struct addrspace as;
        u32 a, pa, hash = fixed_hash();
        CHECK(paging_addrspace_create(&as, LEDGER_OWNER_KERNEL) == 0);
        CHECK((((u32 *)as.pd_phys)[0] & ~(u32)(PAGE_SIZE - 1)) == MEM_FIXED_BOOT_PT_BASE);
        for (a = MEM_FIXED_PAGING_BASE; a < MEM_FIXED_PAGING_END; a += PAGE_SIZE)
            CHECK(as_va_to_pa(as.pd_phys, a, &pa) != 0);
        paging_addrspace_destroy(&as);
        CHECK(!live_addrspaces && fixed_hash() == hash && fixed_paging_valid());
    }

    /* SHM 後方予約は「空」(START == END + 1) なら呼ばれず、**逆転**
     * (START > END + 1) のときだけ撥ねた数に入る。空と逆転を混ぜない
     * ことが肝心 — 空は予約を使い切っただけで、逆転は設計が壊れている。 */
    CHECK(paging_range_reject_count == HOST_EXPECT_REJECT);


    bad = paging_memmap_selftest(tramp);
    SAY("--- memmap mismatch runs (want/seen: 0=NP 1=RW 2=RO 3=RO+USER) ---");
    for (i = 0; i < paging_memmap_bad_count && i < MM_BAD_MAX; i++)
        say_run(paging_memmap_bad[i * 3 + 0], paging_memmap_bad[i * 3 + 1],
                paging_memmap_bad[i * 3 + 2]);
    CHECK(bad == (int)paging_memmap_bad_count);

#if HOST_EXPECT_BAD
    /* --- 予算を超えた配置 (リンカの ASSERT が本来ここまで来させない) --- */
    CHECK(bad == HOST_EXPECT_BAD);
    /* SHM 後方ガードがカーネル帯域を突き抜け、SQLite 帯の先頭ページを
     * not-present にしている。期待値のほうは「帯の境界は固定番地」なので
     * present+RW を要求する — だから食い違いとして見える。 */
    CHECK(MEM_SHM_GUARD_HI > MEM_KERNEL_BAND_END);
    CHECK(paging_memmap_bad[0] == MEM_KERNEL_BAND_END + 1);
    CHECK((paging_memmap_bad[2] >> 4) == 1);     /* 期待 present+RW (SQLite) */
    CHECK((paging_memmap_bad[2] & 0xF) == 0);    /* 実物 NP */
    CHECK(!paging_is_present(MEM_KERNEL_BAND_END + 1));
#else
    /* --- 決裁 D1 の配置。地図と実物が完全に一致する --- */
    CHECK(bad == 0);
    CHECK(paging_memmap_bad_count == 0);
    /* カーネルスタックは SQLite 帯域の末尾。全 16KB が生きている */
    CHECK(paging_is_present(MEM_KSTACK_TOP & ~(PAGE_SIZE - 1)));
    CHECK(paging_is_present(MEM_KSTACK_BASE));
    CHECK(!paging_is_present(MEM_STACK_GUARD));
    /* SHM 帯はカーネル帯域に収まり、スタックには届かない (D1 の要点) */
    CHECK(!paging_is_present(MEM_SHM_GUARD_LO));
    CHECK(!paging_is_present(MEM_SHM_GUARD_HI));
    CHECK(MEM_SHM_GUARD_HI + PAGE_SIZE <= MEM_STACK_GUARD);
    CHECK(MEM_SHM_GUARD_HI + PAGE_SIZE - 1 <= MEM_KERNEL_BAND_END);
    CHECK(MEM_STACK_GUARD > MEM_KERNEL_BAND_END);
    /* GUI 予約は SHM 帯の **末尾** 4 ブロック */
    CHECK(MEM_SHM_GUI_BASE + MEM_SHM_GUI_SIZE == MEM_SHM_BASE + MEM_SHM_SIZE);
    CHECK(paging_is_present(MEM_SHM_GUI_BASE));
#endif

    /* 踏み台ページを「期待に入れていない」と食い違いが 1 本増える
     * (期待値の作り方そのものの回帰止め。票 §4-bis の注記)。
     * 配列を上書きするので、上の区間の照合を全部終えてから呼ぶ。 */
    CHECK(paging_memmap_selftest(0) == bad + 1);
    CHECK(paging_memmap_selftest(tramp) == bad);

    /* 連続して食い違うページは **1 本の区間** にまとまること。
     * 1 ページずつ数えると「4KB のずれ」と「帯ごと丸ごと違う」が
     * 同じ顔になり、件数が意味を失う。 */
    CHECK(paging_set_not_present(MEM_SHELL_HEAP_BASE,
                                 MEM_SHELL_HEAP_BASE + 3 * PAGE_SIZE - 1) == 0);
    CHECK(paging_memmap_selftest(tramp) == bad + 1);
    CHECK(paging_memmap_bad_count == (u32)bad + 1);
    CHECK(paging_memmap_bad[bad * 3 + 0] == MEM_SHELL_HEAP_BASE);
    CHECK(paging_memmap_bad[bad * 3 + 1] ==
          MEM_SHELL_HEAP_BASE + 3 * PAGE_SIZE - 1);
    CHECK(paging_map_range(MEM_SHELL_HEAP_BASE,
                           MEM_SHELL_HEAP_BASE + 3 * PAGE_SIZE,
                           MEM_SHELL_HEAP_BASE, PAGE_RW) == 0);
    CHECK(paging_memmap_selftest(tramp) == bad);

    /* 逆転を渡したら黙って 0 ページ処理して成功を返さない (票 §4 の 2) */
    {
        u32 before = paging_range_reject_count;
        CHECK(paging_set_not_present(0x2000, 0x1000) == -1);
        CHECK(paging_map_range(0x4000, 0x2000, 0, PAGE_RW) == -1);
        CHECK(paging_set_readonly(0x4000, 0x2000) == -1);
        CHECK(paging_pde_clear_user(0x4000, 0x2000) == -1);
        CHECK(paging_range_reject_count == before + 4);
        /* 空の範囲 (start == end) は逆転ではない。数えない。 */
        CHECK(paging_map_range(0x2000, 0x2000, 0x2000, PAGE_RW) == 0);
        CHECK(paging_range_reject_count == before + 4);
        /* 撥ねた呼び出しは 1 ページも変えていない */
        CHECK(paging_is_present(0x2000));
    }

    /* 台帳の backing (FIXED 型、TASK_T1_LEDGER §3-3 / T1-U1): 張る前は予約域
     * (NP) を期待し、張った後は present / supervisor / RW を期待する。USER が
     * 立てば MM 検査が 1 本の食い違いとして拾う。下の 0x2F8000 は NP のまま。 */
    CHECK(!paging_is_present(MEM_LEDGER_META_BASE));
    CHECK(paging_map_ledger_backing());
    CHECK(paging_is_present(MEM_LEDGER_META_BASE));
    CHECK(paging_is_present(MEM_LEDGER_META_END - PAGE_SIZE));
    CHECK(!paging_is_present(MEM_LEDGER_META_BASE - PAGE_SIZE));
    CHECK(!paging_is_present(MEM_LEDGER_META_END));
    CHECK(paging_memmap_selftest(tramp) == bad);
    CHECK(paging_poke_user_bit(MEM_LEDGER_META_BASE, 1) == 0);
    CHECK(paging_memmap_selftest(tramp) == bad + 1);
    (void)paging_poke_user_bit(MEM_LEDGER_META_BASE, 0);
    CHECK(paging_memmap_selftest(tramp) == bad);

    SAY("PASS");
    die(0);
}
