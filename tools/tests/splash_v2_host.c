/* Owner deadlines are driven independently of the scan loop and PIT. */
#include "types.h"
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster_play.h"
#define IO_H
static int irq_on = 1;
static int _irq_enabled(void) { return irq_on; }
#include "../../drivers/video/pc98/splash_raster_run.c"
#include "../../platform/pc98/splash_image.c"
#include "../../drivers/video/pc98/splash_raster_plan.c"

static u32 failure;
static void finish(int rc)
{
    if (rc) {
        char text[] = "line=00000000\n";
        for (u32 j=0; j<8; j++) text[5+j] = "0123456789abcdef"[(failure>>(28-4*j))&15];
        __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(sizeof(text)-1) : "memory");
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(c) do { if (!(c)) { failure=__LINE__; finish(1); } } while (0)
volatile u32 tick_count;
static struct SplashRaster state;
static struct OsriImage image_storage;
static struct PlV2Plan plan_storage;
static u32 allocs, frees, live_image, live_plan, commits, previews, frames, fixed_frames;
static u32 black_calls, logs, load_ticks, transfer_ticks, preview_ticks, frame_ticks;
static int fail_alloc, bad_load, missing, over_budget, fixed_image, frame_result, stop_frame, preview_fail;
static u32 black_since_commit;
static int stat_delay;

int kstrcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }

struct SplashRaster *pc98_splash_raster_state(void) { return &state; }
int gfx_kernel_framebuffer(struct gfx_kernel_fb *fb) { (void)fb; return 0; }
int vfs_stat(const char *path, OS32_Stat *st)
{ (void)path; (void)st; tick_count += stat_delay; return missing; }
void *kmalloc(u32 size)
{
    allocs++;
    if ((int)allocs == fail_alloc) return 0;
    if (size == sizeof(image_storage)) { REQUIRE(!live_image); live_image=1; return &image_storage; }
    REQUIRE(size == sizeof(plan_storage) && !live_plan); live_plan=1; return &plan_storage;
}
void kfree(void *p)
{
    if (p == &image_storage) { REQUIRE(live_image); live_image=0; }
    else { REQUIRE(p == &plan_storage && live_plan); live_plan=0; }
    frees++;
}
int osri_load(const char *path, struct gfx_kernel_fb *fb, struct OsriImage *image)
{
    (void)path; (void)fb;
    tick_count += load_ticks;
    for (u32 i=0; i<sizeof(*image); i++) ((u8 *)image)[i]=0;
    image->version=2; image->band=1; image->count=fixed_image ? 0 : 16;
    for (u32 i=0; i<image->count; i++)
        image->event[i]=(struct OsriEvent){ .line=1, .index=i, .rgb444=1 };
    if (over_budget) image->event[15].rgb444=0x11; /* 32 actual OUTs */
    return bad_load;
}
void palette_set(int index, u8 r, u8 g, u8 b)
{ REQUIRE(index >= 0 && index < 16 && !r && !g && !b); black_calls++; black_since_commit++; }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(!x && !y && w == GFX_WIDTH && h == GFX_HEIGHT); }
void gfx_present_nosync(void)
{
    REQUIRE(black_since_commit == OSRI_COLORS && live_image);
    if (!fixed_image) REQUIRE(live_plan && plan_storage.max_outs == 31);
    commits++; black_since_commit=0; tick_count += transfer_ticks;
}
int pl_preview(struct SplashRaster *s)
{
    REQUIRE(s == &state && commits == 1 && !frames && live_plan && irq_on);
    previews++; tick_count += preview_ticks; return preview_fail;
}
int pl_v2_frame(struct SplashRaster *s, const struct OsriImage *image, const struct PlV2Plan *plan)
{
    REQUIRE(s == &state && image == &image_storage && plan == &plan_storage);
    REQUIRE(previews == 1 && commits == 1 && live_image && live_plan && irq_on);
    frames++; tick_count += frame_ticks;
    REQUIRE(frames <= PLATFORM_SPLASH_TICKS);
    return (int)frames == stop_frame ? PL_FRAME_NO_SYNC : frame_result;
}
int pc98_splash_raster_frame(struct SplashRaster *s, const struct OsriImage *image)
{ REQUIRE(s == &state && image == &image_storage && !live_plan); fixed_frames++; return 0; }
int platform_splash_wait_frame(u32 end_tick) { tick_count=end_tick; return 0; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; logs++; }

static void setup(int mode)
{
    state=(struct SplashRaster){ .checked=1, .dynamic_ok=1, .mode=mode,
        .line_num_us=100000, .line_den=mode ? 3147 : 2483, .total_lines=mode ? 449 : 440 };
    allocs=frees=live_image=live_plan=commits=previews=frames=fixed_frames=black_calls=logs=0;
    black_since_commit=0; fail_alloc=bad_load=missing=over_budget=fixed_image=preview_fail=0;
    stat_delay=stop_frame=0; irq_on=1; frame_result=PL_FRAME_OK;
    load_ticks=5; transfer_ticks=2; preview_ticks=101; frame_ticks=0;
    tick_count=0;
}
static int run(u32 *end)
{
    int rc=platform_splash_try_image("fixture", end);
    REQUIRE(!live_image && !live_plan && frees == allocs - !!fail_alloc);
    REQUIRE(black_calls == commits * 2 * OSRI_COLORS);
    REQUIRE(logs == (rc != PLATFORM_SPLASH_DONE));
    return rc;
}
void _start(void)
{
    for (int mode=0; mode<2; mode++) for (int wrap=0; wrap<2; wrap++) {
        setup(mode); tick_count=wrap ? 0xfffffff0u : 0;
        u32 initial=tick_count, end=initial+PLATFORM_SPLASH_TICKS;
        u32 prefix=load_ticks+transfer_ticks+preview_ticks;
        u32 remaining=PLATFORM_SPLASH_TICKS-prefix;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE);
        unsigned long long p=(unsigned long long)state.total_lines * 100000;
        u32 expected=((unsigned long long)remaining * 10000 * state.line_den) / p;
        REQUIRE(frames == expected && previews == 1 && commits == 1 && allocs == 2);
        REQUIRE(tick_count == initial+prefix); /* No compensation of global PIT. */
        REQUIRE(end-tick_count < 2 && end-tick_count <= remaining);

        setup(mode); frame_result=PL_FRAME_DROP; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_NO_SYNC && frames == expected && previews == 1);
        REQUIRE(end-tick_count < 2);
        setup(mode); frame_ticks=3; end=300;
        prefix=load_ticks+transfer_ticks+preview_ticks;
        remaining=end-prefix;
        u32 tick_limited=(remaining+frame_ticks-1)/frame_ticks;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && frames == tick_limited);
        REQUIRE(end-tick_count <= 1);
        setup(mode); stop_frame=3; end=300;
        prefix=load_ticks+transfer_ticks+preview_ticks;
        remaining=end-prefix;
        REQUIRE(run(&end) == PLATFORM_SPLASH_NO_SYNC && frames == (u32)stop_frame && end-tick_count == ((unsigned long long)remaining * 10000 * state.line_den - (stop_frame-1)*p) / (10000ull * state.line_den));
        /* A single successful frame before NO_SYNC is still a failure. */
        setup(mode); preview_fail=1; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_NO_SYNC && previews == 1 && !frames && end == 300);
        setup(mode); transfer_ticks=300; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && previews == 1 && !frames && end == tick_count);
        setup(mode); preview_ticks=295; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && !frames && end == tick_count);
        setup(mode); load_ticks=201; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_FALLBACK && !commits && !previews && end == 300);
        setup(mode); load_ticks=0; end=100;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && previews == 1 && !frames && end == tick_count);
        setup(mode); end=0;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && !commits && end == tick_count);
        setup(mode); load_ticks=400; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && !commits && end == tick_count);
        setup(mode); over_budget=1; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_FALLBACK && !commits && !previews && end == 300);
        for (int fault=0; fault<7; fault++) {
            setup(mode); end=300;
            if (fault==0) missing=1;
            if (fault==1) bad_load=1;
            if (fault==2) fail_alloc=1;
            if (fault==3) fail_alloc=2;
            if (fault==4) irq_on=0;
            if (fault==5) state.dynamic_ok=0;
            if (fault==6) state.line_den=1;
            REQUIRE(run(&end) == (fault>=4 ? PLATFORM_SPLASH_NO_SYNC : PLATFORM_SPLASH_FALLBACK));
            REQUIRE(!commits && !previews && !frames && end == 300 && irq_on == (fault!=4));
        }
        setup(mode); missing=1; stat_delay=120; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_FALLBACK && end == 300);
        setup(mode); fixed_image=1; end=300;
        REQUIRE(run(&end) == PLATFORM_SPLASH_DONE && allocs == 1 && !previews && !frames && fixed_frames == 1);
    }
    finish(0);
}
