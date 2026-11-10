/* B3: real PC-98 planar bring-up provider; replace only port I/O and the
 * three memory windows it touches (text VRAM, attribute VRAM, graphics planes). */
#include "types.h"
#include "io.h"
#include "memmap.h"
#include "pc98.h"
#include "kstring.h"
#include "gfx_internal.h"
#include "display_restore.h"
enum { EV_OUT, EV_CLEAR };
static struct { unsigned kind, a, b; } trace[64];
static unsigned count;
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void expect(int ok) { if (!ok) finish(1); }
static void event(unsigned kind, unsigned a, unsigned b)
{
    expect(count < 64);
    trace[count].kind = kind; trace[count].a = a; trace[count++].b = b;
}
/* One cell of slack on each side catches off-by-one walks. */
#define CELLS (80 * 25)
static u16 text_chars[CELLS + 2];
static u8 text_attrs[(CELLS + 2) * 2];
static volatile void *host_io(u32 pa)
{
    if (pa == 0xA0000UL) return &text_chars[1];
    if (pa == 0xA2000UL) return &text_attrs[2];
    finish(2);
}
static void *host_plane(u32 pa) { return (void *)pa; }  /* identity token only */
static void *host_clear(void *dst, int value, u32 n)
{
    expect(value == 0);
    event(EV_CLEAR, (unsigned)dst, n);
    return dst;
}
#undef outp
#define outp(p, v) event(EV_OUT, (p), (v))
#define P2V_IO(pa) host_io(pa)
#define P2V(pa) host_plane(pa)
#define kmemset(d, v, n) host_clear((d), (v), (n))
#include "gfx_init_source.inc"

/* The common callers, extracted from the product text (gfx/gfx_core.c and
 * gfx/gfx_core_init.inc) by the test driver: everything they call that is not
 * the provider is a recording fake, so the *executed* order is observed. */
#include "gfx_hal.h"
#include "palette.h"
#include "splash_raster.h"
enum { EV_CALL = 2 };
enum { F_SPLASH = 1, F_SELECT, F_REINIT, F_ENTER, F_DIAG, F_BIND, F_PALETTE, F_SCROLL };
u8 *bb_b, *bb_r, *bb_g, *bb_i, *bb[GVRAM_PLANES];
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
DirtyRectQueue dirty_queue, prev_dirty;
u32 gfx_init_gen;
static int gfx_started;
static u8 planes_b[4], planes_ok;
static void backend_enter(void) { event(EV_CALL, F_ENTER, 0); }
static GfxMode host_mode = { .height = 400 };
static GfxBackend host_backend = { .enter = backend_enter, .selected_mode = &host_mode };
GfxBackend *g_backend = &host_backend;
void pc98_splash_raster_reset(void) { event(EV_CALL, F_SPLASH, 0); }
static int gfx_select_and_init_backend(int legacy200) { event(EV_CALL, F_SELECT, legacy200); return 0; }
static void gfx_reinit_surfaces(int finish_arg) { event(EV_CALL, F_REINIT, finish_arg); }
static void gfx_diag_init(void) { event(EV_CALL, F_DIAG, 0); }
static void gfx_bind_client(void)
{
    event(EV_CALL, F_BIND, 0);
    if (planes_ok) { bb_b = &planes_b[0]; bb_r = &planes_b[1]; bb_g = &planes_b[2]; bb_i = &planes_b[3]; bb[0] = bb_b; }
}
void palette_init(void) { event(EV_CALL, F_PALETTE, 0); }
void gfx_scroll_init(void) { event(EV_CALL, F_SCROLL, 0); }
#include "gfx_core_slice.inc"

static void is(unsigned i, unsigned kind, unsigned a, unsigned b)
{
    expect(i < count && trace[i].kind == kind && trace[i].a == a && trace[i].b == b);
}
/* Literal pre-separation wire values (UNDOCUMENTED io_disp.md), not product macros. */
static void start_case(int legacy200)
{
    count = 0;
    display_planar_start(legacy200);
    expect(count == 4);
    is(0, EV_OUT, 0xA2, 0x4B);                      /* CSRFORM */
    is(1, EV_OUT, 0xA0, legacy200 ? 0x01 : 0x00);   /* L/R: 200 lines double-scan */
    is(2, EV_OUT, 0x68, legacy200 ? 0x09 : 0x08);   /* mode F/F1 */
    is(3, EV_OUT, 0xA2, 0x0D);                      /* START last */
}
static void flip_case(int display, int access)
{
    count = 0;
    display_flip_pages(display, access);
    expect(count == 2);
    is(0, EV_OUT, 0xA4, display ? 1 : 0);           /* display page first */
    is(1, EV_OUT, 0xA6, access ? 1 : 0);
}
/* Executed order of the common bring-up for one mode. */
static void init_case(int legacy200)
{
    unsigned size = legacy200 ? 16000 : 32000, i = 0;
    static const unsigned vram[4] = {0xA8000, 0xB0000, 0xB8000, 0xE0000};
    bb_b = bb_r = bb_g = bb_i = bb[0] = 0; planes_ok = 1; gfx_started = 0;
    for (unsigned c = 0; c < CELLS; c++) { text_chars[1 + c] = 0x1234; text_attrs[2 + c * 2] = 0xE1; }
    count = 0;
    gfx_init_mode(legacy200);
    is(i++, EV_CALL, F_SPLASH, 0); is(i++, EV_CALL, F_SELECT, legacy200);
    is(i++, EV_CALL, F_BIND, 0);
    /* _gfx_common_init: text hidden (memory), then the four backbuffer planes, then FF2. */
    expect(text_chars[1] == 0 && text_attrs[2] == 0);
    for (unsigned p = 0; p < 4; p++) is(i++, EV_CLEAR, (unsigned)&planes_b[p], size);
    is(i++, EV_OUT, 0x6A, 0x01);
    is(i++, EV_OUT, 0xA2, 0x4B); is(i++, EV_OUT, 0xA0, legacy200 ? 1 : 0);
    is(i++, EV_OUT, 0x68, legacy200 ? 0x09 : 0x08); is(i++, EV_OUT, 0xA2, 0x0D);
    for (unsigned page = 0; page < 2; page++) {
        is(i++, EV_OUT, 0xA6, page);
        for (unsigned p = 0; p < 4; p++) is(i++, EV_CLEAR, vram[p], size);
    }
    is(i++, EV_OUT, 0xA4, 0); is(i++, EV_OUT, 0xA6, 1);
    is(i++, EV_CALL, F_PALETTE, 0); is(i++, EV_CALL, F_SCROLL, 0);
    is(i++, EV_CALL, F_REINIT, 1); is(i++, EV_CALL, F_ENTER, 0); is(i++, EV_CALL, F_DIAG, 0);
    expect(count == i && gfx_started == 1 && gfx_flip_enabled == 1);
}
/* A missing plane must leave the text and the mode untouched. */
static void no_plane_case(void)
{
    bb_b = bb_r = bb_g = bb_i = bb[0] = 0; planes_ok = 0;
    text_chars[1] = 0x1234; text_attrs[2] = 0xE1;
    count = 0;
    gfx_init_mode(0);
    for (unsigned i = 0; i < count; i++) expect(trace[i].kind == EV_CALL);
    expect(text_chars[1] == 0x1234 && text_attrs[2] == 0xE1);
    bb_b = &planes_b[0]; bb_r = &planes_b[1]; bb_g = &planes_b[2]; bb_i = 0;
    count = 0;
    _gfx_common_init(32000);
    expect(count == 0 && text_chars[1] == 0x1234 && text_attrs[2] == 0xE1);
}
void _start(void)
{
    init_case(0); init_case(1); no_plane_case();
    start_case(0); start_case(1);
    for (int d = 0; d < 2; d++) for (int a = 0; a < 2; a++) flip_case(d, a);

    count = 0;
    display_planar16();
    expect(count == 1); is(0, EV_OUT, 0x6A, 0x01);

    for (unsigned i = 0; i < CELLS + 2; i++) text_chars[i] = 0x1234;
    for (unsigned i = 0; i < (CELLS + 2) * 2; i++) text_attrs[i] = 0xE1;
    count = 0;
    display_text_hide();
    expect(count == 0);                             /* memory only, no port */
    expect(text_chars[0] == 0x1234 && text_chars[CELLS + 1] == 0x1234);
    expect(text_attrs[0] == 0xE1 && text_attrs[1] == 0xE1 &&
           text_attrs[(CELLS + 1) * 2] == 0xE1);
    for (unsigned i = 0; i < CELLS; i++) {
        expect(text_chars[1 + i] == 0);
        expect(text_attrs[2 + i * 2] == 0);         /* attribute byte */
        expect(text_attrs[3 + i * 2] == 0xE1);      /* odd byte untouched */
    }

    for (unsigned size = 16000; size <= 32000; size += 16000) {
        static const unsigned planes[4] = {0xA8000, 0xB0000, 0xB8000, 0xE0000};
        count = 0;
        gfx_clear_planar_pages(size);
        expect(count == 10);
        for (unsigned page = 0; page < 2; page++) {
            is(page * 5, EV_OUT, 0xA6, page);       /* CPU access page, 0 then 1 */
            for (unsigned p = 0; p < 4; p++)
                is(page * 5 + 1 + p, EV_CLEAR, planes[p], size);
        }
    }
    finish(0);
}
