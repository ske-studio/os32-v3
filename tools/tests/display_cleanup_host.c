/* Real V86 exit functions, extracted without rewriting their bodies. */
#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#undef __cdecl
#define __cdecl
#include "module.h"
#include "pc98.h"
#include "pegc.h"
static u8 boot_bios[4096];
static const u8 *v86_bios_real_lowmem(void) { return boot_bios; }
#include "gfx.h"
int gfx_current_height = 200, gfx_flip_enabled = 1;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)
static unsigned int ports[64], values[64], n, waits;
static int palette, cursor, deny, sound, active, restored;
static void outp(unsigned int p, unsigned int v)
{
    CHECK(n < 64, "bounded OUT trace");
    ports[n] = p; values[n++] = v;
}
static void outpw(unsigned p, unsigned v) { outp(p, v); }
static void io_wait(void) { waits++; }
static void palette_init(void) { palette++; }
static void console_hw_cursor_enable(void) { cursor++; }
static void tss_iomap_deny_all(void) { deny++; }
static int v86_gcap_active(void) { return active; }
struct kdebug kdebug = { .ops = { .gcap_active = v86_gcap_active } };
static void snd_state_for_os32(void) { sound++; }
static void tv_restore(const u16 *tv) { CHECK(*tv == 123, "saved TVRAM"); restored++; }
#include "display_restore.h"
static u16 reserved_cell;
static int gui;
static u32 sync_read=0x81;
static unsigned int inp(unsigned int port) { CHECK(port==PEGC_HSYNC_PORT,"frequency read only"); return sync_read; }
static int con_sink_is_enabled(void) { return gui; }
#undef P2V_IO
#define P2V_IO(addr) ((void *)((addr) == DISPLAY_RECOVER_CELL ? &reserved_cell : (u16 *)0))
#include "display_cleanup_slice.inc"
int main(int argc, char **argv)
{
    static const unsigned int want_ports[] = {
        0xA2, 0x6A, 0x68, 0xA2, 0xA0, 0xA0, 0xA0, 0xA2,
        0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
        0xA2, 0x68, 0x62, 0xA4, 0xA6
    };
    static const unsigned int want_values[] = {
        0x0C, 0x01, 0x08, 0x4B, 0, 0, 0, 0x70,
        0, 0, 0, 0x19, 0, 0, 0, 0, 0x0C, 0x0F, 0x0D, 0, 0
    };
    u32 cell;
    CHECK(DISPLAY_RECOVER_CELL >= TVRAM_CHAR_BASE + TVRAM_ROWS_30 * TVRAM_BPR,
          "probe cell outside console grids");
    reserved_cell = 0x1234;
    gfx_current_height=GFX_HEIGHT; gfx_flip_enabled=0;
    CHECK(!display_recover_ready() && !display_recover_cell_read(&cell) && cell == 0x1234,
          "reserved cell read");
    CHECK(!display_recover_cell_write(cell ^ 0xffff) && reserved_cell == 0xedcb,
          "reserved cell test write");
    CHECK(!display_recover_cell_write(cell) && reserved_cell == 0x1234,
          "reserved cell restore");
    gui = 1;
    CHECK(display_recover_ready() && display_recover_cell_read(&cell) &&
          display_recover_cell_write(1) && reserved_cell == 0x1234, "GUI refuses before write");
    gui = 0; sync_read=0xff;
    CHECK(display_recover_ready() && display_recover_cell_write(1) && reserved_cell==0x1234,
          "unknown frequency refuses before write");
    sync_read=0x81; gfx_current_height=200; gfx_flip_enabled=1;
    CHECK(display_recover_ready(),"unknown CUI refuses before write");
    u16 tv = 123;
    CHECK(argc == 2, "case argument");
    int mode = atoi(argv[1]);
    if (mode == 3 || mode == 4) {
        boot_bios[BIOS_CRT_FLAGS] = BIOS_CRT_GRCG;
        boot_bios[BIOS_GRAPH_FLAGS] = BIOS_GRAPH_EGC;
        if (mode == 4) boot_bios[PEGC_BIOS_ARCH_FLAG] = PEGC_BIOS_ARCH_EXTGFX;
    }
    if (mode == 0 || mode == 3 || mode == 4) {
        v86_io_reset_policy();
        for (unsigned int i = 0; i < n; i++) {
            CHECK(ports[i] != GDC_GFX_CMD || values[i] != GDC_CMD_START,
                  "graphics must stay stopped");
            CHECK(ports[i] != GDC_ACCESS_PAGE || values[i] == GDC_PAGE_0,
                  "access page must be zero");
            CHECK(ports[i] != GDC_DISP_PAGE || values[i] == GDC_PAGE_0,
                  "display page must be zero");
        }
        int display_enabled = 0, text_started = 0;
        for (unsigned int i = 0; i < n; i++) {
            if (ports[i] == MODE_FF1_PORT && values[i] == MFF1_DISP_ON) display_enabled = 1;
            if (ports[i] == GDC_TEXT_CMD && values[i] == GDC_CMD_START) text_started = 1;
        }
        CHECK(display_enabled, "display output must be enabled");
        CHECK(text_started, "text must be started");
        /* Independent expected values for EGC-capable 9801 and 9821.
         * Old 9801 gets no EGC/9821-only writes; captured exits stay separate. */
        unsigned extra = mode == 4 ? 9 : mode == 3 ? 5 : 0;
        static const unsigned ep[] = {0x4a0,0x6a,0x6a,0x6a,0x7c,0x6a,0x6a,0x6a,0x6a};
        static const unsigned ev[] = {0xfff0,7,4,6,0,7,0x20,0x68,6};
        for (unsigned i = 0; i < extra; i++)
            CHECK(ports[i+1] == ep[i] && values[i+1] == ev[i], "standard/extended restore");
        CHECK(n - extra == sizeof(want_ports) / sizeof(*want_ports), "normal OUT count");
        for (unsigned int i = 0; i < n - extra; i++) {
            unsigned j = i ? i + extra : 0;
            CHECK(ports[j] == want_ports[i] && values[j] == want_values[i], "CUI OUT order/value");
        }
        CHECK(palette == 1 && cursor == 1 && deny == 1 && sound == 1, "normal restoration retained");
    } else if (mode == 1) {
        active = 1;
        v86_io_reset_policy();
        CHECK(!n && !palette && !cursor && !sound && deny == 1, "capture bypass retained");
    } else {
        gcap_cui_rebuild(&tv);
        CHECK(n == 21 && restored == 1 && cursor >= 1, "capture rebuild");
        for (unsigned int i = 0; i < n; i++)
            CHECK(ports[i] == want_ports[i] && values[i] == want_values[i], "shared CUI order");
    }
    if (mode != 1) CHECK(gfx_current_height == GFX_HEIGHT && !gfx_flip_enabled, "CUI height flip");
    CHECK(waits == n, "I/O wait for every OUT");
    puts("PASS: V86 CUI exit");
    return 0;
}
