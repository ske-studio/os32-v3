/* Actual byte route and reserved-cell sequence, with fake ports/VRAM. */
#include "types.h"
#define IO_H
#define __GFX_INTERNAL_H
static u8 route;
static int writes, broken, safe=1, restored, redrawn;
static u16 fake_cell_word=0x1234;
static unsigned int inp(unsigned int port) { (void)port; return route; }
static void outp(unsigned int port, unsigned int value)
{ (void)port; writes++; if (!broken) route=(u8)value; }
#include "memmap.h"
#undef P2V_IO
#define P2V_IO(p) ((void *)(uptr)(&fake_cell_word))
int gfx_current_height=480, gfx_flip_enabled, gfx_display_page;
int geforce_pc98_safe(void) { return safe; }
void display_restore_98(void) { restored++; gfx_current_height=400; }
void console_hw_cursor_enable(void) { redrawn++; }
#include "../../drivers/video/pc98/pc98_route.c"
static int steps[8], n, fail_at;
static u32 test_cell;
static int access(void *opaque, u32 step, bool write, u32 *value)
{
    (void)opaque; steps[n++]=step;
    if (write) test_cell=*value; else *value=test_cell;
    return step==(u32)fail_at ? -1 : 0;
}
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int run(void)
{
    route=0xfc; CHECK(!pc98_route_to_98());
    CHECK(!writes && restored==1 && redrawn==1 && fake_cell_word==0x1234 && gfx_current_height==480);
    CHECK(!pc98_route_to_gf() && route==1 && writes==1);
    CHECK(!pc98_route_to_98() && route==0 && writes==2);
    safe=0; route=1; int before=restored;
    CHECK(pc98_route_to_98()==-1 && !route && restored==before && fake_cell_word==0x1234);
    CHECK(pc98_route_cell_check()==-1);
    safe=1; broken=1; route=0;
    CHECK(pc98_route_to_gf()==-1 && !route);
    for (fail_at=0; fail_at<=7; fail_at++) {
        n=0; test_cell=0xabc;
        int rc=pc98_cell_check(access, 0);
        CHECK((rc!=0)==(fail_at==2 || fail_at==3 || fail_at==4 || fail_at==5 || fail_at==7));
        if (fail_at!=2) { CHECK(test_cell==0xabc); CHECK(steps[n-1]==7 || fail_at==5); }
    }
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
