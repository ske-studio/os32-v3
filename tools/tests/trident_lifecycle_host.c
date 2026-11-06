/* D ScreenInit/EnterVT option branches; X ResetVideo and D DPMS oracles.
 * MIT notices in drivers/video/trident/trident.h. */
#include "trident_device_fake.h"
#include "drivers/video/trident/trident_module.h"
#include "backend_under_test.c"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static struct trident_fake f;
static u8 memory[TRI_FAKE_REAL_BYTES], client[TRI_DISPLAY_BYTES];
static struct trident_fake_event trace[32768];
static u32 stages[128], count, status_reads, arms;
static int height, flip, page;
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
WRAP(mode_640,5)
WRAP(route_enable,6)
int __real_trident_screen_blank(struct trident_device *, bool);
int __wrap_trident_screen_blank(struct trident_device *d, bool blank) {
    stages[count++]=7; return __real_trident_screen_blank(d,blank);
}
WRAP(client_init,12)
WRAP(adjust_frame,8)
WRAP(video_reset,9)
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
static int test(void) {
    CHECK(!trident_fake_reset(&f,memory,sizeof(memory),trace,32768));
    f.resources.ready=true;f.resources.client=client;f.resources.client_bytes=sizeof(client);
    io=trident_fake_io;io.read8=read8;
    static TridentModuleContext context;
    context=(TridentModuleContext){&backend,&counters,&f.resources,arm,&height,&flip,&page,owner};
    trident_context=&context;
    init();
    static const u32 first[]={1,2,3,4,5,6,7,8,12,9};
    CHECK(!order(first,10));
    CHECK(!device.last_rc && device.active && device.screen_ready && device.preinit_done && arms==1);
    CHECK(f.sr[1]&32); CHECK(f.cr_color[0x36]==0 && f.cr_color[0x50]==0);
    CHECK(f.cr_color[0x8E]==0x84 && f.cr_color[0xBE]==0x26);
    CHECK(f.ext_dac[0x50]==255 && f.ext_dac[0x54]==255);
    CHECK(!f.reads[2] && !f.writes[2]);
    init(); static const u32 again[]={3,4,5,6,7,8,12,9}; CHECK(!order(again,8));
    CHECK(!device.last_rc && arms==1);
    enter();static const u32 vt[]={5,6}; CHECK(!order(vt,2));
    CHECK(!(f.sr[1]&32));
    leave();static const u32 leaving[]={10,11};CHECK(!order(leaving,2));
    CHECK(!device.active);shutdown();CHECK(!count);
    init();CHECK(!order(again,8));shutdown();static const u32 closing[]={11,10};CHECK(!order(closing,2));
    /* Failure is local to one attempt; the next ScreenInit runs again. */
    trident_fake_fail(&f,f.operations+1,-73);init();CHECK(device.last_rc==-73);
    CHECK(!probe());
    trident_fake_fail(&f,0,0);count=0;prepare();CHECK(probe());
    init();CHECK(!device.last_rc && device.active);CHECK(!order(again,8));
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
    return 0;
}
void _start(void) {
    int rc=test();
    if(rc) { char s[32]="RED lifecycle ";u32 n=14,v=rc;char digits[10];u32 k=0;
      do {digits[k++]='0'+v%10;v/=10;}while(v);while(k)s[n++]=digits[--k];s[n++]='\n';
      __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc?1:0):"memory");__builtin_unreachable();
}
