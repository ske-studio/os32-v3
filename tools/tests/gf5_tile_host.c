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
/* nouveau nouveau_bo.c:178-188: base alignment and pitch*64 row rounding. */
#define TEST_ALIGNMENT 65536U
#define TEST_PITCH 128U
#define TEST_ROW_BYTES (TEST_PITCH * 64U)
#define TEST_ROUND_INPUT (TEST_ROW_BYTES + 1U)
#define TEST_ROUNDED_BYTES (2U * TEST_ROW_BYTES)
#define TEST_BASE TEST_ALIGNMENT
#define TEST_ID 1U
#define TEST_GENERATION 2U
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
static int same_tile(const struct gf5_tile *a,const struct gf5_tile *b)
{
    return a->base==b->base && a->bytes==b->bytes && a->pitch==b->pitch &&
        a->reservation_id==b->reservation_id && a->generation==b->generation;
}
static int boundaries(void)
{
    setup();
    node.value.bytes=GF5_TILE_COUNT*TEST_ALIGNMENT;
    struct gf5_tile t={.base=TEST_BASE,.bytes=TEST_ROW_BYTES,.pitch=TEST_PITCH,
        .reservation_id=TEST_ID,.generation=TEST_GENERATION};
    u32 slot;
    for (u32 i=0;i<GF5_TILE_COUNT;++i) {
        REQUIRE(gf5_tile_find(&h.ctx,&slot)==0 && slot==i);
        t.base=TEST_BASE+i*TEST_ALIGNMENT;
        REQUIRE(gf5_tile_set(&h.ctx,slot,&t)==0 && h.ctx.tile_live[i]);
        gf5_host_trace_clear(&h);
    }
    slot=GF5_TILE_COUNT;
    REQUIRE(gf5_tile_find(&h.ctx,&slot)==GF5_EBUSY && slot==GF5_TILE_COUNT);
    REQUIRE(h.display.nevents==0 && h.calls[F5_PAUSE]==0);
    const u32 reused=GF5_TILE_COUNT/2U;
    REQUIRE(gf5_tile_clear(&h.ctx,reused)==0);
    REQUIRE(gf5_tile_find(&h.ctx,&slot)==0 && slot==reused);
    t.base=TEST_BASE+reused*TEST_ALIGNMENT;
    REQUIRE(gf5_tile_set(&h.ctx,slot,&t)==0 && h.ctx.tile_live[slot]);

    setup();
    t.base=TEST_BASE; t.bytes=TEST_ROW_BYTES;
    REQUIRE(gf5_tile_set(&h.ctx,0,&t)==0);
    struct gf5_tile saved=h.ctx.tile[0];
    gf5_host_trace_clear(&h);
    t.base=TEST_BASE+1U;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EINVAL);
    REQUIRE(h.display.nevents==0 && h.calls[F5_PAUSE]==0 && !h.ctx.tile_live[1]);
    t.base=TEST_BASE+TEST_ALIGNMENT; t.generation=TEST_GENERATION-1U;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_ESTATE);
    REQUIRE(h.display.nevents==0 && h.calls[F5_PAUSE]==0 && !h.ctx.tile_live[1]);
    t.generation=TEST_GENERATION; t.base=U32_MAX-(TEST_ALIGNMENT-1U);
    t.bytes=TEST_ALIGNMENT+1U;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EINVAL);
    REQUIRE(h.display.nevents==0 && h.calls[F5_PAUSE]==0 && !h.ctx.tile_live[1]);
    t.base=TEST_BASE; t.bytes=U32_MAX;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==GF5_EINVAL);
    REQUIRE(h.display.nevents==0 && h.calls[F5_PAUSE]==0 && !h.ctx.tile_live[1]);
    t.base=TEST_BASE+TEST_ALIGNMENT; t.bytes=TEST_ROW_BYTES;
    REQUIRE(gf5_tile_set(&h.ctx,1,&t)==0 && h.ctx.tile_live[1]);
    REQUIRE(same_tile(&h.ctx.tile[0],&saved));
    return 0;
}
static int failures(void)
{
    /* Each index is a distinct boundary, including both live update and clear. */
    for (u32 clear=0;clear<2;++clear) for (u32 stage=0;stage<6;++stage) {
        setup();
        struct gf5_tile t={.base=TEST_BASE,.bytes=TEST_ROW_BYTES,.pitch=TEST_PITCH,
            .reservation_id=TEST_ID,.generation=TEST_GENERATION};
        REQUIRE(gf5_tile_set(&h.ctx,0,&t)==0);
        struct gf5_tile saved=h.ctx.tile[0];
        struct gf_vram_reservation reservation=node.value;
        gf5_host_trace_clear(&h);
        if (stage==0) gf5_host_fail_at(&h,F5_PAUSE,1,GF2_ETIME);
        if (stage==1) gf4_host_fail_at(&h.display,D_WRITE,2,GF2_ETIME);
        if (stage==2) gf4_host_fail_at(&h.display,D_READ,1,GF2_ETIME);
        if (stage==3) gf4_host_fail_at(&h.display,D_WRITE,4,GF2_ETIME);
        if (stage==4) gf5_host_fail_at(&h,F5_RESUME,1,GF2_ETIME);
        if (stage==5) gf5_host_fail_at(&h,F5_ADMIT,1,GF2_ETIME);
        t.base+=TEST_ALIGNMENT;
        i32 rc=clear ? gf5_tile_clear(&h.ctx,0) : gf5_tile_set(&h.ctx,0,&t);
        REQUIRE(rc==GF5_ETIME && h.ctx.source_rc==GF2_ETIME);
        REQUIRE(h.ctx.phase==GF5_FAULT && !h.ctx.accepting && !h.display.ctx.accepting);
        REQUIRE(h.ctx.tile_live[0] && same_tile(&h.ctx.tile[0],&saved));
        REQUIRE(h.display.base.reservations==&node && node.next==0 &&
            node.value.owner==reservation.owner && node.value.off==reservation.off &&
            node.value.bytes==reservation.bytes && node.value.reservation_id==reservation.reservation_id &&
            node.value.generation==reservation.generation);
        REQUIRE(h.calls[F5_PAUSE]==1 && h.calls[F5_RESUME]==(stage>=4) &&
            h.calls[F5_ADMIT]==(stage==5));
        const u32 expected_events[]={0,2,4,5,7,7};
        REQUIRE(h.display.nevents==expected_events[stage]);
    }
    return 0;
}
static int test(void)
{
    int rc=boundaries(); if (rc) return rc;
    rc=failures(); if (rc) return rc;
    setup();
    struct gf5_tile t={.base=TEST_BASE,.bytes=TEST_ROUND_INPUT,.pitch=TEST_PITCH,.reservation_id=TEST_ID,.generation=TEST_GENERATION};
    u32 slot=99;
    REQUIRE(gf5_tile_find(&h.ctx,&slot)==0 && slot==0);
    REQUIRE(gf5_tile_set(&h.ctx,0,&t)==0);
    REQUIRE(h.ctx.tile_live[0] && h.ctx.tile[0].bytes==TEST_ROUNDED_BYTES);
    REQUIRE(h.display.nevents==8 && io_events[7].op==D_ADMISSION);
    const u32 offs[]={0x100604,0x100608,0x100600,0x100600,0x400908,0x400904,0x400900};
    const u32 vals[]={TEST_BASE+TEST_ROUNDED_BYTES-1U,TEST_PITCH,TEST_BASE|1U,0,TEST_PITCH,TEST_BASE+TEST_ROUNDED_BYTES-1U,TEST_BASE|1U};
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
    t.base=TEST_BASE+TEST_ALIGNMENT; t.bytes=TEST_ROW_BYTES;
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
