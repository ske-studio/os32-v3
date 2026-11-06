/* C3 module/Context integration. UNIT 14: link real F3 service, V0 and
 * F4 service/links/save, with the remaining display product dependencies.
 * No service/owner/init stubs. Reuses the existing libc-free module fixture;
 * input is test_module_runtime.image() prefixed by two zero u32 words.
 * Slot expectations follow GF_FEAT4_DESIGN C3-slot/X3/N1/A0z, not nouveau:
 * the dispatch boundary is OS32-specific and introduces no HW sequence.
 * Independent wire expectations: Context v5, one initialize export; limits
 * 160/160/208 KiB from the C3-ABI card. Run after PM schema/import generation.
 */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "../../drivers/video/geforce/geforce_module.c"
#include "../../drivers/video/geforce/geforce_body.c"
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
/* Keep checks observable even when a getter/slot store is inlined. */
#undef CHECK
#define CHECK(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) { \
    const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); \
    finish(1); \
} } while (0)
static const struct gf3_test_ops accel_ops_a;
struct gf1_test_ops gf1_module_ops;
static struct gf_foundation *bound_foundation;
static struct gf2_chan *bound_chan;
void gf2_test_bind_production(struct gf_foundation *f,struct gf2_chan *ch)
{ bound_foundation=f;bound_chan=ch; }
static void production_bind(void)
{ CHECK(bound_foundation==&production.foundation && bound_chan==&production.chan); }
static struct gf_production_resources fixture_resources;
static struct geforce_platform fixture_platform={.resources=&fixture_resources};
struct geforce_platform *geforce_platform_get(void) { return &fixture_platform; }
void geforce_backend_wc_bind(int (*start)(void)) { (void)start; }
void geforce_backend_stop_bind(int (*stop)(void)) { (void)stop; }
int gf_production_prepare(struct gf_production *p,const struct gf_production_resources *b,
                         int (*r)(void *),void *o) { (void)b;(void)r;(void)o;p->prepared=true;return 0; }
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
static int c3_mode(u32 id) { (void)id; return 0; }
static int c3_dpms(u8 head,u32 state) { (void)head;(void)state;return 0; }
static int c3_image(u8 head,const struct GfxCursorImage *cursor)
{ (void)head;(void)cursor;return 0; }
static int c3_move(u8 head,i32 x,i32 y) { (void)head;(void)x;(void)y;return 0; }
static int c3_show(u8 head,int visible) { (void)head;(void)visible;return 0; }
static int c3_vsync(u8 head,u32 after,u32 dl,u32 *seen)
{ (void)head;(void)after;(void)dl;(void)seen;return 0; }
static int c3_flip(u8 head,const struct GfxScanout *surface,int sync,u32 dl)
{ (void)head;(void)surface;(void)sync;(void)dl;return 0; }
static int c3_gamma(u8 head,const u16 rgb[3][256]) { (void)head;(void)rgb;return 0; }
static const GfxMode fixture_modes[]={{640,480,GFX_FMT_PACKED8,8,0,0,640}};
GfxBackend geforce_backend_ops={.name="fixture",.probe=probe,.init=enable,.query=query,
    .shutdown=enable,.present_rect=present,.set_palette=palette,.enter=enable,.leave=enable,
    .prepare=enable,.fill_rect=fill,.blit=blit,.modes=fixture_modes,.mode_count=1,
    .set_mode=c3_mode,.dpms=c3_dpms,.cursor_image=c3_image,.cursor_move=c3_move,
    .cursor_show=c3_show,.vsync=c3_vsync,.page_flip=c3_flip,.gamma=c3_gamma};
static const struct gf4_test_ops display_ops_a, display_ops_b;
static const struct gf4_restore_ops restore_ops_a, restore_ops_b;
static int init_rc;
static int publishing_init(GfxBackend *out,const GeforceModuleContext *ctx)
{
    int rc=geforce_module_init(out,ctx);
    CHECK(!rc && ctx->accel_out==&accel_slot);
    *ctx->accel_out=&accel_ops_a;
    *ctx->display_out=&display_ops_a;
    *ctx->display_restore_out=&restore_ops_a;
    CHECK(!geforce_accel_ops()); /* Not registered yet. */
    CHECK(!geforce_display_ops() && !geforce_display_restore_ops());
    return init_rc;
}
static void start_failures(void)
{
    struct module_record r={0};
    u32 entry=(u32)(uptr)publishing_init;
    init_rc=GF3_ESTATE;
    CHECK(geforce_start(&r,entry,0)==GF3_ESTATE);
    CHECK(!accel_slot && !geforce_accel_ops());
    CHECK(!display_slot && !display_restore_slot);
    init_rc=0;
    /* Required callbacks lie outside the empty module text range. */
    CHECK(geforce_start(&r,entry,0)==MODULE_INIT_FAILED);
    CHECK(!accel_slot && !geforce_accel_ops());
    CHECK(!display_slot && !display_restore_slot);
    r.text=(u32)(uptr)probe & ~(PAGE_SIZE-1);r.text_size=16*PAGE_SIZE;
    /* Valid callbacks but no live handle: registration pin fails. */
    CHECK(geforce_start(&r,entry,0)==MODULE_INIT_FAILED);
    CHECK(!accel_slot && !geforce_accel_ops() && !registered);
}

/* Header validation is independent of the producer: preserve the existing
 * KDEBUG fixture's valid layout, change only component ID and ops version.
 * No body CRC needs recomputing for the read-only header allocation check. */
#define C3_CONTEXT_VERSION 5U
#define C3_OLD_CONTEXT_VERSION 4U
/* PM decision 23: GEFORCE budgets, docs/08_build.md:202. */
#define C3_IMAGE_LIMIT (256U * 1024U)
#define C3_MEM_LIMIT (256U * 1024U)
#define C3_SPAN_LIMIT (320U * 1024U)
static void header_word(u8 *h, u32 word, u32 value)
{
    for (u32 b=0; b<sizeof(value); b++)
        h[word * sizeof(value) + b]=(u8)(value >> (b * 8U));
}
static void abi_check(void)
{
    static const struct module_component_policy rows[]=MODULE_COMPONENT_POLICIES;
    const struct module_component_policy *gf=0;
    for (u32 i=0; i<sizeof(rows)/sizeof(rows[0]); i++)
        if (rows[i].module_id==MODULE_ID_GEFORCE) gf=&rows[i];
    CHECK(gf && gf->ops_version==C3_CONTEXT_VERSION);
    CHECK(gf->ops_slots==1 && gf->export_max==1);
    CHECK(gf->image_max==C3_IMAGE_LIMIT && gf->mem_max==C3_MEM_LIMIT);
    CHECK(gf->load_span_max==C3_SPAN_LIMIT);
    CHECK(GEFORCE_MODULE_OPS_VERSION==C3_CONTEXT_VERSION);
    CHECK(geforce_module_ops.version==C3_CONTEXT_VERSION);
    u8 h[MODULE_HEADER_SIZE];
    kmemcpy(h,file,sizeof(h));
    header_word(h,MI_ID,MODULE_ID_GEFORCE);
    header_word(h,MI_OPS_VERSION,C3_CONTEXT_VERSION);
    struct module_request q=request();
    q.policy.module_id=MODULE_ID_GEFORCE;
    q.policy.ops_version=C3_CONTEXT_VERSION;
    struct module_image_plan plan;
    CHECK(!module_image_header(h,sizeof(h),sizeof(file),&q.policy,&plan));
    header_word(h,MI_OPS_VERSION,C3_OLD_CONTEXT_VERSION);
    struct module_image_plan before=plan;
    CHECK(module_image_header(h,sizeof(h),sizeof(file),&q.policy,&plan)==MODULE_ABI_MISMATCH);
    CHECK(!gf2_host_memcmp(&before,&plan,sizeof(plan)));
    /* Even a caller offering old v4 cannot override the trusted schema. */
    q.policy.ops_version=C3_OLD_CONTEXT_VERSION;
    CHECK(module_image_header(h,sizeof(h),sizeof(file),&q.policy,&plan)==MODULE_ABI_MISMATCH);
    CHECK(!gf2_host_memcmp(&before,&plan,sizeof(plan)));
}
/* Each optional HAL entry must be NULL or reside in pinned module text. */
static void invalid_hooks(struct module_record *r)
{
    GfxBackend saved=geforce_backend_ops;
    r->state=MODULE_STARTING;
    void *outside=(void *)(uptr)(r->text+r->text_size);
    enum { MODE, DPMS, IMAGE, MOVE, SHOW, VSYNC, FLIP, GAMMA, HOOK_COUNT };
    for (u32 i=0; i<HOOK_COUNT; i++) {
        geforce_backend_ops=saved;
        switch (i) {
        case MODE: geforce_backend_ops.set_mode=outside; break;
        case DPMS: geforce_backend_ops.dpms=outside; break;
        case IMAGE: geforce_backend_ops.cursor_image=outside; break;
        case MOVE: geforce_backend_ops.cursor_move=outside; break;
        case SHOW: geforce_backend_ops.cursor_show=outside; break;
        case VSYNC: geforce_backend_ops.vsync=outside; break;
        case FLIP: geforce_backend_ops.page_flip=outside; break;
        case GAMMA: geforce_backend_ops.gamma=outside; break;
        }
        CHECK(geforce_start(r,r->init,0)==MODULE_INIT_FAILED);
        CHECK(!registered && !r->registration_refs);
        CHECK(!display_slot && !display_restore_slot && !accel_slot);
    }
    geforce_backend_ops=saved;
    r->state=MODULE_RELOCATED;
}
static void slot_lifetime(void)
{
    CHECK(context.display_out==&display_slot);
    CHECK(context.display_restore_out==&display_restore_slot);
    CHECK(context.feature5_out==&feature5_slot);
    *context.display_out=&display_ops_a;
    *context.display_restore_out=&restore_ops_a;
    CHECK(geforce_display_ops()==&display_ops_a);
    CHECK(geforce_display_restore_ops()==&restore_ops_a);
    /* C2 owns operation-time publication; each getter rereads its slot. */
    *context.display_out=0;
    *context.display_restore_out=0;
    CHECK(!geforce_display_ops() && !geforce_display_restore_ops());
    *context.display_out=&display_ops_b;
    *context.display_restore_out=&restore_ops_b;
    CHECK(geforce_display_ops()==&display_ops_b);
    CHECK(geforce_display_restore_ops()==&restore_ops_b);
    CHECK(!geforce_detach(0)); /* Registered module retains its loans. */
    CHECK(!geforce_display_ops() && geforce_display_restore_ops()==&restore_ops_b);
    CHECK(context.display_state && context.display_data);
    CHECK(geforce_module_abort()==MODULE_ISOLATED);
    CHECK(!geforce_display_ops() && geforce_display_restore_ops()==&restore_ops_b);
}
void _start(void)
{
    read_input(input,sizeof(input)); read_input(file,sizeof(file));
    kmemcpy(&imports[0].name_hash,file+152,4);
    kmemcpy(&exports[4].name_hash,file+164,4); exports[4].version=1;
    abi_check();
    context.platform=&fixture_platform;
    fixture_platform.opaque=&production;
    CHECK(!gf4_data_bind(&context.display_data,&context.display_state));
    CHECK(!geforce_display_ops() && !geforce_display_restore_ops());
    start_failures();
    struct gf3_service *service=gf3_service_get(&production.chan);
    CHECK(service && service->owner==gf4_service_owner_table());
    CHECK(production.display.display4);
    CHECK(service->owner->owner==production.display.display4);
    CHECK(production.display.display4->legacy==&production.display);
    CHECK(production.display.display4->data==context.display_data);
    CHECK(production.display.display4->state==context.display_state);
    /* Real F3 bind rejects a different slot before publication. */
    const struct gf3_test_ops *other_slot=0;
    GeforceModuleContext other=context;
    other.accel_out=&other_slot;
    GfxBackend rejected;
    CHECK(geforce_module_init(&rejected,&other)==GF3_ESTATE);
    CHECK(!other_slot && !display_slot && !display_restore_slot);
    /* Restore the global Context loan after the deliberately rejected init. */
    CHECK(!geforce_module_init(&rejected,&context));
    /* Failed pre-registration starts keep publication closed. */
    CHECK(!display_slot && !display_restore_slot && !registered);
    struct module_request q=request();
    q.lifecycle=(struct module_lifecycle){.start=geforce_start,.detach=geforce_detach};
    CHECK(!module_load(&q,&geforce_handle));
    struct module_record *r=module_record_get(geforce_handle);
    r->init=(u32)(uptr)publishing_init;
    r->text=(u32)(uptr)probe & ~(PAGE_SIZE-1);
    /* Same callback text fixture as geforce_module_host.c. */
    r->text_size=16*PAGE_SIZE;
    invalid_hooks(r);
    CHECK(!module_start(geforce_handle));
    CHECK(!probes && !enables);
    CHECK(registered && r->registration_refs==1 && return_registered==1);
    production_bind();
    slot_lifetime();
    /* Exercise the unregistered detach branch after the registration tests.
     * No module_abort follows this fixture-only registry state change. */
    registered=0;
    display_slot=&display_ops_a;
    CHECK(!geforce_display_ops() && !geforce_display_restore_ops());
    production.foundation.registered=true;
    CHECK(!geforce_detach(0));
    CHECK(!display_slot && display_restore_slot==&restore_ops_b);
    production.foundation.registered=false;
    unbind_fail=1;
    CHECK(!geforce_detach(0) && display_restore_slot==&restore_ops_b);
    unbind_fail=0;
    CHECK(geforce_detach(0));
    CHECK(!display_slot && !display_restore_slot && !feature5_slot);
    CHECK(!context.display_data && !context.display_state);
    finish(0);
}
