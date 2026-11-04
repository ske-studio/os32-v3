/* B1: expected VGA fields from MIT nouveau dispnv04/crtc.c:245-257,
 * 350-413, 838-903 and arb.c:239-264. Timings are standard modelines. */
#define GF4_HOST_UNIT 1
#include "gf4_display_fake.h"
#define REQUIRE(x) do { __asm__ volatile("" : : : "memory"); if (!(x)) return __LINE__; } while (0)
/* nouveau dispnv04/crtc.c:510-516, dispnv04/nvreg.h:160;
 * PCRTC_CONFIG value: dispnv04/crtc.c:568, dispnv04/nvreg.h:157. */
enum { TEST_CURSOR_DOUBLE_SCAN = 0x10, TEST_CONFIG_HSYNC = 2 };
struct row { struct gf4_timing t; u8 ht,hd,hs,he,vt,ov,vs,ve,vd,vbs,vbe,rpc,lsr,heb,ebr; };
static const struct row rows[] = {
    {{25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V},95,79,83,0x9f,11,0x3e,0xe9,0x2b,0xdf,0xdf,0x0c,4,0x10,0,0},
    {{40000,800,840,968,1056,600,601,605,628,0},127,99,106,26,0x72,0xf0,0x58,0x2c,0x57,0x57,0x73,4,0,0,0},
    {{65000,1024,1048,1184,1344,768,771,777,806,GF4_NEG_H|GF4_NEG_V},163,127,132,0x95,0x24,0xf5,0x02,0x28,0xff,0xff,0x25,4,0,0,0},
    {{108000,1280,1328,1440,1688,1024,1025,1028,1066,0},206,159,167,21,0x28,0x5a,0,0x23,0xff,0xff,0x29,0,0x15,0,0}
};
static int test(void)
{
    struct gfinit_mode out={0};
    for (u32 i=0;i<sizeof(rows)/sizeof(rows[0]);++i) {
        const struct row *r=&rows[i]; struct gf4_mode m={.t=r->t};
        REQUIRE(gf4_timing_calc(&m,&out)==GF4_OK);
        REQUIRE(out.cr[0]==r->ht && out.cr[1]==r->hd && out.cr[4]==r->hs && out.cr[5]==r->he);
        REQUIRE(out.cr[6]==r->vt && out.cr[7]==r->ov && out.cr[0x10]==r->vs && out.cr[0x11]==r->ve);
        REQUIRE(out.cr[0x12]==r->vd && out.cr[0x15]==r->vbs && out.cr[0x16]==r->vbe);
        REQUIRE(out.cr[0x1a]==r->rpc && out.cr[0x25]==r->lsr && out.cr[0x2d]==r->heb && out.cr[0x41]==r->ebr);
    }
    struct gf4_mode m={.t=rows[0].t,.fmt=GF3_INDEX8,.pitch=640};
    struct gf3_surface f={.off=0x1000,.pitch=640,.fmt=GF3_INDEX8};
    REQUIRE(!gf4_format_calc(&m,&f,&out) && out.misc==0xe3 && out.cr[0x28]==1 && out.general==0x20100130 && out.start==0x1000);
    m.fmt=f.fmt=GF3_RGB565; m.pitch=f.pitch=1280;
    REQUIRE(!gf4_format_calc(&m,&f,&out) && out.cr[0x28]==2 && out.general==0x20101130);
    m.fmt=f.fmt=GF3_XRGB8888; m.pitch=f.pitch=2560;
    REQUIRE(!gf4_format_calc(&m,&f,&out) && out.cr[0x28]==3 && out.general==0x20100130 && out.cr[0x19]==0x20 && out.cr[0x13]==0x40);
    m.t.hd=4096; m.pitch=f.pitch=16384;
    REQUIRE(!gf4_format_calc(&m,&f,&out) && out.cr[0x42]==0x40);
    f.off++; REQUIRE(gf4_format_calc(&m,&f,&out)==GF4_ERANGE);
    f.off--; m.pitch++; REQUIRE(gf4_format_calc(&m,&f,&out)==GF4_ERANGE);
    m.fmt=GF3_ARGB8888; REQUIRE(gf4_format_calc(&m,&f,&out)==GF4_EFORMAT);
    m.t=rows[0].t; m.t.hs++; REQUIRE(gf4_timing_calc(&m,&out)==GF4_ERANGE);
    m.t=rows[0].t; m.t.vt=5000; REQUIRE(gf4_timing_calc(&m,&out)==GF4_ERANGE);
    m.t=rows[0].t; m.t.flags|=GF4_INTERLACE|GF4_DBLSCAN;
    REQUIRE(!gf4_timing_calc(&m,&out) && out.cr[0x39]==46 && (out.cr[9]&0x80) &&
            out.config==TEST_CONFIG_HSYNC && (out.cursor_cfg&TEST_CURSOR_DOUBLE_SCAN));
    m.t.flags &= ~GF4_DBLSCAN;
    REQUIRE(!gf4_timing_calc(&m,&out) && out.config==TEST_CONFIG_HSYNC &&
            !(out.cursor_cfg&TEST_CURSOR_DOUBLE_SCAN) && !(out.cr[9]&0x80));
    out.cr[0x47]=0xff; gf4_fifo_calc(&out);
    REQUIRE(out.cr[0x1b]==4 && out.cr[0x20]==0xc0 && out.cr[0x47]==0);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
