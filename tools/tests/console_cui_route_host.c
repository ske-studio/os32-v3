/* x86 CUI baseline: real console, dual policy, PC98 backend and display table.
 * Only MMIO/port access and external HAL/services are replaced. */
#include "types.h"
#include "memmap.h"
#include "pc98.h"
#include "tvram.h"
#include "tvram_private.h"
#include "console_dual.h"
#include "gfx/v2/gfx2_int.h"
#include "pc98_route.h"
#include "kstring.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void say(const char *s)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(kstrlen(s)) : "memory");
}
#define CHECK(c, label) do { if (!(c)) { say("FAIL " label "\n"); finish(1); } } while (0)
/* Count every translated access, and reject FB or any other MMIO address. */
static u16 text[TVRAM_COLS * TVRAM_ROWS], attrs[TVRAM_COLS * TVRAM_ROWS];
static unsigned text_access, attr_access, fb_access;
static void *mmio(u32 address)
{
    if (address >= TVRAM_BASE && address < TVRAM_BASE + sizeof(text)) {
        text_access++; return (u8 *)text + address - TVRAM_BASE;
    }
    if (address >= TVRAM_ATTR && address < TVRAM_ATTR + sizeof(attrs)) {
        attr_access++; return (u8 *)attrs + address - TVRAM_ATTR;
    }
    fb_access++;
    CHECK(0, "FB MMIO");
    return 0;
}
#undef P2V_IO
#define P2V_IO(a) mmio(a)
#define IO_H
static unsigned cursor_io, display_io, waits, ports[16], values[16];
static void outp(unsigned p, unsigned v)
{
    if ((p == GDC_TEXT_CMD && (v == GDC_CMD_CSRW || v == GDC_CMD_CSRFORM)) ||
        p == GDC_TEXT_PARAM) { cursor_io++; return; }
    if (display_io < 16) { ports[display_io] = p; values[display_io] = v; }
    display_io++;
}
static void io_wait(void) { waits++; }
static int sink, safe = 1, started;
static unsigned sink_prints, sink_clears, boot_bytes, enum_calls, route_calls, head_calls, ddc_calls;
int pc98_route_safe(void) { route_calls++; return safe; }
int pc98_route_cell_check(void) { route_calls++; return 0; }
int con_sink_is_enabled(void) { return sink; }
void con_sink_enable(void) { sink = 1; }
void con_sink_disable(void) { sink = 0; }
void con_sink_push_print(const char *s, u32 n, u8 c)
{ (void)s; (void)n; (void)c; if (sink) sink_prints++; }
void con_sink_push_clear(void) { if (sink) sink_clears++; }
void con_sink_push_cursor(int x, int y) { (void)x; (void)y; }
void bootlog_push(const char *s, u32 n) { (void)s; boot_bytes += n; }
int con_output_putchar(char c) { (void)c; return 0; }
void gfx_policy_reeval(void) {}
void kbd_inject_discard(void) {}
#define LEGACY_FAKE_ACTIVE_ENABLE
#include "legacy_guest_fake.h"
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
void *memset(void *p, int c, u32 n)
{ u8 *d = p; for (u32 i = 0; i < n; i++) d[i] = c; return p; }
void *memcpy(void *p, const void *q, u32 n)
{ u8 *d = p; const u8 *s = q; for (u32 i = 0; i < n; i++) d[i] = s[i]; return p; }
u16 unicode_file_lookup(u32 cp) { (void)cp; return 0; }
static struct gfx2_adapter adapters[2], *active;
u32 gfx_init_gen;
int gfx_is_started(void) { enum_calls++; return started; }
struct gfx2_adapter *gfx2_active(void) { enum_calls++; return active; }
struct gfx2_adapter *gfx2_adapter_get(u32 i)
{ enum_calls++; return i < 2 ? &adapters[i] : 0; }
int gfx2_head_info(u32 a, u32 h, GFX2_HeadInfo *v)
{ (void)a; (void)h; head_calls++; *v = (GFX2_HeadInfo){.attached=1, .connector_mask=1}; return 0; }
int gfx2_connector_info(u32 a, u32 c, GFX2_ConnectorInfo *v)
{ (void)a; (void)c; (void)v; ddc_calls++; return 0; }
#include "console_dual_source.inc"
#include "display_dual_source.inc"
#include "pc98_console_source.inc"
int pc98_console_safe(void) { return safe; }
#include "console_source.inc"

static void counters(void)
{
    text_access = attr_access = fb_access = cursor_io = display_io = waits = 0;
    enum_calls = route_calls = head_calls = ddc_calls = 0;
    sink_prints = sink_clears = boot_bytes = 0;
}
static void setup(int cfg, int gfx, int gf)
{
    sink = 0; safe = 1; started = gfx;
    adapters[0] = (struct gfx2_adapter){.index=0, .kind=GFX2_KIND_PC98};
    adapters[1] = (struct gfx2_adapter){.index=1, .kind=GFX2_KIND_GEFORCE, .heads=1};
    active = gfx ? &adapters[gf ? 1 : 0] : 0;
    con_dual_configure(cfg); con_dual_session_end();
    have_key = 0; n_out = 0; cursor_x = cursor_y = 0;
    for (unsigned i = 0; i < TVRAM_COLS * TVRAM_ROWS; i++) { text[i] = '#'; attrs[i] = 0; }
    counters();
}
static void no_display(void)
{
    CHECK(!fb_access && !enum_calls && !route_calls && !head_calls &&
          !ddc_calls && !display_io && !waits, "CUI no FB/enumeration IO");
}
static void cui(void)
{
    /* Independently label the four required conditions; then their full matrix.
     * A CUI getter stays empty even after output: it is not the route guard. */
    for (int cfg0 = 0; cfg0 < 2; cfg0++)
    for (int gfx = 0; gfx < 2; gfx++)
    for (int gf = 0; gf < 2; gf++) {
        setup(cfg0, gfx, gf);
        kprintf(KLOG_ATTR, "boot");
        CHECK(text[0]=='b' && text[1]=='o' && text[2]=='o' && text[3]=='t' && text[4]=='#', "CUI boot log");
        CHECK(attrs[0]==TATTR_WHITE && attrs[3]==TATTR_WHITE && attrs[4]==0, "CUI visible attrs");
        CHECK(text_access==4 && attr_access==4 && cursor_io==3 && boot_bytes==4, "CUI counts");
        int n = -1; con_dual_outputs(&n);
        CHECK(n==0 && !con_dual_active(), "CUI empty cached table");
        no_display();
        /* Starting CUI for the first time must preserve the boot log. */
        console_text_gdc_start();
        CHECK(text[0]=='b' && text_access==4 && cursor_io==10, "CUI first start keeps log");
        no_display();
    }
    say("PASS pre-gfx / DUAL=0 / no-GF / CUI TVRAM only\n");
}
static void gui(void)
{
    /* DUAL=0 or GF missing: GUI freezes TVRAM and logical cursor. */
    for (int gf = 0; gf < 2; gf++) {
        setup(gf ? 0 : 1, 1, gf);
        console_text_gdc_stop(); counters();
        shell_putchar('X', TATTR_WHITE); shell_print("Y", TATTR_WHITE);
        console_set_cursor(7, 3); tvram_clear();
        CHECK(text[0]=='#' && !text_access && !attr_access && !cursor_io &&
              !cursor_x && !cursor_y && sink_prints>=2 && sink_clears==1, "GUI frozen");
        CHECK(!fb_access && !ddc_calls && !display_io, "GUI no drawing IO");
        counters(); console_text_gdc_start();
        CHECK(text[0]==' ' && text[1999]==' ' && text_access==1 && attr_access==2000 &&
              !cursor_x && !cursor_y && cursor_io==10, "GUI ordinary return clear");
    }
    setup(1, 1, 1);
    console_text_gdc_stop(); counters();
    shell_putchar('A', TATTR_WHITE);
    CHECK(text[0]=='A' && attrs[0]==TATTR_WHITE && text_access==1 && attr_access==1 &&
          cursor_x==1 && cursor_io==3 && sink_prints==1, "GF GUI TEXT render");
    CHECK(display_io==3 && waits==3 && route_calls==2 && head_calls==2 && !ddc_calls && !fb_access,
          "GF GUI enumeration counts");
    CHECK(ports[0]==GDC_GFX_CMD && values[0]==GDC_CMD_STOP &&
          ports[1]==MODE_FF1_PORT && values[1]==MFF1_DISP_ON &&
          ports[2]==GDC_TEXT_CMD && values[2]==GDC_CMD_START, "GF GUI display sequence");
    console_set_cursor(4, 2);
    CHECK(cursor_x==4 && cursor_y==2 && cursor_io==6, "GF GUI cursor gate");
    counters(); tvram_clear(); console_session_clear();
    CHECK(sink_clears==2 && text[0]=='A' && !text_access && !attr_access &&
          cursor_x==4 && cursor_y==2 && !cursor_io, "GF GUI clear gate");
    no_display();
    counters(); console_text_gdc_start();
    CHECK(text[0]=='A' && !text_access && !attr_access && cursor_io==7 && !sink &&
          !con_dual_was_live(), "GF GUI return preserves TEXT");
    no_display();
    /* Decode guard remains authoritative even with a live cached TEXT row. */
    safe = 0; counters(); shell_putchar('Z', TATTR_WHITE);
    CHECK(text[0]=='A' && !text_access && !attr_access && !cursor_io, "unsafe route");
    no_display();
    say("PASS GF GUI TEXT / GUI clear and cursor gates\n");
}
void _start(void) { cui(); gui(); finish(0); }
