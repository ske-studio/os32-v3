#include "gfx_dirty.h"
#include "gfx_hal.h"

static u8 shadow[128], screen[128];
u8 *bb_b = shadow;
u32 gfx_init_gen;
int gfx_current_height = 1;
static unsigned frames;
static i32 observe(u32 op, void *buf, u32 size)
{
    if (op == GFX_TRACE_EVENT && ((struct gfx_trace_event *)buf)->kind == GFX_TRACE_FRAME)
        frames++;
    return GFX_TRACE_NORMAL;
}
struct kdebug kdebug = {.ops.dbg_call = observe};
GfxCounters gfx_counters;
DirtyRectQueue dirty_queue, prev_dirty;
static int attempts, fail_on = 2;
static void present(int x, int y, int w, int h)
{
    if (++attempts == fail_on) return;
    for (int i = x; i < x + w; i++) screen[i] = shadow[i];
    gfx_counters.commits++;
    gfx_counters.present_bytes += w;
}
static GfxBackend backend = {.bb_format = GFX_BB_PACKED8, .present_rect = present};
GfxBackend *g_backend = &backend;
int appslot_gui_present_ok(void) { return 1; }
int v1_bridge_begin(u32 r, u32 a) { return 0; }
void v1_bridge_end(u32 r, u32 a, u32 f) {}
void gfx_diag_first_present(u32 n) {}
void gfx_present_diag(void) {}
void gfx_planar_present_dirty(int sync) {}
void _start(void)
{
    for (int i = 0; i < 32; i++) { shadow[i] = 1; shadow[i + 96] = 2; }
    dirty_queue.count = 2;
    dirty_queue.rects[0] = (GFX_Rect){0,0,32,1};
    dirty_queue.rects[1] = (GFX_Rect){96,0,32,1};
    gfx_counters.commits = U32_MAX;
    gfx_present_dirty();
    int rc = dirty_queue.count != 1 || dirty_queue.rects[0].x != 96 ||
             gfx_counters.commits != U32_MAX || gfx_counters.present_bytes != 32;
    gfx_present_dirty();
    if (dirty_queue.count || attempts != 3 || gfx_counters.commits != 0 ||
        gfx_counters.present_bytes != 64) rc = 1;
    for (int i = 0; i < 128; i++) if (screen[i] != shadow[i]) rc = 1;
    if (!rc && frames != EXPECT_FRAMES) rc = 2;
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
