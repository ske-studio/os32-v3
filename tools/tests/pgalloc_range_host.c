/* A0: real pgalloc.c, ILP32, only privileged IRQ/logging replaced. */
#include "types.h"
#define NOINST __attribute__((no_instrument_function))
#define TEST_IF 0x200U
static unsigned int test_flags = TEST_IF | 2U;
static unsigned int saves, restores, scans;
static int watching;
static void verify_commit(void) NOINST;
static void fail(const char *message) NOINST;
static unsigned int irq_save(void) NOINST;
static void irq_restore(unsigned int flags) NOINST;
#define IO_H
#include "../../kernel/pgalloc.c"
#include "../../kernel/physmem.c"
/* Each original case models a fresh boot. Production reinit is now refused;
 * reset only the private harness state, never add a product reset API. */
static void test_boot(u32 kb)
{
    initialized = 0;
    pgalloc_init(kb);
}
#define pgalloc_init test_boot
#define BITMAP_SIZE ((limit_pfn + 31) / 32)
static u32 phys_to_idx(u32 phys) { return phys / PAGE_SIZE; }

/* Weak reference makes pre-implementation RED an assertion, not link error. */
extern u32 pgalloc_alloc_n_range(int n, u32 lo, u32 hi)
    __attribute__((weak));

static void output(const char *s) NOINST;
static void output(const char *s)
{
    unsigned int len = 0;
    while (s[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory");
}
static void finish(int code) __attribute__((noreturn, no_instrument_function));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
static void fail(const char *message)
{
    output("ASSERT FAIL: "); output(message); output("\n"); finish(1);
}
#define CHECK(c) do { if (!(c)) fail(#c); } while (0)

static unsigned int irq_save(void)
{
    unsigned int flags = test_flags;
    test_flags &= ~TEST_IF;
    saves++;
    return flags;
}
static void irq_restore(unsigned int flags)
{
    CHECK(!(test_flags & TEST_IF));
    verify_commit();
    restores++;
    test_flags = flags;
}
void kprintf(unsigned char attr, const char *fmt, ...)
{
    (void)attr; (void)fmt;
}

void __cyg_profile_func_enter(void *fn, void *caller) NOINST;
void __cyg_profile_func_exit(void *fn, void *caller) NOINST;
void __cyg_profile_func_enter(void *fn, void *caller)
{
    (void)caller;
    if (watching && (fn == (void *)bmp_test || fn == (void *)bmp_set)) {
        CHECK(!(test_flags & TEST_IF));
        if (fn == (void *)bmp_test) scans++;
    }
}
void __cyg_profile_func_exit(void *fn, void *caller)
{
    (void)fn; (void)caller;
}

static void basic(void)
{
    u32 lo = MEM_APP_BAND_TOP;
    u32 before;
    CHECK(sizeof(u32) == 4 && sizeof(int) == 4);
    CHECK(pgalloc_alloc_n_range != 0);
    pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
    before = pgalloc_free_pages();
    CHECK(pgalloc_alloc_n_range(2, lo, lo + 2 * PAGE_SIZE) == lo);
    CHECK(pgalloc_free_pages() == before - 2);
    CHECK(bmp_test(phys_to_idx(lo)) && bmp_test(phys_to_idx(lo) + 1));
    pgalloc_free_n(lo, 2);
    CHECK(pgalloc_free_pages() == before);
}

static void irq_atomic(void)
{
    unsigned int initial;
    u32 lo = MEM_APP_BAND_TOP;
    u32 before;
    int enabled;

    for (enabled = 1; enabled >= 0; enabled--) {
        pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
        initial = 0x45U | (enabled ? TEST_IF : 0);
        test_flags = initial;
        saves = restores = 0;
        before = pgalloc_free_pages();
        watching = 1;
        CHECK(pgalloc_alloc_n_range(3, lo, lo + 3 * PAGE_SIZE) == lo);
        watching = 0;
        CHECK(test_flags == initial && saves == 1 && restores == 1);
        CHECK(pgalloc_free_pages() == before - 3);
        watching = 1;
        CHECK(pgalloc_alloc_n_range(1, lo, lo + 3 * PAGE_SIZE) == 0);
        watching = 0;
        CHECK(test_flags == initial && saves == 2 && restores == 2);
        pgalloc_free_n(lo, 3);
        CHECK(pgalloc_free_pages() == before);
    }
}

static u32 expected_bitmap[LEGACY_WORDS];
static u32 expected_used;
static int checking_commit;

static void verify_commit(void)
{
    u32 i;
    if (!checking_commit) return;
    for (i = 0; i < BITMAP_SIZE; i++) {
        CHECK(bitmap[i] == expected_bitmap[i]);
    }
    CHECK(used_pages == expected_used);
}

/* Independently specified result; full bitmap and counts checked both before
 * restoring IF and after return. Failure must leave even unused bits intact. */
static void range_expect(int n, u32 lo, u32 hi, u32 expected)
{
    u32 i, idx, before_total;
    unsigned int initial, old_saves, old_restores;

    before_total = total_pages;
    initial = test_flags;
    old_saves = saves;
    old_restores = restores;
    for (i = 0; i < BITMAP_SIZE; i++) expected_bitmap[i] = bitmap[i];
    expected_used = used_pages;
    if (expected) {
        CHECK(n > 0 && expected >= lo && expected < hi);
        CHECK((u32)n <= (hi - expected) / PAGE_SIZE);
        idx = phys_to_idx(expected);
        for (i = 0; i < (u32)n; i++) {
            CHECK(!bmp_test(idx + i));
            expected_bitmap[(idx + i) / 32] |= 1UL << ((idx + i) % 32);
        }
        expected_used += (u32)n;
    }
    checking_commit = watching = 1;
    scans = 0;
    CHECK(pgalloc_alloc_n_range(n, lo, hi) == expected);
    watching = 0;
    verify_commit();
    checking_commit = 0;
    CHECK(total_pages == before_total);
    CHECK(pgalloc_free_pages() == total_pages - expected_used);
    CHECK(test_flags == initial);
    CHECK(saves - old_saves == restores - old_restores);
    CHECK(saves - old_saves <= 1);
    if (expected) CHECK(saves == old_saves + 1);
    if (hi > lo) CHECK(scans <= (hi - lo) / PAGE_SIZE);
}

static void boundaries(void)
{
    u32 lo = MEM_APP_BAND_TOP;
    u32 end;
    int enabled;
    for (enabled = 0; enabled <= 1; enabled++) {
        pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
        end = pgalloc_limit_pfn() * PAGE_SIZE;
        test_flags = 0x45U | (enabled ? TEST_IF : 0);
        pgalloc_mark_used(lo + PAGE_SIZE, 1);
        range_expect(0, lo, lo + PAGE_SIZE, 0);
        range_expect(-1, lo, lo + PAGE_SIZE, 0);
        range_expect((-2147483647 - 1), lo, end, 0);
        range_expect(2147483647, lo, end, 0);
        range_expect(0x100000, lo, end, 0); /* n * PAGE_SIZE would wrap */
        range_expect(2, lo, lo + PAGE_SIZE, 0);
        range_expect(1, lo, lo, 0);
        range_expect(1, lo + PAGE_SIZE, lo, 0);
        range_expect(1, lo + 1, lo + PAGE_SIZE, 0);
        range_expect(1, lo, lo + PAGE_SIZE - 1, 0);
        range_expect(1, PGALLOC_BASE - PAGE_SIZE, lo, 0);
        range_expect(1, 0, lo, 0);
        range_expect(1, lo, end + PAGE_SIZE, 0);
        range_expect(1, end, end + PAGE_SIZE, 0);
        range_expect(1, lo, 0xfffff000UL, 0);
        range_expect(1, 0xfffff000UL, 0, 0);
        range_expect(1, lo, 0xffffffffUL, 0);
        range_expect(1, end - PAGE_SIZE, end, end - PAGE_SIZE);
        range_expect(1, PGALLOC_BASE, PGALLOC_BASE + PAGE_SIZE, PGALLOC_BASE);
        pgalloc_init(0);
        range_expect(1, PGALLOC_BASE, PGALLOC_BASE + PAGE_SIZE, 0);
        pgalloc_init((PGALLOC_BASE + PAGE_SIZE + 1024) / 1024);
        range_expect(1, PGALLOC_BASE, PGALLOC_BASE + 2 * PAGE_SIZE, 0);
        range_expect(1, PGALLOC_BASE, PGALLOC_BASE + PAGE_SIZE, PGALLOC_BASE);
    }
}

static void fragmentation(void)
{
    u32 lo = MEM_APP_BAND_TOP + 29 * PAGE_SIZE;
    u32 end = lo + 8 * PAGE_SIZE;
    pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
    pgalloc_mark_used(lo + PAGE_SIZE, 1);
    pgalloc_mark_used(lo + 4 * PAGE_SIZE, 1);
    range_expect(3, lo, end, lo + 5 * PAGE_SIZE);
    range_expect(3, lo, end, 0); /* many free pages elsewhere, no fallback */
    range_expect(2, lo, end, lo + 2 * PAGE_SIZE);
    range_expect(1, lo, end, lo);
    range_expect(1, lo, end, 0);
    CHECK(pgalloc_alloc_page() == PGALLOC_BASE); /* APP_BAND remains free */
    pgalloc_free_n(lo + 5 * PAGE_SIZE, 3);
    range_expect(3, lo, end, lo + 5 * PAGE_SIZE);
}

static void exhaustive(void)
{
    unsigned int mask;
    int n, j, run, start;
    u32 lo = MEM_APP_BAND_TOP + 29 * PAGE_SIZE;
    u32 expected;
    for (mask = 0; mask < 256; mask++) {
        for (n = 1; n <= 9; n++) {
            pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
            for (j = 0; j < 8; j++) {
                if (mask & (1U << j)) pgalloc_mark_used(lo + j * PAGE_SIZE, 1);
            }
            expected = 0;
            run = 0;
            start = 0;
            for (j = 0; j < 8; j++) {
                if (mask & (1U << j)) run = 0;
                else {
                    if (!run) start = j;
                    run++;
                    if (run == n) { expected = lo + start * PAGE_SIZE; break; }
                }
            }
            test_flags = 0x45U | ((mask & 1) ? TEST_IF : 0);
            range_expect(n, lo, lo + 8 * PAGE_SIZE, expected);
        }
    }
}

static void generic_regression(void)
{
    u32 count, base = PGALLOC_BASE;
    pgalloc_init(0xffffffffUL);
    count = (MEM_HIGH_RAM_BASE - base) / PAGE_SIZE;
    CHECK(pgalloc_total_pages() == count && pgalloc_free_pages() == count);
    CHECK(pgalloc_alloc_n(0) == 0 && pgalloc_alloc_n(-1) == 0);
    CHECK(pgalloc_alloc_n(2147483647) == 0);
    pgalloc_mark_used(base + PAGE_SIZE, 1);
    pgalloc_mark_used(base + PAGE_SIZE, 1);
    CHECK(pgalloc_free_pages() == count - 1);
    CHECK(pgalloc_alloc_n(2) == base + 2 * PAGE_SIZE);
    CHECK(pgalloc_alloc_page() == base);
    pgalloc_free_page(base + 1);
    pgalloc_free_n(base + 1, 4);
    pgalloc_mark_used(base + 4 * PAGE_SIZE + 1, 1);
    CHECK(pgalloc_free_pages() == count - 4);
    pgalloc_free_page(base);
    pgalloc_free_page(base);
    pgalloc_free_n(base + PAGE_SIZE, 3);
    CHECK(pgalloc_free_pages() == count); /* legacy mark + allocated: releasable */
    pgalloc_free_n(base + 2 * PAGE_SIZE, 2); /* double free stays harmless */
    CHECK(pgalloc_free_pages() == count);
    CHECK(pgalloc_total_pages() == count); /* marks do not remove eligibility */
    pgalloc_init(0xffffffffUL); /* fresh-boot fixture for full-run regression */
    CHECK(pgalloc_alloc_n((int)count) == base);
    CHECK(pgalloc_alloc_page() == 0 && pgalloc_alloc_n(1) == 0);
    range_expect(1, MEM_APP_BAND_TOP, MEM_APP_BAND_TOP + PAGE_SIZE, 0);
    pgalloc_free_n(base, (int)count);
    CHECK(pgalloc_free_pages() == count);
}

/* K6-3: デバイス窓の可否は「RAM の上端」ではなく「窓に RAM が登録されて
 * いるか」で決める。上端で決めた旧条件は、上端が窓より上に出る構成
 * (K6-RAM 以後の 15MB 機: 上端 17MB、穴 15-16MB) で誤判定した。 */
static void device_window_ram(void)
{
    u32 lo = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE;
    u32 hi = MEM_HIGH_RAM_BASE / PAGE_SIZE;

    /* 8MiB: RAM が窓まで届かない = 窓は空いている (K6 以前と同じ判定)。 */
    pgalloc_init(8192);
    CHECK(!pgalloc_range_has_ram(lo, hi));
    CHECK(pgalloc_limit_pfn() * PAGE_SIZE <= MEM_SYSTEM_SPACE_BASE);

    /* 16MiB を丸ごと RAM にした構成 (legacy 経路): 窓が RAM = 張れない。 */
    pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
    CHECK(pgalloc_range_has_ram(lo, hi));
    /* 1 ページでも RAM が重なれば真 (部分一致で見落とさない)。 */
    CHECK(pgalloc_range_has_ram(hi - 1, hi));
    /* 窓の外 (高位側) には RAM は無い。 */
    CHECK(!pgalloc_range_has_ram(hi, hi + 1));
    /* 範囲異常・未初期化は保守的に真 (窓を張らせない)。 */
    CHECK(pgalloc_range_has_ram(hi, lo));
    initialized = 0;
    CHECK(pgalloc_range_has_ram(lo, hi));
    pgalloc_init(MEM_HIGH_RAM_BASE / 1024);
}

void _start(void)
{
    basic();
    output("PASS basic\n");
    irq_atomic();
    output("PASS irq_atomic IF=1/0 success/exhaustion\n");
    boundaries();
    output("PASS invalid/overflow/management boundaries IF=1/0\n");
    fragmentation();
    output("PASS fragmentation/exhaustion/no APP_BAND fallback/reuse\n");
    exhaustive();
    output("PASS exhaustive 8-page occupancy x n=1..9\n");
    generic_regression();
    output("PASS generic alloc/free/mark/init regression\n");
    device_window_ram();
    output("PASS device window RAM query (K6-3)\n");
    finish(0);
}
