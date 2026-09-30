#include "types.h"
static u32 host_cr3;
static unsigned int host_if = 0x202U;
static unsigned int host_irq_save(void)
{
    unsigned int f = host_if;
    host_if &= ~0x200U;
    return f;
}
static void host_irq_restore(unsigned int f) { host_if = f; }
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
/* legacy の pgalloc_init は T1a で撤去した (TASK_T1_LEDGER §4-1)。観測するのは
 * モデル経路の 2 段 (bootstrap / stage) だけ。 */
static u32 bootstrap_calls, stage_calls;
static int observed_bootstrap(struct physmem *m, const struct pgalloc_layout *l,
                              int (*verify)(u32, u32, void *))
{
    bootstrap_calls++;
#ifdef TEST_BOOTSTRAP_FAIL
    (void)m; (void)l; (void)verify;
    return 0;
#else
    return sys_memory_bootstrap_model(m, l, verify);
#endif
}
static int observed_stage(void)
{
    stage_calls++;
#ifdef TEST_STAGE_FAIL
    return 0;
#else
    return sys_memory_stage_online();
#endif
}
#define sys_memory_bootstrap_model observed_bootstrap
#define sys_memory_stage_online observed_stage
#include "memory_boot_host_source.c"
#undef sys_memory_bootstrap_model
#undef sys_memory_stage_online
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { report("FAIL " #x "\n", sizeof("FAIL " #x "\n") - 1); die(1); } } while (0)
extern int memory_boot_init(u32) __attribute__((weak));
static void host_failstop(void)
{
    CHECK(bootstrap_calls == 1 && !pgalloc_alloc_page());
#ifdef TEST_BOOTSTRAP_FAIL
    CHECK(!initialized && !stage_calls && !sys_model_staged);
#else
    CHECK(stage_calls == 1 && pgalloc_model_state() == PGALLOC_BOOTSTRAP);
#endif
    CHECK(host_if == 0x202U);
    die(0);
}
#include "kernel_boot_gate.c"
void _start(void)
{
    u32 args[6] = {0x800000, 0x700000, 3, 0x32, 0xffffffffUL, 0};
    /* FIXED 型の backing [0x2F9000, 0x2FB000) (8MB 級の構成) も実メモリに。 */
    u32 ledger[6] = {MEM_LEDGER_META_BASE,
                     MEM_LEDGER_META_END - MEM_LEDGER_META_BASE,
                     3, 0x32, 0xffffffffUL, 0};
    u32 result, count;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x800000);
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(ledger) : "memory");
    CHECK(result == MEM_LEDGER_META_BASE);
    paging_init(TEST_KB);
    sys_mem_kb = TEST_KB;
    CHECK(memory_boot_init != 0);
    /* 巨大なヒント単独では高位 RAM は 1 ページも昇格しない。 */
    CHECK(!boot_high_end);
#ifdef TEST_TABLES
    /* 表 (bitmap 2 面 + 恒等 PT の workspace) の大きさは検出量から出る。
     * 人為的な上限は無く、置ける場所 — アプリ帯の最大上端から legacy
     * アリーナ上端までの低位 RAM — の広さだけが覆える量を決める。 */
    {
        u32 top, room, fit, huge;
        top = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE;      /* 15MiB: アリーナ上端 */
        room = top - MEM_APP_BAND_MAX_TOP / PAGE_SIZE;
        /* 32MiB / 128MiB は丸ごと通る (切り詰め無し)。 */
        CHECK(memory_boot_high_fit(0x2000, top) == 0x2000);
        CHECK(memory_boot_table_pages(0x2000) <= room);
        CHECK(memory_boot_workspace_pages(0x2000) == MEMORY_BOOT_WORKSPACE_PAGES);
        CHECK(memory_boot_high_fit(0x8000, top) == 0x8000);
        CHECK(memory_boot_table_pages(0x8000) <= room);
        CHECK(memory_boot_workspace_pages(0x8000) == MEMORY_BOOT_WORKSPACE_PAGES +
              0x8000 / PTE_COUNT - PAGING_BOOT_PT_COUNT);
        /* 4GiB 相当 (上端は最上位の ROM/MMIO 帯)。 */
        huge = MEM_PHYS_MMIO_TOP / PAGE_SIZE;
        fit = memory_boot_high_fit(huge, top);
        CHECK(fit > MEM_HIGH_RAM_BASE / PAGE_SIZE && fit <= huge);
        /* 表は RAM の外に出ない。かつ置ける限りは目一杯まで取る。 */
        CHECK(memory_boot_table_pages(fit) <= room);
        CHECK(memory_boot_table_pages(fit + PTE_COUNT) > room);
        /* 2GiB 超を覆える = 人為的に小さい上限は残っていない。 */
        CHECK(fit > 0x80000);
        /* 置き場所が無い構成は 0 (高位 RAM は登録しない — 表は FIXED 型の
         * 固定区間に置くが、そこは高位を覆えない)。でっち上げはしない。 */
        CHECK(memory_boot_high_fit(huge, MEM_APP_BAND_MAX_TOP / PAGE_SIZE) == 0);
        /* FIXED / ARENA_TOP の境界 (§3-3): 3,073 PFN までが FIXED。 */
        CHECK(MEMORY_BOOT_FIXED_MAX_TOP == 3073UL);
        /* D11: 0594h の申告は 2GB で頭打ち (16MB から 2032MB)。 */
        CHECK(MEM_PHYS_RAM_CEILING == 0x80000000UL);
        CHECK((MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB == 2032UL);
        CHECK(MEM_PHYS_RAM_CEILING <= MEM_DEVICE_APERTURE_BASE);
        /* 集積域・同梱域 (TASK_MEMMAP_V3 §4-5 の区間表、定数だけ — 規則は T1b)。
         * 1MB ずつ、ページ整列、隣接して重ならず、8MB 機で成立 (0x700000 まで)、
         * 池の中で共有ライブラリ帯より上。 */
        CHECK(MEM_BOOT_STAGING_SIZE == MEM_1MB && MEM_BOOT_BUNDLE_SIZE == MEM_1MB);
        CHECK(!(MEM_BOOT_STAGING_BASE & (PAGE_SIZE - 1)) &&
              !(MEM_BOOT_BUNDLE_BASE & (PAGE_SIZE - 1)));
        CHECK(MEM_BOOT_STAGING_BASE + MEM_BOOT_STAGING_SIZE == MEM_BOOT_BUNDLE_BASE);
        CHECK(MEM_BOOT_BUNDLE_BASE + MEM_BOOT_BUNDLE_SIZE <= 0x800000UL);
        CHECK(MEM_BOOT_STAGING_BASE >= MEM_POOL_BASE &&
              MEM_BOOT_STAGING_BASE >= MEM_SHLIB_END);
        CHECK(MEM_POOL_BASE == 0x400000UL);
        CHECK(host_if == 0x202U);
        die(0);
    }
#endif
#ifdef TEST_FIXED_REJECT
    /* FIXED 型の検証経路 (§3-3): backing は [MEM_LEDGER_META_BASE,
     * MEM_LEDGER_META_END) の内側で、L0 で RESERVED (RAM でない) こと。
     * RAM 側・範囲外・重なり・種類違いは全部、何も変えずに拒否する。 */
    {
        struct physmem m;
        struct pgalloc_layout l;
        u32 meta = MEM_LEDGER_META_BASE / PAGE_SIZE;
        u32 top = TEST_KB / (PAGE_SIZE / 1024UL);
        physmem_bootstrap_legacy(&m, TEST_KB);
        CHECK(paging_map_ledger_backing());
        l.capacity = pgalloc_metadata_bytes(&m);
        CHECK(l.capacity == PAGE_SIZE);
#define TRY(mf, wf, we, k, expect) do { \
            l.kind = (k); l.metadata_first = (mf); \
            l.metadata = (void *)((mf) * PAGE_SIZE); \
            l.workspace_first = (wf); l.workspace_end = (we); \
            CHECK(!!sys_memory_bootstrap_model(&m, &l, paging_verify_identity) == (expect)); \
            if (!(expect)) CHECK(!initialized && !sys_model_staged && !sys_frozen_end); \
        } while (0)
        /* RAM 側 (低位 RAM の末尾) に置いた FIXED は拒否 */
        TRY(top - 1, meta + 1, meta + 2, PGALLOC_BACKING_FIXED, 0);
        /* 範囲外: 下のガード 0x2F8000、上のカーネルスタックガード */
        TRY(meta - 1, meta + 1, meta + 2, PGALLOC_BACKING_FIXED, 0);
        TRY(meta, meta + 1, meta + 3, PGALLOC_BACKING_FIXED, 0);
        TRY(meta, meta - 1, meta, PGALLOC_BACKING_FIXED, 0);
        /* metadata と workspace の重なり */
        TRY(meta, meta, meta + 1, PGALLOC_BACKING_FIXED, 0);
        /* 知らない種類 */
        TRY(meta, meta + 1, meta + 2, 7, 0);
        /* 8MB で ARENA_TOP は通らない (workspace が MEM_APP_BAND_MAX_TOP 未満) */
        TRY(top - 1, top - 2, top - 1, PGALLOC_BACKING_ARENA_TOP, 0);
        /* 置き場の番地は正しくても、模型でそこが RAM なら拒否 (RESERVED で
         * なければならない — RAM なら池の 1 ページと二重に使われる)。 */
        {
            struct physmem saved = m;
            u32 k;
            /* k = 0: metadata のページだけ RAM、k = 1: workspace のページだけ RAM */
            for (k = 0; k < 2; k++) {
                m.count = 5;
                m.ranges[0].first = 0; m.ranges[0].end = meta + k;
                m.ranges[0].kind = PHYSMEM_RESERVED; m.ranges[0].sources = 0;
                m.ranges[1].first = meta + k; m.ranges[1].end = meta + k + 1;
                m.ranges[1].kind = PHYSMEM_RAM;
                m.ranges[1].sources = PHYSMEM_SOURCE_LEGACY;
                m.ranges[2].first = meta + k + 1;
                m.ranges[2].end = MEM_POOL_BASE / PAGE_SIZE;
                m.ranges[2].kind = PHYSMEM_RESERVED; m.ranges[2].sources = 0;
                m.ranges[3] = saved.ranges[1];
                m.ranges[4] = saved.ranges[2];
                CHECK(saved.count == 3 && m.ranges[3].kind == PHYSMEM_RAM);
                CHECK(pgalloc_metadata_bytes(&m) == PAGE_SIZE);
                TRY(meta, meta + 1, meta + 2, PGALLOC_BACKING_FIXED, 0);
                m = saved;
            }
        }
        /* 正しい FIXED は通る。exec の上端は低位 RAM の上端 (B2)。 */
        TRY(meta, meta + 1, meta + 2, PGALLOC_BACKING_FIXED, 1);
#undef TRY
        CHECK(pgalloc_arena_end() == top);
        CHECK(sys_usable_mem_end() == top * PAGE_SIZE);
        CHECK(host_if == 0x202U);
        die(0);
    }
#endif
/* 15MiB clamp なので legacy 上端は 0xF00。窓の撤去 (2026-09-09) で
 * metadata が 0xEFF、workspace が 0xEFE に上がった (旧: 0xEBF / 0xEBE)。 */
#if defined(TEST_METADATA_PTE) || defined(TEST_WORKSPACE_PTE)
    *(u32 *)0xEFF000 = 0xA55AA55A;
#ifdef TEST_METADATA_PTE
    page_tables[3][0xEFF % PTE_COUNT] |= PTE_USER;
#endif
#ifdef TEST_WORKSPACE_PTE
    page_tables[3][0xEFE % PTE_COUNT] &= ~PTE_PRESENT;
#endif
    CHECK(!memory_boot_init(TEST_KB));
    CHECK(bootstrap_calls == 1 && !stage_calls);
    CHECK(!initialized && !sys_model_staged && !pgalloc_alloc_page());
    CHECK(*(u32 *)0xEFF000 == 0xA55AA55A);
    die(0);
#endif
#if defined(TEST_BOOTSTRAP_FAIL) || defined(TEST_STAGE_FAIL)
    /* Execute the actual kernel gate: reaching shm is an exit(7) failure. */
    host_kernel_boot(TEST_KB);
    CHECK(0);
#endif
#if defined(TEST_HIGH) || defined(TEST_RAMKB) || defined(TEST_CEILING)
    /* The detector is the only source of a high attestation; the host cannot
     * touch the BIOS work area, so stand in for it with the same value. */
    if (TEST_KB > MEM_HIGH_RAM_BASE / 1024UL)
        boot_high_end = TEST_KB / (PAGE_SIZE / 1024UL);
#endif
    /* K6-RAM (2): 未初期化のうちは「実 RAM 合計」を名乗らない。 */
    CHECK(!memory_boot_ram_kb());
    CHECK(memory_boot_init(TEST_KB));
#ifdef TEST_RAMKB
    /* K6-RAM 決裁 (2) — 実 RAM 合計は「登録した span の合計」であって、
     * 上端 (sys_mem_kb) ではない。差は 15-16MiB のシステム空間ちょうど。 */
    {
        u32 low, high, mb;
        low = MEMORY_BOOT_LEGACY_END / 1024UL;      /* 低位 RAM の上端 = 15MiB */
        high = MEM_HIGH_RAM_BASE / PAGE_SIZE;
        mb = MEM_1MB / PAGE_SIZE;
        /* 上端は生の申告のまま。定義は変えない (K6-RAM)。 */
        CHECK(sys_mem_kb == TEST_KB);
        CHECK(memory_boot_ram_kb() == TEST_RAM_KB_EXPECT);
        CHECK(memory_boot_ram_kb() <= sys_mem_kb);
        /* 合計そのものを 3 例で固定する (span の足し算、引き算の特例なし) */
        /* 15MiB 構成 (ExMemory 16): 上端 17MiB、低位 15MiB + 高位 1MiB */
        CHECK(memory_boot_sum_kb(low, high + mb) == 16384UL);
        /* 32MiB 構成 (ExMemory 33): 上端 33MiB、低位 15MiB + 高位 17MiB */
        CHECK(memory_boot_sum_kb(low, high + 17UL * mb) == 32768UL);
        /* 8MiB (FIXED 型): 高位無し。上端と一致する */
        CHECK(memory_boot_sum_kb(8192UL, 0) == 8192UL);
        /* 高位帯に届かない申告は 1 ページも足さない */
        CHECK(memory_boot_sum_kb(8192UL, high) == 8192UL);
        CHECK(memory_boot_sum_kb(8192UL, MEM_SYSTEM_SPACE_BASE / PAGE_SIZE) == 8192UL);
        /* K6-3: デバイス窓 (PEGC のリニア窓 = 15-16MiB のシステム空間) の
         * 可否は「RAM の上端」ではなく「窓に RAM が登録されているか」。
         * 15MiB / 32MiB 構成では上端が窓より上に出るが、窓は穴のままなので
         * 窓は張れる。8MiB は上端が窓より下で、やはり張れる。 */
        CHECK(!pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE, high));
        /* 旧条件は「上端 > 窓」。高位 RAM のある構成でだけ真になり、正しい
         * 判定と食い違う = これが 2026-09-12 の回帰そのもの。 */
        if (TEST_KB > MEM_HIGH_RAM_BASE / 1024UL)
            CHECK(sys_mem_kb * 1024UL > MEM_SYSTEM_SPACE_BASE);
        else
            CHECK(sys_mem_kb * 1024UL <= MEM_SYSTEM_SPACE_BASE);
        CHECK(host_if == 0x202U);
        die(0);
    }
#endif
#ifdef TEST_CEILING
    /* D11: 2GB 以上は RAM として登録しない。detect の頭打ちを越えた証明が
     * 来ても memory_boot_init が同じ上限で切る。[2GB, 4GB) は RAM ではない。 */
    {
        u32 ceil = MEM_PHYS_RAM_CEILING / PAGE_SIZE;
        CHECK(boot_high_end > ceil);
        CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
        CHECK(pgalloc_limit_pfn() == ceil);
        CHECK(physmem_count(&device_boot_map, ceil, PHYSMEM_MAX_PFN,
                            PHYSMEM_MMIO, &count));
        CHECK(count == PHYSMEM_MAX_PFN - ceil);
        CHECK(!pgalloc_range_has_ram(ceil, PHYSMEM_MAX_PFN));
        CHECK(pgalloc_range_has_ram(ceil - 1, ceil));
        CHECK(memory_boot_ram_kb() == MEMORY_BOOT_LEGACY_END / 1024UL +
              (MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / 1024UL);
        CHECK(pgalloc_arena_end() == workspace_first);
        CHECK(sys_usable_mem_end() == workspace_first * PAGE_SIZE);
        CHECK(host_if == 0x202U);
        die(0);
    }
#endif
#ifdef TEST_HIGH
    /* K6-RAM: 16MiB 超を検出量ぶんそのまま池に入れる。人為的な上限は無く、
     * 引かれてよいのは名前の付いたデバイス予約だけ。 */
    {
        u32 p, i, hole, high, arena, limit;
        hole = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE;
        high = MEM_HIGH_RAM_BASE / PAGE_SIZE;
        limit = pgalloc_limit_pfn();
        CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
        CHECK(limit == TEST_KB / (PAGE_SIZE / 1024));
        /* 実 RAM 合計は上端ちょうど 1MiB 下 = 15-16MiB の穴のぶん (K6-RAM (2)) */
        CHECK(memory_boot_ram_kb() == TEST_KB - MEM_1MB / 1024UL);
        /* M5: 15-16MiB の穴は RAM として配られない */
        CHECK(physmem_count(&device_boot_map, hole, high, PHYSMEM_RESERVED, &count));
        CHECK(count == high - hole);
        p = 99;
        CHECK(!pgalloc_alloc_n_pfn(1, hole, high, &p) && p == 99);
        /* 最上位の ROM / PCI MMIO 帯も RAM ではない */
        CHECK(physmem_count(&device_boot_map, MEM_PHYS_MMIO_TOP / PAGE_SIZE,
                            PHYSMEM_MAX_PFN, PHYSMEM_MMIO, &count));
        CHECK(count == PHYSMEM_MAX_PFN - MEM_PHYS_MMIO_TOP / PAGE_SIZE);
        /* OS が割り当てるデバイス窓の帯 (v3、Cirrus のリニア窓) も RAM では
         * なく、デバイス窓の可否の問い合わせは「RAM 無し」と答える。 */
        CHECK(physmem_count(&device_boot_map,
                            MEM_DEVICE_APERTURE_BASE / PAGE_SIZE,
                            MEM_DEVICE_APERTURE_END / PAGE_SIZE,
                            PHYSMEM_MMIO, &count));
        CHECK(count == (MEM_DEVICE_APERTURE_END - MEM_DEVICE_APERTURE_BASE) /
                       PAGE_SIZE);
        CHECK(!pgalloc_range_has_ram(MEM_DEVICE_APERTURE_BASE / PAGE_SIZE,
                                     MEM_DEVICE_APERTURE_END / PAGE_SIZE));
        /* 検出量の最終ページまで配れる (切り詰めが無いことの証明) */
        CHECK(pgalloc_alloc_n_pfn(1, limit - 1, limit, &p) && p == limit - 1);
        CHECK(pgalloc_free_n_pfn(p, 1));
        arena = workspace_first - MEM_APP_BAND_BASE / PAGE_SIZE;
        CHECK(pgalloc_total_pages() > arena);
        /* 連続アリーナ (exec のレイアウト) と表の置き場所は不変 (ARENA_TOP:
         * アリーナの上端 = workspace_first、B2 の等式) */
        CHECK(!ledger_backing_mapped);
        CHECK(pgalloc_arena_end() == workspace_first);
        CHECK(sys_usable_mem_end() == workspace_first * PAGE_SIZE);
        CHECK((u32)eligible == workspace_end * PAGE_SIZE);
        CHECK((u32)eligible >= MEM_APP_BAND_MAX_TOP);
        CHECK((u32)eligible < MEM_SYSTEM_SPACE_BASE);
        CHECK(workspace_first >= MEM_APP_BAND_MAX_TOP / PAGE_SIZE);
        CHECK(workspace_end <= MEM_SYSTEM_SPACE_BASE / PAGE_SIZE);
        /* ブート窓より上の identity PT は workspace (RAM) から動的に取る */
        for (i = PAGING_BOOT_PT_COUNT; i < limit / PTE_COUNT; i++) {
            CHECK((u32)page_tables[i] >= workspace_first * PAGE_SIZE);
            CHECK((u32)page_tables[i] < workspace_end * PAGE_SIZE);
        }
        CHECK(host_if == 0x202U);
        die(0);
    }
#endif
#if defined(TEST_FIXED)
    /* 8MB・9MB・12MB (低位の上端 <= 3,073 PFN): FIXED 型 (§3-3)。legacy は
     * もう無いので、ここもモデル経路で ONLINE になる (D32)。 */
    {
        u32 top = TEST_KB / (PAGE_SIZE / 1024UL);
        u32 meta = MEM_LEDGER_META_BASE / PAGE_SIZE;
        CHECK(top <= MEMORY_BOOT_FIXED_MAX_TOP);
        CHECK(bootstrap_calls == 1 && stage_calls == 1);
        CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
        CHECK(ledger_backing_mapped);
        CHECK((u32)eligible == MEM_LEDGER_META_BASE);
        CHECK(workspace_first == meta + 1);
        CHECK(workspace_end == MEM_LEDGER_META_END / PAGE_SIZE);
        /* backing は L0 で RESERVED (RAM から 1 ページも取っていない) */
        CHECK(physmem_count(&device_boot_map, meta, workspace_end,
                            PHYSMEM_RESERVED, &count) && count == 2);
        CHECK(pgalloc_total_pages() == top - MEM_POOL_BASE / PAGE_SIZE);
        CHECK(pgalloc_limit_pfn() == top);
        /* exec の上端はアリーナの上端 = 低位 RAM の上端 (B2)。8MB なら
         * 0x800000。backing の位置 (0x2F9000) とは無関係。 */
        CHECK(pgalloc_arena_end() == top);
        CHECK(sys_usable_mem_end() == TEST_KB * 1024UL);
        CHECK(sys_usable_mem_end() >= MEM_EXEC_LOAD_ADDR + MEM_EXEC_STACK_SIZE +
              MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN);
        /* backing は present / supervisor / RW、下のガードは NP のまま */
        CHECK(paging_verify_identity(meta, 2, (void *)MEM_LEDGER_META_BASE));
        CHECK(!paging_is_present(MEM_LEDGER_META_BASE - PAGE_SIZE));
        CHECK(!paging_is_present(MEM_LEDGER_META_END));
        /* 8MiB 構成では上端と実 RAM 合計が一致する (穴が上端より上にある) */
        CHECK(memory_boot_ram_kb() == TEST_KB && sys_mem_kb == TEST_KB);
        /* master PT の workspace は backing の 2 ページ目 */
        CHECK(pgalloc_alloc_pt() == MEM_LEDGER_META_BASE + PAGE_SIZE);
        CHECK(pgalloc_alloc_pt() == 0);
        pgalloc_free_pt(MEM_LEDGER_META_BASE + PAGE_SIZE);
        CHECK(pgalloc_alloc_page() == MEM_POOL_BASE);
    }
#elif defined(TEST_ARENA)
    /* 低位の上端が 3,074 PFN 以上 (12MB + 8KB〜15MB)、高位 RAM なし:
     * ARENA_TOP 型。置き場は今と同じ低位 RAM の末尾 (回帰なし)。 */
    {
        u32 top = (TEST_KB < MEMORY_BOOT_LEGACY_END / 1024UL ? TEST_KB :
                   MEMORY_BOOT_LEGACY_END / 1024UL) / (PAGE_SIZE / 1024UL);
        CHECK(top > MEMORY_BOOT_FIXED_MAX_TOP);
        CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
        CHECK(!ledger_backing_mapped);
        CHECK(!paging_is_present(MEM_LEDGER_META_BASE));
        CHECK(workspace_end == top - 1 && workspace_first == top - 2);
        CHECK((u32)eligible == (top - 1) * PAGE_SIZE);
        CHECK(workspace_first >= MEM_APP_BAND_MAX_TOP / PAGE_SIZE);
        CHECK(pgalloc_arena_end() == workspace_first);
        CHECK(sys_usable_mem_end() == workspace_first * PAGE_SIZE);
        CHECK(pgalloc_limit_pfn() == top);
    }
#else
    CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
    CHECK(sys_usable_mem_end() == 0xEFE000);
    CHECK(pgalloc_arena_end() == 0xEFE);
    CHECK(workspace_first == 0xEFE && workspace_end == 0xEFF);
    CHECK((u32)eligible == 0xEFF000);
    CHECK(!ledger_backing_mapped);
    CHECK(physmem_count(&device_boot_map, 0xF00, 0x100000, PHYSMEM_UNKNOWN, &count));
    CHECK(count == 0x100000 - 0xF00);
    /* 窓が無くなったのでアリーナは 15MiB ちょうどで終わる (排他上限 0xF00)。
     * 未証明の [15,16)MiB へは踏み込まない、という意図は変わらない。 */
    CHECK(pgalloc_limit_pfn() == 0xF00);
#endif
    (void)count;
    CHECK(pgalloc_alloc_page() != 0);
    CHECK(host_if == 0x202U);
    die(0);
}
