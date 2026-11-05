/* libc-free fixture state only; per-UNIT product wiring belongs to its stage. */
#include "hostsvc_fixture.h"

static u32 now_tick;
static int interrupt_enabled;
static void (*copy_hook)(void);
static struct { const char *point; int pending, rc; } failures[] = {
    {"nic.tx", 0, 0}, {"nic.rx", 0, 0}, {"heap", 0, 0},
    {"copyout", 0, 0}, {"sfs.reply", 0, 0}
};
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
void hs_reset(u32 start_tick)
{
    now_tick = start_tick;
    interrupt_enabled = 1;
    copy_hook = 0;
    for (u32 i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
        failures[i].pending = 0;
}
void hs_advance(u32 ticks) { now_tick += ticks; }
void hs_set_if(int enabled) { interrupt_enabled = !!enabled; }
u32 hs_now(void) { return now_tick; }
int hs_if(void) { return interrupt_enabled; }
void hs_fail(const char *point, int rc)
{
    for (u32 i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
        if (same(point, failures[i].point)) {
            failures[i].pending = 1;
            failures[i].rc = rc;
            return;
        }
    __builtin_trap(); /* invalid test point must not silently succeed */
}
int hs_failure_take(const char *point, int *rc)
{
    for (u32 i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
        if (same(point, failures[i].point) && failures[i].pending) {
            failures[i].pending = 0;
            *rc = failures[i].rc;
            return 1;
        }
    return 0;
}
void hs_copyout_hook(void (*hook)(void)) { copy_hook = hook; }
void hs_copyout_run_hook(void)
{
    void (*hook)(void) = copy_hook;
    copy_hook = 0;
    if (hook) hook();
}
