#include "gfx_hal.h"
#include "video_service.h"
#include "palette.h"
#include "gf_startlog_host.h"
static void close_display(void);
#ifdef OS32_GEFORCE_MODULE
#include "hwdev/gf/accel.h"
void gf_hook_scan_bind(bool active, const struct gf3_surface *scan)
{ if (active || scan) __builtin_trap(); }
static int hook_fill_calls, hook_blit_calls;
static int hook_args[6];
int geforce_fill_rect(int x, int y, int w, int h, u8 color)
{
    ++hook_fill_calls;
    hook_args[0]=x; hook_args[1]=y; hook_args[2]=w; hook_args[3]=h; hook_args[4]=color;
    return 17;
}
int geforce_blit(int dx, int dy, int sx, int sy, int w, int h)
{
    ++hook_blit_calls;
    hook_args[0]=dx; hook_args[1]=dy; hook_args[2]=sx; hook_args[3]=sy; hook_args[4]=w; hook_args[5]=h;
    return 19;
}
#endif
const unsigned int __os32_abi_1_mem_4_fmt_4 = 0;
static int events[32], n, fail_step;
static PaletteEntry colors[16];
static u8 client[640 * 480];
/* Synthetic nonzero address for testing framebuffer information propagation. */
enum { FAKE_FB_PHYS = sizeof(client), FAIL_WC = 5, LOG_EPOCH = 7, LOG_SEQ = 9 };
void pc98_console_set_safe(int safe) { (void)safe; }
static int identify(void) { return 1; }
static const PaletteEntry *fake_palette_get_all(void) { return colors; }
static void fake_palette_shadow_set(int i, u8 r, u8 g, u8 b) { (void)i; (void)r; (void)g; (void)b; }
static int fake_mode(struct video_device *d, u32 i, struct video_mode *m) { (void)d; (void)i; m->id=7; return 0; }
static int fake_fb(struct video_device *d, struct video_fb *f) { (void)d; f->phys=FAKE_FB_PHYS; f->pitch=640; return 0; }
static int fake_start(struct video_device *d, u32 id, const u8 *p) {
    (void)d; events[n++]=1;
#ifdef OS32_GEFORCE_MODULE
    struct video_fb fb = {0}; u32 width=0, height=0;
    geforce_backend_framebuffer_info(&fb, &width, &height);
    if (fb.phys!=FAKE_FB_PHYS || fb.pitch!=640 || width!=640 || height!=480) return 1;
#endif
    return id!=7 || p[16*3+2]!=0 || p[17*3+2]!=51 || fail_step==1;
}
static int fake_present(const u8 *p, u32 pitch, volatile u8 *v, u32 vp, volatile u8 *bar0, int x, int y, int w, int h, int (*point)(void *), void *context) { (void)bar0; (void)point; (void)context; (void)v; (void)x; (void)y; events[n++]=2; return p!=client || pitch!=640 || vp!=640 || w!=640 || h!=480 || fail_step==2; }
static int fake_cell(void) { events[n++]=3; return fail_step==3; }
static int fake_gf(void) { events[n++]=4; return fail_step==4; }
static int fake_98(void) { events[n++]=6; return 0; }
static int fake_stop(struct video_device *d) { (void)d; events[n++]=5; close_display(); return 0; }
static int fake_pal(struct video_device *d, u32 first, u32 count, const u8 *rgb) { (void)d; return first!=16 || count!=1 || rgb[0]!=255; }
#define palette_get_all fake_palette_get_all
#define palette_shadow_set fake_palette_shadow_set
#define video_mode_info fake_mode
#define video_framebuffer fake_fb
#define video_start fake_start
#define video_stop fake_stop
#define video_palette fake_pal
#define gf_present_rect fake_present
#define pc98_route_cell_check fake_cell
#define pc98_route_to_gf fake_gf
#define pc98_route_to_98 fake_98
#include "../../drivers/video/geforce/backend_geforce.c"
#ifdef OS32_GEFORCE_MODULE
static struct gf2_chan log_chan;
static struct gf4_ctx log_ctx;
static struct gf4_test_ops log_ops;
static const struct gf4_test_ops *log_dispatch;
static GfxBackend module_backend;
static int module_height, module_flip, module_page;
#ifdef GF_F0S_HOST
static struct geforce_heat_bridge test_heat;
static struct geforce_platform test_platform;
#endif
static const GeforceModuleContext module_context = {
#ifdef GF_F0S_HOST
    .heat=&test_heat, .platform=&test_platform,
#endif
    .display_out=&log_dispatch, .backend=&module_backend, .height=&module_height,
    .flip=&module_flip, .page=&module_page
};
const GeforceModuleContext *geforce_context = &module_context;
/* Explicit ownerless HAL fixture: optional F4 hooks remain in the ops
 * table, but invoking their lower layers here must fail, never succeed. */
int gf_vram_next(struct gf2_chan *ch,u32 *cursor,struct gf_vram_reservation *out)
{ (void)ch;(void)cursor;(void)out;__builtin_trap(); }
int gf_vram_match(struct gf2_chan *ch,const void *owner,u32 id,u32 gen,
                  u32 off,u32 bytes,struct gf_vram_reservation *out)
{ (void)ch;(void)owner;(void)id;(void)gen;(void)off;(void)bytes;(void)out;__builtin_trap(); }
int gf3_upload(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
               const struct gf3_ram *ram,bool force,u32 dl)
{ (void)c;(void)s;(void)r;(void)ram;(void)force;(void)dl;__builtin_trap(); }
int gf3_fill(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
             u32 color,u8 alu,u32 mask,u32 dl)
{ (void)c;(void)s;(void)r;(void)color;(void)alu;(void)mask;(void)dl;__builtin_trap(); }
int gf2_fence_emit(struct gf2_chan *ch,struct gf2_fence *out,u32 dl)
{ (void)ch;(void)out;(void)dl;__builtin_trap(); }
int gf2_fence_wait(struct gf2_chan *ch,struct gf2_fence fence,u32 dl)
{ (void)ch;(void)fence;(void)dl;__builtin_trap(); }
#else
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
#endif
static void close_display(void)
{
#ifdef OS32_GEFORCE_MODULE
    log_dispatch = 0;
    log_ctx.last_rc = 0;
    log_chan.last_rc = 0;
#endif
}
static int fake_wc(void) { return fail_step == FAIL_WC ? -1 : 0; }
static int start_logs(struct video_device *d)
{
    static const char *const stages[] = {"video_start", "transfer", "cell", "route", "wc"};
    geforce_backend_wc_bind(fake_wc);
    for (unsigned detail = 0; detail < 4; detail++) {
        for (unsigned i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
            n = failed = active = routed = 0;
            fail_step = i + 1;
            startlog_clear();
#ifdef OS32_GEFORCE_MODULE
            log_chan = (struct gf2_chan){.state=GF2_INIT, .epoch=LOG_EPOCH, .seq=LOG_SEQ, .last_rc=GF2_EIO};
            log_ctx = (struct gf4_ctx){.last_rc=GF4_EFORMAT, .ch=detail == 2 ? 0 : &log_chan};
            log_ops.ctx = &log_ctx;
            log_dispatch = detail == 1 || detail == 2 ? &log_ops : 0;
            struct gf_display_context video = {.display4 = &log_ctx};
            d->context = detail == 3 ? &video : 0;
#endif
            geforce_backend_bind(d, identify, 0, 0, 0);
            gfx_backend_geforce.init();
            /* Compose the independent expected line with the fixture formatter. */
            char actual[sizeof(startlog)];
            for (unsigned j = 0; j < sizeof(startlog); j++) actual[j] = startlog[j];
            startlog_clear();
            kprintf(KLOG_ATTR, "[geforce] start rc=%d stage=%s", fail_step == FAIL_WC ? -1 : 1, stages[i]);
#ifdef OS32_GEFORCE_MODULE
            if (detail == 1 || detail == 3)
                kprintf(KLOG_ATTR, " last=%d gf2=%u/%u/%u ch_rc=%d\n", GF4_EFORMAT, GF2_INIT, (u32)LOG_EPOCH, (u32)LOG_SEQ, GF2_EIO);
            else if (detail == 2)
                kprintf(KLOG_ATTR, " last=%d gf2=na ch_rc=na\n", GF4_EFORMAT);
            else
#endif
                kprintf(KLOG_ATTR, " last=na gf2=na ch_rc=na\n");
            if (!startlog_equal(actual) || !failed || active || routed) return 20;
            unsigned logs = startlog_len;
            gfx_backend_geforce.init();
            if (startlog_len != logs || gfx_backend_geforce.probe()) return 21;
        }
    }
    return 0;
}
static int run(void)
{
    struct video_device d = {0}; GFX_ScreenInfo info; u8 rgb[3]={15,0,0};
#ifdef OS32_GEFORCE_MODULE
    module_backend = geforce_backend_ops;
    if (!module_backend.fill_rect || !module_backend.blit) return 10;
    if (module_backend.fill_rect(-3, 479, 5, 4, 0xa7)!=17 || hook_fill_calls!=1 ||
        hook_args[0]!=-3 || hook_args[1]!=479 || hook_args[2]!=5 || hook_args[3]!=4 || hook_args[4]!=0xa7) return 11;
    if (module_backend.blit(639, 479, 2, 3, 0x7fffffff, 0x7fffffff)!=19 || hook_blit_calls!=1 ||
        hook_args[0]!=639 || hook_args[1]!=479 || hook_args[2]!=2 || hook_args[3]!=3 ||
        hook_args[4]!=0x7fffffff || hook_args[5]!=0x7fffffff) return 12;
#else
    if (gfx_backend_geforce.fill_rect || gfx_backend_geforce.blit) return 10;
#endif
    geforce_backend_bind(&d, identify, 0, 0, 0);
    gfx_backend_geforce.bb_base=client;
    gfx_backend_geforce.prepare();
    if (n || !gfx_backend_geforce.probe() || gfx_backend_geforce.query(&info) || info.flags || info.width!=640 || info.height!=480) return 1;
    gfx_backend_geforce.init();
    for (int k = 0; k < 24; k++)
        for (int c = 0; c < 3; c++)
            if (initial_palette[(232 + k) * 3 + c] != 8 + 10 * k) return 23;
    if (n!=4 || events[0]!=1 || events[1]!=2 || events[2]!=3 || events[3]!=4) return 2;
    if (!startlog_equal("")) return 22;
    gfx_backend_geforce.enter(); if (n!=4) return 3;
    gfx_backend_geforce.set_palette(16,1,rgb); if (!gfx_backend_geforce.probe()) return 4;
    if (geforce_foundation_display(false)!=GF1_OK || active || routed) return 7;
    if (geforce_foundation_display(true)!=GF1_OK || !active || !routed) return 8;
    gfx_backend_geforce.shutdown();
    if (n!=12 || events[4]!=5 || events[5]!=6 || events[6]!=1 || events[9]!=4 || events[10]!=5 || events[11]!=6) return 5;
    n=0; fail_step=3;
    if (geforce_foundation_display(true)!=GF1_EIO) return 9;
    if (n!=5 || events[2]!=3 || events[3]!=5 || events[4]!=6 || gfx_backend_geforce.probe()) return 6;
    return start_logs(&d);
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
