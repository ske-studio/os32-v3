/* DESIGN.final §3/§4. A load may yield, including rebind of the same CLIENT.
 * The private decoded surface is borrowed only during the synchronous hook. */
#include "gfx/gfx_osri.h"
#include "gfx/gfx_osri_view.h"
#include "gfx_hal.h"
#include "drivers/video/of_fb/backend_of_fb.h"
#include "exec/appslot.h"
#include "vfs.h"
#include "kbd.h"
#include "wire_data.h"

static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)

static struct OsriImage store;
static u8 pixels[OSRI_PIXELS_BYTES], shadow[648 * 402];
static int allocations, fail_alloc, live, gate, started = 1, rebind, shown;
static int gui, key, raw, stop;
static AppSlot slot;
static u32 offset, size;
static const u8 *wire;
static unsigned long long generation = 1;
volatile u32 tick_count;
int gfx_is_started(void) { return started; }
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return 3; }
int appslot_stop_pending(void) { return stop; }
AppSlot *appslot_get(int caller) { CHECK(caller == 3); return &slot; }
int serial_gate_active(void) { return gate; }
unsigned long long of_fb_osri_generation(void) { return generation; }
int input_trygetkey(void) { CHECK(!gui); return key; }
int input_trygetrawkey(void) { CHECK(gui); return raw; }
int pm_terminal_trygetkey(void) { return key; }
void pm_terminal_poll(void) { }
void *kmalloc(u32 n)
{
    if (++allocations == fail_alloc) return 0;
    ++live;
    CHECK(n == sizeof(store) || n == sizeof(pixels));
    return n == sizeof(store) ? (void *)&store : (void *)pixels;
}
void kfree(void *p) { CHECK(live > 0 && (p == &store || p == pixels)); --live; }
int gfx_kernel_framebuffer(struct gfx_kernel_fb *fb)
{
    *fb = (struct gfx_kernel_fb){640, 402, 648, GFX_BB_PACKED8, {shadow}};
    return 0;
}
int vfs_open(const char *path, int flags)
{
    CHECK(path && flags == O_RDONLY);
    offset = 0;
    CHECK(gfx_osri_view(path, 50, 3) == OS32_ERR_BUSY);
    return 1;
}
int vfs_read_fd(int fd, void *out, u32 n)
{
    unsigned long long captured;
    CHECK(fd == 1 && !pm_osri_decoded(&captured));
    if (rebind && !offset) ++generation;
    if (n > size - offset) n = size - offset;
    for (u32 i = 0; i < n; ++i) ((u8 *)out)[i] = wire[offset++];
    return n;
}
void vfs_close(int fd) { CHECK(fd == 1); }
static int show(const struct OsriImage *image, struct OsriViewRun *run)
{
    unsigned long long captured;
    const struct gfx_kernel_fb *f = pm_osri_decoded(&captured);
    CHECK(captured == generation && f && f->format == GFX_BB_PLANAR4);
    CHECK(run->caller == 3 && image == &store);
    for (u32 i = 0; i < 4; ++i)
        CHECK(f->planes[i] == pixels + i * GFX_PLANE_SZ &&
              *f->planes[i] == wire[OSRI_PIXELS_OFF + i * GFX_PLANE_SZ]);
    ++shown;
    return GFX_OSRI_SHOW_DONE;
}
static GfxBackend backend = {.osri_show = show};
GfxBackend *g_backend = &backend;
static int call(void)
{
    unsigned long long captured;
    allocations = 0;
    int rc = gfx_osri_view("/host/img", 50, 3);
    CHECK(!live && !pm_osri_decoded(&captured) && shadow[0] == 73);
    return rc;
}
void _start(void)
{
    shadow[0] = 73;
    wire = wire_budget;
    size = sizeof(wire_budget);
    gate = 1;
    g_backend = 0;
    CHECK(call() == OS32_ERR_BUSY);
    gate = 0; started = 0;
    CHECK(call() == OS32_ERR_BUSY);
    started = 1;
    CHECK(call() == OS32_ERR_NOSYS);
    g_backend = &backend;
    for (fail_alloc = 1; fail_alloc <= 2; ++fail_alloc) CHECK(call() == OS32_ERR_NOSPC);
    fail_alloc = 0;
    rebind = 1;
    CHECK(call() == OS32_ERR_IO && !shown);
    rebind = 0;
    CHECK(call() == GFX_OSRI_SHOW_DONE && shown == 1);
    --size;
    CHECK(call() == OS32_ERR_IO && shown == 1);
    wire = wire_bad_crc; size = sizeof(wire_bad_crc);
    CHECK(call() == OS32_ERR_INVAL);
    wire = wire_events; size = sizeof(wire_events);
    CHECK(call() == GFX_OSRI_SHOW_DONE && shown == 2 && !live);
    wire = wire_trailing; size = sizeof(wire_trailing);
    CHECK(call() == OS32_ERR_INVAL && shown == 2);
    wire = wire_within; size = sizeof(wire_within);
    CHECK(call() == GFX_OSRI_SHOW_DONE && shown == 3);
    slot.abort_req = 1;
    CHECK(gfx_osri_poll_abort(3));
    slot.abort_req = 0; stop = 1;
    CHECK(gfx_osri_poll_abort(3));
    stop = 0;
    CHECK(!gfx_osri_poll_abort(3));
    key = 27; CHECK(pm_osri_key() == 0);
    key = 'x'; CHECK(pm_osri_key() == 1);
#if KBOOT_SH
    gui = 1; raw = KEY_ESC;
    CHECK(pm_osri_key() == 2);
    raw = KEY_ESC | KBD_RAW_MAKE; CHECK(pm_osri_key() == 0);
    raw = KEY_SPACE | KBD_RAW_MAKE; CHECK(pm_osri_key() == 1);
#endif
    finish(0);
}
