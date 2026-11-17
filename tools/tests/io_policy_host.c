/* IO_TIER §4/§8: identical manually encoded bytes on little/big endian CPUs. */
#include "io_policy.h"
#include "os32_kapi_shared.h"
#include "io.h" /* Test-local declarations, including in mutation copies. */

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int running, irq_bad;
static unsigned int irq_state = 0x246, saves, restores;

unsigned int irq_save(void)
{
    unsigned int old = irq_state;
    irq_state = 0;
    ++saves;
    return old;
}
void irq_restore(unsigned int state)
{
    if (irq_state != 0) irq_bad = 1;
    irq_state = state;
    ++restores;
}
int io_dispatch_running(void)
{
    if (irq_state != 0) irq_bad = 1;
    return running;
}
static void copy(u8 *dst, const u8 *src, unsigned int n)
{
    for (unsigned int i = 0; i < n; ++i) dst[i] = src[i];
}
static int equal(const u8 *a, const u8 *b, unsigned int n)
{
    for (unsigned int i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static void le32(u8 *p, u32 n)
{
    p[0] = n & 255; p[1] = (n >> 8) & 255;
    p[2] = (n >> 16) & 255; p[3] = (n >> 24) & 255;
}
static void header(u8 *p, u32 count)
{
    for (int i = 0; i < 272; ++i) p[i] = 0;
    p[0] = 'I'; p[1] = 'O'; p[2] = 'T'; p[3] = '1';
    p[4] = 1; p[6] = 16; le32(p + 8, count);
}
static void row(u8 *p, unsigned int n, u32 id, u8 tier, u32 budget)
{
    u8 *r = p + 16 + n * 16;
    le32(r, id); r[4] = tier; le32(r + 8, budget);
}
static int get(u8 *p)
{
    unsigned int needed = 0;
    return io_policy_get(p, 80, &needed) == 0 && needed == 80;
}
static int rejected(const u8 *p, unsigned int len)
{
    u8 before[80], after[80];
    u32 generation = io_policy_generation();
    unsigned int calls = saves;
    CHECK(get(before));
    CHECK(io_policy_set(p, len, 5, 77) == OS32_ERR_INVAL);
    CHECK(get(after) && equal(before, after, 80));
    CHECK(io_policy_generation() == generation && saves == calls);
    return 0;
}
/* Isolated runtime oracle for both restoration-chain link mutations. */
static int three_owner_splice(void)
{
    u8 p[272];
    u32 budget;
    CHECK(io_policy_reset(0, 0) == 0);
    header(p, 1);
    row(p, 0, 1, 1, 201); CHECK(io_policy_set(p, 32, 2, 602) == 0);
    row(p, 0, 1, 1, 303); CHECK(io_policy_set(p, 32, 3, 603) == 0);
    row(p, 0, 1, 1, 404); CHECK(io_policy_set(p, 32, 4, 604) == 0);
    io_policy_owner_exit(3, 603); /* Remove the middle of three live layers. */
    CHECK(io_policy_lookup(1, 0, &budget) == 0 && budget == 404);
    io_policy_owner_exit(4, 604);
    CHECK(io_policy_lookup(1, 0, &budget) == 0 && budget == 201);
    /* Reuse the popped top, then remove the oldest under that new writer. */
    row(p, 0, 1, 1, 405); CHECK(io_policy_set(p, 32, 4, 704) == 0);
    io_policy_owner_exit(2, 602);
    CHECK(io_policy_lookup(1, 0, &budget) == 0 && budget == 405);
    io_policy_owner_exit(4, 704);
    CHECK(io_policy_lookup(1, 0, &budget) == 0 && budget == 120);
    return 0;
}
#ifndef POLICY_SPLICE_ONLY
#define POLICY_SPLICE_ONLY 0
#endif
static int exercise(void)
{
    u8 store[274], *p = store + 1; /* Deliberately alignment 1. */
    u8 standard[80], before[80], after[80], expected[272], bad[272];
    u8 tier;
    u32 budget, min, max, gen;
    unsigned int needed;
    CHECK(io_policy_generation() == 0 && get(standard));
    header(expected, 4);
    for (u32 id = 1; id <= 4; ++id) {
        CHECK(io_policy_limits(id, &tier, &budget, &min, &max) == 0);
        CHECK(tier == (id == 3 ? 3 : 2));
        CHECK(budget == (id == 4 ? 440 : 120) && min == 1 && max == 500);
        row(expected, id - 1, id, tier, budget);
        CHECK(io_policy_lookup(id, &tier, &budget) == 0);
    }
    CHECK(equal(standard, expected, 80));
    store[0] = 0xa5; p[80] = 0x5a;
    CHECK(io_policy_get(p, 81, &needed) == 0 && needed == 80);
    CHECK(equal(p, standard, 80) && store[0] == 0xa5 && p[80] == 0x5a);
    CHECK(io_policy_lookup(99, &tier, &budget) == OS32_ERR_NOTFOUND);
    CHECK(io_policy_limits(99, &tier, &budget, &min, &max) == OS32_ERR_NOTFOUND);
    for (unsigned int cap = 0; cap < 80; ++cap) {
        copy(after, standard, 80);
        CHECK(io_policy_get(after, cap, &needed) == OS32_ERR_FULL);
        CHECK(needed == 80 && equal(after, standard, 80));
    }
    CHECK(io_policy_get(0, 0, &needed) == OS32_ERR_FULL && needed == 80);
    CHECK(io_policy_get(0, 80, &needed) == OS32_ERR_INVAL);
    CHECK(io_policy_get(after, 80, 0) == OS32_ERR_INVAL);
    header(p, 2); row(p, 0, 1, 1, 308); row(p, 1, 2, 1, 267);
    CHECK(io_policy_set(p, 48, 0, 0) == 0 && io_policy_generation() == 1);
    header(p, 1); row(p, 0, 1, 1, 308);
    CHECK(io_policy_set(p, 32, 0, 0) == 0 && io_policy_generation() == 2);
    row(expected, 0, 1, 1, 308);
    CHECK(get(before) && equal(before, expected, 80)); /* Missing RX resets. */
    CHECK(io_policy_lookup(1, &tier, &budget) == 0 && tier == 1 && budget == 308);
    copy(bad, before, 80);
    CHECK(io_policy_set(bad, 80, 0, 0) == 0 && io_policy_generation() == 3);
    CHECK(get(after) && equal(before, after, 80)); /* Full wire round-trip. */
    CHECK(rejected(0, 32) == 0 && rejected(p, 15) == 0);
    CHECK(rejected(p, 31) == 0 && rejected(p, 33) == 0);
    /* Every reserved/header byte is checked, including high LE bytes. */
    static const unsigned int offsets[] = {0,1,2,3,4,5,6,7,9,10,11,12,13,14,15,
                                   21,22,23,28,29,30,31};
    for (unsigned int i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
        copy(bad, p, 32); bad[offsets[i]] ^= 128;
        CHECK(rejected(bad, 32) == 0);
    }
    copy(bad, p, 32); le32(bad + 8, 17);
    CHECK(rejected(bad, 272) == 0);
    le32(bad + 8, 0xffffffffUL); CHECK(rejected(bad, 32) == 0);
    for (u32 id = 1; id <= 4; ++id) {
        CHECK(io_policy_limits(id, 0, 0, &min, &max) == 0);
        header(bad, 1); row(bad, 0, id, 1, min - 1);
        CHECK(rejected(bad, 32) == 0);
        row(bad, 0, id, 1, max + 1); CHECK(rejected(bad, 32) == 0);
        row(bad, 0, id, 0, min); CHECK(rejected(bad, 32) == 0);
        row(bad, 0, id, 4, min); CHECK(rejected(bad, 32) == 0);
        row(bad, 0, id, 1, min); CHECK(io_policy_set(bad, 32, 0, 0) == 0);
        row(bad, 0, id, 3, max); CHECK(io_policy_set(bad, 32, 0, 0) == 0);
    }
    header(bad, 2); row(bad, 0, 2, 1, 256); row(bad, 1, 2, 3, 300);
    CHECK(rejected(bad, 48) == 0); /* Duplicate after a valid changed row. */
    row(bad, 1, 0x01000001, 3, 300); CHECK(rejected(bad, 48) == 0);
    row(bad, 1, 0, 3, 300); CHECK(rejected(bad, 48) == 0);
    row(bad, 1, 1, 3, 501); CHECK(rejected(bad, 48) == 0);
    /* A rejected owner-5 set must not have saved any earlier table. */
    CHECK(io_policy_set(p, 32, 0, 0) == 0 && get(before));
    CHECK(io_policy_reset(5, 77) == 0);
    io_policy_owner_exit(5, 77);
    CHECK(get(after) && equal(before, after, 80));
    gen = io_policy_generation(); running = 1;
    CHECK(io_policy_set(p, 32, 5, 78) == OS32_ERR_BUSY);
    CHECK(io_policy_reset(5, 78) == OS32_ERR_BUSY);
    CHECK(io_policy_generation() == gen && get(after) && equal(before, after, 80));
    running = 0;
    CHECK(io_policy_reset(0, 0) == 0);
    CHECK(io_policy_set(p, 32, 5, 78) == 0);
    io_policy_owner_exit(5, 78); /* BUSY must not save either. */
    CHECK(get(after) && equal(standard, after, 80));
    /* Owner 0 persists; four nested app IDs unwind in reverse order. */
    CHECK(io_policy_set(p, 32, 0, 0) == 0 && get(before));
    io_policy_owner_exit(0, 0);
    CHECK(get(after) && equal(before, after, 80));
    for (u32 owner = 2; owner <= 5; ++owner) {
        row(p, 0, 1, 1, 300 + owner);
        CHECK(io_policy_set(p, 32, owner, 100 + owner) == 0);
        row(p, 0, 1, 3, 400 + owner);
        CHECK(io_policy_set(p, 32, owner, 100 + owner) == 0);
    }
    for (u32 owner = 5; owner >= 2; --owner) {
        gen = io_policy_generation();
        io_policy_owner_exit(owner, 99); CHECK(io_policy_generation() == gen);
        running = 1; /* P3 applies to normal/fault/kill common reclaim. */
        io_policy_owner_exit(owner, 100 + owner); running = 0;
        CHECK(io_policy_generation() == gen + 1);
        CHECK(io_policy_lookup(1, &tier, &budget) == 0);
        CHECK(budget == (owner == 2 ? 308 : 399 + owner));
        io_policy_owner_exit(owner, 100 + owner);
        CHECK(io_policy_generation() == gen + 1);
    }
    CHECK(get(after) && equal(before, after, 80));
    /* GUI siblings can finish in either order: no dead owner's resurrection. */
    for (unsigned int reverse = 0; reverse < 2; ++reverse) {
        CHECK(io_policy_reset(0, 0) == 0);
        row(p, 0, 1, 1, 201); CHECK(io_policy_set(p, 32, 2, 202) == 0);
        row(p, 0, 1, 1, 303); CHECK(io_policy_set(p, 32, 3, 203) == 0);
        gen = io_policy_generation();
        io_policy_owner_exit(reverse ? 3 : 2, reverse ? 203 : 202);
        CHECK(io_policy_lookup(1, &tier, &budget) == 0);
        CHECK(budget == (reverse ? 201u : 303u));
        CHECK(io_policy_generation() == gen + reverse);
        io_policy_owner_exit(reverse ? 2 : 3, reverse ? 202 : 203);
        CHECK(get(after) && equal(standard, after, 80));
    }
    /* A -> B -> A: the returning writer must become the newest saved layer. */
    for (unsigned int reset = 0; reset < 2; ++reset) {
        for (unsigned int reverse = 0; reverse < 2; ++reverse) {
            CHECK(io_policy_reset(0, 0) == 0);
            row(p, 0, 1, 1, 201); CHECK(io_policy_set(p, 32, 2, 202) == 0);
            row(p, 0, 1, 1, 303); CHECK(io_policy_set(p, 32, 3, 203) == 0);
            row(p, 0, 1, 1, 202);
            CHECK((reset ? io_policy_reset(2, 202) : io_policy_set(p, 32, 2, 202)) == 0);
            io_policy_owner_exit(reverse ? 3 : 2, reverse ? 203 : 202);
            CHECK(io_policy_lookup(1, &tier, &budget) == 0);
            CHECK(budget == (reverse ? (reset ? 120u : 202u) : 303u));
            io_policy_owner_exit(reverse ? 2 : 3, reverse ? 202 : 203);
            CHECK(get(after) && equal(standard, after, 80));
        }
    }
    /* Unlink the oldest beneath two survivors; patch its immediate successor. */
    row(p, 0, 1, 1, 201); CHECK(io_policy_set(p, 32, 2, 302) == 0);
    row(p, 0, 1, 1, 303); CHECK(io_policy_set(p, 32, 3, 303) == 0);
    row(p, 0, 1, 1, 404); CHECK(io_policy_set(p, 32, 4, 304) == 0);
    io_policy_owner_exit(2, 302);
    CHECK(io_policy_lookup(1, &tier, &budget) == 0 && budget == 404);
    io_policy_owner_exit(4, 304);
    CHECK(io_policy_lookup(1, &tier, &budget) == 0 && budget == 303);
    io_policy_owner_exit(3, 303);
    CHECK(get(after) && equal(standard, after, 80));
    /* Reused ID bypasses its old lifetime without reviving its policy later. */
    row(p, 0, 1, 1, 201); CHECK(io_policy_set(p, 32, 2, 402) == 0);
    row(p, 0, 1, 1, 303); CHECK(io_policy_set(p, 32, 3, 403) == 0);
    row(p, 0, 1, 1, 202); CHECK(io_policy_set(p, 32, 2, 502) == 0);
    io_policy_owner_exit(2, 402);
    CHECK(io_policy_lookup(1, &tier, &budget) == 0 && budget == 202);
    io_policy_owner_exit(2, 502);
    CHECK(io_policy_lookup(1, &tier, &budget) == 0 && budget == 303);
    io_policy_owner_exit(3, 403);
    CHECK(get(after) && equal(standard, after, 80));
    /* Restore the permanent test baseline for the existing reset checks. */
    row(p, 0, 1, 1, 308); CHECK(io_policy_set(p, 32, 0, 0) == 0);
    /* First reset saves; repeated set/reset must preserve that source. */
    for (u32 incarnation = 0; incarnation < 3; ++incarnation) {
        gen = io_policy_generation();
        CHECK(io_policy_reset(1, incarnation) == 0);
        CHECK(io_policy_generation() == gen + 1);
        CHECK(io_policy_set(p, 32, 1, incarnation) == 0);
        CHECK(io_policy_reset(1, incarnation) == 0);
        CHECK(io_policy_generation() == gen + 3);
        gen = io_policy_generation();
        io_policy_owner_exit(1, incarnation ? incarnation - 1 : 10);
        CHECK(io_policy_generation() == gen);
        io_policy_owner_exit(1, incarnation);
        CHECK(io_policy_generation() == gen + 1 && get(after));
        CHECK(equal(before, after, 80));
    }
    header(p, 0); CHECK(io_policy_set(p, 16, 0, 0) == 0);
    CHECK(get(after) && equal(standard, after, 80));
    CHECK(!irq_bad && irq_state == 0x246 && saves == restores && saves > 0);
    irq_state = 0; CHECK(io_policy_reset(0, 0) == 0 && irq_state == 0);
    return 0;
}
void _start(void)
{
    int rc = POLICY_SPLICE_ONLY ? three_owner_splice() : exercise();
    if (!POLICY_SPLICE_ONLY && !rc) rc = three_owner_splice();
#if defined(__PPC__)
    register int code __asm__("r3") = rc;
    register int syscall __asm__("r0") = 1;
    __asm__ volatile("sc" : : "r"(code), "r"(syscall) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
#endif
    __builtin_unreachable();
}
