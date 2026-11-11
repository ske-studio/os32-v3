/* Execute the shipped guest against fake hardware/KAPI edges and real C16.
 * Trident mode changes revoke DISPLAY leases and replace the alias SID. */
#include "gfx2_int.h"
#define main guest_main
#include "../../userland/tests/gfx2_i3.c"
#undef main
#include <stdarg.h>
#include "kstring.h"
static u32 kind, token_live, display_sid, scan_sid, next_sid, mode_calls, freed;
static int inject, stage_errors, released_before_mode, alias_passed;
static GfxBackend v1;
static struct gfx2_adapter adapter = {.v1=&v1};
static void say(const char *s)
{
    u32 n=0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void stop(int rc)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n"); stop(1); } } while (0)
static int contains(const char *s, const char *needle)
{
    for (; *s; s++) {
        u32 n=0; while (needle[n] && s[n]==needle[n]) n++;
        if (!needle[n]) return 1;
    }
    return 0;
}
static void print(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (contains(fmt, "stage=")) {
        va_list ap; va_start(ap,fmt); const char *stage=va_arg(ap,const char *);
        say(stage); say("\n"); stage_errors++; va_end(ap);
    }
}
static void init(void) {}
static void shutdown(void) {}
static int adapters(void) { return 1; }
static int info(u32 a, GFX2_AdapterInfo *o)
{ (void)a; *o=(GFX2_AdapterInfo){.flags=GFX2_AF_ACTIVE,.backend_kind=kind,.heads=1,.connectors=1}; return 0; }
static int caps(u32 a, GFX2_Caps *o, u32 n)
{
    (void)a; CHECK(n==sizeof(*o)); *o=(GFX2_Caps){0};
    if (kind!=OS32_SURFACE_GEFORCE) {
        o->cpu_direct=1; o->sync_mode=1; kstrncpy(o->sync_profile,"test",sizeof(o->sync_profile));
    }
    return 0;
}
static int head(u32 a,u32 h,GFX2_HeadInfo *o)
{ (void)a;(void)h; *o=(GFX2_HeadInfo){.scanout_sid=scan_sid}; return 0; }
static int conn(u32 a,u32 c,GFX2_ConnectorInfo *o)
{ (void)a;(void)c; *o=(GFX2_ConnectorInfo){.possible_heads=1,.status=GFX2_CONN_ON}; return 0; }
static int count(u32 a,u32 h,u32 f) { (void)a;(void)h;(void)f; return kind==OS32_SURFACE_GEFORCE ? 1 : 2; }
static int mode(u32 a,u32 h,u32 f,u32 i,GFX2_ModeInfo *o)
{ (void)a;(void)h; if (i>=(u32)count(a,h,f)) return OS32_ERR_INVAL; *o=(GFX2_ModeInfo){.id=7+i,.w=(kind!=OS32_SURFACE_GEFORCE && !i) ? 32 : 64,.h=4,.fmt=f}; return 0; }
static int query(u32 role,OS32_SurfaceQueryResult *o)
{ CHECK(role==OS32_SURFACE_DISPLAY); *o=(OS32_SurfaceQueryResult){.count=1}; return 0; }
static int lease(u32 r,const OS32_SurfaceRef *ref,u32 access,OS32_LeaseView *o)
{ (void)ref; CHECK(r==OS32_SURFACE_DISPLAY && access==OS32_SURFACE_RW && !token_live); o->token=++next_sid; token_live=o->token; return 0; }
static int unlease(u32 token)
{
    if (token!=token_live || !token) return OS32_ERR_INVAL;
    token_live=0; if (kind==OS32_SURFACE_GEFORCE) scan_sid=display_sid;
    return 0;
}
static int screen(u32 a,u32 h,u32 r,u32 *sid)
{ (void)a;(void)h; CHECK(r==GFX2_ROLE_DISPLAY); *sid=display_sid; return 0; }
static int desc(u32 sid,GFX2_SurfaceDesc *o)
{ CHECK(sid==display_sid); *o=(GFX2_SurfaceDesc){.w=64,.h=4,.fmt=GFX2_FMT_INDEX8}; return 0; }
static int gen(u32 a,u32 *g) { (void)a; *g=1; return 0; }
static int submit(u32 a,const GFX2_Op *op,u32 n,u32 g,u32 *done,u32 *fence)
{
    (void)a; CHECK(n==1 && g==1); *done=1; *fence=1;
    if (op->op==GFX2_OP_READBACK) kmemset((void *)op->data,5,128);
    return 0;
}
static int present(u32 a,u32 h,i16 x,i16 y,u16 w,u16 hh)
{ (void)a;(void)h;(void)x;(void)y;(void)w;(void)hh; return 0; }
static int create(u32 a,u16 w,u16 h,u8 f,u8 p,u8 use,u32 *sid)
{ (void)a;(void)w;(void)h;(void)f;(void)p;(void)use; *sid=++next_sid; return 0; }
static int free_surface(u32 sid) { CHECK(sid && sid!=display_sid); freed++; return 0; }
static int trident_mode(u32 id)
{
    CHECK(id==8); mode_calls++; released_before_mode=!token_live;
    token_live=0; display_sid++; /* resources.configure: old token/SID revoked */
    return inject ? OS32_ERR_IO : 0;
}
static int modeset(u32 a,u32 h,u32 cm,u32 id,u32 sid)
{
    (void)a; CHECK(id==(kind==OS32_SURFACE_GEFORCE ? 7u : 8u));
    if (kind==OS32_SURFACE_GEFORCE) { scan_sid=sid; return inject ? OS32_ERR_IO : 0; }
    alias_passed=sid==display_sid;
    GFX2_ModeInfo m={.id=id}; struct gfx2_surf s={.sid=sid};
    return gfx2_v1_shim.mode_set(&adapter,h,cm,&m,&s);
}
static int flip(u32 a,u32 h,u32 sid,u32 flags,u32 ms)
{ (void)a;(void)h;(void)flags;(void)ms; if (kind!=OS32_SURFACE_GEFORCE) return OS32_ERR_NOSYS; scan_sid=sid; return 0; }
static int blank(u32 a,u32 h,u32 after,u32 ms,u32 *seen)
{ (void)a;(void)h;(void)ms; *seen=after+1; return 0; }
static int poll(u32 a,u32 *done,u32 *failed) { (void)a; *done=1; *failed=0; return 0; }
static i32 stat(i32 a,void *o,u32 n) { (void)a; kmemset(o,0,n); return n; }
/* Unused C16 edges; table references are retained by the linker. */
int gfx2_caps_from_v1(struct gfx2_adapter *a,GFX2_Caps *o) { (void)a;(void)o; return 0; }
int gfx2_v1_enum_head(const GfxBackend *b,u32 h,GFX2_HeadInfo *o) { (void)b;(void)h;(void)o; return 0; }
int gfx2_v1_enum_conn(const GfxBackend *b,u32 c,GFX2_ConnectorInfo *o) { (void)b;(void)c;(void)o; return 0; }
int gfx2_v1_enum_mode(const GfxBackend *b,u32 h,u32 f,u32 i,GFX2_ModeInfo *o) { (void)b;(void)h;(void)f;(void)i;(void)o; return 0; }
void _start(void)
{
    KernelAPI api={.gfx_init=init,.gfx_shutdown=shutdown,.kprintf=print,
        .gfx2_adapter_count=adapters,.gfx2_adapter_info=info,.gfx2_caps=caps,
        .gfx2_head_info=head,.gfx2_connector_info=conn,.gfx2_mode_count=count,.gfx2_mode_get=mode,
        .surface_query=query,.surface_lease=lease,.surface_unlease=unlease,
        .gfx2_screen_surface=screen,.gfx2_surface_desc=desc,.gfx2_adapter_gen=gen,
        .gfx2_submit=submit,.gfx2_present=present,.gfx2_surface_create=create,
        .gfx2_surface_free=free_surface,.gfx2_mode_set=modeset,.gfx2_flip=flip,
        .gfx2_vblank_wait=blank,.gfx2_fence_query=poll,.mem_stat=stat};
    const u32 kinds[]={OS32_SURFACE_TRIDENT,OS32_SURFACE_PEGC,OS32_SURFACE_GEFORCE};
    for (u32 k=0;k<3;k++) for (inject=0;inject<2;inject++) {
        kind=kinds[k]; v1.set_mode=kind==OS32_SURFACE_TRIDENT ? trident_mode : 0;
        token_live=mode_calls=freed=stage_errors=released_before_mode=alias_passed=0;
        display_sid=scan_sid=1; next_sid=10;
        int rc=guest_main(0,0,&api);
        int failure=inject && kind!=OS32_SURFACE_PEGC;
        CHECK(rc==failure && stage_errors==failure && !token_live && freed==2);
        if (kind==OS32_SURFACE_TRIDENT) CHECK(mode_calls==1 && released_before_mode && alias_passed);
    }
    say("GREEN shipped I3: Trident C16/revoke, PEGC NOSYS, GF scanout, mode failure cleanup\n");
    stop(0);
}
