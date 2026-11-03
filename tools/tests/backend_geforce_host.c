#include "gfx_hal.h"
#include "video_service.h"
#include "palette.h"
#ifdef OS32_GEFORCE_MODULE
#include "hwdev/gf/accel.h"
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
void pc98_console_set_safe(int safe) { (void)safe; }
static int identify(void) { return 1; }
static const PaletteEntry *fake_palette_get_all(void) { return colors; }
static void fake_palette_shadow_set(int i, u8 r, u8 g, u8 b) { (void)i; (void)r; (void)g; (void)b; }
static int fake_mode(struct video_device *d, u32 i, struct video_mode *m) { (void)d; (void)i; m->id=7; return 0; }
static int fake_fb(struct video_device *d, struct video_fb *f) { (void)d; f->phys=0; f->pitch=640; return 0; }
static int fake_start(struct video_device *d, u32 id, const u8 *p) {
    (void)d; events[n++]=1;
#ifdef OS32_GEFORCE_MODULE
    struct video_fb fb; u32 width=0, height=0;
    geforce_backend_framebuffer_info(&fb, &width, &height);
    if (fb.phys!=0 || fb.pitch!=640 || width!=640 || height!=480) return 1;
#endif
    return id!=7 || p[16*3+2]!=0 || p[17*3+2]!=51 || fail_step==1;
}
static int fake_present(const u8 *p, u32 pitch, volatile u8 *v, u32 vp, volatile u8 *bar0, int x, int y, int w, int h, int (*point)(void *), void *context) { (void)bar0; (void)point; (void)context; (void)v; (void)x; (void)y; events[n++]=2; return p!=client || pitch!=640 || vp!=640 || w!=640 || h!=480 || fail_step==2; }
static int fake_cell(void) { events[n++]=3; return fail_step==3; }
static int fake_gf(void) { events[n++]=4; return fail_step==4; }
static int fake_98(void) { events[n++]=6; return 0; }
static int fake_stop(struct video_device *d) { (void)d; events[n++]=5; return 0; }
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
static GfxBackend module_backend;
static int module_height, module_flip, module_page;
static const GeforceModuleContext module_context = {
    .backend=&module_backend, .height=&module_height,
    .flip=&module_flip, .page=&module_page
};
const GeforceModuleContext *geforce_context = &module_context;
#else
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
#endif
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
    if (n!=4 || events[0]!=1 || events[1]!=2 || events[2]!=3 || events[3]!=4) return 2;
    gfx_backend_geforce.enter(); if (n!=4) return 3;
    gfx_backend_geforce.set_palette(16,1,rgb); if (!gfx_backend_geforce.probe()) return 4;
    if (geforce_foundation_display(false)!=GF1_OK || active || routed) return 7;
    if (geforce_foundation_display(true)!=GF1_OK || !active || !routed) return 8;
    gfx_backend_geforce.shutdown();
    if (n!=12 || events[4]!=5 || events[5]!=6 || events[6]!=1 || events[9]!=4 || events[10]!=5 || events[11]!=6) return 5;
    n=0; fail_step=3;
    if (geforce_foundation_display(true)!=GF1_EIO) return 9;
    if (n!=5 || events[2]!=3 || events[3]!=5 || events[4]!=6 || gfx_backend_geforce.probe()) return 6;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
