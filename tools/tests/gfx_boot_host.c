/* ========================================================================
 *  gfx_boot_host.c — 起動時の ⑥ gfx の識別 → 予約 → 写像 → BB → SURFACE
 *
 *  実行: python3 -B tools/tests/test_gfx_boot.py [--mutate]
 *  票: docs/tasks/v3/TASK_T1_LEDGER.md §3-3 ⑥・⑨、§3-6、§3-8、§4-5 (T1e)
 *
 *  実物の kernel/paging.c + kernel/pgalloc.c + kernel/sys.c + kernel/physmem.c
 *  を ILP32 でそのまま組み、段つき起動 (sys_memory_bootstrap_model →
 *  sys_memory_stage_online) で台帳を ONLINE にしてから、gfx/gfx_core.c から
 *  切り出した本物の gfx_boot_reserve / gfx_bb_phys_range /
 *  gfx_client_to_gshell (test_gfx_boot.py が生成する gfx_boot_slice.inc) を
 *  走らせる。切り出しは次の呼び出しを記録係 (host_*) へ付け替える — 記録係は
 *  順序を記録して実物へ渡す (写像だけは指定の窓で失敗させられる):
 *    backend の identify → P / C、ledger_reserve_set → R、paging_map_phys → M、
 *    pgalloc_alloc_n_owner → B、ledger_surface_create → S、
 *    ledger_arena_freeze → F
 *  BIOS ワーク 045Ch の読み (9821 系か) だけは host_bda に置き換える。
 *
 *  構成は -D で 1 プロセス 1 通り (⑥ は起動に 1 回だけなので):
 *    CFG_KB      8192 (FIXED 型) / 17408 (低位 15MB + 高位 1MB) / 65536
 *    PREF        GFX_PREF_*          IS9821  BIOS ワーク 045Ch bit6
 *    PEGC_ID / CIRRUS_ID  各 backend の副作用のない識別の答え
 *    HW          模擬選択で使う装置 (0 = 無し / LEDGER_SF_PEGC / _CIRRUS)
 *    FAIL_MAP    写像を失敗させる窓 (0 / LEDGER_SF_PEGC / LEDGER_SF_CIRRUS)
 *    FAIL_BB     1 = アリーナを AS owner で埋めて BB を取れなくする
 *
 *  見るもの:
 *    - 順序: 識別 → 予約 (1 回、owner = gfx) → 写像 (予約済みの窓だけ、
 *      supervisor + PCD) → BB → SURFACE → 凍結
 *    - 予約: 候補の窓が DEVICE (owner = gfx) で、併合後の res_mask。写像や BB
 *      の失敗や模擬選択によらず予約は残る (永久保持)
 *    - 写像: 写像範囲だけ (PEGC 512KB、Xe10 は 2MB で decode 4MB の残りは
 *      張らない)、失敗した窓の PTE は変わらない、模擬選択後も PTE は残る
 *    - BB: 量は候補の最大 (PEGC が残れば 300KB、無ければ 0)、アリーナ内の
 *      上端 (8MB 0x7B5000 / 17MB 0xEB2000 / 64MB も低位、X14)、0 で埋まる、
 *      owner = boot。sys_usable_mem_end() は BB の下端 (§3-6)、BB が無ければ
 *      凍結した exec 上端のまま (ledger_arena_top() = pgalloc_arena_end())
 *    - SURFACE: planar は常に、PEGC の CLIENT (RAM)、Cirrus の CLIENT +
 *      DISPLAY (MMIO、UC、表示面は kernel)
 *    - 模擬選択に対して gfx_bb_phys_range は選択中の CLIENT、⑨ の移譲は
 *      その CLIENT だけ (RAM は L2 のページごと、固定 RAM は SURFACE_BACKING
 *      区間も、MMIO は SURFACE だけ)、2 回目は無操作、ledger_selfcheck 0 件
 *
 *  保証範囲: backend の実物の選択処理・probe は呼ばず、HW と SURFACE の
 *  存在から g_backend を代入する。予約・写像・確保・移譲と 14 変異は検証するが、
 *  Cirrus probe 失敗 → PEGC 選択や fb->planes[0] までの統合は保証しない。
 *  予約拒否・SURFACE 登録拒否は 17 構成に含めていない。
 *
 *  GNU11 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */
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
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    if (!code && host_if != 0x202U) code = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL " #x); die(1); } } while (0)

#include "gfx_hal.h"
#include "gfx.h"
#include "pegc.h"
#include "wab_xe10.h"

/* ---- 構成 (-D) ---- */
#ifndef CFG_KB
#define CFG_KB 17408
#endif
#ifndef PREF
#define PREF GFX_PREF_AUTO
#endif
#ifndef IS9821
#define IS9821 1
#endif
#ifndef PEGC_ID
#define PEGC_ID 1
#endif
#ifndef CIRRUS_ID
#define CIRRUS_ID 1
#endif
#ifndef HW
#define HW 0
#endif
#ifndef FAIL_MAP
#define FAIL_MAP 0
#endif
#ifndef FAIL_BB
#define FAIL_BB 0
#endif

/* ---- カーネルの残りの依存 ---- */
void *kmemset(void *dst, int val, u32 n)
{
    u8 *p = (u8 *)dst;
    while (n--) *p++ = (u8)val;
    return dst;
}
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* ---- gfx_core.c の周り (切り出しが引く名前) ---- */
static int g_backend_pref = PREF;
const GfxBackend gfx_backend_pc98;
GfxBackend gfx_backend_pegc;
GfxBackend gfx_backend_cirrus;
const GfxBackend *g_backend = &gfx_backend_pc98;

static char trace[40];
static u32 ntrace;
static void note(char c) { if (ntrace < sizeof(trace) - 1) trace[ntrace++] = c; }
static u32 count(char c)
{
    u32 i, n = 0;
    for (i = 0; i < ntrace; i++) n += trace[i] == c;
    return n;
}

int pegc_identify(void) { note('P'); CHECK(ledger_region_count == 3); return PEGC_ID; }
int cirrus_identify(void) { note('C'); CHECK(ledger_region_count == 3); return CIRRUS_ID; }
static u8 host_bda(volatile u32 addr)
{
    CHECK(addr == PEGC_BIOS_ARCH_FLAG);
    return IS9821 ? PEGC_BIOS_ARCH_EXTGFX : 0;
}

/* [first, end) の全ページが owner = gfx の DEVICE 区間 1 本に入っているか。 */
static const struct ledger_region *gfx_region(u32 first, u32 end)
{
    u32 i;
    const struct ledger_region *r;
    for (i = 0; i < ledger_region_count; i++) {
        r = &ledger_regions[i];
        if (r->type == LEDGER_R_DEVICE && r->owner == LEDGER_OWNER_GFX &&
            r->first <= first && end <= r->end) return r;
    }
    return 0;
}

int host_reserve(u32 owner, const struct ledger_span *s, u32 n)
{
    note('R');
    CHECK(owner == LEDGER_OWNER_GFX);
    CHECK(count('R') == 1);                 /* 候補は 1 回の一括 commit (B3) */
    return ledger_reserve_set(owner, s, n);
}
int host_map(u32 virt, u32 phys, u32 n, u32 flags)
{
    note('M');
    CHECK(virt == phys && flags == (PAGE_RW | PTE_PCD));
    /* 予約 → 写像: 張る窓は予約済み (装置の予約の内側だけを張る) */
    CHECK(gfx_region(phys / PAGE_SIZE, phys / PAGE_SIZE + n) != 0);
    if ((FAIL_MAP == LEDGER_SF_PEGC && phys == PEGC_LINEAR_BASE) ||
        (FAIL_MAP == LEDGER_SF_CIRRUS && phys == WAB_XE10_LINEARWIN_BASE))
        return -1;
    return paging_map_phys(virt, phys, n, flags);
}
int host_alloc(u32 owner, int n, u32 first, u32 end, u32 dir, u32 *pfn)
{
    note('B');
    return pgalloc_alloc_n_owner(owner, n, first, end, dir, pfn);
}
int host_surface(const struct ledger_surface *sf, u32 *sid)
{
    note('S');
    return ledger_surface_create(sf, sid);
}
void host_freeze(void)
{
    note('F');
    ledger_arena_freeze();
}

#include "gfx_boot_slice.inc"

/* ---- 起動 (memory_boot_init の ③ と同じ段) ---- */
static u32 arena_top_pfn;   /* 凍結した exec 上端 (PFN) */

static u32 ws_pages(u32 limit)
{
    u32 pages = 1;
    if (limit > PAGING_BOOT_MAP_SIZE / PAGE_SIZE)
        pages += (limit + PTE_COUNT - 1) / PTE_COUNT - PAGING_BOOT_PT_COUNT;
    return pages;
}

static void boot(void)
{
    struct physmem m;
    struct pgalloc_layout l;
    u32 low_kb = CFG_KB > 15360 ? 15360 : CFG_KB;
    u32 limit = CFG_KB / 4, top;
    {
        u32 low_args[6] = {MEM_GFX_BB_BASE, MEM_GFX_BB_SIZE, 3, 0x32, 0xffffffffUL, 0};
        u32 low_result;
        __asm__ volatile("int $0x80" : "=a"(low_result) : "a"(90), "b"(low_args) : "memory");
        CHECK(low_result == MEM_GFX_BB_BASE);
    }
    host_map_fixed_paging();
    paging_init(CFG_KB);
    physmem_bootstrap_legacy(&m, low_kb);
    if (CFG_KB > 16384)
        CHECK(physmem_add_trusted(&m, 4096, limit, PHYSMEM_SOURCE_SYNTHETIC));
    top = physmem_legacy_end(&m);
    l.capacity = pgalloc_metadata_bytes(&m);
    if (CFG_KB > 12288) {
        l.kind = PGALLOC_BACKING_ARENA_TOP;
        l.metadata_first = top - l.capacity / PAGE_SIZE;
        l.metadata = (void *)(l.metadata_first * PAGE_SIZE);
        l.workspace_end = l.metadata_first;
        l.workspace_first = l.workspace_end - ws_pages(limit);
        arena_top_pfn = l.workspace_first;
    } else {
        CHECK(paging_map_ledger_backing());
        l.kind = PGALLOC_BACKING_FIXED;
        l.metadata_first = MEM_LEDGER_META_BASE / PAGE_SIZE;
        l.metadata = (void *)MEM_LEDGER_META_BASE;
        l.workspace_first = l.metadata_first + 1;
        l.workspace_end = MEM_LEDGER_META_END / PAGE_SIZE;
        arena_top_pfn = top;
    }
    CHECK(sys_memory_bootstrap_model(&m, &l, paging_verify_identity));
    CHECK(sys_memory_stage_online());
    CHECK(paging_boot_context());
    /* ③ の区間のうち ⑥ に効くもの: planar BB の固定面と背景 2 本 */
    CHECK(ledger_register_region(LEDGER_R_SURFACE_BACKING, LEDGER_OWNER_BOOT,
                                 MEM_GFX_BB_BASE / PAGE_SIZE,
                                 (MEM_GFX_BB_BASE + MEM_GFX_BB_SIZE) / PAGE_SIZE,
                                 LEDGER_CACHE_WB, 0));
    CHECK(ledger_register_region(LEDGER_R_BACKGROUND, LEDGER_OWNER_KERNEL,
                                 MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                                 MEM_HIGH_RAM_BASE / PAGE_SIZE, LEDGER_CACHE_UC, 0));
    CHECK(ledger_register_region(LEDGER_R_BACKGROUND, LEDGER_OWNER_KERNEL,
                                 MEM_PHYS_RAM_CEILING / PAGE_SIZE, PHYSMEM_MAX_PFN,
                                 LEDGER_CACHE_UC, 0));
    CHECK(pgalloc_arena_end() == arena_top_pfn);
    CHECK(sys_usable_mem_end() == arena_top_pfn * PAGE_SIZE);
}

static u32 pte(u32 va)
{
    u32 *t = page_tables[va >> 22];
    return t ? t[(va >> PAGE_SHIFT) % PTE_COUNT] : 0;
}

/* 写像範囲が supervisor + RW + PCD の恒等で張られているか。 */
static int mapped(u32 base, u32 bytes)
{
    u32 va;
    for (va = base; va < base + bytes; va += PAGE_SIZE)
        if ((pte(va) & (0xFFFFF000UL | PTE_PRESENT | PAGE_RW | PTE_USER | PTE_PCD)) !=
            (va | PTE_PRESENT | PAGE_RW | PTE_PCD)) return 0;
    return 1;
}

static u32 pte_before_pegc, pte_before_cirrus, pte_before_decode;

void _start(void)
{
    u32 args[6] = {0x200000, 0xE00000, 3, 0x32, 0xffffffffUL, 0};
    u32 result, i, p, filler = 0, base, size, bb_first = 0;
    int is_c, pc, cc, map_pegc, map_cirrus, bb, cirrus_ok, sel;
    const struct ledger_surface *sp, *se, *sc, *sd, *cl;
    struct ledger_surface snap[LEDGER_MAX_SURFACES];
    char want[40];
    u32 nw = 0;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x200000);
    boot();

    /* 期待値 (§3-8 の表): 候補 → 写像 → BB → SURFACE */
    is_c = IS9821 && PREF != GFX_PREF_PC98;
    pc = is_c && PREF != GFX_PREF_CIRRUS && PEGC_ID;
    cc = is_c && PREF != GFX_PREF_PEGC && CIRRUS_ID;
    map_pegc = pc && FAIL_MAP != LEDGER_SF_PEGC;
    map_cirrus = cc && FAIL_MAP != LEDGER_SF_CIRRUS;
    bb = map_pegc && !FAIL_BB;
    cirrus_ok = map_cirrus;
    if (is_c && PREF != GFX_PREF_CIRRUS) want[nw++] = 'P';
    if (is_c && PREF != GFX_PREF_PEGC) want[nw++] = 'C';
    if (pc || cc) want[nw++] = 'R';
    if (pc) want[nw++] = 'M';
    if (cc) want[nw++] = 'M';
    if (map_pegc) want[nw++] = 'B';
    want[nw++] = 'S';
    if (bb) want[nw++] = 'S';
    if (cirrus_ok) { want[nw++] = 'S'; want[nw++] = 'S'; }
    want[nw++] = 'F';

    pte_before_pegc = pte(PEGC_LINEAR_BASE);
    pte_before_cirrus = pte(WAB_XE10_LINEARWIN_BASE);
    pte_before_decode = pte(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_SIZE);
    if (FAIL_BB) {
        /* アリーナを AS owner で埋める (AS は凍結の対象外 = 上端は動かない) */
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 9, "fill", &filler));
        CHECK(pgalloc_alloc_n_owner(filler, (int)(arena_top_pfn - 0x500), 0x500,
                                    arena_top_pfn, LEDGER_BOTTOM_UP, &p));
    }
    /* BB の置き場には前の中身があったことにする (0 で埋めることを見る) */
    if (bb) kmemset((void *)((arena_top_pfn - 75) * PAGE_SIZE), 0xA5, 75 * PAGE_SIZE);

    gfx_boot_reserve();

    /* 順序 */
    CHECK(ntrace == nw);
    for (i = 0; i < nw; i++) CHECK(trace[i] == want[i]);

    /* 予約 (写像・BB の成否と模擬選択によらず残る) */
    CHECK(!pc == !gfx_region(PEGC_LINEAR_BASE / PAGE_SIZE,
                             (PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE) / PAGE_SIZE));
    CHECK(!cc == !gfx_region(WAB_XE10_LINEARWIN_BASE / PAGE_SIZE,
                             (WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_DECODE) / PAGE_SIZE));
    /* 銀行窓は PEGC の窓の中 (PEGC も候補なら併合された 1 区間に入る) */
    CHECK(!(pc || cc) == !gfx_region(WAB_XE10_WIN_BASE / PAGE_SIZE,
                                     (WAB_XE10_WIN_BASE + WAB_XE10_WIN_SIZE) / PAGE_SIZE));
    if (pc && cc) {   /* PEGC 窓と銀行窓は併合され、根拠は 2 本 (B10) */
        const struct ledger_region *r = gfx_region(PEGC_LINEAR_BASE / PAGE_SIZE,
                                                   PEGC_LINEAR_BASE / PAGE_SIZE + 1);
        CHECK(r->first == PEGC_LINEAR_BASE / PAGE_SIZE &&
              r->end == (PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE) / PAGE_SIZE);
        p = r->res_mask;
        CHECK(p && (p & (p - 1)) && !((p & (p - 1)) & ((p & (p - 1)) - 1)));
    }

    /* 写像: 張ったのは写像範囲だけ、失敗した窓の PTE は前のまま */
    if (map_pegc) CHECK(mapped(PEGC_LINEAR_BASE, PEGC_LINEAR_SIZE));
    else CHECK(pte(PEGC_LINEAR_BASE) == pte_before_pegc);
    if (map_cirrus) CHECK(mapped(WAB_XE10_LINEARWIN_BASE, WAB_XE10_LINEARWIN_SIZE));
    else CHECK(pte(WAB_XE10_LINEARWIN_BASE) == pte_before_cirrus);
    CHECK(pte(WAB_XE10_LINEARWIN_BASE + WAB_XE10_LINEARWIN_SIZE) == pte_before_decode);

    /* BB と SURFACE */
    sp = ledger_surface_find(LEDGER_SF_PC98, LEDGER_ROLE_CLIENT);
    se = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_CLIENT);
    sc = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_CLIENT);
    sd = ledger_surface_find(LEDGER_SF_CIRRUS, LEDGER_ROLE_DISPLAY);
    CHECK(sp && sp->first == MEM_GFX_BB_BASE / PAGE_SIZE &&
          sp->npages == MEM_GFX_BB_SIZE / PAGE_SIZE &&
          sp->backing == LEDGER_SB_FIXED_RAM && sp->owner == LEDGER_OWNER_BOOT);
    CHECK(!se == !bb);
    CHECK(ledger_owner_pages(LEDGER_OWNER_BOOT) == (bb ? 75U : 0U));
    if (bb) {
        bb_first = se->first;
        /* アリーナ内の上端 (旧 sys_reserve_top と同じ区間、X14) */
        CHECK(bb_first == arena_top_pfn - 75);
        CHECK(CFG_KB != 8192 || bb_first * PAGE_SIZE == 0x7B5000UL);
        CHECK(CFG_KB != 17408 || bb_first * PAGE_SIZE == 0xEB2000UL);
        CHECK(bb_first * PAGE_SIZE + MEM_GFX_BB8_SIZE <= MEM_SYSTEM_SPACE_BASE);
        CHECK(se->npages == 75 && se->backing == LEDGER_SB_RAM &&
              se->owner == LEDGER_OWNER_BOOT && se->cache == LEDGER_CACHE_WB);
        for (i = 0; i < 75; i++) CHECK(owner_map[bb_first + i] == LEDGER_OWNER_BOOT);
        for (i = 0; i < MEM_GFX_BB8_SIZE; i++)
            CHECK(((const u8 *)(bb_first * PAGE_SIZE))[i] == 0);
        CHECK(sys_usable_mem_end() == bb_first * PAGE_SIZE);
        CHECK(ledger_arena_top() == bb_first);
    } else {
        /* 永続確保が無ければアリーナの上端 = 凍結した exec 上端のまま */
        CHECK(ledger_arena_top() == pgalloc_arena_end());
        CHECK(sys_usable_mem_end() == arena_top_pfn * PAGE_SIZE);
    }
    CHECK(!sc == !cirrus_ok && !sd == !cirrus_ok);
    if (cirrus_ok) {
        CHECK(sc->first * PAGE_SIZE == WAB_XE10_LINEARWIN_BASE + MEM_GFX_BB8_SIZE &&
              sc->npages == 75 && sc->backing == LEDGER_SB_MMIO &&
              sc->cache == LEDGER_CACHE_UC && sc->owner == LEDGER_OWNER_BOOT &&
              sc->perm_max == LEDGER_PERM_RW);
        CHECK(sd->first * PAGE_SIZE == WAB_XE10_LINEARWIN_BASE && sd->npages == 75 &&
              sd->backing == LEDGER_SB_MMIO && sd->cache == LEDGER_CACHE_UC &&
              sd->owner == LEDGER_OWNER_KERNEL && sd->perm_max == LEDGER_PERM_NONE);
    }
    CHECK(ledger_check_fail == 0);

    /* ⑦ の模擬選択: HW と SURFACE の存在から代入する。
     * 実物の選択処理・probe と fb->planes[0] までの統合は実行しない。 */
    sel = LEDGER_SF_PC98;
    if (HW == LEDGER_SF_CIRRUS && sc) sel = LEDGER_SF_CIRRUS;
    if (HW == LEDGER_SF_PEGC && se) sel = LEDGER_SF_PEGC;
    g_backend = sel == LEDGER_SF_CIRRUS ? &gfx_backend_cirrus :
                sel == LEDGER_SF_PEGC ? (const GfxBackend *)&gfx_backend_pegc :
                &gfx_backend_pc98;
    cl = sel == LEDGER_SF_CIRRUS ? sc : sel == LEDGER_SF_PEGC ? se : sp;
    gfx_bb_phys_range(&base, &size);
    CHECK(base == cl->first * PAGE_SIZE && size == cl->npages * PAGE_SIZE);

    /* ⑨ 選択中の CLIENT だけを boot → gshell */
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) snap[i] = ledger_surfaces[i];
    gfx_client_to_gshell();
    CHECK(cl->owner == LEDGER_OWNER_GSHELL);
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        if (&ledger_surfaces[i] != cl) CHECK(ledger_surfaces[i].owner == snap[i].owner);
    if (sel == LEDGER_SF_PEGC) {
        for (i = 0; i < 75; i++) CHECK(owner_map[bb_first + i] == LEDGER_OWNER_GSHELL);
        CHECK(ledger_owner_pages(LEDGER_OWNER_BOOT) == 0 &&
              ledger_owner_pages(LEDGER_OWNER_GSHELL) == 75);
    } else {
        CHECK(ledger_owner_pages(LEDGER_OWNER_BOOT) == (bb ? 75U : 0U));
        CHECK(ledger_owner_pages(LEDGER_OWNER_GSHELL) == 0);
    }
    for (i = 0; i < ledger_region_count; i++) {
        const struct ledger_region *r = &ledger_regions[i];
        if (r->type == LEDGER_R_SURFACE_BACKING)
            CHECK(r->owner == (sel == LEDGER_SF_PC98 ? LEDGER_OWNER_GSHELL
                                                     : LEDGER_OWNER_BOOT));
        if (r->type == LEDGER_R_DEVICE) CHECK(r->owner == LEDGER_OWNER_GFX);
    }
    /* 2 回目 (GUI → CUI → GUI) は無操作、BB の番地も変わらない */
    for (i = 0; i < LEDGER_MAX_SURFACES; i++) snap[i] = ledger_surfaces[i];
    gfx_client_to_gshell();
    for (i = 0; i < LEDGER_MAX_SURFACES; i++)
        CHECK(ledger_surfaces[i].owner == snap[i].owner &&
              ledger_surfaces[i].first == snap[i].first);
    gfx_bb_phys_range(&p, &size);
    CHECK(p == base);
    /* 写像と予約は模擬選択の後も残る */
    if (map_pegc) CHECK(mapped(PEGC_LINEAR_BASE, PEGC_LINEAR_SIZE));
    if (map_cirrus) CHECK(mapped(WAB_XE10_LINEARWIN_BASE, WAB_XE10_LINEARWIN_SIZE));
    CHECK(ledger_selfcheck("t") && ledger_check_fail == 0);
    (void)filler;
    SAY("gfx_boot: PASS");
    die(0);
}
