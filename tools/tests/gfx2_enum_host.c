#include "gfx2_int.h"
#include "kstring.h"
static struct gfx2_adapter a;
static GfxBackend v1;
static GfxMode modes[] = {{640, 480, 1, 8, 0, 0, 640}, {800, 600, 1, 8, 7, 0, 800}};
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i == 0 ? &a : 0; }
static int head(void *c, u32 h, GFX2_HeadInfo *o) { (void)c; (void)h; *o = (GFX2_HeadInfo){.attached=1, .connector_mask=2}; return 0; }
static int conn(void *c, u32 h, GFX2_ConnectorInfo *o) { (void)c; (void)h; *o = (GFX2_ConnectorInfo){0}; return 0; }
static int calls;
static int mode(void *c, u32 h, u32 f, u32 i, GFX2_ModeInfo *o) { (void)c; calls++; return gfx2_v1_enum_mode(&v1,h,f,i,o); }
static int edid(void *c, u32 h, u32 b, u8 *o) { (void)c; (void)h; (void)b; (void)o; return 0; }
static const GfxBackendV2 be = {.enum_head=head, .enum_conn=conn, .enum_mode=mode, .edid=edid};
int main(void)
{
    GFX2_ModeInfo m = {0}; GFX2_HeadInfo h = {0}; GFX2_ConnectorInfo c = {0};
    a.v1=&v1; a.be=&be; a.heads=1; a.caps.connectors=1;
    v1.modes=modes; v1.mode_count=2;
    if (gfx2_mode_count(0,0,1)!=2 || gfx2_mode_get(0,0,1,1,&m) || m.id!=7) return 1;
    m.id=123; calls=0;
    if (gfx2_mode_get(0,0,1,2,&m)!=OS32_ERR_INVAL || m.id!=123 || calls!=3) return 2;
    h.head=123;
    if (gfx2_head_info(0,1,&h)!=OS32_ERR_INVAL || h.head!=123) return 3;
    if (gfx2_head_info(0,0,&h) || !h.attached || h.connector_mask!=2) return 6;
    a.head[0].conn_mask=4; /* State written by gfx2_mode_set. */
    if (gfx2_head_info(0,0,&h) || h.connector_mask!=4) return 7;
    a.kind=GFX2_KIND_PC98;
    if (gfx2_v1_enum_conn(&v1,0,&c) || c.type!=GFX2_CONN_PC98_ANALOG || c.status!=GFX2_CONN_ON) return 4;
    a.kind=GFX2_KIND_TRIDENT;
    if (gfx2_v1_enum_conn(&v1,0,&c) || c.type!=GFX2_CONN_VGA) return 5;
    return 0;
}
void _start(void) { int rc=main(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
