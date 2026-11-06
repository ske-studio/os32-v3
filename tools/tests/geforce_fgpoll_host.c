/* F0's resident dispatch boundary, GF_GUARD_THERM_DESIGN §5/A11.
 * The OS32 cooperative adapter replaces nouveau therm/temp.c:162-208's
 * worker scheduling; no hardware sequence or temperature policy is modeled.
 * Include the real module/accessor, using only F5 callbacks as test doubles.
 */
#include "../../drivers/video/geforce/geforce_module.c"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { __asm__ volatile("" : : : "memory"); \
    if (!(x)) finish(1); } while (0)

static struct gf5 first, second;
static u32 first_calls, second_calls, depth;
static bool recurse;
static i32 poll_first(struct gf5 *g)
{
    CHECK(g==&first);
    first_calls++;
    /* Bound even a broken guard, so its mutant fails by assertion. */
    if (recurse && !depth) {
        depth++;
        geforce_foreground_poll();
        depth--;
    }
    return GF5_ESTATE;
}
static i32 poll_second(struct gf5 *g)
{
    CHECK(g==&second);
    second_calls++;
    return 0;
}
static const struct gf5_test_ops first_ops={.ctx=&first,.service_poll=poll_first};
static const struct gf5_test_ops second_ops={.ctx=&second,.service_poll=poll_second};
static const struct gf5_test_ops absent_poll={.ctx=&first};

void _start(void)
{
    /* A populated slot is still unavailable before module registration. */
    feature5_slot=&first_ops;
    geforce_foreground_poll();
    CHECK(!first_calls);
    registered=1;
    feature5_slot=0;
    geforce_foreground_poll();
    feature5_slot=&absent_poll;
    geforce_foreground_poll();
    CHECK(!first_calls);
    feature5_slot=&first_ops;
    recurse=true;
    geforce_foreground_poll();
    CHECK(first_calls==1 && !depth);
    /* Failed poll must release the guard for the next cooperative point. */
    geforce_foreground_poll();
    CHECK(first_calls==2);
    feature5_slot=&second_ops;
    geforce_foreground_poll();
    CHECK(first_calls==2 && second_calls==1);
    feature5_slot=0;
    geforce_foreground_poll();
    CHECK(second_calls==1);
    feature5_slot=&first_ops;
    registered=0;
    geforce_foreground_poll();
    CHECK(first_calls==2);
    registered=1;
    geforce_foreground_poll();
    CHECK(first_calls==3);
    finish(0);
}
