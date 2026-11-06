/* libc-free lifecycle + real register/primitive assembly. Shared device model
 * only: stage wrappers observe/inject rc, never emulate registers or memory.
 * Reference MIT notices: trident.h and trident_pc98init_host.c.
 */
#include "trident_device_fake.h"
#include <stdarg.h>
#include "drivers/video/trident/trident_module.h"
/* Include the real HAL boundary to inspect its private retained error state.
 * A separate cross compilation checks its production translation unit. */
#include "backend_under_test.c"
#define N(a) (sizeof(a) / sizeof((a)[0]))
static int equal(u32 a, u32 b)
{ __asm__ volatile ("" : "+r"(a), "+r"(b) : : "memory"); return a == b; }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define CALL(x) do { int result=(x); if (result) return result; } while (0)
static struct trident_fake f;
static u8 storage[TRI_BAR0_DECODE_BYTES], client[TRI_DISPLAY_BYTES];
static struct trident_fake_event trace[2 * TRI_DISPLAY_BYTES + 16384];
static u32 scratch[TRI_VRAM_SCRATCH_BYTES / sizeof(u32)];
static u32 calls[17], starts[17], ends[17], order[64], norder;
static u32 fail_step, fail_restore, fail_disable, fail_lock, fail_arm, fail_alloc, fail_client;
static u32 allocs, frees, arms, tvram, logs, emergency_logs, recursion;
static bool emergency, mono, unreadable;
static int oracle_error;
static int height, flip, page;
static GfxBackend backend;
static GfxCounters counters;
static PaletteEntry system_palette[16];
const TridentModuleContext *trident_context;
static int (*recovery_callback)(void *);
static void *recovery_opaque;
static u32 output_traces;
static char serial_text[16384];
static u32 serial_length;
static void emit(const void *p, u32 n)
{
    u32 rc = 4;
    __asm__ volatile ("int $0x80" : "+a"(rc) : "b"(1), "c"(p), "d"(n) : "memory");
}
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *s)
{
    if (emergency) ++emergency_logs;
    while (*s) {
        if (serial_length + 1 < sizeof(serial_text)) serial_text[serial_length++] = *s;
        if (*s++ == '\n') ++logs;
    }
    serial_text[serial_length] = 0;
}
/* Capture production diagnostic arguments without a console sink. */
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            char one[] = {*fmt++, 0}; serial_puts_polled(one); continue;
        }
        ++fmt;
        u32 width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        switch (*fmt++) {
        case 's': serial_puts_polled(va_arg(ap, const char *)); break;
        case 'u': serial_diag_u32(va_arg(ap, u32)); break;
        case 'd': serial_diag_i32(va_arg(ap, int)); break;
        case 'x': serial_diag_hex(va_arg(ap, u32), width ? width : 8); break;
        }
    }
    va_end(ap);
}
static bool contains(const char *part)
{
    for (u32 i = 0; i < serial_length; ++i) {
        u32 j = 0;
        while (part[j] && serial_text[i+j] && part[j] == serial_text[i+j]) ++j;
        if (!part[j]) return true;
    }
    return false;
}
static u32 occurrences(const char *part)
{
    u32 total=0;
    for (u32 i=0;i<serial_length;++i) {
        u32 j=0;
        while (part[j] && serial_text[i+j] && part[j]==serial_text[i+j]) ++j;
        if (!part[j]) ++total;
    }
    return total;
}
void serial_puts(const char *s) { serial_puts_polled(s); }
const PaletteEntry *palette_get_all(void) { return system_palette; }
void palette_shadow_set(int i, u8 r, u8 g, u8 b)
{ system_palette[i] = (PaletteEntry){r,g,b}; }
void *kmalloc(u32 bytes)
{ ++allocs; if (emergency || bytes != sizeof(scratch)) oracle_error=1; return fail_alloc ? 0 : scratch; }
void kfree(void *p)
{ ++frees; if (emergency || p != scratch) oracle_error=1; }
static int owner(void) { return 42; }
static int arm(int (*cb)(void *), void *opaque, u32 who)
{
    ++arms;
    if (who != 42 || f.operations || !device.pinned) oracle_error=1;
    recovery_callback=cb; recovery_opaque=opaque;
    return fail_arm ? -71 : 0;
}
static TridentModuleContext context;
static u32 begin(struct trident_device *d, u32 step)
{
    if (d != &device || !d->armed || !d->pinned) oracle_error=1;
    if (norder < N(order)) order[norder++] = step;
    else oracle_error=1;
    ++calls[step]; starts[step]=f.trace_count;
    return f.trace_count;
}
static void end(u32 step, u32 start)
{
    ends[step]=f.trace_count;
    if (output_traces) {
        u32 header[]={step, f.trace_count-start};
        emit(header,sizeof(header)); emit(trace+start,(f.trace_count-start)*sizeof(trace[0]));
    }
}
#define WRAP(name, step) \
int __real_##name(struct trident_device *); \
int __wrap_##name(struct trident_device *d) \
{ u32 start=begin(d,step); int rc=fail_step==step ? -73 : __real_##name(d); end(step,start); return rc; }
WRAP(trident_identify, TRI_STEP_IDENTIFY)
WRAP(trident_capacity_plan, TRI_STEP_CAPACITY_PLAN)
WRAP(trident_state_save, TRI_STEP_SAVE)
WRAP(trident_mode_640, TRI_STEP_MODE)
WRAP(trident_route_enable, TRI_STEP_ENABLE)
int __real_trident_vram_verify(struct trident_device *);
int __wrap_trident_vram_verify(struct trident_device *d)
{
    u32 start=begin(d,TRI_STEP_VERIFY);
    if (!d->init_done) oracle_error=1;
    int rc=fail_step==TRI_STEP_VERIFY ? -73 : __real_trident_vram_verify(d);
    end(TRI_STEP_VERIFY,start);
    if (!rc && fail_client) trident_fake_fail(&f,f.operations+1,-74);
    return rc;
}
int __real_trident_pc98_init(struct trident_device *);
int __wrap_trident_pc98_init(struct trident_device *d)
{
    u32 start=begin(d,TRI_STEP_PC98_INIT);
    #include "lifecycle_expected.h"
    /* First Save precedes Init; later ScreenInit saves the current mode. */
    if (!d->saved.valid || !d->declared_bytes ||
        (!d->first_init_done && (
        d->saved.value[TRI_SAVE_AR+0x10] != 0x14 ||
        d->saved.value[TRI_SAVE_CR+0x11] != 0x12))) oracle_error=1;
    int rc=fail_step==TRI_STEP_PC98_INIT ? -73 : __real_trident_pc98_init(d);
    if (!rc) {
        const struct trident_fake_event *expected = mono ? init_mono : init_color;
        u32 count=mono ? N(init_mono) : N(init_color);
        if (f.trace_count-start != count || trident_fake_accept_init(&f,start,expected,count))
            oracle_error=1;
    }
    end(TRI_STEP_PC98_INIT,start); return rc;
}
int __real_trident_state_restore(struct trident_device *);
int __wrap_trident_state_restore(struct trident_device *d)
{
    u32 start=begin(d,TRI_STEP_RESTORE);
    if (!d->active || !d->returning) oracle_error=1;
    if (recursion) { recursion=0; if (trident_restore98(d)!=TRI_E_BUSY) oracle_error=1; }
    int rc=fail_restore ? -81 : __real_trident_state_restore(d);
    end(TRI_STEP_RESTORE,start); return rc;
}
int __real_trident_route_disable(struct trident_device *);
int __wrap_trident_route_disable(struct trident_device *d)
{
    u32 start=begin(d,TRI_STEP_DISABLE);
    int rc=fail_disable ? -82 : __real_trident_route_disable(d);
    end(TRI_STEP_DISABLE,start); return rc;
}
int __real_trident_crtc_lock(struct trident_device *, u16, bool);
int __wrap_trident_crtc_lock(struct trident_device *d,u16 base,bool lock)
{
    /* This symbol is also used internally by Init and Mode. */
    if (!d->returning) return __real_trident_crtc_lock(d,base,lock);
    u32 start=begin(d,TRI_STEP_LOCK);
    int rc=fail_lock ? -83 : __real_trident_crtc_lock(d,base,lock);
    end(TRI_STEP_LOCK,start); return rc;
}
static int setup(bool use_mono, bool bad_dac)
{
    device=(struct trident_device){0};
    registers_pending=false; register_entry=0; serial_length=0; serial_text[0]=0;
    CHECK(!trident_fake_reset(&f,storage,sizeof(storage),trace,N(trace)));
    f.resources.client=client;
    f.resources.client_bytes=sizeof(client);
    f.resources.ready=true; /* kernel resource transaction committed */
    mono=use_mono; unreadable=bad_dac;
    f.regs[TRI_MISC_WRITE]=!mono;
    f.regs[TRI_CR_COLOR+6]=0x5A;
    f.regs[TRI_CR_MONO+6]=0xA5;
    f.cr_color[0x11]=f.cr_mono[0x11]=0x12;
    f.ar[0x10]=0x14;
    f.dac_readable=!unreadable;
    backend=trident_backend_ops; counters=(GfxCounters){0};
    context=(TridentModuleContext){&backend,&counters,&f.resources,arm,&height,&flip,&page,owner};
    trident_context=&context;
    fail_step=fail_restore=fail_disable=fail_lock=fail_arm=fail_alloc=fail_client=0;
    allocs=frees=arms=tvram=logs=emergency_logs=recursion=0;
    norder=0; oracle_error=0; emergency=false;
    for (u32 i=0;i<N(calls);++i) calls[i]=starts[i]=ends[i]=0;
    for (u32 i=0;i<N(system_palette);++i) system_palette[i]=(PaletteEntry){i,i,i};
    height=400; flip=page=1;
    CHECK(!backend.probe());
    CHECK(!trident_backend_prepare());
    CHECK(!f.operations && device.pinned && !device.armed);
    CHECK(backend.probe() && backend.bb_base==client && backend.bb_size==sizeof(client));
    /* Use the shared fake through the real regs.c wrappers. prepare's actual
     * bind was checked above, and production_binding below exercises its ops. */
    trident_fake_bind(&f,&device);
    return 0;
}
static int start(void)
{
    backend.init();
    if (output_traces && !device.last_rc) end(TRI_STEP_CLIENT,ends[TRI_STEP_VERIFY]);
    CHECK(!device.last_rc && device.active && device.init_done && device.first_init_done);
    CHECK(equal(device.step,TRI_STEP_ACTIVE) && !oracle_error);
    CHECK(equal(device.verified_bytes,f.resources.mapped_bytes < TRI_FAKE_REAL_BYTES ? f.resources.mapped_bytes : TRI_FAKE_REAL_BYTES) && equal(device.declared_bytes,4194304));
    CHECK(allocs==1 && frees==1 && !device.scratch && !device.scratch_bytes && arms==1);
    CHECK(height==480 && !flip && !page);
    CHECK(contains("[trident] diag regs gen=1 when=init SR0b="));
    CHECK(occurrences("[trident] diag regs gen=1 when=init")==6);
    CHECK(contains(" CR50=00") && contains(" CRbe="));
    CHECK(contains(" XD00=") && contains(" XD57=") && f.ext_index==4);
    return 0;
}
/* init calls prepare again: preserve the already-installed shared fake seam
 * only in the host link, while exercising real regs_bind separately below. */
int __real_trident_regs_bind(struct trident_device *);
int __wrap_trident_regs_bind(struct trident_device *d)
{
    bool fake_installed = d->io == &trident_fake_io;
    int rc=__real_trident_regs_bind(d);
    if (!rc && fake_installed) trident_fake_bind(&f,d);
    return rc;
}
static int image(void)
{
    /* H:359-392: 8 bands, white frame and asymmetric marks. */
    for (u32 y=0;y<480;++y) for (u32 x=0;x<640;++x) {
        u8 c=x/80;
        if (!y || y==479 || !x || x==639 ||
            (x>=16 && x<32 && y>=16 && y<32) ||
            (x>=608 && x<616 && y>=32 && y<64)) c=7;
        client[y*640+x]=c;
    }
    f.trace_lfb=true;
    u32 start=f.trace_count;
    backend.present_rect(0,0,640,480);
    end(TRI_STEP_ACTIVE,start);
    f.trace_lfb=false;
    CHECK(contains("stage=registers entry=1 SR00=03 01 0f 00 0e"));
    CHECK(contains("CR10=ea 2c df 50 00 df 0b c3 ff"));
    CHECK(contains("CR59=") && contains("GR00=") && contains("AR10=41 00 0f 00 00"));
    CHECK(contains("MISC00=eb") && contains("START.CR0c=00") && contains("START.CR0d=00"));
    CHECK(contains("START.CR1e=80") && contains("START.CR27=08"));
    CHECK(contains("entry=1 DAC.MASK00=ff") && contains("entry=1 DAC00=00 00 00"));
    CHECK(contains("entry=1 DAC0f=3f 3f 3f"));
    CHECK(contains("entry=1 CLIENT00=07 07 07 07 07 07 07 07"));
    CHECK(contains("entry=1 CLIENT00019000=07 00 00 00 00 00 00 00"));
    CHECK(contains("entry=1 CLIENT00025940=04 04 04 04 04 04 04 04"));
    CHECK(contains("diag crc gen=1 bytes=307200 rc=0 client=54907c7b lfb=54907c7b"));
    CHECK(contains("diag mismatch gen=1 count=0 bytes=307200 rc=0"));
    CHECK(contains("diag sample gen=1 x=320 y=240 client=04 lfb=04 rc=0"));
    CHECK(contains("diag sample gen=1 x=340 y=260 client=04 lfb=04 rc=0"));
    CHECK(!registers_pending && register_entry==1);
    CHECK(contains("[trident] diag regs gen=1 when=present SR0b="));
    CHECK(occurrences("[trident] diag regs gen=1 when=present")==6);
    CHECK(!device.last_rc && equal(counters.present_bytes,307200) && counters.commits==1);
    for (u32 i=0;i<sizeof(client);++i) CHECK(equal(storage[i],client[i]));
    for (u32 i=0;i<16;++i) {
        u32 v=(i<<2)|(i>>2);
        CHECK(equal(f.dac[i*3],v) && equal(f.dac[i*3+1],v) && equal(f.dac[i*3+2],v));
    }
    CHECK(f.dac[767]==59); /* gray8=238 rounds to 59/63 */
    GFX_ScreenInfo info;
    CHECK(!backend.query(&info));
    CHECK(info.width==640 && info.height==480 && info.bpp==8 && !info.flags);
    CHECK(info.lease_first==16 && info.lease_count==240 && !info.reserved[0]);
    return 0;
}
static int sequence(void)
{
    for (u32 m=0;m<2;++m) {
        CALL(setup(m,false)); CALL(start()); CALL(image());
        u32 lines=logs;
        if (!output_traces) {
            backend.present_rect(0,0,1,1);
            CHECK(logs==lines); /* one dump for this activation */
        }
        struct trident_saved saved=device.saved;
        u32 ops=f.operations;
        backend.enter(); CHECK(equal(f.operations,ops));
        CHECK(!device.last_rc);
        backend.leave();
        CHECK(!device.active && device.leave_done && !device.last_rc);
        CHECK(order[norder-3]==TRI_STEP_RESTORE && order[norder-2]==TRI_STEP_LOCK && order[norder-1]==TRI_STEP_DISABLE);
        ops=f.operations; backend.shutdown(); backend.shutdown();
        CHECK(equal(f.operations,ops) && device.closed);
        u32 second_order=norder;
        backend.init(); CHECK(device.active && !device.closed && !device.leave_done);
        ops=f.operations; backend.enter(); CHECK(equal(f.operations,ops));
        CHECK(calls[TRI_STEP_SAVE]==2 && calls[TRI_STEP_PC98_INIT]==2 && calls[TRI_STEP_VERIFY]==2);
        CHECK(norder==second_order+5 && order[second_order]==TRI_STEP_SAVE &&
              order[second_order+1]==TRI_STEP_PC98_INIT &&
              order[second_order+2]==TRI_STEP_MODE && order[second_order+3]==TRI_STEP_ENABLE && order[second_order+4]==TRI_STEP_VERIFY);
        CHECK(ends[TRI_STEP_SAVE]>starts[TRI_STEP_SAVE]+100 &&
              ends[TRI_STEP_SAVE]==starts[TRI_STEP_PC98_INIT] &&
              ends[TRI_STEP_PC98_INIT]==starts[TRI_STEP_MODE]);
        CHECK(calls[TRI_STEP_MODE]==2 && calls[TRI_STEP_ENABLE]==2 && allocs==2 && frees==2);
        /* Real second ScreenInit must reload all 256 colors after boot DAC
         * restoration, then sample that reloaded image before present. */
        for (u32 i=0;i<16;++i) for (u32 c=0;c<3;++c)
            CHECK(f.dac[i*3+c]==((i<<2)|(i>>2)));
        u32 color=16;
        for (u32 r=0;r<6;++r) for (u32 g=0;g<6;++g) for (u32 b=0;b<6;++b,++color) {
            CHECK(f.dac[color*3]==r*63/5 && f.dac[color*3+1]==g*63/5 && f.dac[color*3+2]==b*63/5);
        }
        for (u32 gray=0;gray<24;++gray,++color) for (u32 c=0;c<3;++c)
            CHECK(f.dac[color*3+c]==((8+gray*10)*63+127)/255);
        /* Distinct second-run contents prove these are fresh RAM samples,
         * independent of register/DAC equality and the one-pixel transfer. */
        client[0]=0x12; client[160*640]=0x34;
        client[168*640+172]=0x56; client[240*640+320]=0x78;
        backend.present_rect(0,0,1,1);
        CHECK(contains("entry=2 DAC.MASK00=ff") && contains("entry=2 DAC0f=3f 3f 3f"));
        CHECK(contains("entry=2 CLIENT00=12 00 00 00 00 00 00 00"));
        CHECK(contains("entry=2 CLIENT00019000=34 00 00 00 00 00 00 00"));
        CHECK(contains("entry=2 CLIENT0001a4ac=56 00 00 00 00 00 00 00"));
        CHECK(contains("entry=2 CLIENT00025940=78 00 00 00 00 00 00 00"));
        CHECK(client[0]==0x12 && client[160*640]==0x34 &&
              client[168*640+172]==0x56 && client[240*640+320]==0x78);
        for (u32 i=0;i<TRI_DAC_BYTES;++i) CHECK(equal(saved.value[i],device.saved.value[i]));
        backend.shutdown();
        CHECK(order[norder-3]==TRI_STEP_DISABLE && order[norder-2]==TRI_STEP_RESTORE && order[norder-1]==TRI_STEP_LOCK);
        CHECK(!device.active && !device.last_rc && device.closed && device.armed && device.pinned);
        CHECK(f.gr[6]==0x0C && device.saved.valid && f.cr_color[0x3A]==0);
        CHECK(!oracle_error);
    }
    /* EnterVT after LeaveVT never resets client/palette or probes again. */
    CALL(setup(false,false)); CALL(start()); backend.leave();
    client[0]=123; f.dac[0]=9;
    backend.enter(); CHECK(device.active && client[0]==123 && f.dac[0]==9);
    CHECK(calls[TRI_STEP_MODE]==2 && calls[TRI_STEP_SAVE]==1 && calls[TRI_STEP_PC98_INIT]==1 && calls[TRI_STEP_VERIFY]==1);
    CHECK(!contains("stage=registers")); /* not during Enable/client setup */
    u8 sr[256], cr[256], gr[256], ar[32];
    for (u32 i=0;i<256;++i) { sr[i]=f.sr[i]; cr[i]=f.cr_color[i]; gr[i]=f.gr[i]; }
    for (u32 i=0;i<32;++i) ar[i]=f.ar[i];
    backend.present_rect(0,0,1,1);
    CHECK(contains("stage=registers entry=2 SR00=") && !registers_pending);
    CHECK(client[0]==123 && storage[0]==123 && f.dac[0]==9);
    for (u32 i=0;i<256;++i) CHECK(sr[i]==f.sr[i] && cr[i]==f.cr_color[i] && gr[i]==f.gr[i]);
    for (u32 i=0;i<32;++i) CHECK(ar[i]==f.ar[i]);
    CHECK((f.ar_index & 0x20) && !f.ar_data);
    u32 lines=logs; backend.present_rect(0,0,1,1); CHECK(logs==lines);
    /* A second ScreenInit while active still resets mode/palette/client. */
    backend.init(); CHECK(!device.last_rc && device.active && client[0]==0);
    CHECK(calls[TRI_STEP_MODE]==3 && calls[TRI_STEP_ENABLE]==3 &&
        calls[TRI_STEP_SAVE]==2 && calls[TRI_STEP_PC98_INIT]==2 && calls[TRI_STEP_VERIFY]==2);
    return 0;
}
static int dac_observation(void)
{
    CALL(setup(false,false)); CALL(start());
    /* Hardware values, including non-FF mask and hidden command, are evidence;
     * diagnostics must not replace them with the intended HAL palette. */
    for (u32 i=0;i<TRI_DAC_BYTES;++i) f.dac[i]=(i*7+3)&63;
    f.regs[TRI_DAC_MASK]=0x5A; f.hidden_dac=0xA6; f.hidden_reads=4;
    f.regs[TRI_DAC_WRITE_INDEX]=42; f.dac_write=42*3; f.dac_read=87;
    backend.present_rect(0,0,1,1);
    CHECK(!device.last_rc && counters.commits==1);
    CHECK(contains("entry=1 DAC.MASK00=5a"));
    CHECK(contains("entry=1 DAC00=03 0a 11") && contains("entry=1 DAC0f=3e 05 0c"));
    CHECK(f.regs[TRI_DAC_MASK]==0x5A && f.hidden_dac==0xA6);
    CHECK(f.regs[TRI_DAC_WRITE_INDEX]==42 && f.dac_write==42*3);
    for (u32 i=0;i<TRI_DAC_BYTES;++i) CHECK(f.dac[i]==((i*7+3)&63));
    /* Inject a failed component read directly at the diagnostic seam. */
    trident_fake_fail(&f,f.operations+4,-94);
    serial_length=0; serial_text[0]=0;
    palette_before_present();
    CHECK(contains("DAC00=?? ?? ??") && contains("DAC0f=?? ?? ??"));
    CHECK(contains("register DAC rc=-94") && !device.last_rc && device.active);
    return 0;
}
static int failures(void)
{
    const u32 stages[]={TRI_STEP_IDENTIFY,TRI_STEP_CAPACITY_PLAN,TRI_STEP_SAVE,
        TRI_STEP_PC98_INIT,TRI_STEP_MODE,TRI_STEP_ENABLE,TRI_STEP_VERIFY};
    for (u32 i=0;i<N(stages);++i) {
        CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; fail_step=stages[i]; backend.init();
        CHECK(equal(device.last_rc,(u32)-73) && device.failed_step==stages[i]);
        CHECK(!backend.probe() && !device.active && logs && !oracle_error);
        CHECK(equal(backend.query(0),(u32)-73));
        u32 ops=f.operations; backend.init(); backend.enter();
        CHECK(equal(f.operations,ops) && equal(device.last_rc,(u32)-73));
        if (stages[i]==TRI_STEP_PC98_INIT) CHECK(!device.init_done);
        if (stages[i]==TRI_STEP_SAVE) {
            CHECK(!device.saved.valid && !calls[TRI_STEP_RESTORE]);
            CHECK(calls[TRI_STEP_DISABLE]==1 && calls[TRI_STEP_LOCK]==1);
            CHECK(order[norder-2]==TRI_STEP_DISABLE && order[norder-1]==TRI_STEP_LOCK);
            CHECK(f.cr_color[0x11] & 0x80);
        }
    }
    /* Identify index/read + boot Misc + capacity index/read. Save then reads
     * Misc, writes DAC mask/index, and reads the first two palette bytes
     * (vgaHW.c:1016-1166; state_save.c's SaveColormap reference). */
    enum { BEFORE_SAVE_ACCESSES = 2 + 1 + 2, SAVE_SECOND_DAC_READ = 1 + 2 + 2 };
    /* Real Save fails after register accesses, with no valid snapshot. */
    for (u32 m=0; m<2; ++m) {
        CALL(setup(m,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES;
        trident_fake_fail(&f,BEFORE_SAVE_ACCESSES + SAVE_SECOND_DAC_READ,-98); backend.init();
        CHECK(equal(device.last_rc,(u32)-98) && device.failed_step==TRI_STEP_SAVE);
        CHECK(!device.saved.valid && !device.active && !calls[TRI_STEP_RESTORE]);
        CHECK(calls[TRI_STEP_DISABLE]==1 && calls[TRI_STEP_LOCK]==1);
        CHECK(order[norder-2]==TRI_STEP_DISABLE && order[norder-1]==TRI_STEP_LOCK);
        CHECK((m ? f.cr_mono[0x11] : f.cr_color[0x11]) & 0x80);
        CHECK(!oracle_error);
    }
    CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; fail_arm=1; backend.init();
    CHECK(equal(device.last_rc,(u32)-71) && !f.operations && !device.armed);
    CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; fail_alloc=1; backend.init();
    CHECK(equal(device.last_rc,(u32)TRI_E_RESOURCE) && !device.active && !frees);
    CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES;
    fail_client=1; backend.init();
    CHECK(equal(device.last_rc,(u32)-74) && device.failed_step==TRI_STEP_CLIENT &&
        !device.first_init_done && !device.active && device.init_done && device.saved.valid);
    CALL(setup(false,true)); CALL(start());
    CHECK(logs>=2 && !device.last_rc); /* DAC diagnostic + VRAM line */
    backend.present_rect(0,0,1,1); CHECK(!device.last_rc && counters.commits==1);
    /* Actual per-access error through real regs/present into HAL's void seam. */
    CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; CALL(start());
    /* A failed diagnostic read is reported, never latched as a mode error. */
    trident_fake_fail(&f,f.operations+3,-90); backend.present_rect(0,0,1,1);
    CHECK(!device.last_rc && device.active && counters.commits==1);
    CHECK(contains("SR00=?? 01 0f 00 0e"));
    trident_fake_fail(&f,f.operations+1,-91); backend.present_rect(0,0,1,1);
    CHECK(equal(device.last_rc,(u32)-91) && !backend.probe() && !device.active && logs);
    CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; CALL(start());
    trident_fake_fail(&f,f.operations+1,-92); u8 rgb[]={15,15,15}; backend.set_palette(0,1,rgb);
    CHECK(equal(device.last_rc,(u32)-92) && !device.active);
    /* General fault and #PF caller contract: recovery failure cannot skip TVRAM.
     * Real isr/dispatch wiring belongs to tri-emerg, not this source scope. */
    for (u32 pf=0;pf<2;++pf) for (u32 leave_first=0;leave_first<2;++leave_first)
        for (u32 which=1;which<8;++which) {
            CALL(setup(false,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; CALL(start());
            fail_restore=which&1; fail_disable=which&2; fail_lock=which&4; recursion=1;
            u32 before=logs;
            emergency=true;
            int rc=leave_first ? trident_leave(&device) : recovery_callback(recovery_opaque);
            ++tvram; /* unconditionally, just as both fault callers must do */
            emergency=false;
            int want=leave_first ? (fail_restore ? -81 : fail_lock ? -83 : -82) :
                                  (fail_disable ? -82 : fail_restore ? -81 : -83);
            CHECK(equal(rc,want) && tvram==1 && logs==before && !emergency_logs);
            CHECK(equal(device.restore_rc,fail_restore ? -81 : fail_lock ? -83 : 0));
            CHECK(equal(device.disable_rc,fail_disable ? -82 : 0));
            CHECK(calls[TRI_STEP_LOCK]==1 && calls[TRI_STEP_DISABLE]==1 && !device.active);
            u32 ops=f.operations; backend.init(); CHECK(equal(f.operations,ops));
            CHECK(!oracle_error);
        }
    /* Real register fault partway through Restore, followed by Lock/Disable. */
    for (u32 off=1;off<=81;off+=80) {
        CALL(setup(true,false)); f.resources.mapped_bytes=TRI_DISPLAY_BYTES; CALL(start());
        trident_fake_fail(&f,f.operations+off,-97);
        emergency=true;
        int rc=trident_leave(&device);
        ++tvram;
        emergency=false;
        CHECK(equal(rc,(u32)-97) && equal(device.restore_rc,(u32)-97) && !device.disable_rc);
        CHECK(calls[TRI_STEP_LOCK]==1 && calls[TRI_STEP_DISABLE]==1 && !device.active && !emergency_logs);
    }
    return 0;
}
static int production_binding(void)
{
    CALL(setup(false,false));
    struct trident_device local={.res=&f.resources};
    CHECK(!__real_trident_regs_bind(&local));
    CHECK(local.io && local.io->read8 && local.io->read16 && local.io->read32 &&
        local.io->write8 && local.io->write16 && local.io->write32 && local.io->port8 && local.io->write_span32);
    CHECK(!trident_write32(&local,TRI_SPACE_LFB,0,0x12345678));
    u32 value=0; CHECK(!trident_read32(&local,TRI_SPACE_LFB,0,&value));
    CHECK(equal(value,0x12345678) && !f.operations);
    CHECK(!backend.fill_rect && !backend.blit && !backend.set_mode && !backend.dpms &&
        !backend.cursor_image && !backend.cursor_move && !backend.cursor_show &&
        !backend.vsync && !backend.page_flip && !backend.gamma && !backend.osri_show);
    CHECK(backend.mode_count==1 && backend.modes[0].pitch==640 && backend.modes[0].dac_bits==6);
    context.resources=0; CHECK(equal(trident_backend_prepare(),(u32)TRI_E_INVAL));
    CHECK(!backend.probe() && equal(backend.query(0),(u32)TRI_E_INVAL));
    return 0;
}
static int diagnostic_observations(void)
{
    CALL(setup(false,false)); CALL(start());
    CHECK(contains("diag init stage=IDENTIFY rc=0 step=4 first_init_done=0 skipped=0"));
    CHECK(contains("diag init stage=CLIENT rc=0 step=11"));
    registers_pending=false; /* test readback independently of register dump */
    serial_length=0; serial_text[0]=0;
    for (u32 i=0;i<5;++i) { client[17+i]=0x56; storage[17+i]=0xAB; }
    diag_after_present();
    CHECK(contains("diag mismatch gen=1 count=5 bytes=307200 rc=0"));
    CHECK(contains("diag mismatch gen=1 n=0 offset=17 client=56 lfb=ab"));
    CHECK(contains("diag mismatch gen=1 n=3 offset=20 client=56 lfb=ab"));
    CHECK(!contains("diag mismatch gen=1 n=4"));
    u32 lines=logs;
    diag_after_present(); CHECK(logs==lines);
    device.verified_bytes=TRI_DISPLAY_BYTES-1;
    backend.present_rect(0,0,640,480);
    CHECK(contains("diag present gen=1 rc=-"));
    CHECK(contains("end=307200 verified_bytes=307199"));
    lines=logs;
    diag_present_error(TRI_E_ACCESS,0,0,640,480); CHECK(logs==lines);
    return 0;
}
void _start(void)
{
    int rc=sequence();
    if (!rc) rc=diagnostic_observations();
    if (!rc) rc=dac_observation();
    if (!rc) rc=failures();
    if (!rc) rc=production_binding();
    if (!rc) {
        output_traces=1;
        rc=setup(false,false);
        if (!rc) rc=start();
        if (!rc) rc=image();
        if (!rc) backend.shutdown();
        if (!rc && oracle_error) rc=__LINE__;
    }
    if (rc) { char b[32]="RED lifecycle "; u32 n=14, v=rc, digits=0; char tmp[12];
        do {tmp[digits++]='0'+v%10; v/=10;} while(v);
        while(digits) b[n++]=tmp[--digits];
        b[n++]='\n'; emit(b,n); }
    u32 result=rc ? 1 : 0;
    __asm__ volatile ("int $0x80" : : "a"(1),"b"(result) : "memory");
    __builtin_unreachable();
}
