/* F0's resident dispatch boundary, GF_GUARD_THERM_DESIGN §5/A11.
 * The OS32 cooperative adapter replaces nouveau therm/temp.c:162-208's
 * worker scheduling; no hardware sequence or temperature policy is modeled.
 * Include the real module/accessor, using only F5 callbacks as test doubles.
 */
static void host_disable(void);
static void host_halt(void) __attribute__((noreturn));
#include "../../drivers/video/geforce/geforce_module.c"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { __asm__ volatile("" : : : "memory"); \
    if (!(x)) finish(1); } while (0)

static bool safe_route=true;
static u32 stops, notifications, aborts, syncs, serials;
static int clear_rc=GF5_EBUSY;
static int stop_heat(void)
{
    CHECK(foreground_poll_busy && heat_bridge.latched);
    stops++;
    geforce_foreground_poll();
    return -77; /* Failed stop still attempts recovery. */
}
int gf_production_return(void) { return stop_heat(); }
int pc98_route_safe(void) { return safe_route; }
int vfs_sync(void) { CHECK(stops==2 && !safe_route); syncs++; return -88; }
void serial_puts_polled(const char *s) { CHECK(syncs==1 && s); serials++; }
void geforce_display_failed(void) { CHECK(stops==1); notifications++; }
int appslot_abort_request(void) { CHECK(notifications==1); aborts++; return 0; }
void __cdecl kprintf(u8 a,const char *f,...) { (void)a;(void)f; }
static int clear_heat(void) { CHECK(foreground_poll_busy); return clear_rc; }
static void host_disable(void) { CHECK(syncs==1 && serials>5); }
static void host_halt(void)
{
    CHECK(stops==2 && syncs==1 && serials>5 && notifications==1 && aborts==1);
    CHECK(heat_reason==-33 && heat_milli_c==120000 && heat_source_rc==-44);
    finish(0);
}
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
    feature5_slot=0;
    heat_bridge.stop=stop_heat; heat_bridge.clear=clear_heat;
    geforce_heat_stop_request(-11,110000,-22);
    geforce_heat_stop_request(-99,999,-99);
    CHECK(heat_bridge.latched && heat_pending && heat_reason==-11);
    CHECK(geforce_heat_latch_clear()==GF5_ESTATE);
    geforce_foreground_poll();
    CHECK(stops==1 && notifications==1 && aborts==1 && !heat_pending);
    geforce_foreground_poll();
    CHECK(stops==1 && !syncs);
    CHECK(geforce_heat_latch_clear()==GF5_EBUSY && heat_bridge.latched);
    clear_rc=0;
    CHECK(!geforce_heat_latch_clear() && !heat_bridge.latched);
    safe_route=false;
    geforce_heat_stop_request(-33,120000,-44);
    geforce_foreground_poll();
    finish(1); /* The no-route path must halt. */
}
