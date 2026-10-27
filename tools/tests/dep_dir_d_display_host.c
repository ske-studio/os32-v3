/* Same I/O/MMIO seam for b400acb6 and resident platform display. */
#include "types.h"
#include "io.h"
#include "gfx_internal.h"
#include "v86_bios.h"
#include "display_restore.h"
#include "platform_display.h"
#include "display_private.h"
#include "pegc.h"
#include "con_sink.h"
#include "console.h"
#define TRACE_WORDS 300000
static u32 trace[TRACE_WORDS], count;
static u8 saved_bios[4096];
static u32 cell_value, sync_value;
static int gui;
int gfx_current_height, gfx_flip_enabled;
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void word(u32 v) { if (count == TRACE_WORDS) finish(2); trace[count++] = v; }
static void event(u32 kind, u32 a, u32 b)
{
    word(kind); word(a); word(b); word(gfx_current_height); word(gfx_flip_enabled);
}
static void trace_port(unsigned p, unsigned v) { event(1,p,v); }
static void port_word(unsigned p, unsigned v) { event(2,p,v); }
static void wait_port(void) { event(3,0,0); }
static unsigned input(unsigned p) { event(4,p,sync_value); return sync_value; }
static u32 load(u32 address, u32 width) { event(5,address,width); word(cell_value); return cell_value & (width == 2 ? 0xffffU : 0xffU); }
static void store(u32 address, u32 width, u32 value)
{
    event(6,address,width); word(value);
    u32 mask = width == 2 ? 0xffffU : 0xffU;
    cell_value = (cell_value & ~mask) | (value & mask);
    word(cell_value);
}
const u8 *v86_bios_real_lowmem(void) { event(7,0,0); return saved_bios; }
int con_sink_is_enabled(void) { event(8,gui,0); return gui; }
void palette_init(void) { event(9,0,0); }
void console_hw_cursor_enable(void) { event(10,0,0); }
#define outp(p,v) trace_port(p,v)
#define outpw(p,v) port_word(p,v)
#define io_wait() wait_port()
#define inp(p) input(p)
#include "display_source.inc"
static void result(int rc, u32 value) { event(11,(u32)rc,value); word(cell_value); }
void _start(void)
{
    for (u32 flags=0; flags<8; flags++) {
        event(12,flags,0);
        saved_bios[BIOS_GRAPH_FLAGS] = flags & 1 ? BIOS_GRAPH_EGC : 0;
        saved_bios[BIOS_CRT_FLAGS] = flags & 2 ? BIOS_CRT_GRCG : 0;
        saved_bios[PEGC_BIOS_ARCH_FLAG] = flags & 4 ? PEGC_BIOS_ARCH_EXTGFX : 0;
        gfx_current_height=480; gfx_flip_enabled=1;
        display_restore_98();
        gfx_current_height=200; gfx_flip_enabled=1;
        display_text_start();
    }
    static const int heights[]={200,400,480};
    for (gui=0;gui<2;gui++) for (u32 h=0;h<3;h++)
    for (int flip=0;flip<2;flip++) for (sync_value=0;sync_value<256;sync_value++) {
        gfx_current_height=heights[h]; gfx_flip_enabled=flip; cell_value=0xa5a51234;
        event(13,sync_value,gui); result(display_recover_ready(),0);
        u32 value=0xdeadbeef;
        int rc=display_recover_cell_read(&value); result(rc,value);
        rc=display_recover_cell_read(0); result(rc,0);
        static const u32 values[]={0,0xffff,0x10000,0xffffffff};
        for(u32 i=0;i<4;i++) { rc=display_recover_cell_write(values[i]); result(rc,values[i]); }
    }
    platform_display_kcg_code();
    u32 bytes=count*sizeof(trace[0]);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(trace), "d"(bytes) : "memory");
    finish(0);
}
