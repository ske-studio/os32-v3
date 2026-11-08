/* Real entry/loader/service/backend/control; deterministic scan and OS stubs. */
#define _start raster_original_start
#include "splash_raster_host.c"
#undef _start
#include "config.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "gfx_osri_view.h"
#include "splash_raster_play.h"
#include "redir_access.h"
#include "pcm_contract.h"
#include "../../exec/appslot.h"
#include "wire_data.h"

static u16 text_attrs[TVRAM_COLS * TVRAM_ROWS];
#undef P2V_IO
#define P2V_IO(addr) ((void *)text_attrs)
#include "../../drivers/video/pc98/splash_raster_run.c"
#include "../../drivers/video/pc98/splash_raster_plan.c"
#define pl_v2_frame_poll real_frame_poll
#include "../../drivers/video/pc98/splash_raster_play.c"
#undef pl_v2_frame_poll
#include "../../gfx/gfx_osri.c"
#include "../../gfx/gfx_osri_view.c"
#include "../../drivers/video/pc98/backend_pc98.c"
#include "../../drivers/video/pc98/backend_pc98_osri.c"
#include "../../kapi/kapi_osri.c"

volatile u32 tick_count;
GfxBackend *g_backend = &gfx_backend_pc98;
GfxCounters gfx_counters;
int gfx_current_height = GFX_HEIGHT, gfx_flip_enabled, gfx_display_page;
DirtyRectQueue dirty_queue, prev_dirty;
static AppSlot slot;
static int started, gui, owner, gate, mounted, pcm, fail_alloc, bad_range;
static int allocs, frees, live, opened, closed, commits, clears, reads, kills;
static int open_error, read_error, hook_calls, frame_mode, frame_calls, reenter;
static u32 pos, file_size, load_ticks, frame_tick_step, cancel_frame, key_frame;
static const u8 *file_data;
static u8 pixels[OSRI_PIXELS_BYTES], path_store[VFS_MAX_PATH];
static struct OsriImage image_store;
static struct PlV2Plan plan_store;
static u16 text_store[OSRI_TEXT_CELLS];
static PaletteEntry saved_colors[OSRI_COLORS], colors[OSRI_COLORS];
static int keys[64], key_count, key_pos, gets, peeks, copy_count;
static int raw_keys[64], raw_count, raw_pos, raw_gets, stop_pending;
static const char *user_path;
static u32 user_bytes;
static int use_real_frame, fixed_cancel_peek, fixed_peeks;
static u32 repeat_key_every;

int gfx_is_started(void) { return started; }
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return owner; }
int appslot_stop_pending(void) { return stop_pending; }
AppSlot *appslot_get(int id) { REQUIRE(id == 2); return &slot; }
int serial_gate_active(void) { return gate; }
int serialfs_is_mounted(void) { return mounted; }
int pcm_state(void) { return pcm; }
void ring3_fault_kill(void) { kills++; }
int caller_access_get(struct caller_access *c)
{ *c = (struct caller_access){ .origin = CALLER_USER, .app_id = 2 }; return 1; }
int copy_caller_bytes(const struct caller_access *c, const void *src, void *dst, u32 n)
{
    REQUIRE(c->app_id == 2 && n == 1);
    u32 offset = (u32)(uptr)src - (u32)(uptr)user_path;
    copy_count++;
    if (bad_range || offset >= user_bytes) return 0;
    *(u8 *)dst = *(const u8 *)src;
    return 1;
}
void *kmemset(void *dst, int value, u32 bytes)
{ u8 *p = dst; while (bytes--) *p++ = value; return dst; }
void *kmalloc(u32 n)
{
    allocs++;
    if (allocs == fail_alloc) return 0;
    live++;
    if (n == VFS_MAX_PATH) { kmemset(path_store, 0, sizeof(path_store)); return path_store; }
    if (n == sizeof(image_store)) return &image_store;
    if (n == sizeof(plan_store)) return &plan_store;
    REQUIRE(n == sizeof(text_store)); return text_store;
}
void kfree(void *p)
{
    REQUIRE(p == path_store || p == &image_store || p == &plan_store || p == text_store);
    REQUIRE(live > 0); live--; frees++;
}
int gfx_kernel_framebuffer(struct gfx_kernel_fb *fb)
{
    *fb = (struct gfx_kernel_fb){ .width = GFX_WIDTH, .height = gfx_current_height,
        .pitch = GFX_BPL, .format = GFX_BB_PLANAR4 };
    for (u32 i = 0; i < GVRAM_PLANES; i++) fb->planes[i] = pixels + i * GFX_PLANE_SZ;
    return 0;
}
int vfs_open(const char *path, int flags)
{
    REQUIRE(path == (char *)path_store && path != user_path && flags == O_RDONLY);
    REQUIRE(path[0] == '/' && path[1] == 'x' && !path[2]);
    if (open_error) return open_error;
    opened++; return 3;
}
void vfs_close(int fd) { REQUIRE(fd == 3); closed++; }
int vfs_read_fd(int fd, void *out, u32 n)
{
    REQUIRE(fd == 3); reads++;
    if (read_error) return -1;
    if (reads == 1) tick_count += load_ticks;
    if (n > 37) n = 37;
    u32 i = 0;
    while (i < n && pos < file_size) ((u8 *)out)[i++] = file_data[pos++];
    return i;
}
void palette_get(int i, u8 *r, u8 *g, u8 *b)
{ *r = colors[i].r; *g = colors[i].g; *b = colors[i].b; }
void palette_set(int i, u8 r, u8 g, u8 b)
{ colors[i] = (PaletteEntry){r,g,b}; }
void gfx_clear_planar_pages(u32 n) { REQUIRE(n == GFX_PLANE_SZ); clears++; }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(!x && !y && w == GFX_WIDTH && h == GFX_HEIGHT); dirty_queue.count++; }
void gfx_present_dirty(void) { }
void gfx_present_nosync(void)
{
    REQUIRE(pos == file_size && opened == closed);
    REQUIRE(pixels[0] == 0x81 && pixels[GFX_PLANE_SZ] == 0x42);
    for (u32 i = 0; i < OSRI_TEXT_CELLS; i++) REQUIRE(!text_attrs[i]);
    commits++;
    if (reenter) REQUIRE(gfx_osri_view("/x", 1, 2) == OS32_ERR_BUSY);
}
int input_peekkey(void)
{
    REQUIRE(irq_on); peeks++;
    if (fixed_cancel_peek) {
        tick_count += 300000u; /* Fifty minutes per boundary; fixed ignores time. */
        if (++fixed_peeks == fixed_cancel_peek) { keys[0]=OSRI_ESC_ASCII; key_count=1; }
    }
    return key_pos < key_count ? keys[key_pos] : -1;
}
int input_trygetkey(void)
{ REQUIRE(irq_on); gets++; return key_pos < key_count ? keys[key_pos++] : -1; }
int input_trygetrawkey(void)
{ REQUIRE(irq_on); raw_gets++; return raw_pos < raw_count ? raw_keys[raw_pos++] : -1; }
int pl_v2_frame_poll(struct SplashRaster *s, const struct OsriImage *image,
                     const struct PlV2Plan *plan, int (*poll)(void *), void *ctx)
{
    frame_calls++;
    REQUIRE(frame_calls < 1000000);
    if (use_real_frame) return real_frame_poll(s, image, plan, poll, ctx);
    if (frame_mode == PL_FRAME_NO_SYNC) return frame_mode;
    if (frame_calls == (int)cancel_frame) slot.abort_req = 1;
    if (frame_calls == (int)key_frame ||
        (repeat_key_every && (u32)frame_calls % repeat_key_every == 0)) {
        keys[0] = 'a'; key_count = 1; key_pos = 0;
    }
    tick_count += frame_tick_step;
    if (poll(ctx)) return PL_FRAME_CANCEL;
    return frame_mode;
}
static void reset(int dynamic)
{
    setup(1, 0);
    raster = (struct SplashRaster){ .checked=1, .dynamic_ok=1, .mode=SPLASH_RASTER_31K,
        .line_num_us=SPLASH_RASTER_LINE_NUM_US, .line_den=SPLASH_RASTER_31K_DEN,
        .total_lines=449 };
    started=1; gui=gate=mounted=pcm=fail_alloc=bad_range=0; owner=7;
    allocs=frees=live=opened=closed=commits=clears=reads=kills=0;
    open_error=read_error=hook_calls=frame_calls=reenter=0;
    gfx_flip_enabled=gfx_display_page=0;
    tick_count=pos=load_ticks=frame_tick_step=cancel_frame=key_frame=0;
    frame_mode=PL_FRAME_OK; g_backend=&gfx_backend_pc98; gfx_current_height=GFX_HEIGHT;
    key_count=key_pos=gets=peeks=copy_count=use_real_frame=0;
    fixed_cancel_peek=fixed_peeks=0; repeat_key_every=0;
    raw_count=raw_pos=raw_gets=stop_pending=0;
    slot.abort_req=0; file_data=dynamic ? wire_events : wire_fixed;
    file_size=dynamic ? sizeof(wire_events) : sizeof(wire_fixed);
    kmemset(pixels, 0xa5, sizeof(pixels));
    user_path="/x"; user_bytes=3; dirty_queue.count=prev_dirty.count=0;
    for (u32 i=0; i<OSRI_COLORS; i++) colors[i]=saved_colors[i]=(PaletteEntry){i,i/2,i/3};
    for (u32 i=0; i<OSRI_TEXT_CELLS; i++) text_attrs[i]=(u16)(0xa000u+i);
}
static int show(u32 ms)
{
    int rc=kapi_osri_show(user_path, ms);
    REQUIRE(!live && opened == closed && irq_on && !osri_active);
    REQUIRE(allocs == frees + !!fail_alloc);
    for (u32 i=0; i<OSRI_COLORS; i++) {
        REQUIRE(colors[i].r == saved_colors[i].r && colors[i].g == saved_colors[i].g &&
                colors[i].b == saved_colors[i].b);
    }
    for (u32 i=0; i<OSRI_TEXT_CELLS; i++) REQUIRE(text_attrs[i] == (u16)(0xa000u+i));
    REQUIRE(clears == commits);
    if (commits) {
        REQUIRE(writes && write_port[writes-1] == GDC_ACCESS_PAGE);
        REQUIRE(write_value[writes-1] == (u32)(gfx_flip_enabled ? 1-gfx_display_page : 0));
    }
    if (allocs >= 2) {
        for (u32 i=0; i<sizeof(pixels); i++) REQUIRE(!pixels[i]);
        REQUIRE(!dirty_queue.count && !prev_dirty.count);
    }
    return rc;
}
static void entry_and_failures(void)
{
    reset(1); REQUIRE(show(0) == OS32_ERR_INVAL && !allocs);
    reset(1); REQUIRE(show(GFX_OSRI_SHOW_MAX_MS+1) == OS32_ERR_INVAL && !allocs);
    reset(1); user_path=""; user_bytes=1;
    REQUIRE(show(1) == OS32_ERR_INVAL && copy_count == 1 && !opened);
    static char long_path[VFS_MAX_PATH];
    kmemset(long_path, 'x', sizeof(long_path));
    reset(1); user_path=long_path; user_bytes=sizeof(long_path);
    REQUIRE(show(1) == OS32_ERR_INVAL && copy_count == VFS_MAX_PATH && !opened);
    reset(1); bad_range=1;
    REQUIRE(show(1) == OS32_ERR_INVAL && kills == 1 && copy_count == 1);
    reset(1); user_bytes=2;
    REQUIRE(show(1) == OS32_ERR_INVAL && kills == 1 && copy_count == 3);
    /* NUL immediately before/after a page boundary: no full-range pre-read. */
    static char pages[2 * PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
    for (u32 offset=PAGE_SIZE-3; offset<=PAGE_SIZE-2; offset++) {
        reset(1); pages[offset]='/'; pages[offset+1]='x'; pages[offset+2]=0;
        user_path=pages+offset;
        REQUIRE(show(1000) == GFX_OSRI_SHOW_DONE && copy_count == 3 && !kills);
    }
    for (int fault=1; fault<=4; fault++) {
        reset(1); fail_alloc=fault;
        REQUIRE(show(1000) == OS32_ERR_NOSPC && !commits);
    }
    const int failures[]={OS32_ERR_NOTFOUND, OS32_ERR_IO, OS32_ERR_NOSPC, OS32_ERR_INVAL};
    for (u32 i=0; i<sizeof(failures)/sizeof(failures[0]); i++) {
        reset(1); open_error=failures[i]; REQUIRE(show(1000) == failures[i] && !commits);
    }
    reset(1); read_error=1; REQUIRE(show(1000) == OS32_ERR_IO && !commits);
    reset(1); file_size--; REQUIRE(show(1000) == OS32_ERR_IO && !commits);
    reset(1); file_data=wire_bad_crc; file_size=sizeof(wire_bad_crc);
    REQUIRE(show(1000) == OS32_ERR_INVAL && !commits);
    for (int fault=0; fault<7; fault++) {
        reset(1);
        if (fault==0) started=0;
        if (fault==1) gui=1;
        if (fault==2) gate=1;
        if (fault==3) pcm=PCM_ST_OPEN;
        if (fault==4) mounted=1;
        if (fault==5) osri_active=1;
        if (fault==6) pcm=PCM_ST_OPENING;
        REQUIRE(kapi_osri_show(user_path, 1) == OS32_ERR_BUSY && !opened && !live);
        osri_active=0;
    }
    reset(1); g_backend=0; REQUIRE(show(1) == OS32_ERR_NOSYS && !opened);
    static GfxBackend absent;
    reset(1); g_backend=&absent; REQUIRE(show(1) == OS32_ERR_NOSYS && !opened);
    reset(1); gfx_current_height=GFX_HEIGHT_200;
    REQUIRE(show(1) == OS32_ERR_NOSYS && !opened);
    reset(1); raster.dynamic_ok=0; REQUIRE(show(1) == OS32_ERR_NOSYS && !commits);
    reset(1); raster.mode=SPLASH_RASTER_31K+1;
    REQUIRE(show(1000) == OS32_ERR_NOSYS && !commits);
    reset(1); file_data=wire_budget; file_size=sizeof(wire_budget);
    REQUIRE(show(1000) == OS32_ERR_NOSYS && !commits);
    reset(1); gui=1; owner=2; reenter=1;
    REQUIRE(show(1000) == GFX_OSRI_SHOW_DONE && commits == 1);
    REQUIRE(!dirty_queue.count && !prev_dirty.count && started && owner == 2);
    reset(1); gfx_flip_enabled=1; gfx_display_page=1;
    REQUIRE(show(1000) == GFX_OSRI_SHOW_DONE && commits == 1);
}
static void deadlines(void)
{
    /* Exact externally calculated 31k floor(30s * 3147 / 44900000). */
    reset(1); REQUIRE(show(GFX_OSRI_SHOW_EXTEND_MS) == 0 && frame_calls == 2102);
    reset(1); raster.mode=SPLASH_RASTER_24K; raster.line_den=SPLASH_RASTER_24K_DEN;
    raster.total_lines=440;
    REQUIRE(show(GFX_OSRI_SHOW_EXTEND_MS) == 0 && frame_calls == 1692);
    reset(1); frame_mode=PL_FRAME_DROP;
    REQUIRE(show(1000) == OS32_ERR_AGAIN && frame_calls == 70);
    reset(1); frame_mode=PL_FRAME_NO_SYNC;
    REQUIRE(show(1000) == OS32_ERR_AGAIN && frame_calls == 1);
    reset(1); load_ticks=101;
    REQUIRE(show(1000) == OS32_ERR_AGAIN && !frame_calls);
    reset(1); REQUIRE(show(1) == OS32_ERR_AGAIN && !frame_calls);
    reset(1); tick_count=0xfffffff0u; frame_tick_step=1;
    REQUIRE(show(1000) == 0 && frame_calls == 70 && tick_count == 54);
    reset(1); keys[0]=OSRI_ESC_ASCII; key_count=1;
    REQUIRE(show(1000) == 1 && gets == 1 && key_pos == 1 && !raster.frame);
    reset(1); cancel_frame=2; REQUIRE(show(1000) == 1 && frame_calls == 2 && !gets);
    reset(1); for (int i=0; i<32; i++) keys[i]='a'; key_count=32;
    REQUIRE(show(1000) == 0 && gets == 32 && key_pos == 32 && frame_calls == 2102);
    reset(1); key_frame=60;
    REQUIRE(show(1000) == 0 && frame_calls == 2161 && gets == 1);
    reset(1); keys[0]='a'; key_count=1;
    REQUIRE(show(GFX_OSRI_SHOW_MAX_MS) == 0 && frame_calls == 8410);

    reset(1); frame_tick_step=1; repeat_key_every=1400; cancel_frame=22000;
    REQUIRE(show(GFX_OSRI_SHOW_EXTEND_MS) == 1 && frame_calls == 22000 && tick_count == 22000);
    REQUIRE(gets == 15); /* 220 seconds of repeated extensions, no global cap. */
    reset(1); frame_tick_step=100; key_frame=1;
    REQUIRE(show(1) == OS32_ERR_AGAIN); /* Cannot fit a first frame at all. */
    reset(1); frame_tick_step=100; key_frame=1; cancel_frame=3;
    REQUIRE(show(1000) == 1 && frame_calls == 3); /* Input precedes expiry after wait. */

    struct PlRun r;
    reset(1); pl_run_init(&r, &raster, 0xfffffff0u, 120000000u);
    r.good=1; r.consumed=r.period*70;
    pl_run_extend(&r, 0x54u, 30000000u);
    REQUIRE(r.start == 0x54u && !r.frames && !r.consumed && r.good == 1);
    REQUIRE(r.budget_us == 119000000u);
    /* Model wins over stalled PIT, remaining rounds down independently. */
    pl_run_init(&r, &raster, 0, 30000000u); r.consumed=r.period*70;
    pl_run_extend(&r, 0, 1);
    REQUIRE(r.budget_us == 29001271u && r.start == 0 && !r.consumed);
    pl_run_extend(&r, 0, 30000000u); REQUIRE(r.budget_us == 30000000u);
    /* 5000 extensions span >71.6 min and wrap ticks without accumulating R. */
    pl_run_init(&r, &raster, 0xffff0000u, 30000000u);
    for (u32 i=0; i<5000; i++) {
        u32 now=r.start+2000;
        pl_run_extend(&r, now, 30000000u);
        REQUIRE(r.start == now && r.budget_us == 30000000u && !r.consumed && !r.frames);
    }
}
static void gui_input(void)
{
    /* Raw codes, including modifiers, are distinct from the idle inject ring. */
    reset(1); gui=1; owner=2; use_real_frame=1;
    raw_keys[0]=KEY_ESC | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT); raw_count=1;
    keys[0]='a'; key_count=1;
    REQUIRE(show(1000) == GFX_OSRI_SHOW_CANCELLED && raw_pos == 1 && raw_gets == 1);
    REQUIRE(!gets && !peeks && !key_pos && !raster.frame);

    reset(1); gui=1; owner=2;
    raw_keys[0]=KEY_ESC; raw_keys[1]=KEY_SPACE; raw_count=2; /* Breaks cannot extend or cancel. */
    REQUIRE(show(1000) == GFX_OSRI_SHOW_DONE && frame_calls == 70 && raw_pos == 2);
    REQUIRE(!gets && !peeks);

    reset(1); gui=1; owner=2;
    raw_keys[0]=KEY_SPACE | KBD_RAW_MAKE; raw_count=1;
    REQUIRE(show(1000) == GFX_OSRI_SHOW_DONE && frame_calls == 2102 && raw_pos == 1);
    REQUIRE(!gets && !peeks);

    /* One boundary consumes at most 32 events, including breaks. */
    reset(1); gui=1; owner=2;
    for (u32 i=0; i<OSRI_POLL_KEYS; i++) raw_keys[i]=KEY_SPACE;
    raw_keys[OSRI_POLL_KEYS]=KEY_ESC | KBD_RAW_MAKE; raw_count=OSRI_POLL_KEYS+1;
    REQUIRE(show(1000) == GFX_OSRI_SHOW_CANCELLED && frame_calls == 2);
    REQUIRE(raw_pos == (int)OSRI_POLL_KEYS+1 && raw_gets == raw_pos && !gets && !peeks);

    reset(0); gui=1; owner=2;
    raw_keys[0]=KEY_ESC; raw_keys[1]=KEY_SPACE | KBD_RAW_MAKE;
    raw_keys[2]=KEY_ESC | KBD_RAW_MAKE; raw_count=3;
    REQUIRE(show(1) == GFX_OSRI_SHOW_CANCELLED && raw_pos == 3 && raw_gets == 3);
    REQUIRE(!gets && !peeks && !frame_calls);

    for (int dynamic=0; dynamic<=1; dynamic++) {
        reset(dynamic); gui=1; owner=2; stop_pending=1;
        raw_keys[0]=KEY_SPACE | KBD_RAW_MAKE;
        raw_keys[1]=KEY_ESC | KBD_RAW_MAKE; raw_count=2;
        REQUIRE(show(1000) == GFX_OSRI_SHOW_CANCELLED && stop_pending == 1);
        REQUIRE(!slot.abort_req && !raw_gets && !raw_pos && !gets && !peeks);
    }
}
static int boundary_poll(void *ctx)
{
    REQUIRE(ctx == &slot && irq_on && !writes && !raster.frame);
    REQUIRE(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC);
    hook_calls++; return 1;
}
static void boundaries_and_fixed(void)
{
    reset(1); image_store=(struct OsriImage){0}; plan_store=(struct PlV2Plan){0};
    REQUIRE(real_frame_poll(&raster, &image_store, &plan_store, boundary_poll, &slot) == PL_FRAME_CANCEL);
    REQUIRE(hook_calls == 1 && irq_on && !writes && !raster.frame && !raster.dropped);
    reset(0); keys[0]='a'; keys[1]=OSRI_ESC_ASCII; key_count=2;
    REQUIRE(show(1) == 1 && gets == 2 && commits == 1 && !frame_calls);
    reset(0); fixed_cancel_peek=5;
    REQUIRE(show(1) == 1 && fixed_peeks == 5 && tick_count == 1500000u);
    reset(0); slot.abort_req=1; tick_count=0x80000000u;
    REQUIRE(show(GFX_OSRI_SHOW_MAX_MS) == 1 && !gets && !frame_calls);
    reset(0); raster.dynamic_ok=0; keys[0]=OSRI_ESC_ASCII; key_count=1;
    REQUIRE(show(1) == GFX_OSRI_SHOW_CANCELLED && gets == 1);
    reset(1); use_real_frame=1; keys[0]=OSRI_ESC_ASCII; key_count=1;
    REQUIRE(show(1000) == 1 && gets == 1 && irq_on);
}
void _start(void)
{
    entry_and_failures(); deadlines(); gui_input(); boundaries_and_fixed(); finish(0);
}
