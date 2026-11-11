/* B1 with the G1 fixture: independent section 9 rows and SF packets. */
#define GF3_HOST_UNIT 1
#include "gf_accel_host.h"
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static struct gf3_host h;
static struct gf3_host_event events[128];
static struct gf3_host_packet packets[8];
static u32 words[32];
static struct gf_vram_reservation_node node;
static int owner;
static void fresh(void)
{
    gf3_host_init(&h);
    h.events=events; h.cevents=128; h.packets=packets; h.cpackets=8;
    h.words=words; h.cwords=32;
    node=(struct gf_vram_reservation_node){.value={&owner, 0x1000, 0x10000, 7, 3}};
    h.reservations=&node;
}
static int test(void)
{
    static const struct gf3_format expected[] = {
        {1,1,1,3,3,0,0}, {2,4,4,1,1,1,7}, {4,6,6,3,3,4,4},
        {4,10,11,3,3,4,3}, {1,1,1,3,3,0,8}, {2,0,0,0,0,0,5}, {2,0,0,0,0,0,6}
    };
    fresh();
    for (unsigned i=0; i<7; ++i) {
        struct gf3_format f;
        REQUIRE(!gf3_format_get((enum gf3_fmt)i,&f));
        REQUIRE(f.cpp==expected[i].cpp && f.sf==expected[i].sf && f.sf_gdi==expected[i].sf_gdi &&
                f.gdi==expected[i].gdi && f.pat==expected[i].pat && f.ifc==expected[i].ifc && f.sifm==expected[i].sifm);
    }
    struct gf3_format f={99,99,99,99,99,99,99};
    struct gf3_span p={99,99,99,99,99};
    REQUIRE(gf3_format_get((enum gf3_fmt)7,&f)==GF3_EFORMAT && f.cpp==99);
    REQUIRE(gf3_format_get(GF3_INDEX8,0)==GF3_EINVAL);
    REQUIRE(!gf3_span_check(3,20,8,4,2,&p) && p.off==3 && p.bytes==12);
    REQUIRE(gf3_span_check(3,14,8,4,2,&p)==GF3_ERANGE && p.bytes==12);
    REQUIRE(gf3_span_check(0,U32_MAX,U32_MAX,1,3,&p)==GF3_ERANGE);
    REQUIRE(!gf3_span_check(20,20,0,0,2,&p) && p.bytes==0);
    struct gf3_ram ram={0,20,8};
    REQUIRE(gf3_ram_check(&ram,4,2,&p)==GF3_EINVAL);
    ram.ptr=&owner;
    REQUIRE(!gf3_ram_check(&ram,4,2,&p) && p.bytes==12);
    struct gf3_surface s={0x1000,512,64,8,8,GF3_RGB565,7,3, 0};
    struct gf3_rect r={2,1,3,2};
    REQUIRE(!gf3_surface_check(&h.ctx,&s,r,GF3_USE_2D,&f,&p));
    REQUIRE(p.off==0x1044 && p.rowbytes==6 && p.pitch==64 && p.rows==2 && p.bytes==70);
    REQUIRE(h.events[0].op==H_MATCH && h.events[0].a[0]==7 && h.events[0].a[1]==3);
    /* EXA offset/pitch (nouveau_exa.c:434-435), Xv pitch
     * (nouveau_xv.c:929-942). All drawing uses reject independently. */
    for (enum gf3_surface_use use=GF3_USE_2D; use<=GF3_USE_SIFM_SRC; ++use) {
        if (use==GF3_USE_BYTES) continue;
        struct gf3_surface bad=s;
        bad.pitch=40; bad.fmt=GF3_INDEX8;
        if (use==GF3_USE_IFC || use==GF3_USE_SIFM_SRC) bad.fmt=GF3_RGB565;
        f.cpp=99; p.bytes=99;
        REQUIRE(gf3_surface_check(&h.ctx,&bad,r,use,&f,&p)==GF3_ERANGE);
        REQUIRE(f.cpp==99 && p.bytes==99);
        bad=s; bad.off++;
        REQUIRE(gf3_surface_check(&h.ctx,&bad,r,use,&f,&p)==GF3_ERANGE);
        REQUIRE(f.cpp==99 && p.bytes==99);
        REQUIRE(!gf3_surface_check(&h.ctx,&s,r,use,&f,&p));
    }
    struct gf3_surface bytes=s;
    bytes.off++; bytes.pitch=17; bytes.bytes=136;
    REQUIRE(!gf3_surface_check(&h.ctx,&bytes,r,GF3_USE_BYTES,&f,&p));
    f.cpp=99; p.bytes=99; s.generation=4;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_2D,&f,&p)==GF3_ESTATE && f.cpp==99 && p.bytes==99);
    s.generation=3; s.bytes=0x10001;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_BYTES,&f,&p)==GF3_ERANGE);
    s.bytes=256; s.pitch=15;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_BYTES,&f,&p)==GF3_ERANGE);
    s.pitch=64; s.bytes=512; s.fmt=GF3_YUY2; r.x=1;
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_SIFM_SRC,&f,&p)==GF3_ERANGE);
    REQUIRE(gf3_surface_check(&h.ctx,&s,r,GF3_USE_2D,&f,&p)==GF3_EFORMAT);
    s.fmt=GF3_ARGB8888; s.pitch=64; s.bytes=512;
    struct gf3_surface dst={0x2000,1024,128,8,8,GF3_ARGB8888,7,3, 0};
    gf3_host_trace_clear(&h);
    REQUIRE(!gf3_surface_set(&h.ctx,&s,&dst,true,123));
    REQUIRE(h.npackets==1 && packets[0].subc==GF3_SUBC_SURFACE2D && packets[0].method==0x300 &&
            !packets[0].noninc && packets[0].count==4 && packets[0].deadline==123);
    REQUIRE(words[0]==11 && words[1]==0x00800040 && words[2]==0x1000 && words[3]==0x2000);
    gf3_host_trace_clear(&h);
    REQUIRE(!gf3_surface_set(&h.ctx,&s,&s,false,124) && words[0]==10);
    gf3_host_trace_clear(&h);
    gf3_host_fail_at(&h,H_MATCH,1,GF3_ESTATE);
    REQUIRE(gf3_surface_set(&h.ctx,&s,&s,false,124)==GF3_ESTATE && !h.npackets);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
