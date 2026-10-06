"""所有権台帳の核 (T1b) — 実ソース ILP32 ハーネス、エミュレータ不要。

  python3 -B tools/tests/test_ledger.py            # 肯定側 (unittest)
  python3 -B tools/tests/test_ledger.py --mutate   # 否定側 + 肯定側

票 docs/archive/v3/TASK_T1_LEDGER.md §4-2 (T1b)。kernel/pgalloc.c を丸ごと取り込み
(test_pgalloc_model.py と同じ流儀: 特権命令の irq_save / irq_restore だけを
贋物にし、irq_restore の度に L1 / L2 の不変条件を全ページで検査する)、次を見る:

  - owner 付きの確保 / 解放 / 移譲 / 一括回収、TOP_DOWN / BOTTOM_UP
  - 他 owner 混在の解放・移譲・claim は全件不変で拒否 (診断カウンタは増える)
  - DEVICE / PERSIST owner の回収拒否、DEVICE は RAM を持てない
  - owner 表の満杯、番号の返却 (参照が残れば拒否) と再利用
  - R7: 合成モジュールの init 途中失敗 → 一括回収で 0、bundle → モジュール
    移譲の後の失敗は当該 owner だけ回収され bundle の残りは不変
  - 会計 (L1・L2・owner の pages・used/total・区間の本数) は失敗時不変
  - R1: 割り込み / 例外の操作を診断して panic、変更前に停止
  - 区間の表 (重なり拒否・OUTSIDE・起動時だけ) と ledger_selfcheck
  - 永久予約 (PERSIST だけ、L2 に owner が残る — B11。T1e で呼び手の sys_reserve_top は撤去)
  - asm: 全 IRQ スタブの IRQ_ENTER / IRQ_LEAVE、全例外入口の EXC_ENTER と
    復帰点の EXC_LEAVE (静的)、exec_setjmp / exec_longjmp の深さの控えと
    復元 (nasm で組んで実行)、jmpbuf の長さの直書きが無いこと (B8)、
    broker の判定も全 IRQ 深さ (T2a R1)

--mutate は §4-2 の変異 (owner 検査を外す / IRQ_LEAVE を 1 本抜く / 例外の
復帰点の -1 を 1 つ抜く / 回収で DEVICE を拒否しない / exec_longjmp の深さ
復元を抜く) を写しに当て、どれかの試験が RED になることを見る。実物の
ソースは書き換えない (メモリ上の写しをハーネスへ書き出すだけ)。
"""
import host32
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCES = ('kernel/pgalloc.c', 'tools/tests/pgalloc_host_fixture.h',
           'kernel/setjmp.asm', 'kernel/isr_stub.asm', 'include/ksetjmp.h',
           'kernel/irq.c')


def load(mutation=None):
    texts = {rel: (ROOT / rel).read_text() for rel in SOURCES}
    if mutation:
        _, rel, old, new = mutation
        if old not in texts[rel]:
            raise SystemExit('変異 %s の当て先が見つからない: %s' % (mutation[0], rel))
        texts[rel] = texts[rel].replace(old, new, 1)
    return texts


PRE = r'''#include "types.h"
#define NOINST __attribute__((no_instrument_function))
static void host_verify_commit(void) NOINST;
static unsigned int host_if = 0x202, saves, restores;
static int host_boot_ctx = 1;
static int host_expect_panic;
static void host_panic_check(void) NOINST;
static void _stop(void) __attribute__((unused)) NOINST;
static void _stop(void) {
    host_panic_check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(250) : "memory");
    for (;;) {}
}
static unsigned int irq_save(void) NOINST;
static void irq_restore(unsigned int f) NOINST;
static unsigned int irq_save(void) { unsigned int f = host_if; host_if &= ~0x200U; saves++; return f; }
static void irq_restore(unsigned int f) { host_verify_commit(); restores++; host_if = f; }
'''

POST = r'''
#include "pgalloc_host_fixture.h"
int paging_boot_context(void) { return host_boot_ctx; }
/* Ledger-only fixture has a master identity alias even after boot. */
u32 paging_current_cr3(void) { return 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
static void host_bad_irq(void) __attribute__((noreturn, no_instrument_function));
static void host_bad_irq(void) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(250) : "memory"); for (;;) {}
}
/* irq_restore の度に: IF が落ちていたこと、L1 (a ⇒ e) と L2 (e では a ⇔ owner ≠ 0)、
 * owner の pages が L2 の数と一致すること、used / total が bitmap と一致すること。 */
static void host_verify_commit(void) {
    u32 p, e, a, ne, na, o, cnt[LEDGER_MAX_OWNERS];
    if (host_if & 0x200U) host_bad_irq();
    if (!initialized) return;
    ne = na = 0;
    for (o = 0; o < LEDGER_MAX_OWNERS; o++) cnt[o] = 0;
    for (p = 0; p < limit_pfn; p++) {
        e = (eligible[p / 32] >> (p % 32)) & 1;
        a = (bitmap[p / 32] >> (p % 32)) & 1;
        if (a && !e) host_bad_irq();
        if (e && a != (owner_map[p] != 0)) host_bad_irq();
        cnt[owner_map[p]]++;
        ne += e; na += a;
    }
    if (ne != total_pages || na != used_pages) host_bad_irq();
    for (o = 1; o < LEDGER_MAX_OWNERS; o++)
        if (cnt[o] != ledger_owners[o].pages) host_bad_irq();
}
void __cyg_profile_func_enter(void *fn, void *caller) NOINST;
void __cyg_profile_func_exit(void *fn, void *caller) NOINST;
void __cyg_profile_func_enter(void *fn, void *caller) {
    (void)caller;
    if ((fn == (void *)take_page || fn == (void *)give_page ||
         fn == (void *)bmp_set || fn == (void *)bmp_clear) && (host_if & 0x200U))
        host_bad_irq();
}
void __cyg_profile_func_exit(void *fn, void *caller) { (void)fn; (void)caller; }
#define STR1(x) #x
#define STR(x) STR1(x)
#define CHECK(x) do { if (!(x)) { static const char msg[] = "CHECK " STR(__LINE__) ": " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(msg), "d"(sizeof(msg)-1) : "memory"); return 1; } } while (0)
#define K LEDGER_OWNER_KERNEL

/* 失敗時全不変 (会計) の写し: L1 / L2 / owner 表の会計 / 区間の本数 */
static u32 snap_bits[4096], snap_used, snap_total, snap_regions, snap_pages[LEDGER_MAX_OWNERS];
static u8 snap_owner[65536];
static u8 snap_kind[LEDGER_MAX_OWNERS];
static void snap(void) {
    u32 i, words = (limit_pfn + 31) / 32;
    for (i = 0; i < words * 2; i++) snap_bits[i] = eligible[i];
    for (i = 0; i < limit_pfn; i++) snap_owner[i] = owner_map[i];
    for (i = 0; i < LEDGER_MAX_OWNERS; i++) {
        snap_pages[i] = ledger_owners[i].pages;
        snap_kind[i] = ledger_owners[i].kind;
    }
    snap_used = used_pages; snap_total = total_pages; snap_regions = ledger_region_count;
}
static int same(void) {
    u32 i, words = (limit_pfn + 31) / 32;
    for (i = 0; i < words * 2; i++) if (snap_bits[i] != eligible[i]) return 0;
    for (i = 0; i < limit_pfn; i++) if (snap_owner[i] != owner_map[i]) return 0;
    for (i = 0; i < LEDGER_MAX_OWNERS; i++)
        if (snap_pages[i] != ledger_owners[i].pages || snap_kind[i] != ledger_owners[i].kind)
            return 0;
    return snap_used == used_pages && snap_total == total_pages &&
           snap_regions == ledger_region_count;
}
static void host_panic_check(void) {
    if (host_expect_panic && !(host_if & 0x200U) &&
        ledger_check_tag && ledger_check_tag[0] == 'R' &&
        ledger_check_tag[1] == '1' &&
        ledger_irq_ops == (kctx_irq_depth != 0) &&
        ledger_exc_ops == (kctx_exc_depth != 0) &&
        ledger_owner_pages(LEDGER_OWNER_FIXED_LAST + 1) == 1) {
        host_verify_commit();
        __asm__ volatile("int $0x80" : : "a"(1), "b"(0) : "memory");
    }
}
static int test(void) {
'''

END = r'''
    CHECK(host_if == 0x202 && saves == restores);
    CHECK(ledger_selfcheck("end"));
    return 0;
}
void _start(void) { int r = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r) : "memory"); for (;;) {} }
'''

FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
         '-static', '-no-pie', '-ffunction-sections', '-finstrument-functions',
         '-Wl,--gc-sections']


def run_c(texts, body):
    """本体を組んで実行し、(成功, 出力) を返す。"""
    with tempfile.TemporaryDirectory(prefix='os32-ledger-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'pgalloc_host_fixture.h').write_text(texts['tools/tests/pgalloc_host_fixture.h'])
        src = texts['kernel/pgalloc.c'].replace('#include "io.h"', '')
        (tmp / 'test.c').write_text(PRE + src + POST + body + END)
        cmd = ['gcc'] + FLAGS + ['-I' + str(tmp)]
        cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers', 'sdk/include/os32')]
        build = subprocess.run(cmd + [str(tmp / 'test.c'), str(ROOT / 'kernel/physmem.c'),
                                      '-o', str(tmp / 'test')], capture_output=True, text=True)
        if build.returncode:
            return False, 'compile\n' + build.stderr
        out = host32.run([str(tmp / 'test')], capture_output=True, text=True, timeout=120)
        return out.returncode == 0, 'rc=%d %s%s' % (out.returncode, out.stdout, out.stderr)


# ---- C の本体 (各 1 回の新しい起動。池は 16MB の fixture) -------------------

BODIES = {}

BODIES['alloc_free_transfer_reclaim'] = r'''
    u32 a, b, p, q, r, n, bad;
    host_pool_boot(16384);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "app", &a) && a == LEDGER_OWNER_FIXED_LAST + 1);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "app", &b) && b == a + 1);
    CHECK(ledger_owners[a].kind == LEDGER_KIND_AS && ledger_owners[a].id == 2 &&
          ledger_owners[a].name[0] == 'a' && ledger_owners[a].name[3] == 0);
    CHECK(pgalloc_alloc_n_owner(a, 4, 2048, 2152, LEDGER_BOTTOM_UP, &p) && p == 2048);
    CHECK(pgalloc_alloc_n_owner(b, 2, 2048, 2152, LEDGER_TOP_DOWN, &q) && q == 2150);
    /* TOP_DOWN は使用中を飛ばして下へ: 4197 を塞ぐと 2 本は [4195, 4197) */
    CHECK(pgalloc_alloc_n_owner(b, 1, 2149, 2150, LEDGER_TOP_DOWN, &r) && r == 2149);
    CHECK(pgalloc_alloc_n_owner(a, 2, 2052, 2152, LEDGER_TOP_DOWN, &r) && r == 2147);
    CHECK(pgalloc_free_n_owner(a, r, 2));
    CHECK(pgalloc_free_n_owner(b, 2149, 1));
    CHECK(ledger_owner_pages(a) == 4 && ledger_owner_pages(b) == 2);
    CHECK(owner_map[2048] == a && owner_map[2151] == b && owner_map[2052] == 0);
    CHECK(pgalloc_alloc_phys(a, 1) == MEM_POOL_BASE);   /* 汎用の池は下から */
    /* 移譲 */
    CHECK(ledger_transfer(p, 2, a, b));
    CHECK(ledger_owner_pages(a) == 3 && ledger_owner_pages(b) == 4);
    snap();
    CHECK(!ledger_transfer(p, 3, b, a));        /* p+2 は a のもの → 全件不変 */
    CHECK(!ledger_transfer(p, 1, b, LEDGER_OWNER_GFX));  /* DEVICE へは渡さない */
    CHECK(same());
    /* 他 owner の解放は全件不変で拒否、診断は増える */
    bad = ledger_bad_free;
    CHECK(!pgalloc_free_n_owner(a, p, 1));
    CHECK(!pgalloc_free_n_owner(b, p, 3));      /* b, b, a の混在 */
    CHECK(!pgalloc_free_n_owner(0, p, 1));
    CHECK(same() && ledger_bad_free == bad + 3);
    CHECK(pgalloc_free_n_owner(b, p, 2));
    CHECK(!pgalloc_free_n_owner(b, p, 1));      /* 二重解放 */
    /* 一括回収 */
    CHECK(ledger_reclaim_owner(a, &n) && n == 3 && ledger_owner_pages(a) == 0);
    CHECK(ledger_reclaim_owner(b, &n) && n == 2 && ledger_owner_pages(b) == 0);
    CHECK(ledger_reclaim_owner(a, &n) && n == 0);
    CHECK(ledger_reclaim_pages == 5);
    CHECK(pgalloc_free_pages() == pgalloc_total_pages());
    CHECK(ledger_owner_retire(a) && ledger_owner_retire(b));
'''

BODIES['refusals_keep_accounting'] = r'''
    u32 a, d, p, n, cr, rr;
    host_pool_boot(16384);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "app", &a));
    CHECK(ledger_owner_new(LEDGER_KIND_DEVICE, 0, "nic", &d));
    CHECK(pgalloc_alloc_n_owner(a, 3, 2500, 2600, LEDGER_BOTTOM_UP, &p) && p == 2500);
    snap();
    /* claim: 他 owner のページが 1 つでも混じれば全件不変で拒否 */
    cr = ledger_claim_refused;
    CHECK(!ledger_claim_fixed(LEDGER_OWNER_SHLIB, 2490, 2501));
    CHECK(same() && ledger_claim_refused == cr + 1);
    /* 同じ owner なら冪等 (空きだけを取る) */
    CHECK(ledger_claim_fixed(a, 2498, 2503));
    CHECK(ledger_owner_pages(a) == 5 && owner_map[2498] == a && owner_map[2502] == a);
    CHECK(ledger_claim_fixed(a, 2498, 2503) && ledger_owner_pages(a) == 5);
    snap();
    /* DEVICE は RAM を持てない (確保・claim・回収・返却のすべて) */
    CHECK(!pgalloc_alloc_n_owner(d, 1, 3000, 3001, LEDGER_BOTTOM_UP, &p));
    CHECK(pgalloc_alloc_phys(d, 1) == 0);
    CHECK(!ledger_claim_fixed(d, 3000, 3001));
    CHECK(!ledger_reclaim_owner(d, &n) && !ledger_reclaim_owner(LEDGER_OWNER_GFX, &n));
    rr = ledger_retire_refused;
    CHECK(!ledger_owner_retire(d) && !ledger_owner_retire(LEDGER_OWNER_GFX));
    /* PERSIST は回収も返却もしない (R5 (b)) */
    CHECK(!ledger_reclaim_owner(LEDGER_OWNER_KERNEL, &n) &&
          !ledger_reclaim_owner(LEDGER_OWNER_BOOT, &n));
    CHECK(!ledger_owner_retire(LEDGER_OWNER_SHLIB));
    CHECK(ledger_retire_refused == rr + 3);
    /* 番号の範囲外・空き番号 */
    CHECK(!ledger_reclaim_owner(0, &n) && !ledger_reclaim_owner(LEDGER_MAX_OWNERS, &n) &&
          !ledger_reclaim_owner(40, &n));
    CHECK(pgalloc_alloc_phys(40, 1) == 0 && !ledger_claim_fixed(0, 3000, 3001));
    /* 確保の失敗: 大きすぎる・範囲外・逆順・向き違い */
    n = 77;
    CHECK(!pgalloc_alloc_n_owner(a, 101, 2500, 2600, LEDGER_TOP_DOWN, &n));
    CHECK(!pgalloc_alloc_n_owner(a, 1, 2600, 2500, LEDGER_BOTTOM_UP, &n));
    CHECK(!pgalloc_alloc_n_owner(a, 1, 4095, 4097, 2, &n));
    CHECK(!pgalloc_alloc_n_owner(a, 1, 0, 1024, LEDGER_TOP_DOWN, &n)); /* 池の下は eligible でない */
    CHECK(n == 77);
    /* B11: boot で確保 → SURFACE 登録 → gshell へ L2 ごと移譲。 */
    CHECK(!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 2, 2502, 2504,
                                 LEDGER_BOTTOM_UP, &p) && same());
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 2, 3000, 3002,
                                LEDGER_TOP_DOWN, &p) && p == 3000);
    {
        struct ledger_surface sf = { .first = 3000, .npages = 2,
            .width = 1, .height = 2, .pitch = PAGE_SIZE, .planes = 1,
            .owner = LEDGER_OWNER_BOOT, .backing = LEDGER_SB_RAM,
            .backend = LEDGER_SF_PEGC, .role = LEDGER_ROLE_CLIENT };
        u32 sid;
        CHECK(ledger_surface_create(&sf, &sid));
        CHECK(ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL));
        CHECK(ledger_surfaces[sid].owner == LEDGER_OWNER_GSHELL);
        CHECK(owner_map[3000] == LEDGER_OWNER_GSHELL &&
              owner_map[3001] == LEDGER_OWNER_GSHELL);
        CHECK(ledger_owner_pages(LEDGER_OWNER_BOOT) == 0 &&
              ledger_owner_pages(LEDGER_OWNER_GSHELL) == 2);
        snap();
        CHECK(!pgalloc_alloc_n_owner(a, 1, 3000, 3002, LEDGER_TOP_DOWN, &p));
        CHECK(!ledger_claim_fixed(a, 2999, 3002) && same());
        CHECK(!pgalloc_free_n_owner(a, 3000, 2) && same());
        CHECK(ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL) && same());
    }
    CHECK(ledger_reclaim_owner(a, &n) && n == 5);
    CHECK(ledger_owner_retire(a));
'''

BODIES['owner_table_full_and_reuse'] = r'''
    u32 o[LEDGER_MAX_OWNERS], x, i, n, p;
    host_pool_boot(16384);
    CHECK(!ledger_owner_new(LEDGER_KIND_PERSIST, 0, "x", &x));   /* PERSIST は固定番号だけ */
    CHECK(!ledger_owner_new(0, 0, "x", &x) && !ledger_owner_new(LEDGER_KIND_AS, 0, "x", 0));
    n = LEDGER_MAX_OWNERS - 1 - LEDGER_OWNER_FIXED_LAST;
    for (i = 0; i < n; i++) CHECK(ledger_owner_new(LEDGER_KIND_AS, i, "as", &o[i]));
    x = 99;
    CHECK(!ledger_owner_new(LEDGER_KIND_MODULE, 0, "full", &x) && x == 99);
    /* 返却: pages が残れば拒否 → 回収してから返せる */
    CHECK(pgalloc_alloc_n_owner(o[5], 1, 2048, 2049, LEDGER_BOTTOM_UP, &p));
    CHECK(!ledger_owner_retire(o[5]) && ledger_retire_refused == 1);
    CHECK(ledger_reclaim_owner(o[5], &p) && p == 1);
    CHECK(ledger_owner_retire(o[5]));
    CHECK(!ledger_owner_retire(o[5]));                    /* 既に空き */
    /* 返した番号は再利用される */
    CHECK(ledger_owner_new(LEDGER_KIND_MODULE, 9, "modxxxxxxx", &x) && x == o[5]);
    CHECK(ledger_owners[x].kind == LEDGER_KIND_MODULE && ledger_owners[x].name[7] == 'x');
    /* 区間・SURFACE から参照が残れば拒否 */
    CHECK(ledger_register_region(LEDGER_R_FIXED, x, 100, 101, LEDGER_CACHE_WB, 0));
    CHECK(!ledger_owner_retire(x));
    ledger_regions[0].owner = LEDGER_OWNER_KERNEL;
    ledger_surfaces[3].npages = 1; ledger_surfaces[3].owner = (u8)x;
    CHECK(!ledger_owner_retire(x));
    ledger_surfaces[3].npages = 0; ledger_surfaces[3].owner = 0;
    CHECK(ledger_owner_retire(x));
    for (i = 0; i < n; i++) if (i != 5) CHECK(ledger_owner_retire(o[i]));
'''

BODIES['r7_module_and_bundle'] = r'''
    u32 m, m2, p, q, n, lo, hi, i;
    host_pool_boot(16384);
    /* 合成モジュール: init の途中で失敗 → 一括回収で 0 → 番号を返す */
    CHECK(ledger_owner_new(LEDGER_KIND_MODULE, 0, "mod", &m));
    CHECK(pgalloc_alloc_n_owner(m, 3, 2048, 3072, LEDGER_BOTTOM_UP, &p));
    CHECK(pgalloc_alloc_n_owner(m, 5, 2048, 3072, LEDGER_TOP_DOWN, &q));
    CHECK(ledger_claim_fixed(m, 2500, 2504));
    CHECK(ledger_owner_pages(m) == 12);
    CHECK(ledger_reclaim_owner(m, &n) && n == 12 && ledger_owner_pages(m) == 0);
    CHECK(ledger_owner_retire(m));
    CHECK(pgalloc_free_pages() == pgalloc_total_pages());
    /* bundle → モジュール移譲の後の失敗: 当該 owner だけ回収、bundle の残りは不変 */
    lo = MEM_BOOT_BUNDLE_BASE / PAGE_SIZE;
    hi = lo + MEM_BOOT_BUNDLE_SIZE / PAGE_SIZE;
    CHECK(ledger_claim_fixed(LEDGER_OWNER_BUNDLE, lo, hi));
    CHECK(ledger_owner_new(LEDGER_KIND_MODULE, 1, "m1", &m));
    CHECK(ledger_owner_new(LEDGER_KIND_MODULE, 2, "m2", &m2));
    CHECK(ledger_transfer(lo, 16, LEDGER_OWNER_BUNDLE, m));
    CHECK(ledger_transfer(lo + 16, 8, LEDGER_OWNER_BUNDLE, m2));
    CHECK(pgalloc_alloc_n_owner(m, 2, 2048, 3072, LEDGER_BOTTOM_UP, &p));
    snap();
    CHECK(ledger_reclaim_owner(m, &n) && n == 18);
    CHECK(ledger_owner_pages(LEDGER_OWNER_BUNDLE) == hi - lo - 24);
    CHECK(ledger_owner_pages(m2) == 8);
    for (i = lo + 24; i < hi; i++) CHECK(owner_map[i] == LEDGER_OWNER_BUNDLE);
    for (i = lo + 16; i < lo + 24; i++) CHECK(owner_map[i] == m2);
    for (i = lo; i < lo + 16; i++) CHECK(owner_map[i] == 0);
    CHECK(ledger_owner_retire(m));
    CHECK(ledger_reclaim_owner(m2, &n) && n == 8 && ledger_owner_retire(m2));
    /* bundle は PERSIST: 回収しない */
    CHECK(!ledger_reclaim_owner(LEDGER_OWNER_BUNDLE, &n));
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_BUNDLE, lo + 24, (int)(hi - lo - 24)));
'''

for name, irq, exc, call in (
    ('r1_irq_alloc_panic', 1, 0, 'pgalloc_alloc_n_owner(a, 1, 2048, 3072, LEDGER_BOTTOM_UP, &p)'),
    ('r1_exc_free_panic', 0, 1, 'pgalloc_free_n_owner(a, p, 1)'),
    ('r1_nested_reclaim_panic', 1, 1, 'ledger_reclaim_owner(a, &n)'),
):
    BODIES[name] = r'''
    u32 a, p, n = 0;
    (void)n;
    host_pool_boot(16384);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "app", &a));
    CHECK(pgalloc_alloc_n_owner(a, 1, 2048, 3072, LEDGER_BOTTOM_UP, &p));
    CHECK(ledger_irq_ops == 0 && ledger_exc_ops == 0);
    kctx_irq_depth = %d; kctx_exc_depth = %d;
    host_expect_panic = 1;
    (void)%s;
    CHECK(0); /* panic は必ず変更前に止める */
''' % (irq, exc, call)

BODIES['regions_and_selfcheck'] = r'''
    u32 i, a, fail;
    host_pool_boot(16384);
    CHECK(ledger_selfcheck("t0") && ledger_check_fail == 0);
    CHECK(ledger_register_region(LEDGER_R_FIXED, K, 0, 1, LEDGER_CACHE_WB, LEDGER_RF_PERMANENT));
    snap();
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 0, 2, LEDGER_CACHE_WB, 0));      /* 重なり */
    CHECK(!ledger_register_region(LEDGER_R_DEVICE, K, 10, 11, LEDGER_CACHE_UC, 0));   /* T1d */
    CHECK(!ledger_register_region(LEDGER_R_FIXED, 0, 10, 11, LEDGER_CACHE_WB, 0));    /* owner */
    CHECK(!ledger_register_region(LEDGER_R_FIXED, 40, 10, 11, LEDGER_CACHE_WB, 0));
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 11, 10, LEDGER_CACHE_WB, 0));
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 10, 1048577, LEDGER_CACHE_WB, 0));
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 10, 11, 2, 0));
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 10, 11, LEDGER_CACHE_WB, LEDGER_RF_OUTSIDE));
    CHECK(!ledger_register_region(0, K, 10, 11, LEDGER_CACHE_WB, 0));
    host_boot_ctx = 0;                                           /* 起動時だけ */
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 10, 11, LEDGER_CACHE_WB, 0));
    host_boot_ctx = 1;
    CHECK(same());
    /* 背景は管理範囲の外 (4GiB 端) まで完全な範囲で持ち、OUTSIDE が付く */
    CHECK(ledger_register_region(LEDGER_R_BACKGROUND, K, MEM_PHYS_RAM_CEILING / PAGE_SIZE,
                                 PHYSMEM_MAX_PFN, LEDGER_CACHE_UC, LEDGER_RF_PERMANENT));
    CHECK(ledger_regions[1].flags == (LEDGER_RF_PERMANENT | LEDGER_RF_OUTSIDE) &&
          ledger_regions[1].end == PHYSMEM_MAX_PFN);
    CHECK(ledger_regions[0].flags == LEDGER_RF_PERMANENT);
    /* 表の満杯 */
    for (i = ledger_region_count; i < LEDGER_MAX_REGIONS; i++)
        CHECK(ledger_register_region(LEDGER_R_FIXED, K, 100 + i, 101 + i, LEDGER_CACHE_WB, 0));
    CHECK(!ledger_register_region(LEDGER_R_FIXED, K, 99, 100, LEDGER_CACHE_WB, 0));
    CHECK(ledger_selfcheck("t1") && ledger_check_fail == 0);
    /* 自己検査が壊れを拾う: 区間の重なり / 死んだ owner を指す SURFACE */
    ledger_regions[3].first = ledger_regions[2].first;
    CHECK(!ledger_selfcheck("overlap"));
    CHECK(ledger_check_fail == 1 && ledger_check_tag[0] == 'o');
    ledger_regions[3].first = ledger_regions[2].first + 1;
    CHECK(ledger_selfcheck("t2"));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 1, "as", &a));
    ledger_surfaces[0].npages = 1; ledger_surfaces[0].owner = (u8)a;
    CHECK(ledger_selfcheck("t3"));
    /* AS は SURFACE を持っていれば回収できない */
    CHECK(!ledger_reclaim_owner(a, &fail));
    ledger_surfaces[0].owner = 50;                              /* 空き番号 */
    fail = ledger_check_fail;
    CHECK(!ledger_selfcheck("surface") && ledger_check_fail == fail + 1);
    ledger_surfaces[0].npages = 0; ledger_surfaces[0].owner = 0;
    CHECK(ledger_owner_retire(a));
'''

# ---- asm の静的検査 -------------------------------------------------------

IRQ_LABELS = ['irq_stub_common_%1', 'irq_stub_0', 'irq_stub_1', 'irq_stub_2',
              'irq_stub_4', 'irq_stub_7', 'irq_stub_11', 'irq_stub_12', 'irq_stub_13']


def _code(line):
    return line.split(';', 1)[0].strip()


def _body(asm, label):
    """label: から次の global / %macro / %endmacro の手前までの命令列 (コメント除く)。"""
    lines = asm.splitlines()
    start = next(i for i, l in enumerate(lines) if _code(l) == label + ':')
    out = []
    for l in lines[start + 1:]:
        c = _code(l)
        if c.startswith('global ') or c.startswith('%macro') or c.startswith('%endmacro'):
            break
        if c:
            out.append(c)
    return out


def asm_problems(asm):
    """IRQ_ENTER / IRQ_LEAVE / EXC_ENTER / EXC_LEAVE の置き場を検査する。"""
    bad = []
    for m in ('IRQ_ENTER', 'IRQ_LEAVE', 'EXC_ENTER', 'EXC_LEAVE'):
        body = asm.split('%macro ' + m + ' 0', 1)
        if len(body) != 2 or '[ss:kctx_' not in body[1].split('%endmacro', 1)[0]:
            bad.append('%s は ss: で書く' % m)
    for label in IRQ_LABELS:
        ins = _body(asm, label)
        if not ins or ins[0] != 'IRQ_ENTER':
            bad.append('%s: 入口が IRQ_ENTER でない' % label)
        rets = [i for i, c in enumerate(ins) if c == 'IRETD_USER']
        if not rets:
            bad.append('%s: 復帰点が無い' % label)
        for i in rets:
            if i < 2 or ins[i - 1] not in ('popad', 'pop     eax') or ins[i - 2] != 'IRQ_LEAVE':
                bad.append('%s: 復帰点の直前に IRQ_LEAVE が無い' % label)
        if ins.count('IRQ_LEAVE') != len(rets) or ins.count('IRQ_ENTER') != 1:
            bad.append('%s: IRQ_ENTER / IRQ_LEAVE の数' % label)
        if 'EXC_ENTER' in ins or 'EXC_LEAVE' in ins:
            bad.append('%s: IRQ に例外の深さ' % label)
    # 例外: 全部の入口 (isr_common・#GP / #PF・V86 分岐・default) と全部の復帰点
    exc = asm.split('global irq_stub_common_%1', 1)[0] if 'global irq_stub_common_%1' in asm \
        else asm.split('%macro IRQ_COMMON', 1)[0]
    ins = [_code(l) for l in exc.splitlines() if _code(l)]
    for i, c in enumerate(ins):
        if c in ('isr_common:', '%%from_v86:', '.from_v86:'):
            if ins[i + 1] != 'EXC_ENTER':
                bad.append('例外の入口 %s の次が EXC_ENTER でない' % c)
        if c == 'isr_stub_14:' and 'EXC_ENTER' not in ins[i + 1:i + 3]:
            bad.append('isr_stub_14 の入口に EXC_ENTER が無い')
        if c == 'IRETD_USER':
            j = i - 1
            while j >= 0 and ins[j].startswith('add '):
                j -= 1
            if ins[j] == 'EXC_LEAVE' and ins[j - 1] == 'EXC_ENTER':
                continue                      # isr_stub_default (下で別に見る)
            if ins[j] != 'popad' or ins[j - 1] != 'EXC_LEAVE':
                bad.append('例外の復帰点 (%d 番目の命令) の前に EXC_LEAVE が無い' % i)
    if ins.count('EXC_ENTER') < 6:
        bad.append('例外の入口の EXC_ENTER が足りない (%d)' % ins.count('EXC_ENTER'))
    d = _body(asm, 'isr_stub_default')
    if d[:3] != ['EXC_ENTER', 'EXC_LEAVE', 'IRETD_USER']:
        bad.append('isr_stub_default')
    if any('irq_in_irq' in _code(l) for l in asm.splitlines()):
        bad.append('isr_stub.asm が irq_in_irq を触る (B1)')
    return bad


SETJMP_C = r'''#include "ksetjmp.h"
volatile u32 kctx_irq_depth, kctx_exc_depth;
static u32 buf[KSETJMP_BUF_LEN + 1];
static void out(const char *s, u32 n) { __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(s), "d"(n) : "memory"); }
#define CHECK(x) do { if (!(x)) { out("FAIL " #x "\n", sizeof("FAIL " #x "\n") - 1); return 1; } } while (0)
static volatile int round;
static int test(void) {
    CHECK(KSETJMP_BUF_LEN == 8);
    buf[KSETJMP_BUF_LEN] = 0x5a5a5a5aUL;
    kctx_irq_depth = 0; kctx_exc_depth = 0;
    round = 0;
    if (exec_setjmp(buf) == 0) {
        /* CTRL+STOP (IRQ1 の上) / fault kill (例外の上) から抜ける形 */
        kctx_irq_depth = 2; kctx_exc_depth = 1;
        round = 1;
        exec_longjmp(buf);
    }
    CHECK(round == 1);
    CHECK(kctx_irq_depth == 0 && kctx_exc_depth == 0);
    CHECK(buf[6] == 0 && buf[7] == 0 && buf[KSETJMP_BUF_LEN] == 0x5a5a5a5aUL);
    /* 控えは setjmp の時点の値 (入れ子の setjmp 点が深さ > 0 の場合も戻す) */
    kctx_irq_depth = 3; kctx_exc_depth = 2;
    round = 0;
    if (exec_setjmp(buf) == 0) {
        kctx_irq_depth = 9; kctx_exc_depth = 9;
        round = 1;
        exec_longjmp(buf);
    }
    CHECK(round == 1 && kctx_irq_depth == 3 && kctx_exc_depth == 2);
    CHECK(buf[6] == 3 && buf[7] == 2 && buf[KSETJMP_BUF_LEN] == 0x5a5a5a5aUL);
    return 0;
}
void _start(void) { int r = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r) : "memory"); for (;;) {} }
'''


def setjmp_run(texts):
    with tempfile.TemporaryDirectory(prefix='os32-setjmp-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 'setjmp.asm').write_text(texts['kernel/setjmp.asm'])
        (tmp / 'ksetjmp.h').write_text(texts['include/ksetjmp.h'])
        (tmp / 't.c').write_text(SETJMP_C)
        r = subprocess.run(['nasm', '-f', 'elf32', str(tmp / 'setjmp.asm'), '-o', str(tmp / 's.o')],
                           capture_output=True, text=True)
        if r.returncode:
            return False, 'compile\n' + r.stderr
        r = subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Werror', '-O0',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                            '-static', '-no-pie', '-I' + str(tmp), '-I' + str(ROOT / 'include'),
                            str(tmp / 't.c'), str(tmp / 's.o'), '-o', str(tmp / 't')],
                           capture_output=True, text=True)
        if r.returncode:
            return False, 'compile\n' + r.stderr
        r = host32.run([str(tmp / 't')], capture_output=True, text=True, timeout=30)
        return r.returncode == 0, r.stdout + r.stderr


def jmpbuf_hardcoded():
    """jmpbuf の長さの直書き (B8、T1-R8)。保存先は KSETJMP_BUF_LEN で宣言する。"""
    hits = []
    for top in ('kernel', 'exec', 'include'):
        for f in sorted((ROOT / top).rglob('*.[ch]')):
            for n, line in enumerate(f.read_text(errors='replace').splitlines(), 1):
                if re.search(r'jmpbuf\s*\[\s*[0-9]', line):
                    hits.append('%s:%d' % (f.relative_to(ROOT), n))
    return hits


def broker_problems(irq_c):
    """T2a R1: 固定 IRQ を含む全 IRQ 深さに統一する。"""
    bad = []
    for name in ('irq_register_check', 'irq_unregister_find'):
        if not re.search(name + r'\([^;]*kctx_irq_depth\)', irq_c):
            bad.append(name + ': 全 IRQ 深さでない')
    if re.search(r'irq_in_irq\s*(?:=|\+\+|--)', irq_c):
        bad.append('broker が別の深さを保持')
    return bad


def all_checks(texts):
    """(名前, 成功, 出力) を全部返す (変異の判定用)。"""
    out = []
    for name, body in BODIES.items():
        ok, log = run_c(texts, body)
        out.append((name, ok, log))
    bad = asm_problems(texts['kernel/isr_stub.asm'])
    out.append(('asm', not bad, '\n'.join(bad)))
    ok, log = setjmp_run(texts)
    out.append(('setjmp', ok, log))
    return out


class Ledger(unittest.TestCase):
    texts = None

    @classmethod
    def setUpClass(cls):
        cls.texts = load()

    def run_body(self, name):
        ok, log = run_c(self.texts, BODIES[name])
        self.assertTrue(ok, log)

    def test_alloc_free_transfer_reclaim(self):
        self.run_body('alloc_free_transfer_reclaim')

    def test_refusals_keep_accounting(self):
        self.run_body('refusals_keep_accounting')

    def test_owner_table_full_and_reuse(self):
        self.run_body('owner_table_full_and_reuse')

    def test_r7_module_and_bundle(self):
        self.run_body('r7_module_and_bundle')

    def test_r1_counting(self):
        for name in ('r1_irq_alloc_panic', 'r1_exc_free_panic', 'r1_nested_reclaim_panic'):
            self.run_body(name)

    def test_regions_and_selfcheck(self):
        self.run_body('regions_and_selfcheck')

    def test_asm_depth_markers(self):
        self.assertEqual(asm_problems(self.texts['kernel/isr_stub.asm']), [])

    def test_setjmp_saves_and_restores_depth(self):
        ok, log = setjmp_run(self.texts)
        self.assertTrue(ok, log)

    def test_jmpbuf_length_not_hardcoded(self):
        self.assertEqual(jmpbuf_hardcoded(), [])
        v86 = (ROOT / 'kernel/v86.c').read_text()
        self.assertIn('jmpbuf[KSETJMP_BUF_LEN]', (ROOT / 'kernel/v86_mem.h').read_text())
        self.assertIn('#define v86_jmpbuf v86_session.jmpbuf', v86)

    def test_broker_judgement_unchanged(self):
        self.assertEqual(broker_problems(self.texts['kernel/irq.c']), [])

    def test_old_api_is_gone(self):
        # 旧 API (owner を取らない口) は T1b の最後で消す (§3-2)。
        header = (ROOT / 'kernel/pgalloc.h').read_text()
        for name in ('pgalloc_alloc_page', 'pgalloc_free_page', 'pgalloc_alloc_n',
                     'pgalloc_free_n', 'pgalloc_mark_used', 'pgalloc_alloc_n_range',
                     'pgalloc_alloc_n_pfn', 'pgalloc_free_n_pfn'):
            self.assertNotRegex(header, r'\b%s\s*\(' % name)


# 変異 (§4-2)。当て先は load() の写し (実物は書き換えない)。
MUTATIONS = [
    ('r1-panic-removed', 'kernel/pgalloc.c',
     '    for (;;) { _stop(); }', '    return;'),
    ('owner-check-removed', 'kernel/pgalloc.c',
     '    return bit(eligible, p) && bit(bitmap, p) && owner_map[p] == owner;',
     '    (void)owner;\n    return bit(eligible, p) && bit(bitmap, p);'),
    ('claim-owner-blind', 'kernel/pgalloc.c',
     '        if (bit(eligible, p) && bit(bitmap, p) && owner_map[p] != owner) {',
     '        if (0) {'),
    ('irq4-leave-missing', 'kernel/isr_stub.asm',
     '        call    serial_irq_handler\n\n'
     '        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)\n'
     '        mov     al, OCW2_EOI\n        out     PIC1_CMD, al\n\n'
     '        IRQ_LEAVE\n',
     '        call    serial_irq_handler\n\n'
     '        ;; マスタPICにEOI送出 (PC-98: ポート 0x00)\n'
     '        mov     al, OCW2_EOI\n        out     PIC1_CMD, al\n\n'),
    ('exc-return-leave-missing', 'kernel/isr_stub.asm',
     '        EXC_LEAVE\n        popad\n        add     esp, 8',
     '        popad\n        add     esp, 8'),
    ('reclaim-device', 'kernel/pgalloc.c',
     '    if (kind == LEDGER_KIND_DEVICE || kind == LEDGER_KIND_PERSIST) goto done;',
     '    if (kind == LEDGER_KIND_PERSIST) goto done;'),
    ('longjmp-no-restore', 'kernel/setjmp.asm',
     '        mov     ecx, [eax+24]\n        mov     [kctx_irq_depth], ecx\n'
     '        mov     ecx, [eax+28]\n        mov     [kctx_exc_depth], ecx\n',
     ''),
]


def _mutate_one(m):
    results = all_checks(load(m))
    # コンパイル (組み立て) の失敗は RED と数えない — 実行時の検査が変異を
    # 拾った証拠にならない (Codex P3)。
    reds = [name for name, ok, log in results
            if not ok and not log.startswith('compile')]
    builds = [name for name, ok, log in results
              if not ok and log.startswith('compile')]
    return m[0], reds, builds


def mutate():
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import mutpar
    red = 0
    for name, reds, builds in mutpar.run_ordered(_mutate_one, MUTATIONS, processes=True):
        ok = bool(reds) and not builds
        print('  変異 %-26s %s' % (name, ('RED (%s)' % ', '.join(reds)) if ok
                                   else ('**組み立てで落ちた (%s) — 実行時の検出の証拠にならない**'
                                         % ', '.join(builds)) if builds
                                   else '**GREEN — 試験が穴を見逃した**'))
        red += ok
    print('%d/%d の変異が RED' % (red, len(MUTATIONS)))
    return 0 if red == len(MUTATIONS) else 1


if __name__ == '__main__':
    if '--mutate' in sys.argv:
        sys.argv.remove('--mutate')
        rc = mutate()
        if rc:
            sys.exit(rc)
    unittest.main()
