#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "../../drivers/video/geforce/geforce_module.c"
#include "../../drivers/video/geforce/geforce_body.c"
static struct geforce_platform fixture_platform;
struct geforce_platform *geforce_platform_get(void) { return &fixture_platform; }
void geforce_backend_stop_bind(int (*stop)(void)) { (void)stop; }
int gf_production_prepare(struct gf_production *p,const struct gf_production_resources *b,
                         int (*r)(void *),void *o) { (void)p;(void)b;(void)r;(void)o;return -1; }
int gf_production_identity(struct gf_production *p) { (void)p;return -1; }
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
static u32 probes, enables;
static int probe(void) { probes++; return 1; }
static void enable(void) { enables++; }
static int query(GFX_ScreenInfo *s) { (void)s; return 0; }
static void present(int x,int y,int w,int h) { (void)x;(void)y;(void)w;(void)h; }
static void palette(int x,int n,const u8 *p) { (void)x;(void)n;(void)p; }
void geforce_backend_bind(struct video_device *d,int (*i)(void),u32 bar,int (*p)(void *),void *o)
{ (void)d;(void)i;(void)bar;(void)p;(void)o; }
static const GfxMode fixture_modes[]={{640,480,GFX_FMT_PACKED8,8,0,0,640}};
GfxBackend geforce_backend_ops={.name="fixture",.probe=probe,.init=enable,.query=query,
    .shutdown=enable,.present_rect=present,.set_palette=palette,.enter=enable,.leave=enable,
    .prepare=enable,.modes=fixture_modes,.mode_count=1};
void _start(void)
{
    read_input(input,sizeof(input)); read_input(file,sizeof(file));
    kmemcpy(&imports[0].name_hash,file+152,4);
    kmemcpy(&exports[4].name_hash,file+164,4); exports[4].version=1;
    context.platform=&fixture_platform;
    struct module_request q=request();
    q.lifecycle=(struct module_lifecycle){.start=geforce_start,.detach=geforce_detach};
    CHECK(!gfx_backend_geforce.probe());
    CHECK(!module_load(&q,&geforce_handle));
    struct module_record *r=module_record_get(geforce_handle);
    r->init=(u32)(uptr)geforce_module_init;
    r->text=(u32)(uptr)probe & ~(PAGE_SIZE-1); r->text_size=16*PAGE_SIZE;
    if (input[1]==1) geforce_backend_ops.prepare=0;
    if (input[1]==2) geforce_backend_ops.fill_rect=(void *)enable;
    int rc=module_start(geforce_handle);
    CHECK(!probes && !enables); /* KDEBUG=0 registration never starts hardware. */
    if (input[1]) {
        CHECK(rc==MODULE_INIT_FAILED && !registered && !r->registration_refs);
        CHECK(!gfx_backend_geforce.probe()); empty(); finish(0);
    }
    CHECK(!rc && registered && r->registration_refs==1 && return_registered==1);
    gfx_backend_geforce=candidate;
    CHECK(geforce_context->backend==&gfx_backend_geforce);
    CHECK(gfx_backend_geforce.prepare && !gfx_backend_geforce.fill_rect && !gfx_backend_geforce.blit);
    CHECK(gfx_backend_geforce.probe()); gfx_backend_geforce.init();
    CHECK(probes==1 && enables==1);
    u32 before=returned;
    CHECK(geforce_module_abort()==MODULE_ISOLATED && returned==before && registered);
    finish(0);
}
