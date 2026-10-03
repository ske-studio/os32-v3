/* Real appmem.c, ILP32; only Linux write/exit form the host fixture. */
#include "appmem.h"
#include "memmap.h"
#include "paging.h"

#define WRAP_BYTES 0xfffff001UL
#define ONE_SHORT_BYTES 0xfffff000UL
#define UNKNOWN_FLAG 4U
#define TEST_ATTR 8U
static u32 checks;
static void say(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void die(int code) __attribute__((noreturn));
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
#define CHECK(name, x) do { checks++; if (!(x)) { \
    say("FAIL: " name "\n", sizeof("FAIL: " name "\n") - 1); die(1); } } while (0)
static int equal(const void *a, const void *b, u32 size)
{
    const u8 *x = a, *y = b;
    for (u32 i = 0; i < size; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static struct appmem_layout layout(void)
{
    return (struct appmem_layout){MEM_EXEC_LOAD_ADDR + 1,
        MEM_EXEC_LOAD_ADDR + 2 * PAGE_SIZE,
        MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN,
        MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE};
}
static int prepare(struct appmem_table *t, struct appmem_layout *l,
                   u32 bytes, u32 hint, u32 flags, struct appmem_plan *p)
{
    return appmem_prepare(t, l, bytes, hint, flags, APPMEM_ANON, 0, p);
}
static void rejected(struct appmem_table *t, struct appmem_layout *l,
                     u32 bytes, u32 hint, u32 flags, int error, const char *name)
{
    struct appmem_table before = *t;
    struct appmem_plan p = {0}, previous = p;
    checks++;
    if (prepare(t, l, bytes, hint, flags, &p) != error) {
        u32 len = 0;
        while (name[len]) len++;
        say("FAIL: ", sizeof("FAIL: ") - 1); say(name, len); say("\n", 1); die(1);
    }
    CHECK("reject table unchanged", equal(t, &before, sizeof(*t)));
    CHECK("reject plan unchanged", equal(&p, &previous, sizeof(p)));
}
static void number(u32 n)
{
    char buf[10]; u32 len = 0;
    do { buf[len++] = '0' + n % 10; n /= 10; } while (n);
    for (u32 i = 0; i < len / 2; i++) {
        char tmp = buf[i]; buf[i] = buf[len - i - 1]; buf[len - i - 1] = tmp;
    }
    say(buf, len);
}
static void unmap_table(void)
{
    u32 lo = MEM_EXEC_LOAD_ADDR + PAGE_SIZE;
    struct appmem_table t = {0}, before;
    struct appmem_unmap_plan p = {0}, old;
    /* Whole, head, tail, middle; metadata survives both residual edges. */
    for (u32 shape = 0; shape < 4; shape++) {
        t = (struct appmem_table){0};
        t.e[0] = (struct appmem_extent){lo, lo + 4*PAGE_SIZE, APPMEM_LIBC_INITIAL, TEST_ATTR};
        u32 base = lo + ((shape == 2 || shape == 3) ? PAGE_SIZE : 0);
        u32 end = lo + ((shape == 1 || shape == 3) ? 3*PAGE_SIZE : 4*PAGE_SIZE);
        CHECK("unmap table prepare", !appmem_unmap_prepare(&t, base, end, APPMEM_PUBLIC_UNMAP_MASK, &p));
        CHECK("unmap metadata preserved", (!p.left.base || (p.left.kind == APPMEM_LIBC_INITIAL && p.left.flags == TEST_ATTR)) &&
              (!p.right.base || (p.right.kind == APPMEM_LIBC_INITIAL && p.right.flags == TEST_ATTR)));
        CHECK("unmap table valid", appmem_unmap_plan_valid(&t, &p));
        appmem_unmap_publish(&t, &p);
        CHECK("unmap table remnants", (!p.left.base || equal(&t.e[0], &p.left, sizeof(p.left))) &&
              (!p.right.base || equal(&t.e[!!p.left.base], &p.right, sizeof(p.right))));
        CHECK("unmap table empty tail", !t.e[p.remain_count].base && !t.e[p.remain_count].kind);
    }
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){lo, lo+PAGE_SIZE, APPMEM_LIBC_INITIAL, TEST_ATTR};
    t.e[1] = (struct appmem_extent){lo+PAGE_SIZE, lo+2*PAGE_SIZE, APPMEM_ANON, 0};
    CHECK("unmap adjacent kinds", !appmem_unmap_prepare(&t, lo, lo+2*PAGE_SIZE, APPMEM_PUBLIC_UNMAP_MASK, &p));
    appmem_unmap_publish(&t, &p);
    CHECK("unmap adjacent removed", !t.e[0].base);
    for (u32 count = APPMEM_EXTENT_MAX-1; count <= APPMEM_EXTENT_MAX; count++) {
        t = (struct appmem_table){0};
        for (u32 i = 0; i < count; i++)
            t.e[i] = (struct appmem_extent){lo+4*i*PAGE_SIZE, lo+(4*i+3)*PAGE_SIZE, APPMEM_ANON, TEST_ATTR};
        before = t; old = p;
        int rc = appmem_unmap_prepare(&t, lo+PAGE_SIZE, lo+2*PAGE_SIZE, APPMEM_PUBLIC_UNMAP_MASK, &p);
        CHECK("unmap table slot reason", rc == (count == APPMEM_EXTENT_MAX ? APPMEM_EFULL : 0));
        CHECK("unmap prepare table unchanged", equal(&t, &before, sizeof(t)));
        if (rc) CHECK("unmap full plan unchanged", equal(&p, &old, sizeof(p)));
        else {
            appmem_unmap_publish(&t, &p);
            CHECK("unmap split shifts tail", t.e[2].base == lo+4*PAGE_SIZE && t.e[APPMEM_EXTENT_MAX-1].base == lo+4*(count-1)*PAGE_SIZE);
        }
    }
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){lo, lo+3*PAGE_SIZE, APPMEM_EXEC_LARGE, TEST_ATTR};
    CHECK("unmap private identity prepare", !appmem_unmap_prepare(&t, lo+PAGE_SIZE, lo+2*PAGE_SIZE,
          APPMEM_KIND_MASK(APPMEM_EXEC_LARGE), &p));
    CHECK("unmap private identity", p.left.kind == APPMEM_EXEC_LARGE && p.right.kind == APPMEM_EXEC_LARGE &&
          p.left.flags == TEST_ATTR && p.right.flags == TEST_ATTR);
    t.e[0].flags++;
    before = t;
    CHECK("unmap stale metadata invalid", !appmem_unmap_plan_valid(&t, &p));
    appmem_unmap_publish(&t, &p);
    CHECK("unmap stale publish unchanged", equal(&t, &before, sizeof(t)));
    t.e[0].flags = TEST_ATTR;
    t.e[0].base += PAGE_SIZE;
    CHECK("unmap stale edge invalid", !appmem_unmap_plan_valid(&t, &p));
    const u32 invalid[][2] = {{lo+1,lo+PAGE_SIZE},{lo,lo},{lo,lo+1},
        {MEM_SHLIB_BASE,lo},{lo,MEM_LEASE_BASE},{0,~(u32)0}};
    for (u32 i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        before = t; old = p;
        CHECK("unmap invalid range", appmem_unmap_prepare(&t, invalid[i][0], invalid[i][1], APPMEM_PUBLIC_UNMAP_MASK, &p) == APPMEM_EINVAL);
        CHECK("unmap reject unchanged", equal(&t, &before, sizeof(t)) && equal(&p, &old, sizeof(p)));
    }
    struct appmem_layout reusable = layout();
    reusable.primary_mapped_end = PAGE_ALIGN_UP(reusable.img_end);
    struct appmem_plan remap;
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){MEM_EXEC_HEAP_BASE-PAGE_SIZE, MEM_EXEC_HEAP_BASE, APPMEM_LIBC_INITIAL, 0};
    CHECK("libc hole prepare", !appmem_unmap_prepare(&t, t.e[0].base, t.e[0].end, APPMEM_PUBLIC_UNMAP_MASK, &p));
    appmem_unmap_publish(&t, &p);
    CHECK("libc hole flags0 reuse", !prepare(&t, &reusable, PAGE_SIZE, 0, 0, &remap) && remap.base == MEM_EXEC_HEAP_BASE-PAGE_SIZE);
    t.e[0] = (struct appmem_extent){lo+PAGE_SIZE,lo+2*PAGE_SIZE,APPMEM_LIBC_INITIAL,0};
    CHECK("libc anon distinct prepare", !prepare(&t, &reusable, PAGE_SIZE, lo+2*PAGE_SIZE, APPMEM_MAP_EXACT, &remap));
    appmem_publish(&t, &remap);
    CHECK("libc anon do not merge", t.e[0].kind == APPMEM_LIBC_INITIAL && t.e[1].kind == APPMEM_ANON);
    /* Same-count stale map proposals must recalculate insertion/merge edges. */
    struct appmem_layout l = layout();
    struct appmem_plan m;
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){lo+3*PAGE_SIZE,lo+4*PAGE_SIZE,APPMEM_ANON,0};
    CHECK("same count prepare", !prepare(&t, &l, PAGE_SIZE, lo+2*PAGE_SIZE, APPMEM_MAP_EXACT, &m));
    t.e[0] = (struct appmem_extent){lo+PAGE_SIZE,lo+2*PAGE_SIZE,APPMEM_ANON,0};
    before = t;
    CHECK("same count merge invalid", !appmem_plan_valid(&t, &m));
    appmem_publish(&t, &m);
    CHECK("same count publish unchanged", equal(&t, &before, sizeof(t)));
    t.e[0] = (struct appmem_extent){lo+5*PAGE_SIZE,lo+6*PAGE_SIZE,APPMEM_ANON,0};
    CHECK("same count lost neighbor invalid", !appmem_plan_valid(&t, &m));
    CHECK("same count insertion prepare", !prepare(&t, &l, PAGE_SIZE, lo+2*PAGE_SIZE, APPMEM_MAP_EXACT, &m));
    t.e[0] = (struct appmem_extent){lo,lo+PAGE_SIZE,APPMEM_ANON,0};
    CHECK("same count insertion invalid", !appmem_plan_valid(&t, &m));
}

static void run(void)
{
    struct appmem_layout l = layout();
    struct appmem_table t = {0}, before;
    struct appmem_plan p = {0}, old;
    u32 low = l.primary_mapped_end, high = MEM_EXEC_HEAP_BASE;
    CHECK("extent bytes", sizeof(t.e[0]) == 16 && sizeof(t) == 16 * APPMEM_EXTENT_MAX);
    CHECK("round page", !prepare(&t, &l, 1, 0, 0, &p) && p.end - p.base == PAGE_SIZE);
    CHECK("flags0 preserves break", p.base == high - PAGE_SIZE && p.base > low);
    CHECK("prepare read only", !t.e[0].base);
    CHECK("topdown guard", !prepare(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, &p) &&
          p.base == l.guard_b - PAGE_SIZE && p.end == l.guard_b);
    CHECK("topdown whole window", !prepare(&t, &l, l.guard_b - l.exec_heap_cur_end,
          0, APPMEM_MAP_TOPDOWN, &p) && p.base == l.exec_heap_cur_end);
    rejected(&t, &l, l.guard_b - l.exec_heap_cur_end + PAGE_SIZE, 0,
             APPMEM_MAP_TOPDOWN, APPMEM_ENOVA, "upper short");
    CHECK("lower exact fit", !prepare(&t, &l, high - low, 0, 0, &p) && p.base == low && p.end == high);
    rejected(&t, &l, high - low + PAGE_SIZE, 0, 0, APPMEM_ENOVA, "lower short");
    CHECK("lowest page", !prepare(&t, &l, PAGE_SIZE, low, APPMEM_MAP_EXACT, &p) && p.base == low);
    CHECK("highest page", !prepare(&t, &l, PAGE_SIZE, high - PAGE_SIZE, APPMEM_MAP_EXACT, &p) && p.end == high);
    CHECK("upper hint flags0", !prepare(&t, &l, PAGE_SIZE, l.exec_heap_cur_end, 0, &p) && p.base == l.exec_heap_cur_end);
    CHECK("lower hint topdown", !prepare(&t, &l, PAGE_SIZE, low, APPMEM_MAP_TOPDOWN, &p) && p.base == low);
    CHECK("exact wins topdown", !prepare(&t, &l, PAGE_SIZE, low,
          APPMEM_MAP_EXACT | APPMEM_MAP_TOPDOWN, &p) && p.base == low);
    t.e[0] = (struct appmem_extent){low, low + PAGE_SIZE, APPMEM_ANON, 0};
    CHECK("exact collision", prepare(&t, &l, PAGE_SIZE, low, APPMEM_MAP_EXACT, &p) == APPMEM_ENOVA);
    CHECK("exact topdown collision", prepare(&t, &l, PAGE_SIZE, low,
          APPMEM_MAP_EXACT | APPMEM_MAP_TOPDOWN, &p) == APPMEM_ENOVA);
    t = (struct appmem_table){0};
    rejected(&t, &l, 0, 0, 0, APPMEM_EINVAL, "zero");
    rejected(&t, &l, WRAP_BYTES, 0, 0, APPMEM_EINVAL, "round overflow");
    rejected(&t, &l, ONE_SHORT_BYTES, low, 0, APPMEM_EINVAL, "hint addition wrap");
    rejected(&t, &l, PAGE_SIZE, low + 1, 0, APPMEM_EINVAL, "unaligned");
    rejected(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_EXACT, APPMEM_EINVAL, "null exact");
    rejected(&t, &l, PAGE_SIZE, low, UNKNOWN_FLAG, APPMEM_EINVAL, "unknown flag");
    const u32 outside[] = {PAGE_SIZE, MEM_SHLIB_BASE, MEM_APP_BAND_MAX_TOP, MEM_LEASE_BASE};
    for (u32 i = 0; i < sizeof(outside) / sizeof(outside[0]); i++)
        rejected(&t, &l, PAGE_SIZE, outside[i], 0, APPMEM_EINVAL, "outside");
    const u32 fixed[] = {MEM_EXEC_LOAD_ADDR, low - PAGE_SIZE, MEM_EXEC_HEAP_BASE,
                        l.guard_b, MEM_APP_STACK_TOP - PAGE_SIZE};
    for (u32 i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++) {
        rejected(&t, &l, PAGE_SIZE, fixed[i], APPMEM_MAP_EXACT, APPMEM_ENOVA, "fixed");
        CHECK("fixed hint fallback", !prepare(&t, &l, PAGE_SIZE, fixed[i], 0, &p) && p.base == high - PAGE_SIZE);
    }
    rejected(&t, &l, 2 * PAGE_SIZE, high - PAGE_SIZE, APPMEM_MAP_EXACT, APPMEM_ENOVA, "cross boundary");
    CHECK("round max no overflow", prepare(&t, &l, ONE_SHORT_BYTES, 0, 0, &p) == APPMEM_ENOVA);
    CHECK("hint success", !prepare(&t, &l, PAGE_SIZE, low + PAGE_SIZE, 0, &p) && p.base == low + PAGE_SIZE);
    appmem_publish(&t, &p);
    before = t; old = p;
    CHECK("exact collision", prepare(&t, &l, PAGE_SIZE, low + PAGE_SIZE, APPMEM_MAP_EXACT, &p) == APPMEM_ENOVA);
    CHECK("exact unchanged", equal(&t, &before, sizeof(t)) && equal(&p, &old, sizeof(p)));
    CHECK("hint collision fallback", !prepare(&t, &l, PAGE_SIZE, low + PAGE_SIZE, 0, &p) && p.base == high - PAGE_SIZE);
    CHECK("hint collision unchanged", equal(&t, &before, sizeof(t)));
    appmem_publish(&t, &p);
    CHECK("sorted append", t.e[0].base == low + PAGE_SIZE && t.e[1].base == high - PAGE_SIZE);
    CHECK("descending occupied top", !prepare(&t, &l, PAGE_SIZE, 0, 0, &p) && p.base == high - 2 * PAGE_SIZE);
    appmem_publish(&t, &p);
    CHECK("right merge", t.e[1].base == high - 2 * PAGE_SIZE && t.e[1].end == high && !t.e[2].base);
    CHECK("left merge prepare", !prepare(&t, &l, PAGE_SIZE, low + 2 * PAGE_SIZE, APPMEM_MAP_EXACT, &p));
    appmem_publish(&t, &p);
    CHECK("left merge", t.e[0].base == low + PAGE_SIZE && t.e[0].end == low + 3 * PAGE_SIZE);
    CHECK("sorted prepend prepare", !prepare(&t, &l, PAGE_SIZE, low, APPMEM_MAP_EXACT, &p));
    appmem_publish(&t, &p);
    CHECK("sorted prepend", t.e[0].base == low && t.e[0].end == low + 3 * PAGE_SIZE);

    /* P3-3: both a changed count and a newly occupied same-count range. */
    before = t;
    appmem_publish(&t, &p);
    CHECK("stale publish range", equal(&t, &before, sizeof(t)));
    CHECK("stale proposal prepare", !prepare(&t, &l, PAGE_SIZE, high - 4 * PAGE_SIZE, APPMEM_MAP_EXACT, &old));
    /* Use a separate valid table with a different count. */
    t = (struct appmem_table){0};
    before = t;
    appmem_publish(&t, &old);
    CHECK("stale publish count", equal(&t, &before, sizeof(t)));

    /* Hole clipped by extents at both edges; exactly one free page remains. */
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){low, high - 2 * PAGE_SIZE, APPMEM_ANON, 0};
    t.e[1] = (struct appmem_extent){high - PAGE_SIZE, high, APPMEM_ANON, 0};
    CHECK("bounded hole exact fit", !prepare(&t, &l, PAGE_SIZE, 0, 0, &p) && p.base == high - 2 * PAGE_SIZE);
    appmem_publish(&t, &p);
    CHECK("bridge merges both", t.e[0].base == low && t.e[0].end == high && !t.e[1].base && !t.e[1].end && !t.e[1].kind && !t.e[1].flags);
    rejected(&t, &l, PAGE_SIZE, 0, 0, APPMEM_ENOVA, "no VA");
    t.e[0].end = high - 2 * PAGE_SIZE;
    t.e[1] = (struct appmem_extent){high - PAGE_SIZE, high, APPMEM_ANON, 0};
    rejected(&t, &l, 2 * PAGE_SIZE, 0, 0, APPMEM_ENOVA, "hole short");

    /* Attribute and kind distinctions, including identical EXEC_LARGE IDs. */
    for (u32 variant = 0; variant < 3; variant++) {
        u32 kind = variant == 2 ? APPMEM_EXEC_LARGE : APPMEM_ANON;
        u32 attr = variant == 1 ? TEST_ATTR : 0;
        t = (struct appmem_table){0};
        t.e[0] = (struct appmem_extent){low, low + PAGE_SIZE, kind, attr};
        u32 next_kind = variant == 0 ? APPMEM_EXEC_ARENA : kind;
        CHECK("distinct prepare", !appmem_prepare(&t, &l, PAGE_SIZE, low + PAGE_SIZE,
              APPMEM_MAP_EXACT, next_kind, 0, &p));
        appmem_publish(&t, &p);
        CHECK("distinct extents", t.e[0].end == low + PAGE_SIZE && t.e[1].base == low + PAGE_SIZE);
    }
    t = (struct appmem_table){0};
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        t.e[i] = (struct appmem_extent){low + 2 * i * PAGE_SIZE,
            low + (2 * i + 1) * PAGE_SIZE, APPMEM_ANON, 0};
    }
    before = t; old = p;
    CHECK("full reason", prepare(&t, &l, PAGE_SIZE, 0, 0, &p) == APPMEM_EFULL);
    CHECK("full table unchanged", equal(&t, &before, sizeof(t)));
    CHECK("full plan unchanged", equal(&p, &old, sizeof(p)));
    CHECK("full left merge prepare", !prepare(&t, &l, PAGE_SIZE,
          t.e[APPMEM_EXTENT_MAX - 1].end, APPMEM_MAP_EXACT, &p));
    CHECK("full merge no slot", p.remove_count == 1 && p.count == APPMEM_EXTENT_MAX);
    appmem_publish(&t, &p);
    CHECK("full bridge prepare", !prepare(&t, &l, PAGE_SIZE, low + PAGE_SIZE, APPMEM_MAP_EXACT, &p));
    CHECK("full bridge releases slot", p.remove_count == 2);
    appmem_publish(&t, &p);
    CHECK("full bridge sorted tail", t.e[0].end == low + 3 * PAGE_SIZE &&
          t.e[1].base == low + 4 * PAGE_SIZE && !t.e[APPMEM_EXTENT_MAX - 1].base);
    CHECK("insert middle prepare", !appmem_prepare(&t, &l, PAGE_SIZE, low + 3 * PAGE_SIZE,
          APPMEM_MAP_EXACT, APPMEM_EXEC_ARENA, 0, &p));
    appmem_publish(&t, &p);
    CHECK("insert middle sorted", t.e[1].kind == APPMEM_EXEC_ARENA && t.e[2].base == low + 4 * PAGE_SIZE);

    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){l.exec_heap_cur_end, l.guard_b, APPMEM_ANON, 0};
    rejected(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, APPMEM_ENOVA, "topdown no VA");
    t = (struct appmem_table){0};
    t.e[0] = (struct appmem_extent){l.guard_b - PAGE_SIZE, l.guard_b, APPMEM_ANON, 0};
    CHECK("topdown occupied guard", !prepare(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, &p) && p.base == l.guard_b - 2 * PAGE_SIZE);
    t.e[0].base = l.exec_heap_cur_end; t.e[0].end = l.guard_b - PAGE_SIZE;
    CHECK("topdown fragmented fit", !prepare(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, &p) && p.end == l.guard_b);
    rejected(&t, &l, 2 * PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, APPMEM_ENOVA, "topdown fragment short");
    t = (struct appmem_table){0}; l = layout();
    l.primary_mapped_end = MEM_EXEC_HEAP_BASE;
    rejected(&t, &l, PAGE_SIZE, 0, 0, APPMEM_ENOVA, "empty lower");
    l.exec_heap_cur_end = l.guard_b;
    rejected(&t, &l, PAGE_SIZE, 0, APPMEM_MAP_TOPDOWN, APPMEM_ENOVA, "empty upper");
    l = layout(); l.primary_mapped_end--;
    rejected(&t, &l, PAGE_SIZE, 0, 0, APPMEM_EINVAL, "bad layout");
    l = layout(); t.e[1] = (struct appmem_extent){low, low + PAGE_SIZE, APPMEM_ANON, 0};
    rejected(&t, &l, PAGE_SIZE, 0, 0, APPMEM_EINVAL, "empty not tail");
    t.e[0] = t.e[1];
    rejected(&t, &l, PAGE_SIZE, 0, 0, APPMEM_EINVAL, "overlapping table");
    CHECK("null table", appmem_prepare(0, &l, PAGE_SIZE, 0, 0, APPMEM_ANON, 0, &p) == APPMEM_EINVAL);
    CHECK("null layout", prepare(&t, 0, PAGE_SIZE, 0, 0, &p) == APPMEM_EINVAL);
    CHECK("null output", prepare(&t, &l, PAGE_SIZE, 0, 0, 0) == APPMEM_EINVAL);
}
void _start(void)
{
    run(); unmap_table();
    say("PASS appmem CHECKS=", sizeof("PASS appmem CHECKS=") - 1); number(checks); say("\n", 1);
    die(0);
}
