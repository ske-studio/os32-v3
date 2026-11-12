#define GF3_HOST_UNIT 1
#include "gf_accel_host.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static struct gf3_host h;
static struct gf3_host_event events[64];
static struct gf3_host_packet packets[8];
static u32 words[32];
static struct gf_vram_reservation_node node;
static int owner;
static int range(void *opaque, u32 handle, u32 off, u32 bytes)
{
    if (opaque != &owner || handle != GF3_HANDLE_GART_DMA) return GF3_EINVAL;
    if (off > 8192 || bytes > 8192-off) return GF3_ERANGE;
    return 0;
}
static int test(void)
{
    gf3_host_init(&h);
    h.events=events; h.cevents=64; h.packets=packets; h.cpackets=8;
    h.words=words; h.cwords=32;
    node=(struct gf_vram_reservation_node){.value={&owner, 0x1000, 0x10000, 7, 3}};
    h.reservations=&node;
    struct gf3_surface s={0,8192,64,8,8,GF3_RGB565,0,0,GF3_HANDLE_GART_DMA};
    struct gf3_rect r={0,0,8,8};
    struct gf3_format f; struct gf3_span p;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_SIFM_SRC,&f,&p)==GF3_EINVAL);
    gf3_range_query_set(&h.ctx, range, &owner);
    REQUIRE(!gf3_surface_check(&h.ctx,&s,r,GF3_USE_SIFM_SRC,&f,&p));
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_2D,&f,&p)==GF3_EUNSUPPORTED);
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_GDI,&f,&p)==GF3_EUNSUPPORTED);
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_IFC,&f,&p)==GF3_EUNSUPPORTED);
    s.off=4096;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_SIFM_SRC,&f,&p)==GF3_ERANGE);
    s.off=0xfffff000U;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_SIFM_SRC,&f,&p)==GF3_ERANGE);
    struct gf3_surface v={0x1000,512,64,8,8,GF3_RGB565,7,3,0};
    REQUIRE(!gf3_surface_check(&h.ctx,&v,r,GF3_USE_2D,&f,&p));
    v.generation=4;
    REQUIRE(gf3_surface_check(&h.ctx,&v,r,GF3_USE_2D,&f,&p)==GF3_ESTATE);
    v.generation=3; s.off=0;
    gf3_host_trace_clear(&h);
    REQUIRE(gf3_surface_set(&h.ctx,&s,&v,false,123)==GF3_EUNSUPPORTED && !h.npackets);
    REQUIRE(gf3_surface_set(&h.ctx,&v,&s,false,123)==GF3_EUNSUPPORTED && !h.npackets);
    REQUIRE(!gf3_surface_set(&h.ctx,&v,&v,false,123));
    REQUIRE(h.npackets==2 && packets[0].method==0x184 && words[0]==GF3_HANDLE_VRAM_DMA && words[1]==GF3_HANDLE_VRAM_DMA);
    gf3_host_trace_clear(&h);
    gf3_host_fail_at(&h,H_EMIT,1,GF3_ESTATE);
    REQUIRE(gf3_surface_set(&h.ctx,&v,&v,false,123)==GF3_ESTATE && h.npackets==0);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
