/* B5: independent register oracle from nouveau nvkm/subdev/fb/nv44.c:30-46
 * and nvkm/engine/gr/nv44.c:31-46. */
#define GF5_HOST_UNIT 5
#include "gf5_fake.h"
#define REQUIRE(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) return __LINE__; } while (0)
static struct gf5_host h;
static struct gf4_state state;
static struct gf5_host_event events[128];
static struct gf4_host_event io_events[128];
static struct gf_vram_reservation_node node;
static int owner;
static void setup(void)
{
    gf5_host_init(&h,&state);
    h.events=events; h.cevents=128;
    h.display.events=io_events; h.display.cevents=128;
    h.ctx.deadline=1000;
    node=(struct gf_vram_reservation_node){.value={.owner=&owner,.off=0x10000,
        .bytes=0x60000,.reservation_id=1,.generation=2}};
    h.display.base.reservations=&node;
}
static int test(void)
{
    setup();
    struct gf5_tile t={.base=0x10000,.bytes=8193,.pitch=128,.reservation_id=1,.generation=2};
    u32 slot=99;
    REQUIRE(gf5_tile_find(&h.ctx,&slot)==0 && slot==0);
    REQUIRE(gf5_tile_set(&h.ctx,0,&t)==0);
    REQUIRE(h.ctx.tile_live[0] && h.ctx.tile[0].bytes==16384);
    REQUIRE(h.display.nevents==8 && io_events[7].op==D_ADMISSION);
    const u32 offs[]={0x100604,0x100608,0x100600,0x100600,0x400908,0x400904,0x400900};
    const u32 vals[]={0x13fff,128,0x10001,0,128,0x13fff,0x10001};
    for (u32 i=0;i<7;++i) {
        REQUIRE(io_events[i].op==(i==3 ? D_READ : D_WRITE));
        REQUIRE(io_events[i].a[1]==offs[i]);
        if (i!=3) REQUIRE(io_events[i].a[3]==vals[i]);
    }
    REQUIRE(h.calls[F5_PAUSE]==1 && h.calls[F5_RESUME]==1 && h.calls[F5_ADMIT]==1);
    REQUIRE(gf5_tile_find(&h.ctx,&slot)==0 && slot==1);
    gf5_host_trace_clear(&h);
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EBUSY && h.calls[F5_PAUSE]==0);
    t.base=0x60000; t.bytes=0x10001;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EINVAL && h.ctx.source_rc==GF3_ERANGE && h.calls[F5_PAUSE]==0);
    t.base=0x20000; t.bytes=8192;
    REQUIRE(gf5_tile_set(&h.ctx,12,&t)==GF5_EINVAL);
    REQUIRE(gf5_tile_set(&h.ctx,11,&t)==0 && h.ctx.tile_live[11]);
    gf5_host_trace_clear(&h);
    REQUIRE(gf5_tile_clear(&h.ctx,11)==0 && !h.ctx.tile_live[11]);
    REQUIRE(h.display.nevents==8 && io_events[7].op==D_ADMISSION && io_events[0].a[1]==0x1006b4 && io_events[6].a[1]==0x4009b0);
    for (u32 i=0;i<7;++i) if (i!=3) REQUIRE(io_events[i].a[3]==0);
    gf5_host_trace_clear(&h);
    gf4_host_fail_at(&h.display,D_WRITE,2,-1);
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EIO);
    REQUIRE(!h.ctx.tile_live[1] && h.ctx.tile_live[0] && h.ctx.phase==GF5_FAULT && h.ctx.source_rc==-1);
    REQUIRE(h.calls[F5_RESUME]==0 && h.calls[F5_ADMIT]==0 && h.display.nevents==2);
    setup(); gf5_host_fail_at(&h,F5_PAUSE,1,GF2_ETIME);
    REQUIRE(gf5_tile_set(&h.ctx,0,&t)==GF5_ETIME);
    REQUIRE(!h.ctx.tile_live[0] && h.display.nevents==0 && h.calls[F5_RESUME]==0);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
