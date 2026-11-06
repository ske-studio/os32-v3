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

volatile u32 tick_count;
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

/* Absolute deadlines remain in F5, including above the low-32 clock wrap. */
static u64 clock_now;
static u32 clock_calls;
static i32 clock_rc;
static i32 clock64(void *owner,u64 *out)
{
    CHECK(owner==&second);
    clock_calls++;
    if (clock_rc) return clock_rc;
    *out=clock_now;
    return 0;
}
static void deadline_cases(void)
{
    CHECK(geforce_fg_due(0)); /* First syscall must reach foreground work. */
    registered=1;
    feature5_slot=&second_ops;
    second.links.owner=&second;
    second.links.clock64=clock64;
    clock_now=0x100000000ULL;
    tick_count=0xfffffffeU;
    second.next_sensor=clock_now+1000000;
    second.next_auto=clock_now+500000;
    second.next_step=clock_now+200000;
    second.next_toggle=clock_now+25000;
    geforce_foreground_poll();
    CHECK(second_calls==1 && clock_calls==1);
    CHECK(!geforce_fg_due(0xfffffffeU) && !geforce_fg_due(0xffffffffU));
    CHECK(geforce_fg_due(0) && geforce_fg_due(1));
    CHECK(clock_calls==1); /* The query cannot call the module or its clock. */
    /* Fan steps faster than a sensor second must not be throttled to 1Hz. */
    second.next_toggle=0;
    second.next_step=clock_now+5000;
    geforce_foreground_poll();
    CHECK(geforce_fg_due(tick_count));
    second.next_step=clock_now; /* Overdue timers remain immediately due. */
    geforce_foreground_poll();
    CHECK(geforce_fg_due(tick_count));
    second.next_step=0;
    second.next_auto=0;
    second.fan.sensor_poll=true;
    second.next_sensor=0;
    geforce_foreground_poll();
    CHECK(geforce_fg_due(tick_count));
    second.next_sensor=clock_now+1000000;
    geforce_foreground_poll();
    CHECK(!geforce_fg_due(tick_count) && geforce_fg_due(tick_count+100));
    /* A missing slot (save/restore) or failed clock defers to the next point. */
    feature5_slot=0;
    geforce_foreground_poll();
    CHECK(geforce_fg_due(tick_count));
    feature5_slot=&second_ops;
    clock_rc=GF5_EIO;
    geforce_foreground_poll();
    CHECK(geforce_fg_due(tick_count));
    clock_rc=0;
    second_calls=0;
    registered=0;
    foreground_deadline_valid=false;
}

void _start(void)
{
    deadline_cases();
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
    foreground_deadline_valid=true; foreground_next_tick=tick_count+100;
    CHECK(geforce_fg_due(tick_count)); /* pending overrides a future deadline */
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
