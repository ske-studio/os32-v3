/* ========================================================================
 *  sbrk_tier_host.c — CPL=3 プログラムの sbrk 物理「二段構え」の性質試験
 *
 *  対象票: docs/archive/gui_v13/TASK_K5B_kernel.md (作業 8)
 *  決裁:   docs/archive/gui_v13/TASK_K5_multiapp.md 決裁表「sbrk は二段構え」
 *  実行:   python3 -B tools/tests/test_sbrk_tier.py
 *  記録:   tools/tests/k5b_kernel_tdd.md (回 4)
 *
 *  試験するのは 4 つの性質:
 *    (1) 空きが十分 → 段 1。sbrk 上端は従来式 (K5b-K 以前) と同じ guard_a。
 *    (2) 空きが少ない → 段 2。sbrk 上端は code_end + MEM_EXEC_SBRK_MIN。
 *    (3) 段 2 でも足りない → 起動を拒否し、既に走っているアプリに触らない。
 *    (4) 3 領域の外で取る付随ページ (共有ライブラリの .data 複製) も
 *        勘定に入れる (K7、2026-09-11)。
 *
 *  (1)(2)(4) は **実物の判定関数** exec/exec.c の exec_sbrk_pick_tier() /
 *  exec_ring3_pages() / exec_ring3_extra_pages() を試験ドライバが切り出して
 *  ここへ差し込んで回す
 *  (tools/tests/test_pgalloc_model.py が exec_child_claim を切り出すのと
 *  同じ流儀。並行して書いた別式ではなく、出荷するコードそのものを見る)。
 *  (3) は実物の exec/appslot.c をそのままコンパイルして回す。
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */

#include "types.h"
#include "paging.h"          /* PAGE_SIZE / memmap.h (MEM_EXEC_* / MEM_APP_BAND_*) */

/* ---- 実物のカーネルコード (ハードウェアには一切触らない部分) ---------- */
extern void res_owner_set(int owner);
extern int  res_owner_get(void);
static int host_owner = 1;
void res_owner_set(int owner) { host_owner = owner; }
int  res_owner_get(void)      { return host_owner; }

/* appslot.c が引く (票 T9 §12 T1: リダイレクト表を ID の文脈にする)。
 * この票の対象外なので空の錠にする — 表そのものは実物の fs/fd_redirect.c と
 * 組んで tools/tests/multiapp_impl_host.c のケース 24 が見ている。 */
#include "fd_redirect.h"
void fd_redirect_save(FdRedirectState *out)         { (void)out; }
void fd_redirect_restore(const FdRedirectState *in) { (void)in; }
void fd_redirect_clear_state(FdRedirectState *st)   { (void)st; }

#include "appslot.c"

/* 共有ライブラリの .data/.bss 複製ページ数 (kernel/shlib.c の実物の口)。
 * ホストでは値を差し替えられるようにして、未ロード (0) と 4 ページの
 * 両方を回す。exec_ring3_extra_pages() がこれを呼ぶ (K7)。 */
static u32 host_shlib_pages = 0;
u32 shlib_data_pages(void);
u32 shlib_data_pages(void) { return host_shlib_pages; }

/* exec/exec.c から切り出した本物の判定関数 + RING3_USTACK_SIZE の定義。
 * 中身は tools/tests/test_sbrk_tier.py が生成する (実物のテキストそのまま)。 */
#include "exec_sbrk_tier.inc"

/* ---- 出力 ------------------------------------------------------------- */
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text)
{
    u32 len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    if (cond) {
        report("  ok   ");
    } else {
        report("  FAIL ");
        failures++;
    }
    report(name);
    report("\n");
}

/* ---- 試験に使うレイアウト --------------------------------------------
 *  exec_launch() が heap_size 未指定の CPL=3 プログラムに対して組む
 *  レイアウトを、そのままの式で 1 つ作る。帯は既定の 1 枚 (4MB)。
 *
 *    band_top   = (MEM_LEGACY_APP_BASE + MEM_APP_BAND_PDE_SIZE)
 *    heap_top   = band_top - スタック - ガード          (= RING3_HEAP_TOP)
 *    code_end   = ページ境界へ切り上げた text+bss の終端
 *    avail      = heap_top - code_end - 最低分 - ガード
 *    exec_heap  = avail / 2                             (heap_size 未指定)
 *    guard_a    = heap_top - exec_heap - ガード
 * ---------------------------------------------------------------------- */
typedef struct {
    u32 load_base;
    u32 code_end;
    u32 guard_a;
    u32 exec_heap_size;
    u32 stack_size;
} Layout;

static int layout_make(Layout *L, u32 text_bss, u32 band_pdes)
{
    u32 band_top = MEM_LEGACY_APP_BASE + band_pdes * MEM_APP_BAND_PDE_SIZE;
    u32 heap_top = band_top - RING3_USTACK_SIZE - PAGE_SIZE;
    u32 avail;

    L->load_base = MEM_PHYS_EXEC_FLOOR;
    L->stack_size = MEM_EXEC_STACK_SIZE;
    L->code_end = PAGE_ALIGN_UP(MEM_PHYS_EXEC_FLOOR + text_bss);
    if (heap_top < L->code_end) return 0;
    if (heap_top - L->code_end < MEM_EXEC_SBRK_MIN + PAGE_SIZE + MEM_EXEC_HEAP_MIN)
        return 0;
    avail = heap_top - L->code_end - MEM_EXEC_SBRK_MIN - PAGE_SIZE;
    L->exec_heap_size = (avail / 2) & ~(u32)(PAGE_SIZE - 1);
    if (L->exec_heap_size < MEM_EXEC_HEAP_MIN) L->exec_heap_size = MEM_EXEC_HEAP_MIN;
    L->guard_a = heap_top - L->exec_heap_size - PAGE_SIZE;
    return 1;
}

static u32 pages_at(const Layout *L, u32 sbrk_end)
{
    return exec_ring3_pages(MEM_EXEC_LOAD_ADDR, MEM_EXEC_LOAD_ADDR + sbrk_end - L->load_base,
                            L->exec_heap_size, L->stack_size);
}

/* ====================================================================== */
/*  性質 1 — 空き物理に余裕があれば従来どおり「帯の残り」を張る            */
/* ====================================================================== */
static void case_tier1_when_free(void)
{
    Layout L;
    u32 hi, lo, need_hi, sbrk_end;
    int t;

    report("case 1: 空きが十分 -> 段 1 (従来式)\n");
    check(layout_make(&L, 0x10000UL, 1), "1a 64KB のプログラムでレイアウトが組める");
    hi = L.guard_a;
    lo = L.code_end + MEM_EXEC_SBRK_MIN;
    check(lo < hi, "1b この構成では最低分より帯の残りの方が広い (段が分かれる)");
    need_hi = pages_at(&L, hi);

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, need_hi, &sbrk_end);
    check(t == 1, "1c ちょうど収まる空きなら段 1");
    check(sbrk_end == hi, "1d 段 1 の sbrk 上端は guard_a (K5b-K 以前と同じ)");
    check(sbrk_end - L.code_end > MEM_EXEC_SBRK_MIN,
          "1e 段 1 は最低分より広い物理を張る");

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size,
                            need_hi + 1024, &sbrk_end);
    check(t == 1 && sbrk_end == hi, "1f 空きに余裕があっても段 1 のまま");

    /* 従来式 = 「帯の残り半分」。exec_heap が半分、sbrk が残り半分 + 最低分。 */
    check(L.exec_heap_size >= MEM_EXEC_HEAP_MIN,
          "1g exec_heap は折半で取れている");
    check(pages_at(&L, hi) > pages_at(&L, lo),
          "1h 段 1 の方が必要な物理は多い");
}

/* ====================================================================== */
/*  性質 2 — 空きが足りなければ最低分 (256KB) に落とす                     */
/* ====================================================================== */
static void case_tier2_when_tight(void)
{
    Layout L;
    u32 hi, lo, need_hi, need_lo, sbrk_end;
    int t;

    report("case 2: 空きが少ない -> 段 2 (最低分 256KB)\n");
    (void)layout_make(&L, 0x10000UL, 1);
    hi = L.guard_a;
    lo = L.code_end + MEM_EXEC_SBRK_MIN;
    need_hi = pages_at(&L, hi);
    need_lo = pages_at(&L, lo);
    check(need_lo < need_hi, "2a 段 2 の方が必要な物理は少ない");

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size,
                            need_hi - 1, &sbrk_end);
    check(t == 2, "2b 段 1 が 1 ページ足りないだけで段 2 へ落ちる");
    check(sbrk_end == lo, "2c 段 2 の sbrk 上端は code_end + MEM_EXEC_SBRK_MIN");
    check(sbrk_end - L.code_end == MEM_EXEC_SBRK_MIN,
          "2d 段 2 で張るのはきっかり 256KB");

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, need_lo, &sbrk_end);
    check(t == 2 && sbrk_end == lo, "2e 段 2 ちょうどの空きでも段 2 で立つ");

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, 0, &sbrk_end);
    check(t == 2 && sbrk_end == lo,
          "2f 空きゼロでも段の選択は段 2 (立てるかの判定は呼び出し側)");

    /* 帯の残りが最低分より狭い縁: 落としても従来式と同じものを張っている。 */
    {
        Layout T;
        u32 tight_end = 0;
        (void)layout_make(&T, 0x10000UL, 1);
        T.code_end = T.guard_a - (MEM_EXEC_SBRK_MIN / 2);
        t = exec_sbrk_pick_tier(T.load_base, T.code_end, T.guard_a,
                                T.exec_heap_size, T.stack_size, 0, &tight_end);
        check(tight_end == T.guard_a,
              "2g 帯の残りが最低分より狭ければ guard_a で頭打ち");
        check(t == 1, "2h その場合は従来式と同じものを張ったので段 1 と数える");
    }
}

/* ====================================================================== */
/*  性質 3 — 段 2 でも足りなければ拒否し、既存アプリに触らない             */
/* ====================================================================== */
static void case_nomem_leaves_others(void)
{
    Layout L;
    u32 need_lo;
    int id2, id3, live_before, cur_before, owner_before, r;

    report("case 3: 段 2 でも足りない -> 拒否、既存アプリはそのまま\n");
    (void)layout_make(&L, 0x10000UL, 1);
    need_lo = pages_at(&L, L.code_end + MEM_EXEC_SBRK_MIN);

    appslot_init();
    id2 = appslot_start_admit(0, need_lo, need_lo);
    check(id2 == 2, "3a 1 本目 (段 2 ちょうどの空き) は立つ");
    appslot_start_commit(id2, 0, need_lo);
    id3 = appslot_start_admit(0, need_lo, need_lo * 2);
    check(id3 == 3, "3b 2 本目も立つ");
    appslot_start_commit(id3, 0, need_lo);

    live_before = appslot_live();
    cur_before = appslot_cur();
    owner_before = res_owner_get();
    check(live_before == 2, "3c ここまでで生きている非シェルのアプリは 2 本");

    r = appslot_start_admit(0, need_lo, need_lo - 1);
    check(r == EXEC_ERR_NOMEM, "3d 段 2 の枚数が空きを 1 ページ超えたら拒否");
    check(appslot_live() == live_before, "3e 拒否で生存アプリの数は変わらない");
    check(appslot_cur() == cur_before, "3f 拒否で「現在のアプリ」は変わらない");
    check(res_owner_get() == owner_before, "3g 拒否で資源の所有者も変わらない");
    check(appslot_at(id2)->state == APP_STATE_RUNNING &&
          appslot_at(id3)->state == APP_STATE_RUNNING,
          "3h 既に走っている 2 本は RUNNING のまま");
    check(appslot_at(id2)->pages == need_lo && appslot_at(id3)->pages == need_lo,
          "3i 既存アプリの取り分 (pages) も削られない");

    /* 段 1 の枚数では入らないが段 2 なら入る空き = 「二段構えが効く」場面。 */
    {
        u32 need_hi = pages_at(&L, L.guard_a);
        int r_hi = appslot_start_admit(0, need_hi, need_lo);
        int r_lo;
        check(r_hi == EXEC_ERR_NOMEM, "3j 段 1 の枚数なら拒否される空きで…");
        r_lo = appslot_start_admit(0, need_lo, need_lo);
        check(r_lo == 4, "3k …段 2 の枚数なら 3 本目が立つ");
    }
}

/* ====================================================================== */
/*  性質 4 — 3 領域の外で per-app に取る付随ページ (K7)                    */
/*                                                                        */
/*  8MB 構成 (CUI の最低動作環境) のアプリ帯の空きは                       */
/*    ((MEM_LEGACY_APP_BASE + MEM_APP_BAND_PDE_SIZE) - MEM_PHYS_EXEC_FLOOR) / PAGE_SIZE = 768 ページ。   */
/*  段 1 の 3 領域 + PD + アプリ PT はここへ **ちょうど** 収まる            */
/*  (帯 768 - ガード 2 + PD 1 + PT 1)。よって段 1 を採ると空きが 0 になり、 */
/*  直後の shlib_addrspace_attach() の 4 ページが取れず、gui_demo が        */
/*  「shlib data attach failed (out of memory)」で立たなかった              */
/*  (PM 実測 2026-09-11、exec_sbrk_tier_last = 1)。                        */
/*  3 領域の後に同じ pgalloc から取る付随ページを勘定に入れれば段 2 へ倒れ、 */
/*  4 ページが残る。                                                       */
/* ====================================================================== */

/* 共有ライブラリ libos32gui.shlib の .data/.bss ページ数 (実測 2026-09-11)。*/
#define K7_SHLIB_DATA_PAGES  4

static void case_extra_pages_k7(void)
{
    Layout L;
    u32 free_8mb, need_hi_bare, need_hi, need_lo, sbrk_end;
    int t, id;

    report("case 4: 付随ページ (shlib data) を勘定に入れる (K7)\n");
    (void)layout_make(&L, 0x10000UL, 1);
    free_8mb = ((MEM_LEGACY_APP_BASE + MEM_APP_BAND_PDE_SIZE) - MEM_PHYS_EXEC_FLOOR) / PAGE_SIZE;

    host_shlib_pages = 0;
    need_hi_bare = pages_at(&L, L.guard_a);
    check(need_hi_bare == free_8mb + 3,
          "4a 8MB の空きに段 1 の 3 領域 + PD + PT はちょうど収まる");

    host_shlib_pages = K7_SHLIB_DATA_PAGES;
    need_hi = pages_at(&L, L.guard_a);
    check(need_hi == need_hi_bare + K7_SHLIB_DATA_PAGES,
          "4b 付随ページはそのまま枚数に足される");
    check(need_hi > free_8mb, "4c 付随ページを足すと段 1 は 8MB の空きを超える");

    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, free_8mb, &sbrk_end);
    check(t == 2, "4d だから段 1 を選ばず段 2 へ倒す (K7 の本体)");
    check(sbrk_end == L.code_end + MEM_EXEC_SBRK_MIN,
          "4e 段 2 の sbrk 上端は従来どおり code_end + 最低分");

    need_lo = pages_at(&L, sbrk_end);
    check(need_lo <= free_8mb, "4f 段 2 は付随ページ込みでも空きに収まる");
    host_shlib_pages = 0;
    check(pages_at(&L, sbrk_end) + K7_SHLIB_DATA_PAGES <= free_8mb,
          "4g 段 2 の 3 領域 + PD + PT の後に shlib の 4 ページが残る");
    host_shlib_pages = K7_SHLIB_DATA_PAGES;

    appslot_init();
    id = appslot_start_admit(1, need_lo, free_8mb);
    check(id == 2, "4h 段 2 の枚数なら GUI アプリが 1 本立つ (受入 G6)");
    check(appslot_start_admit(1, need_hi, free_8mb) == EXEC_ERR_NOMEM,
          "4i 段 1 の枚数 (付随込み) なら admit は NOMEM");

    /* 付随ページを含めても収まる空き = 段 1 のまま (15MB 以上の構成)。 */
    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, need_hi, &sbrk_end);
    check(t == 1 && sbrk_end == L.guard_a,
          "4j 付随ページを含めて収まるなら従来どおり段 1");

    /* 未ロード (CUI しかない機械) では 1 ページも増えない。 */
    host_shlib_pages = 0;
    check(pages_at(&L, L.guard_a) == need_hi_bare,
          "4k shlib 未ロードなら枚数は K7 以前と同じ");
    sbrk_end = 0;
    t = exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                            L.exec_heap_size, L.stack_size, free_8mb, &sbrk_end);
    check(t == 2 && sbrk_end == L.code_end + MEM_EXEC_SBRK_MIN,
          "4l shlib 未ロードでも疎PT込みでは8MBは段2");
}

static void case_variable_stack_boundary(void)
{
    Layout L;
    u32 end, hi, lo, gap;
    (void)layout_make(&L, 0x10000UL, 1); /* legacy 8MB byte budget stays fixed */
    L.stack_size = 512UL * 1024;
    hi = pages_at(&L, L.guard_a);
    lo = pages_at(&L, L.code_end + MEM_EXEC_SBRK_MIN);
    check(hi == 835, "8MB + 512KB: tier1 needs 835 pages, not legacy 768");
    appslot_init();
    for (gap = 1; gap <= 67; gap++) {
        check(exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
                  L.exec_heap_size, L.stack_size, hi - gap, &end) == 2,
              "underestimate gap chooses tier2");
        check(lo <= hi - gap, "tier2 fits underestimated gap");
        check(appslot_start_admit(0, pages_at(&L, end), hi - gap) == APP_ID_MIN,
              "actual tier2 admission succeeds across the gap");
    }
    check(exec_sbrk_pick_tier(L.load_base, L.code_end, L.guard_a,
              L.exec_heap_size, L.stack_size, hi, &end) == 1,
          "exact actual tier1 boundary");
}

int main(void)
{
    failures = 0;
    checks = 0;
    report("sbrk tier (exec/exec.c 二段構え, 決裁 2026-09-11)\n");
    case_tier1_when_free();
    case_tier2_when_tight();
    case_nomem_leaves_others();
    case_extra_pages_k7();
    case_variable_stack_boundary();
    if (checks < 34) {
        report("TOO FEW CHECKS\n");
        die(1);
    }
    if (failures) {
        report("FAILURES\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
    return 0;
}

void _start(void)
{
    main();
}
