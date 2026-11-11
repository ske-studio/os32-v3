/* CONSOLE_DUAL A1/A1g/A2/A3/A4: real sources, only external services faked. */
#include "types.h"
#include "console_dual.h"
#include "pc98_route.h"
#include "gfx/v2/gfx2_int.h"
#include "console_backend.h"
#include "utf8.h"
#include "kprintf.h"
#define IO_H
void *memset(void *p, int c, u32 n)
{ unsigned char *d=p; for (unsigned i=0; i<n; i++) d[i]=c; return p; }
void *memcpy(void *p, const void *q, u32 n)
{ unsigned char *d=p; const unsigned char *s=q; for (unsigned i=0; i<n; i++) d[i]=s[i]; return p; }
static u8 route;
static int reads, writes, waits, cells, cell_bad, gf_safe = 1;
static unsigned ports[64], values[64];
static unsigned int inp(unsigned int p) { (void)p; reads++; return route; }
static void outp(unsigned int p, unsigned int v)
{
    if (writes < 64) { ports[writes] = p; values[writes] = v; }
    writes++;
    if (p == PC98_ROUTE_PORT) route = v;
}
static void io_wait(void) { waits++; }
static int sink, v86, clears, prints, warnings, conn_calls, head_calls;
int con_sink_is_enabled(void) { return sink; }
void con_sink_enable(void) { sink = 1; }
void con_sink_disable(void) { sink = 0; }
void con_sink_push_clear(void) { if (sink) clears++; }
void con_sink_push_print(const char *s, u32 n, u8 c)
{ (void)s; (void)n; (void)c; if (sink) prints++; }
void con_sink_push_cursor(int x, int y) { (void)x; (void)y; }
#define LEGACY_FAKE_ACTIVE_ENABLE
#define LEGACY_FAKE_ACTIVE v86
#include "legacy_guest_fake.h"
int gfx_current_height = 768, gfx_flip_enabled = 1, gfx_display_page = 1;
u32 gfx_init_gen;
static int started;
static struct gfx2_adapter adapters[2], *active;
static GFX2_HeadInfo heads[2];
struct gfx2_adapter *gfx2_active(void) { return active; }
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i < 2 ? &adapters[i] : 0; }
int gfx_is_started(void) { return started; }
int gfx2_head_info(u32 a, u32 h, GFX2_HeadInfo *v)
{ (void)a; head_calls++; *v = heads[h]; return 0; }
int gfx2_connector_info(u32 a, u32 c, GFX2_ConnectorInfo *v)
{
    (void)a; conn_calls++; reads++; v->type = c + 1; v->status = CON_CONN_ON;
    return 0;
}
void kprintf(u8 c, const char *s, ...) { (void)c; (void)s; warnings++; }
/* route_set/to_gf are extracted verbatim; safety/cell are external boundaries. */
int geforce_pc98_safe(void) { return gf_safe; }
int pc98_route_safe(void) { reads++; return !route && gf_safe; }
int pc98_route_cell_check(void) { cells++; return cell_bad; }
static int console_safe = 1;
void pc98_console_set_safe(int v) { console_safe = v; }
#include "route.inc"
#include "console_dual.c"
#include "display_dual.c"

static u16 plane[80 * 25];
static int backend_clears, cursor_moves, cursor_shapes;
static const struct con_geometry geometry = {80, 25};
const struct con_geometry *con_backend_geometry(void) { return &geometry; }
int con_backend_safe(void) { return console_safe; }
u8 con_backend_color(unsigned r) { (void)r; return 7; }
void con_backend_clear(void)
{ backend_clears++; for (int i = 0; i < 2000; i++) plane[i] = ' '; }
void con_backend_putchar(int x, int y, char c, u8 a)
{ (void)a; plane[y * 80 + x] = c; }
void con_backend_scroll(int rows) { (void)rows; }
void con_backend_cursor_at(int x, int y) { (void)x; (void)y; cursor_moves++; }
void con_backend_cursor_shape(int v) { (void)v; cursor_shapes++; }
void bootlog_push(const char *s, u32 n) { (void)s; (void)n; }
int con_output_putchar(char c) { (void)c; return 0; }
void gfx_policy_reeval(void) {}
void kbd_inject_discard(void) {}
u32 kstrlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
#include "console.c"
static void tvram_print(int x, int y, const char *s, u8 attr)
{ while (*s) con_backend_putchar(x++, y, *s++, attr); }
/* Actual kernel.c loading branch, extracted with a unique anchor. */
static void loading(void)
{
#include "loading.inc"
}
static void loading_cui(void)
{
#include "loading_cui.inc"
}
#include "dpms.inc"
static int dpms_calls[2], dpms_state;
static int fake_dpms(void *ctx, u32 head, u32 state)
{ (void)ctx; dpms_calls[head]++; dpms_state=state; return 0; }
static const struct GfxBackendV2 fake_be = {.dpms=fake_dpms};

static void say(const char *s)
{
    u32 n = kstrlen(s);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, label) do { if (!(c)) { say("FAIL " label "\n"); return 1; } } while (0)
static void setup(void)
{
    active = &adapters[1];
    adapters[0] = (struct gfx2_adapter){.index=0, .kind=GFX2_KIND_PC98};
    adapters[1] = (struct gfx2_adapter){.index=1, .kind=GFX2_KIND_GEFORCE, .heads=2};
    heads[0] = (GFX2_HeadInfo){.attached=1, .connector_mask=2, .mode_id=11, .scanout_sid=3};
    heads[1] = (GFX2_HeadInfo){.connector_mask=1, .mode_id=12, .scanout_sid=4};
    sink = started = gf_safe = console_safe = 1;
    v86 = route = cell_bad = 0;
    have_key = 0; con_dual_session_end(); con_dual_configure(1);
}
static int a1(void)
{
    setup();
    /* Cartesian policy matrix; force a fresh key for each independent case. */
    for (int cfg0=0; cfg0<2; cfg0++) for (sink=0; sink<2; sink++)
    for (v86=0; v86<2; v86++) for (int kind=1; kind<=5; kind++)
    for (started=0; started<2; started++) for (gf_safe=0; gf_safe<2; gf_safe++)
    for (cell_bad=0; cell_bad<2; cell_bad++) {
        con_dual_configure(cfg0); active->kind=kind; have_key=0;
        reads=writes=waits=cells=conn_calls=0;
        int want=cfg0 && sink && !v86 && kind==GFX2_KIND_GEFORCE && started && gf_safe && !cell_bad;
        CHECK(con_dual_active()==want, "A1 matrix");
        CHECK(!conn_calls, "A1 no DDC");
        if (v86) CHECK(!reads && !writes && !cells, "A1 V86 IO");
        if (!cfg0 || !sink || !started) CHECK(!writes && !cells, "A1 early reject");
    }
    setup(); active=0;
    CHECK(!con_dual_active(), "A1 no adapter");
    active=&adapters[1]; CHECK(con_dual_active(), "A1 initial active");
    /* Same heads/gen/started: only the ACTIVE adapter identity changes. */
    adapters[0]=adapters[1]; adapters[0].index=0; adapters[0].kind=GFX2_KIND_PC98;
    active=&adapters[0]; CHECK(!con_dual_active(), "A1 ACTIVE switch");
    active=&adapters[1]; CHECK(con_dual_active(), "A1 ACTIVE resume");
    started=0; CHECK(!con_dual_active(), "A1 stop");
    started=1; CHECK(con_dual_active(), "A1 restart");
    u32 k=display_dual_key(); gfx_init_gen++;
    CHECK(display_dual_key()!=k, "A1 init gen");
    return 0;
}
static int a1g_a2(void)
{
    setup();
    for (int state=0; state<=2; state++) for (int mask=0; mask<4; mask++) {
        active->state=state;
        heads[0].attached=mask&1; heads[1].attached=!!(mask&2);
        have_key=0; CHECK(con_dual_active(), "A1g TEXT live");
        int n; const struct con_out *o=con_dual_outputs(&n);
        CHECK(n==3 && o[0].role==CON_OUT_TEXT && o[0].running, "A1g rows");
        for (int h=0; h<2; h++) {
            int desk=state==GFX2_AD_OK && heads[h].attached;
            CHECK(o[h+1].head==h && o[h+1].adapter==1, "A1g identity");
            CHECK(o[h+1].running==!!heads[h].attached && o[h+1].live==desk &&
                  o[h+1].role==(desk ? CON_OUT_DESK : CON_OUT_NONE), "A1g roles");
            CHECK(o[h+1].connector==1-h && o[h+1].conn_type==GFX2_CONN_UNKNOWN &&
                  o[h+1].conn_status==CON_CONN_UNKNOWN, "A1g connector");
        }
    }
    setup(); CHECK(con_dual_active(), "A1g baseline");
    /* Every individual key field must invalidate the cached table. */
    u32 *fields[]={&active->index,&active->gen,&active->state,&active->flags,
        &heads[0].attached,&heads[0].connector_mask,&heads[0].mode_id,&heads[0].scanout_sid,
        &heads[1].attached,&heads[1].connector_mask,&heads[1].mode_id,&heads[1].scanout_sid};
    for (unsigned i=0; i<sizeof(fields)/sizeof(fields[0]); i++) {
        u32 k=display_dual_key(); int before=cells;
        ++*fields[i]; CHECK(display_dual_key()!=k, "A1g key field");
        CHECK(con_dual_active() && cells==before+1, "A1g rebuild");
        --*fields[i]; CHECK(con_dual_active(), "A1g restore");
    }
    heads[0].connector_mask=12; heads[1].connector_mask=0;
    CHECK(con_dual_active(), "A1g remap");
    int n; const struct con_out *o=con_dual_outputs(&n);
    CHECK(o[1].connector==2 && o[2].connector==255, "A1g least bit");
    CHECK(!conn_calls, "A1g decision no DDC");
    struct con_out diag[CON_OUT_MAX];
    CHECK(display_dual_diag(diag, CON_OUT_MAX)==3 && conn_calls==1 &&
          diag[1].conn_type==3 && diag[1].conn_status==CON_CONN_ON, "A1g diagnostic DDC");
    CHECK(!display_dual_outputs(diag,0) && !display_dual_outputs(0,3), "A1g bounds");
    CHECK(display_dual_outputs(diag,1)==1, "A1g capacity");
    writes=waits=0; display_dual_text_on();
    CHECK(gfx_current_height==768 && gfx_flip_enabled==1 && gfx_display_page==1, "A2 geometry");
    CHECK(writes==3 && waits==3 && ports[0]==GDC_GFX_CMD && values[0]==GDC_CMD_STOP &&
          ports[1]==MODE_FF1_PORT && values[1]==MFF1_DISP_ON &&
          ports[2]==GDC_TEXT_CMD && values[2]==GDC_CMD_START, "A2 IO sequence");
    return 0;
}
static int a3_a4(void)
{
    setup(); CHECK(con_dual_active(), "A3 active");
    shell_print("AB",7); CHECK(plane[0]=='A' && plane[1]=='B', "A3 render gate");
    console_set_cursor(4,2); CHECK(cursor_x==4 && cursor_y==2, "A3 cursor gate");
    int shape=cursor_shapes; console_set_cursor(3,2);
    CHECK(cursor_shapes==shape, "A3 hidden cursor");
    int before=clears; tvram_clear(); CHECK(backend_clears==0 && clears==before+1 && plane[0]=='A', "A3 clear gate");
    shell_print("history\n",7); CHECK(plane[0]=='A', "A3 history");
    started=0; active=&adapters[0]; CHECK(!con_dual_active() && con_dual_was_live(), "A3 shutdown");
    int c=clears; console_session_clear();
    CHECK(clears==c+1 && backend_clears==0 && plane[0]=='A', "A3 session CLEAR");
    c=prints; loading();
    CHECK(prints==c+1 && plane[0]=='A', "A3 loading append");
    started=1; active=&adapters[1]; CHECK(con_dual_active(), "A3 GF resume");
    console_text_gdc_start();
    CHECK(!sink && !con_dual_was_live() && plane[0]=='A' && backend_clears==0, "A3 CUI keep");
    con_dual_configure(0); console_text_gdc_stop();
    int x=cursor_x,y=cursor_y; shell_print("X",7); console_set_cursor(9,9); tvram_clear();
    CHECK(cursor_x==x && cursor_y==y && plane[0]=='A' && backend_clears==0, "A3 ordinary GUI");
    console_text_gdc_start(); CHECK(backend_clears==1 && plane[0]==' ', "A3 ordinary CUI");
    setup(); sink=0; started=0; con_dual_session_end();
    cursor_x=0; cursor_y=0; con_backend_clear(); backend_clears=0;
    shell_print("boot\n",7); console_session_clear(); loading();
    started=1; active=&adapters[1]; shell_print("next",7);
    CHECK(plane[0]=='b' && plane[80]=='L' && plane[160]=='n' && !backend_clears, "A3 CUI GUI append");
    console_text_gdc_start(); loading_cui();
    CHECK(plane[0]=='b', "A3 CUI loading append");
    adapters[1].be=&fake_be;
    for (int cfg0=0; cfg0<2; cfg0++) for (int gf=0; gf<2; gf++) {
        con_dual_configure(cfg0); active=&adapters[1];
        active->kind=gf ? GFX2_KIND_GEFORCE : GFX2_KIND_PC98;
        dpms_calls[0]=dpms_calls[1]=0; gfx_dual_dpms(3);
        CHECK(dpms_calls[0]==(cfg0 && gf) && !dpms_calls[1] &&
              (!(cfg0 && gf) || dpms_state==3), "A3 DPMS");
    }
    for (int cfg0=0; cfg0<2; cfg0++) for (route=0; route<2; route++) {
        con_dual_configure(cfg0);
        for (gf_safe=0; gf_safe<2; gf_safe++) {
            u8 old=route; writes=0; console_safe=9;
            CHECK(!pc98_route_to_gf(), "A4 result");
            if (cfg0) CHECK(!writes && route==old && console_safe==(!old && gf_safe), "A4 dual route");
            else CHECK(route==1 && writes==!old, "A4 normal route");
            route=old;
        }
    }
    return 0;
}
static int warning_test(void)
{
    /* Run before any other unsupported target can consume the one-time warning. */
    CHECK(!con_dual_cfg(), "A1 default off");
    setup(); active->kind=GFX2_KIND_PEGC; started=0;
    CHECK(!con_dual_active() && !warnings, "A1 no early warning");
    started=1; CHECK(!con_dual_active() && warnings==1, "A1 warning once");
    gfx_init_gen++; CHECK(!con_dual_active() && warnings==1, "A1 no repeated warning");
    return 0;
}
void _start(void)
{
    int rc=warning_test() || a1() || a1g_a2() || a3_a4();
    if (!rc) say("PASS A1 A1g A2 A3 A4\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
