/* Real policy with observable surface revoke/regen and appslot/GUI inputs. */
#include "gfx_hal.h"
#include "gfx_policy.h"
#include "pgalloc.h"
#include "../../exec/appslot.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)

/* Hooks are identities, never invoked by policy. */
static int fill(int x,int y,int w,int h,u8 c)
{ (void)x;(void)y;(void)w;(void)h;(void)c;return 0; }
static int copy(int x,int y,int sx,int sy,int w,int h)
{ (void)x;(void)y;(void)sx;(void)sy;(void)w;(void)h;return 0; }
static int rop(int x,int y,int w,int h,u8 c,u8 r)
{ (void)r;return fill(x,y,w,h,c); }
static int put(int x,int y,int w,int h,const u8 *s,u32 p,int k)
{ (void)s;(void)p;(void)k;return fill(x,y,w,h,0); }
static int get(int x,int y,int w,int h,u8 *s,u32 p)
{ (void)s;(void)p;return fill(x,y,w,h,0); }
static int mono(int x,int y,int w,int h,const u8 *s,u32 p,u8 f,int b,
                int cx,int cy,int cw,int ch)
{ (void)cx;(void)cy;(void)cw;(void)ch;return put(x,y,w,h,s,p,f+b); }
static int logs;
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; logs++; }
static int gui, owner = GFX_OWNER_WM, ok = 1, events, mapped = 1, token = 1;
static u16 expected_gen = 1;
static GfxBackend be;
GfxBackend *g_backend = &be;
int con_sink_is_enabled(void) { return gui; }
int appslot_gfx_owner(void) { return owner; }
static int direct_ok(void) { return ok; }
void gfx_reinit_surface_roles(int done,int display_only)
{
    CHECK(!display_only);
    CHECK(be.policy == GFX_POLICY_SHADOW);
    CHECK(be.fill_rect == be.shadow_fill_rect && be.blit == be.shadow_blit && !be.put);
    CHECK(gfx_policy_gen == expected_gen);
    CHECK(done == events % 2);
    if (!done) mapped = 0;
    else { CHECK(!mapped); token = 0; }
    events++;
}
static void direct_hooks(const GfxDirectOps *d)
{
    CHECK(be.policy == GFX_POLICY_DIRECT);
    CHECK(be.fill_rect == d->fill_rect && be.blit == d->blit);
    CHECK(be.fill_rop == d->fill_rop && be.put == d->put);
    CHECK(be.get == d->get && be.mono == d->mono);
}
static void shadow_hooks(void)
{
    CHECK(be.policy == GFX_POLICY_SHADOW);
    CHECK(be.fill_rect == be.shadow_fill_rect && be.blit == be.shadow_blit);
    CHECK(!be.fill_rop && !be.put && !be.mono && !be.get);
}
void _start(void)
{
    GfxDirectOps d = {fill,copy,rop,put,mono,get};
    CHECK(gfx_policy_gen == 1 && !gfx_scan_changed);
    g_backend = 0;
    CHECK(!gfx_direct_wanted() && !gfx_policy_blocks_map(LEDGER_ROLE_CLIENT));
    gfx_policy_reeval();
    g_backend = &be;
    gui = 1;
    CHECK(!gfx_direct_wanted());
    be.direct_ops = &d;
    CHECK(!gfx_direct_wanted());
    be.direct_ok = direct_ok;
    CHECK(gfx_direct_wanted()); /* active SHADOW hooks are NULL */
#define MISSING(member, fn) do { d.member = 0; CHECK(!gfx_direct_wanted()); d.member = fn; } while (0)
    MISSING(fill_rect, fill); MISSING(blit, copy); MISSING(fill_rop, rop);
    MISSING(put, put); MISSING(get, get);
    d.mono = 0; CHECK(gfx_direct_wanted()); d.mono = mono;
    ok = 0; CHECK(!gfx_direct_wanted()); ok = 1;
    gui = 0; CHECK(!gfx_direct_wanted()); gui = 1;
    owner = APP_ID_MIN; CHECK(gfx_fullscreen_owner() && !gfx_direct_wanted());
    gfx_scan_changed = 1;
    gfx_policy_reeval();
    CHECK(be.policy == GFX_POLICY_SHADOW && !events && mapped && token);
    CHECK(gfx_policy_gen == 2 && !gfx_scan_changed);
    gfx_policy_reeval(); CHECK(gfx_policy_gen == 2);
    owner = GFX_OWNER_WM;
    CHECK(!gfx_fullscreen_owner());
    gfx_policy_gen = 1;
    gfx_scan_changed = 1;
    gfx_policy_reeval();
    CHECK(events == 2 && !mapped && !token);
    CHECK(gfx_policy_gen == 2 && !gfx_scan_changed);
    direct_hooks(&d);
    CHECK(gfx_policy_blocks_map(LEDGER_ROLE_CLIENT));
    CHECK(gfx_policy_blocks_map(LEDGER_ROLE_DISPLAY));
    CHECK(!gfx_policy_blocks_map(LEDGER_ROLE_TVRAM));
    CHECK(!gfx_policy_blocks_map(0xffffffffU));
    gfx_policy_reeval(); CHECK(gfx_policy_gen == 2 && events == 2);
    gfx_policy_gen = 65535;
    gfx_scan_changed = 1; gfx_policy_reeval();
    CHECK(gfx_policy_gen == 1 && !gfx_scan_changed && events == 2);
    direct_hooks(&d);
    be.shadow_fill_rect = fill; be.shadow_blit = copy;
    owner = APP_ID_MIN;
    gfx_policy_reeval();
    shadow_hooks(); CHECK(gfx_policy_gen == 2 && events == 2);
    CHECK(!gfx_policy_blocks_map(LEDGER_ROLE_CLIENT));
    CHECK(!gfx_policy_blocks_map(LEDGER_ROLE_DISPLAY));
    /* Same SHADOW scan change preserves the fullscreen mapping/token. */
    mapped = token = 1;
    gfx_scan_changed = 1; gfx_policy_reeval();
    CHECK(mapped && token && events == 2 && gfx_policy_gen == 3 && !gfx_scan_changed);
    gfx_policy_reeval(); CHECK(gfx_policy_gen == 3);
    /* Saved hooks still allow reentry after active hooks were cleared. */
    owner = GFX_OWNER_WM; CHECK(gfx_direct_wanted());
    expected_gen = gfx_policy_gen;
    d.mono = 0;
    gfx_policy_reeval(); direct_hooks(&d);
    CHECK(events == 4 && !mapped && !token && gfx_policy_gen == 4);
    owner = APP_ID_MIN;
    gfx_policy_reeval(); shadow_hooks(); CHECK(gfx_policy_gen == 5);
    owner = GFX_OWNER_WM;
    gfx_policy_gen = 3;
    /* Other reasons to leave DIRECT use the same publication path. */
    be.policy = GFX_POLICY_DIRECT; gui = 0;
    gfx_policy_reeval(); shadow_hooks(); CHECK(gfx_policy_gen == 4);
    gui = 1; be.policy = GFX_POLICY_DIRECT; ok = 0;
    gfx_policy_reeval(); shadow_hooks(); CHECK(gfx_policy_gen == 5);
    ok = 1; be.policy = GFX_POLICY_DIRECT; be.direct_ops = 0;
    gfx_policy_reeval(); shadow_hooks(); CHECK(gfx_policy_gen == 6 && logs == 1);
    gfx_policy_reeval(); CHECK(logs == 1);
    finish(0);
}
