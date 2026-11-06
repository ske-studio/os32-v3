#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "../../drivers/video/geforce/geforce_module.c"
#include "../../drivers/video/geforce/geforce_body.c"
#define GF3_HOST_UNIT 12
#include "gf_accel_host.h"
/* Keep checks observable even when a getter/slot store is inlined. */
#undef CHECK
#define CHECK(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)
static const struct gf3_test_ops accel_ops_a, accel_ops_b;
struct gf1_test_ops gf1_module_ops;
static struct gf_foundation *bound_foundation;
static struct gf2_chan *bound_chan;
/* Link real F3/F4 registration; only the kernel/platform boundary is modeled. */
void gf2_test_bind_production(struct gf_foundation *f,struct gf2_chan *ch)
{ bound_foundation=f;bound_chan=ch; }
static void production_bind(void)
{ CHECK(bound_foundation==&production.foundation && bound_chan==&production.chan); }
static struct gf_production_resources fixture_resources;
static struct geforce_platform fixture_platform={.resources=&fixture_resources};
struct geforce_platform *geforce_platform_get(void) { return &fixture_platform; }
void geforce_backend_wc_bind(int (*start)(void)) { (void)start; }
void geforce_backend_stop_bind(int (*stop)(void)) { (void)stop; }
static int prepare_mode;
int gf_production_prepare(struct gf_production *p,const struct gf_production_resources *b,
                         int (*r)(void *),void *o) { (void)b;(void)r;(void)o;p->prepared=prepare_mode==0;return prepare_mode==1 ? -1 : 0; }
int gf_production_identity(struct gf_production *p) { (void)p;return -1; }
int gf_production_mapped(struct gf_production *p) { (void)p; __builtin_trap(); }
int gf_production_decode_safe(struct gf_production *p) { (void)p;return -1; }
int gf_production_quiesce(struct gf_production *p) { (void)p;return -1; }
int gf_production_point(void *p) { (void)p;return -1; }
int pc98_route_select_98(void) { return 0; }
int pc98_route_to_98(void) { return 0; }
static int return_registered;
void gf_production_set_return(int (*callback)(void *),void *opaque)
{ (void)callback;(void)opaque;CHECK(registered);return_registered++; }
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
static u32 probes, enables, unbinds;
static int unbind_fail;
static int diagnostic_stops, diagnostic_fail=1;
static int diagnostic_call(struct gf_foundation *f,const struct gf1_test_request *q,
                           struct gf1_test_reply *reply)
{
    CHECK(q->op==GF1T_STOP);diagnostic_stops++;
    reply->rc=diagnostic_fail ? GF1_EIO : 0;
    if (!diagnostic_fail) f->running=false;
    return reply->rc;
}
int geforce_tick_bind(void (*poll)(void *),void *arg)
{ CHECK(!poll && !arg);unbinds++;return unbind_fail; }
static int probe(void) { probes++; return 1; }
static void enable(void) { enables++; }
static int query(GFX_ScreenInfo *s) { (void)s; return 0; }
static void present(int x,int y,int w,int h) { (void)x;(void)y;(void)w;(void)h; }
static void palette(int x,int n,const u8 *p) { (void)x;(void)n;(void)p; }
static int fill(int x,int y,int w,int h,u8 color)
{ (void)x;(void)y;(void)w;(void)h;(void)color;return 0; }
static int blit(int dx,int dy,int sx,int sy,int w,int h)
{ (void)dx;(void)dy;(void)sx;(void)sy;(void)w;(void)h;return 0; }
void geforce_backend_bind(struct video_device *d,int (*i)(void),u32 bar,int (*p)(void *),void *o)
{ (void)d;(void)i;(void)bar;(void)p;(void)o; }
static const GfxMode fixture_modes[]={{640,480,GFX_FMT_PACKED8,8,0,0,640}};
GfxBackend geforce_backend_ops={.name="fixture",.probe=probe,.init=enable,.query=query,
    .shutdown=enable,.present_rect=present,.set_palette=palette,.enter=enable,.leave=enable,
    .prepare=enable,.fill_rect=fill,.blit=blit,.modes=fixture_modes,.mode_count=1};
static int init_rc;
static int publishing_init(GfxBackend *out,const GeforceModuleContext *ctx)
{
    int rc=geforce_module_init(out,ctx);
    CHECK(!rc && ctx->accel_out==&accel_slot);
    *ctx->accel_out=&accel_ops_a;
    CHECK(!geforce_accel_ops()); /* Not registered yet. */
    return init_rc;
}
static void start_failures(void)
{
    struct module_record r={0};
    u32 entry=(u32)(uptr)publishing_init;
    init_rc=GF3_ESTATE;
    CHECK(geforce_start(&r,entry,0)==GF3_ESTATE);
    CHECK(!accel_slot && !geforce_accel_ops());
    init_rc=0;
    /* Required callbacks lie outside the empty module text range. */
    CHECK(geforce_start(&r,entry,0)==MODULE_INIT_FAILED);
    CHECK(!accel_slot && !geforce_accel_ops());
    r.text=(u32)(uptr)probe & ~(PAGE_SIZE-1);r.text_size=16*PAGE_SIZE;
    /* Valid callbacks but no live handle: registration pin fails. */
    CHECK(geforce_start(&r,entry,0)==MODULE_INIT_FAILED);
    CHECK(!accel_slot && !geforce_accel_ops() && !registered);
}
void _start(void)
{
    read_input(input,sizeof(input)); read_input(file,sizeof(file));
    kmemcpy(&imports[0].name_hash,file+152,4);
    kmemcpy(&exports[4].name_hash,file+164,4); exports[4].version=1;
    context.platform=&fixture_platform;
    fixture_platform.opaque=&production;
    CHECK(!gf4_data_bind(&context.display_data,&context.display_state));
    CHECK(!geforce_accel_ops());
    /* Boot reservation missing, failed prepare, and incomplete prepare all
     * fail before any diagnostic/service owner is published. */
    fixture_platform.resources=0;
    CHECK(geforce_module_init(&candidate,&context)==-1);
    CHECK(!foundation_slot && !accel_slot && !gf1_module_ops.production && !bound_foundation);
    fixture_platform.resources=&fixture_resources;
    prepare_mode=1;
    CHECK(geforce_module_init(&candidate,&context)==-1);
    CHECK(!foundation_slot && !accel_slot && !gf1_module_ops.production && !bound_foundation);
    prepare_mode=2;
    CHECK(geforce_module_init(&candidate,&context)==-1);
    CHECK(!foundation_slot && !accel_slot && !gf1_module_ops.production && !bound_foundation);
    prepare_mode=0;
    start_failures();
    accel_slot=&accel_ops_a;
    CHECK(!geforce_accel_ops()); /* Hidden until registration succeeds. */
    production.foundation.registered=true;
    CHECK(!geforce_detach(0) && !unbinds);
    CHECK(!accel_slot && !geforce_accel_ops());
    production.foundation.registered=false;
    production.foundation.bm=true;CHECK(!geforce_detach(0) && !unbinds);
    production.foundation.bm=false;
    production.foundation.mapped=true;CHECK(!geforce_detach(0) && !unbinds);
    production.foundation.mapped=false;
    production.foundation.running=true;CHECK(!geforce_detach(0) && !unbinds);
    production.foundation.running=false;
    struct gf_inst instance={0};production.foundation.imem.used=&instance;
    CHECK(!geforce_detach(0) && !unbinds);production.foundation.imem.used=0;
    unbind_fail=1;
    accel_slot=&accel_ops_a;
    CHECK(!geforce_detach(0) && unbinds==1);
    CHECK(!accel_slot);
    unbind_fail=0;CHECK(geforce_detach(0) && unbinds==2);
    CHECK(!gf4_data_bind(&context.display_data,&context.display_state));
    struct module_request q=request();
    q.lifecycle=(struct module_lifecycle){.start=geforce_start,.detach=geforce_detach};
    CHECK(!gfx_backend_geforce.probe());
    CHECK(!module_load(&q,&geforce_handle));
    struct module_record *r=module_record_get(geforce_handle);
    r->init=(u32)(uptr)publishing_init;
    r->text=(u32)(uptr)probe & ~(PAGE_SIZE-1); r->text_size=16*PAGE_SIZE;
    /* A live STARTING record makes a missing hook check reach registration,
     * so either bad hook must fail before taking a registration reference. */
    r->state=MODULE_STARTING;
    geforce_backend_ops.fill_rect=(void *)(uptr)(r->text+r->text_size);
    CHECK(geforce_start(r,r->init,0)==MODULE_INIT_FAILED && !accel_slot);
    CHECK(!registered && !r->registration_refs);
    geforce_backend_ops.fill_rect=fill;
    geforce_backend_ops.blit=(void *)(uptr)(r->text+r->text_size);
    CHECK(geforce_start(r,r->init,0)==MODULE_INIT_FAILED && !accel_slot);
    CHECK(!registered && !r->registration_refs);
    geforce_backend_ops.blit=blit;
    r->state=MODULE_RELOCATED;
    if (input[1]==1) geforce_backend_ops.prepare=0;
    if (input[1]==2) geforce_backend_ops.fill_rect=(void *)(uptr)(r->text+r->text_size);
    int rc=module_start(geforce_handle);
    CHECK(!probes && !enables); /* KDEBUG=0 registration never starts hardware. */
    if (input[1]) {
        CHECK(rc==MODULE_INIT_FAILED && !registered && !r->registration_refs);
        CHECK(!accel_slot && !geforce_accel_ops());
        CHECK(!gfx_backend_geforce.probe()); empty(); finish(0);
    }
    CHECK(!rc && registered && r->registration_refs==1 && return_registered==1);
    production_bind();
    CHECK(geforce_foundation_ops()==&gf1_module_ops);
    CHECK(geforce_accel_ops()==&accel_ops_a);
    /* Model the service's stop/recover stores through the actual Context. */
    *geforce_context->accel_out=0;
    CHECK(!geforce_accel_ops());
    *geforce_context->accel_out=&accel_ops_b;
    CHECK(geforce_accel_ops()==&accel_ops_b);
    CHECK(!geforce_detach(r) && !geforce_accel_ops() && !accel_slot);
    *geforce_context->accel_out=&accel_ops_a;
    CHECK(geforce_accel_ops()==&accel_ops_a);
    gfx_backend_geforce=candidate;
    CHECK(geforce_context->backend==&gfx_backend_geforce);
    CHECK(gfx_backend_geforce.prepare && gfx_backend_geforce.fill_rect==fill && gfx_backend_geforce.blit==blit);
    CHECK(gfx_backend_geforce.probe()); gfx_backend_geforce.init();
    CHECK(probes==1 && enables==1);
    u32 before=returned;
    CHECK(geforce_module_abort()==MODULE_ISOLATED && returned==before && registered);
    CHECK(!accel_slot && !geforce_accel_ops());
    struct gf_foundation diagnostic={.running=true};
    struct gf1_test_request stop={sizeof(stop),GF1T_STOP,0};
    struct gf1_test_reply reply;
    gf1_module_ops.call=diagnostic_call;
    CHECK(geforce_foundation_ops()==&gf1_module_ops);
    if (!geforce_foundation_ops()) finish(1);
    CHECK(geforce_foundation_ops()->call(&diagnostic,&stop,&reply)==GF1_EIO);
    CHECK(diagnostic.running && diagnostic_stops==1);
    CHECK(geforce_module_abort()==MODULE_ISOLATED && registered);
    CHECK(geforce_foundation_ops()==&gf1_module_ops);
    if (!geforce_foundation_ops()) finish(1);
    diagnostic_fail=0;
    CHECK(!geforce_foundation_ops()->call(&diagnostic,&stop,&reply));
    CHECK(!diagnostic.running && diagnostic_stops==2 && returned==before);
    finish(0);
}
