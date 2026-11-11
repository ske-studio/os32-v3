/* D ScreenInit/EnterVT option branches; X ResetVideo and D DPMS oracles.
 * MIT notices in drivers/video/trident/trident.h. */
#include "trident_device_fake.h"
#include "drivers/video/trident/trident_module.h"
#include "backend_under_test.c"
#include "drivers/video/trident/probes.h"
#include "concrete_probe_broker.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static struct trident_fake f;
static u8 memory[TRI_FAKE_REAL_BYTES], ram_client[1228800];
static u32 lfb8, lfb32;
static struct trident_fake_event trace[32768];
static u32 stages[128], count, status_reads, arms;
static int height, flip, page, optional_fail, configure_fail;
static const struct TridentDiagnosticOps *diagnostic;
int sys_time_now(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
static GfxBackend backend;
static GfxCounters counters;
static PaletteEntry colors[16];
const TridentModuleContext *trident_context;
const PaletteEntry *palette_get_all(void) { return colors; }
void palette_shadow_set(int i,u8 r,u8 g,u8 b) { colors[i]=(PaletteEntry){r,g,b}; }
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s) { (void)s; }
void serial_puts(const char *s) { (void)s; }
static int owner(void) { return 42; }
static int arm(int (*cb)(void *),void *opaque,u32 who) {
    if (!cb || opaque!=&device || who!=42 || f.operations || !device.pinned) return -99;
    ++arms; return 0;
}
static struct trident_io io;
static int write8(void *o, enum trident_space space, u32 off, u8 v) {
    if (space == TRI_SPACE_LFB) ++lfb8;
    return trident_fake_io.write8(o,space,off,v);
}
static int write32(void *o, enum trident_space space, u32 off, u32 v) {
    if (space == TRI_SPACE_LFB) ++lfb32;
    return trident_fake_io.write32(o,space,off,v);
}
static int read8(void *o,enum trident_space space,u32 off,u8 *v) {
    /* Deterministic VSYNC waveform: already high, then low, then rising.
     * Four reads per complete transition exercises both wait loops. */
    if (space==TRI_SPACE_REGS && off==TRI_CR_COLOR+6)
        f.regs[off]=((status_reads++ % 4)==0 || (status_reads%4)==0)?8:0;
    return trident_fake_io.read8(o,space,off,v);
}
int __wrap_trident_regs_bind(struct trident_device *d) {
    d->io=&io; d->io_opaque=&f; return 0;
}
#define WRAP(name,id) \
int __real_trident_##name(struct trident_device *); \
int __wrap_trident_##name(struct trident_device *d) { \
    stages[count++]=id; return __real_trident_##name(d); }
WRAP(identify,1)
WRAP(capacity_plan,2)
WRAP(state_save,3)
WRAP(pc98_init,4)
int __real_tri_mode_set(struct trident_device *, const struct tri_mode *);
int __wrap_tri_mode_set(struct trident_device *d, const struct tri_mode *m) {
    stages[count++]=5; return __real_tri_mode_set(d,m);
}
int __real_tri_accel_init(struct trident_device *, const struct tri_mode *);
int __wrap_tri_accel_init(struct trident_device *d, const struct tri_mode *m) {
    stages[count++]=14;
    if (optional_fail==14) return -81;
    return __real_tri_accel_init(d,m);
}
#define TRI_WRAP(name,id) \
int __real_tri_##name(struct trident_device *); \
int __wrap_tri_##name(struct trident_device *d) { \
    stages[count++]=id; \
    if (optional_fail==id) return -81; \
    return __real_tri_##name(d); }
TRI_WRAP(cursor_init,15)
TRI_WRAP(palette_load_default,16)
TRI_WRAP(palette_reload,17)
TRI_WRAP(sync,18)
int __real_tri_video_stop(struct trident_device *, bool);
int __wrap_tri_video_stop(struct trident_device *d, bool release) {
    stages[count++]=19;
    if (optional_fail==19) return -81;
    return __real_tri_video_stop(d,release);
}
static int configure(const struct tri_mode *m, u32 bytes, bool final) {
    if (final) stages[count++]=13;
    /* 640x480 XRGB8888 is the largest HAL-visible mode in the 2MiB fake.
     * The selected INDEX8 screen must reserve this fixed display extent. */
    if (final && (bytes!=(f.resources.ram_fallback ? 1228800u : 1966080u) || (!f.resources.ram_fallback && m->id!=device.mode.selected.id))) return -90;
    if (!final && f.operations == 0 && bytes != 307200u) return -90;
    if (final && configure_fail) return configure_fail;
    u32 pitch=m->display_width*(m->bpp/8);
    f.resources.display=(struct tri_surface){0,pitch*m->vd,pitch,m->hd,m->vd,
                                           m->bpp/8,TRI_FMT_INDEX8};
    f.resources.client_surface=f.resources.display;
    f.resources.client=f.resources.ram_fallback ? ram_client : f.vram;
    f.resources.client_bytes=pitch*m->vd;
    ++f.resources.generation;
    return 0;
}
WRAP(route_enable,6)
int __real_trident_screen_blank(struct trident_device *, bool);
int __wrap_trident_screen_blank(struct trident_device *d, bool blank) {
    stages[count++]=7; return __real_trident_screen_blank(d,blank);
}
WRAP(client_init,12)
WRAP(adjust_frame,8)
int __real_trident_video_reset(struct trident_device *);
int __wrap_trident_video_reset(struct trident_device *d) {
    stages[count++]=9;
    if (optional_fail==9) return -81;
    return __real_trident_video_reset(d);
}
WRAP(state_restore,10)
WRAP(route_disable,11)
static int order(const u32 *want,u32 n) {
    CHECK(count==n);
    for(u32 i=0;i<n;++i) CHECK(stages[i]==want[i]);
    count=0; return 0;
}
static int writes(const u32 (*want)[3],u32 n) {
    u32 at=0;
    for(u32 i=0;i<f.trace_count;++i) if(trace[i].write) {
        CHECK(at<n && trace[i].offset==want[at][0] && trace[i].value==want[at][1] && trace[i].width==want[at][2]);
        ++at;
    }
    CHECK(at==n && !f.trace_overflow); return 0;
}
static int probe_same(const char *a,const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static u32 probe_records, probe_id;
static void record_screeninit(u32 id) {
    if (id != 0 || !device.screen_ready || device.step != TRI_STEP_ACTIVE)
        probe_id = 1;
    ++probe_records;
}
static int test(void) {
    const struct probe_decl *decls;
    u32 ndecls;
    CHECK(!trident_get_probe_decls(&decls, &ndecls) && ndecls == 1);
    CHECK(probe_same(decls[0].name, "tri.screeninit.post"));
    CHECK(!decls[0].slot->hit && !decls[0].slot->id);
    CHECK(!concrete_register(decls,ndecls));

    CHECK(!trident_fake_reset(&f,memory,sizeof(memory),trace,32768));
    f.resources.ready=true;f.resources.client=memory;f.resources.client_bytes=TRI_DISPLAY_BYTES;
    f.resources.configure=configure;
    io=trident_fake_io;io.read8=read8;io.write8=write8;io.write32=write32;
    static TridentModuleContext context;
    context=(TridentModuleContext){&backend,&counters,&f.resources,arm,&height,&flip,&page,owner,TRIDENT_MODULE_CONTEXT_SIZE,&diagnostic,300,1000000};
    trident_context=&context;
    init();
    static const u32 first[]={1,2,3,4,5,6,7,8,12,19,13,14,15,16,9};
    CHECK(!order(first,15));
    CHECK(!device.last_rc && device.active && device.screen_ready && device.preinit_done && arms==1);
    CHECK(f.sr[1]&32); CHECK(f.cr_color[0x36]==0x80 && f.cr_color[0x50]==0);
    CHECK(f.cr_color[0x8E]==0x84 && f.cr_color[0xBE]==0x26);
    CHECK(f.ext_dac[0x50]==255 && f.ext_dac[0x54]==255);
    CHECK(device.accel.initialized && device.cursor.available);
    CHECK(device.mode.client.off==device.mode.display.off && !f.resources.ram_fallback);
    CHECK(!probe_records);
    CHECK(!concrete_arm(record_screeninit,ndecls));
    init();
    CHECK(probe_records == 1 && !probe_id);
    static const u32 reinit[]={18,19,11,10,3,4,5,6,7,8,12,19,13,14,15,16,9};
    static const u32 again[]={3,4,5,6,7,8,12,19,13,14,15,16,9};
    CHECK(!order(reinit,17));
    CHECK(!device.last_rc && arms==1);
    enter();static const u32 vt[]={5,6,14,17}; CHECK(!order(vt,4));
    CHECK(!(f.sr[1]&32));
    CHECK(probe_records == 1);
    CHECK(!concrete_stop());
    leave();static const u32 leaving[]={18,19,10,11};CHECK(!order(leaving,4));
    CHECK(!device.active);shutdown();CHECK(!count);
    init();CHECK(!order(again,13));shutdown();static const u32 closing[]={18,19,11,10};CHECK(!order(closing,4));
    /* Failure is local to one attempt; the next ScreenInit runs again. */
    trident_fake_fail(&f,f.operations+1,-73);init();CHECK(device.last_rc==-73);
    CHECK(!probe());
    trident_fake_fail(&f,0,0);count=0;prepare();CHECK(probe());
    init();CHECK(!device.last_rc && device.active);CHECK(!order(again,13));
    /* D:3692-3703: each optional failure keeps display/query usable and
     * removes only the affected capability. Enter retains availability. */
    for (u32 i=0;i<3;++i) {
        shutdown(); count=0;
        optional_fail=(u32[]){14,15,9}[i];
        init(); CHECK(!order(again,13));
        GFX_ScreenInfo info;
        CHECK(device.active && device.screen_ready && device.last_rc==-81);
        CHECK(device.failed_step==TRI_STEP_CLIENT && !display_rc && probe() && !query(&info));
        if (i==0) CHECK(!device.accel.initialized && device.mode.no_accel &&
                        !(device.mode.features&TRI_CAP_GE) && !backend.fill_rect && !backend.blit);
        if (i==1) CHECK(!device.cursor.available && !(device.mode.features&TRI_CAP_CURSOR) && !backend.cursor_image);
        if (i==2) CHECK(device.video.last_rc==-81 && !device.video.polling && !(device.mode.features&TRI_CAP_VIDEO));
        leave(); CHECK(!order(leaving,4));
        enter(); CHECK(!order(vt,4));
        CHECK(device.active && device.screen_ready && !display_rc);
        if (i==1) CHECK(!device.cursor.available && !(device.mode.features&TRI_CAP_CURSOR));
        if (i==2) CHECK(!(device.mode.features&TRI_CAP_VIDEO));
        optional_fail=0;
    }
    shutdown(); count=0; device.settings.no_accel=true;
    init(); CHECK(!order(again,13));
    CHECK(device.active && device.screen_ready && !device.accel.initialized &&
          device.mode.no_accel && !backend.fill_rect && !device.cursor.available);
    leave(); CHECK(!order(leaving,4)); enter(); CHECK(!order(vt,4));
    shutdown(); count=0; device.settings.no_accel=false;
    configure_fail=-82; init();
    CHECK(device.last_rc==-82 && !device.screen_ready && !device.active);
    configure_fail=0; count=0; init(); CHECK(!order(again,13));
    /* Mandatory palette failures reject ScreenInit/Enter; optional setup
     * diagnostics above must not be confused with these display failures. */
    shutdown(); count=0; optional_fail=16; init();
    CHECK(device.last_rc==-81 && display_rc==-81 && !device.screen_ready && !device.active);
    optional_fail=0; count=0; init(); CHECK(!order(again,13));
    optional_fail=17; enter();
    CHECK(display_rc==-81 && device.last_rc==-81 && !device.active);
    optional_fail=0; count=0; init(); CHECK(!order(again,13));
    /* A failed Sync/overlay stop still restores and disables the route. */
    for (int fail=18;fail<=19;++fail) {
        optional_fail=fail; device.video.polling=true;
        CHECK(trident_close(&device)==-81); CHECK(!order(closing,4));
        CHECK(!device.active && !device.returning && !device.video.polling);
        optional_fail=0; init(); CHECK(!order(again,13));
    }
    /* ResetVideo exact write order/width, and two complete VSYNC periods. */
    count=0;status_reads=0;trident_fake_trace_reset(&f);
    CHECK(!trident_video_reset(&device));CHECK(status_reads==8);
    static const u32 video[][3]={{0x3D4,0x848E,2},{0x83C8,0x57,1},{0x83C6,0xC0,1},
      {0x3D4,0x26BE,2},{0x83C8,0x50,1},{0x83C6,255,1},{0x83C8,0x51,1},{0x83C6,0,1},
      {0x83C8,0x52,1},{0x83C6,0,1},{0x83C8,0x54,1},{0x83C6,255,1},
      {0x83C8,0x55,1},{0x83C6,0,1},{0x83C8,0x56,1},{0x83C6,0,1}};
    CHECK(!writes(video,16));
    trident_fake_trace_reset(&f);f.sr[1]=1;
    CHECK(!trident_screen_blank(&device,true));
    static const u32 blank[][3]={{0x3C4,1,1},{0x3C4,0,1},{0x3C5,1,1},{0x3C4,1,1},
      {0x3C5,33,1},{0x3C4,0,1},{0x3C5,3,1}};
    CHECK(!writes(blank,7));
    trident_fake_trace_reset(&f);f.cr_color[0x1E]=255;f.cr_color[0x27]=255;
    CHECK(!trident_adjust_frame(&device));
    static const u32 frame[][3]={{0x3D4,0x000C,2},{0x3D4,0x000D,2},{0x3D4,0x1E,1},
      {0x3D5,0xDF,1},{0x3D4,0x27,1},{0x3D5,0xF8,1}};
    CHECK(!writes(frame,6));
    static const u8 pm[]={3,2,2,0};
    for(u32 mode=0;mode<4;++mode) {
      f.sr[14]=0xA5;f.ext_dac[4]=0xBC;f.gr[0x23]=0xDC;trident_fake_trace_reset(&f);
      CHECK(!trident_dpms_set(&device,mode));
      u32 dpms[][3]={{0x3C4,14,1},{0x3C5,0xC2,1},{0x83C8,4,1},{0x3CE,0x23,1},
        {0x3CF,0xDC|mode,1},{0x83C8,4,1},{0x83C6,0xBC|pm[mode],1},{0x3C4,0xA50E,2}};
      CHECK(!writes(dpms,8));
    }
    device.active=false;trident_fake_trace_reset(&f);
    CHECK(!trident_dpms_set(&device,TRI_DPMS_OFF) && !f.trace_count);
    CHECK(!trident_protect(&device,TRI_CR_COLOR,true) && !f.trace_count);
    trident_fake_fail(&f,f.operations+1,-74);
    CHECK(trident_screen_init(&device)==-74);
    trident_fake_fail(&f,0,0);
    CHECK(!trident_screen_init(&device) && !device.last_rc);
    extern int tri_test_bind_rc;
    extern u32 tri_test_unregisters;
    u32 unregisters = tri_test_unregisters;
    shutdown(); count=0;
    CHECK(tri_test_unregisters == unregisters + 1);
    tri_test_bind_rc = OS32_ERR_FULL;
    init(); count=0;
    GFX_ScreenInfo failed_info;
    CHECK(display_rc == OS32_ERR_FULL && !device.active && !device.sync_ctx);
    CHECK(query(&failed_info) != 0);
    tri_test_bind_rc = 0;
    init(); count=0;
    CHECK(!display_rc && device.active && device.sync_ctx);
    return 0;
}
static int fallback_test(void) {
    shutdown(); count=0;
    device=(struct trident_device){0};
    CHECK(!trident_fake_reset(&f,memory,sizeof(memory),trace,32768));
    f.resources.ready=true; f.resources.configure=configure;
    f.resources.ram_fallback=true;
    f.resources.mapped_bytes=sizeof(memory);
    /* Drawing fixture: full PC98 Init traces are checked by trident-pc98init. */
    f.pc98_initialized=true;
    for (u32 i=0;i<sizeof(memory);++i) memory[i]=0xA5;
    lfb8=lfb32=0;
    init(); count=0;
    CHECK(!display_rc && device.active && device.screen_ready);
    CHECK(device.settings.ram_fallback && device.mode.no_accel && f.cr_color[0x36]==0);
    CHECK(device.mode.client.pitch==640 && backend.bb_base==ram_client && backend.bb_pitch==640);
    CHECK(!device.accel.initialized && !device.accel.pattern.allocated && !(device.mode.features&TRI_CAP_GE));
    /* No sync.cfg: the common binding defaults to byte/plain. */
    CHECK(!backend.fill_rect && !backend.blit && lfb8==307200 && !lfb32);
    for(u32 i=0;i<307200;++i) CHECK(!memory[i] && !ram_client[i]);
    CHECK(memory[1228800]==0xA5);
    /* CPU dirty drawing reaches DISPLAY through the HAL present counter. */
    ram_client[641]=0x69; counters.present_bytes=0;
    present(1,1,1,1); count=0;
    CHECK(memory[641]==0x69 && counters.present_bytes==1);
    ram_client[614399]=0x3A;
    CHECK(!tri_hal_mode(3)); count=0; /* RGB565 */
    CHECK(memory[614399]==0x3A);
    CHECK(device.mode.client.cpp==2 && backend.bb_pitch==1280 && !device.accel.initialized);
    CHECK(!backend.fill_rect && !backend.blit);
    ram_client[7]=0x37; memory[7]=0;
    leave(); count=0; enter(); count=0;
    CHECK(!display_rc && device.screen_ready && memory[7]==0x37 && !device.accel.initialized);
    static const u32 pixel=0xffffffffu;
    struct GfxCursorImage im={&pixel,4,4,1,1,0,0};
    u32 before=lfb8, before32=lfb32;
    CHECK(!tri_hal_cursor_image(0,&im)); count=0;
    CHECK(lfb8==before+TRI_CURSOR_IMAGE_BYTES && lfb32==before32);
    u32 off=device.cursor.allocation.off;
    for(u32 i=0;i<TRI_CURSOR_IMAGE_BYTES;++i) CHECK(memory[off+i]==device.cursor.image[i]);
    static const u8 data[32]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    const u32 formats[]={TRI_VIDEO_RV16,TRI_VIDEO_YUY2,TRI_VIDEO_YV12};
    struct tri_rect rect={0,0,4,2};
    for(u32 i=0;i<3;++i) {
        struct tri_video_image imv={.format=formats[i],.width=4,.height=2,.data=data};
        CHECK(!tri_video_query(&imv));
        /* Byte present isolates the overlay upload's own store width. */
        u32 put32=lfb32; device.present_method=TRI_PRESENT_BYTE;
        CHECK(!tri_video_put(&device,&imv,&rect,&rect,0x1234)); count=0;
        device.present_method=TRI_PRESENT_DWORD_GAP;
        CHECK(lfb32==put32 && memory[device.video.allocation.off]==1);
        CHECK(memory[device.video.allocation.off+2]==(i==2?2:3));
    }
    /* Present must preserve the cursor and overlay behind fixed B. */
    u8 cursor=memory[off], overlay=memory[device.video.allocation.off];
    present(0,0,640,480); count=0;
    CHECK(memory[off]==cursor && memory[device.video.allocation.off]==overlay);
    shutdown(); count=0; prepare(); count=0;
    CHECK(device.mode.selected.id==TRI_MODE_DEFAULT_ID && backend.bb_pitch==640);
    init(); count=0;
    CHECK(!display_rc && device.mode.client.cpp==1 && !device.accel.initialized);
    shutdown(); count=0;
    return 0;
}
void _start(void) {
    int rc=test();
    if (!rc) rc=fallback_test();
    if(rc) { char s[32]="RED lifecycle ";u32 n=14,v=rc;char digits[10];u32 k=0;
      do {digits[k++]='0'+v%10;v/=10;}while(v);while(k)s[n++]=digits[--k];s[n++]='\n';
      __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc?1:0):"memory");__builtin_unreachable();
}
