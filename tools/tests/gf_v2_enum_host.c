#include "drivers/video/geforce/gf_v2.h"
#include "hwdev/gf/display/api.h"
static struct gf4_state state;
static struct gf4_route routes[3];
static struct gf4_ctx c;
static struct gf4_test_ops ops;
static int fail;
const struct gf4_test_ops *gf_v2_disp(void) { return &ops; }
int gf_v2_deadline(struct gf2_chan *ch, u32 us, u32 *dl) { (void)ch; (void)us; *dl=1; return 0; }
int gf_v2_rc(int rc) { return rc; }
int gf4_ddc_probe(struct gf4_ctx *ctx, u8 bus, u32 dl) { (void)ctx; (void)bus; (void)dl; return fail; }
static int read_edid(struct gf4_ctx *ctx, u8 bus, u8 block, u8 *b, u32 dl)
{
    u32 i; (void)ctx; (void)bus; (void)block; (void)dl;
    for (i=0;i<128;i++) b[i]=0;
    for (i=1;i<7;i++) b[i]=255;
    b[20]=128; b[126]=2; return 0;
}
#define CHECK(x,n) do { if (!(x)) return n; } while (0)
static int test(void)
{
    GFX2_ConnectorInfo a,b; GFX2_ModeInfo m; GFX2_HeadInfo h;
    ops.ctx=&c; ops.ddc_read=read_edid; c.state=&state; c.routes=routes; c.nroutes=3;
    routes[0]=(struct gf4_route){.connector=4,.type=GF4_ANALOG,.head_mask=1};
    routes[1]=(struct gf4_route){.connector=4,.type=GF4_TMDS,.head_mask=1};
    routes[2]=(struct gf4_route){.connector=5,.type=GF4_TV,.head_mask=2};
    CHECK(!gf_v2_enum_conn(0,0,&a) && !gf_v2_enum_conn(0,1,&b),1);
    CHECK(a.type==GFX2_CONN_DVI_I && b.type==GFX2_CONN_DVI_I && a.status==GFX2_CONN_OFF && b.status==GFX2_CONN_ON && b.edid_blocks==3 && !b.clone_mask,2);
    fail=1; CHECK(!gf_v2_enum_conn(0,0,&a) && !gf_v2_enum_conn(0,1,&b) && a.status==GFX2_CONN_UNKNOWN && b.status==GFX2_CONN_OFF,3);
    c.tv_sense_valid=1; c.tv_pin_mask=1; c.tv_subconnector=2;
    CHECK(!gf_v2_enum_conn(0,2,&a) && a.type==GFX2_CONN_TV_SVIDEO && a.status==GFX2_CONN_ON,4);
    state.nmodes=2; state.mode_connector[0]=4; state.mode_connector[1]=5;
    state.modes[0]=(struct gf4_mode){.id=7,.fmt=GF3_INDEX8,.t={.khz=25175,.hd=640,.vd=480,.ht=800,.vt=525}};
    state.modes[1]=state.modes[0]; state.modes[1].id=8;
    CHECK(!gf_v2_enum_mode(0,0,1,0,&m) && m.id==7 && m.refresh_mhz==59940,5);
    m.id=123; CHECK(gf_v2_enum_mode(0,0,1,1,&m)==OS32_ERR_INVAL && m.id==123,6);
    CHECK(!gf_v2_enum_mode(0,1,1,0,&m) && m.id==8,7);
    CHECK(!gf_v2_enum_head(0,0,&h) && h.max_w==4096 && !h.attached,8);
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1),"b"(rc)); __builtin_unreachable(); }
