/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
/* D0 independent ROM fixtures. Recreate byte records using nouveau c03c1a1
 * nvkm/subdev/bios/dcb.c:28-95,121-193,221-244; i2c.c:29-104,136-163;
 * gpio.c:30-57,78-149 and include/nvkm/subdev/bios/gpio.h:8,10.
 * Expected records below are literal decoded values, not product formulas.
 * Link this TU with hwdev/gf/display/dcb.c, -nostdlib, GF4 UNIT 14.
 * Registration and mutation execution belong to PM/A2. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"

/* Arbitrary disjoint fixture placements; table sizes are the cited formats. */
enum {
    ROM_BYTES = 2048, DCB = 128, I2C = 512, GPIO = 640, BMP = 768,
    DCB_PTR = 0x36, HEADER = 12, OUTPUT_BYTES = 8, PORT_BYTES = 4,
    I2C_HEADER = 4, GPIO_HEADER = 4, LEGACY_PORTS = 16,
    ROUTE_CAPACITY = 32, BUS_CAPACITY = 16, GPIO40_BYTES = 4,
    GPIO41_BYTES = 5, GPIO_OLD_BYTES = 2,
    /* bios/i2c.c:140-155: BMP v4+ offset, old fixed position. */
    BMP_MAJOR = 5, BMP_I2C = 0x36, BMP_OLD_I2C = 0x48,
    /* gpio.c:40-41,136-145: pre-DCB fields. */
    GPIO_OLD_PTR_BACK = 15, GPIO_FIXED_CONF_BACK = 5, GPIO_FIXED_ADDR_BACK = 4,
    /* i2c.c:147-155 default drive/sense, independent of product constants. */
    DEFAULT_DRIVE0 = 0x3f, DEFAULT_SENSE0 = 0x3e,
    DEFAULT_DRIVE1 = 0x37, DEFAULT_SENSE1 = 0x36,
    /* include/nvkm/subdev/bios/gpio.h:8,10. */
    TAG_TVDAC0 = 0x0c, TAG_TVDAC1 = 0x2d
};
static u8 rom[ROM_BYTES],rom_before[ROM_BYTES];
static struct gf4_dcb_values out,old;
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[1]; /* Pure parsing must emit no I/O. */

static void put16(u32 at,u32 value)
{
    assert(at+2<=ROM_BYTES);
    rom[at]=(u8)value; rom[at+1]=(u8)(value>>8);
}
static void put32(u32 at,u32 value)
{
    put16(at,value); put16(at+2,value>>16);
}
static void fresh(u8 version,u8 count)
{
    gf2_host_memset(rom,0,sizeof(rom));
    put16(DCB_PTR,DCB);
    rom[DCB]=version; rom[DCB+1]=HEADER;
    rom[DCB+2]=count; rom[DCB+3]=OUTPUT_BYTES;
    /* DCB signature bytes, dcb.c:48-51. */
    rom[DCB+6]=0xcb; rom[DCB+7]=0xbd; rom[DCB+8]=0xdc; rom[DCB+9]=0x4e;
}
static void ccb(u8 version,u8 count)
{
    put16(DCB+4,I2C);
    rom[I2C]=version; rom[I2C+1]=I2C_HEADER;
    rom[I2C+2]=count; rom[I2C+3]=PORT_BYTES;
}
static void gpio_table(u8 version,u8 count,u8 stride)
{
    put16(DCB+10,GPIO);
    rom[GPIO]=version;
    if (version<0x30) {
        rom[GPIO+1]=stride; rom[GPIO+2]=count;
    } else {
        rom[GPIO+1]=GPIO_HEADER; rom[GPIO+2]=count; rom[GPIO+3]=stride;
    }
}
static void parse_ok(u32 bmp)
{
    gf2_host_memcpy(rom_before,rom,sizeof(rom));
    assert(gf4_dcb_parse(rom,sizeof(rom),bmp,&out)==GF4_OK);
    assert(!gf2_host_memcmp(rom,rom_before,sizeof(rom)));
    assert(host.nevents==0);
}
static void unchanged(i32 rc,u32 bytes,u32 bmp)
{
    gf2_host_memset(&out,0xa5,sizeof(out)); old=out;
    assert(gf4_dcb_parse(rom,bytes,bmp,&out)==rc);
    assert(!gf2_host_memcmp(&out,&old,sizeof(out)));
    assert(host.nevents==0);
}
static void route_equal(const struct gf4_route *a,const struct gf4_route *b)
{
    assert(a->connector==b->connector && a->head==b->head && a->type==b->type);
    assert(a->or_mask==b->or_mask && a->i2c_index==b->i2c_index);
    assert(a->location==b->location && a->head_mask==b->head_mask);
    assert(a->ext_type==b->ext_type && a->dual_link==b->dual_link);
    assert(a->max_khz==b->max_khz && a->tv_norm==b->tv_norm);
}
static void routes(void)
{
    /* dcb.c:131-139,181-187. bus bits are deliberately nonzero and
     * independent of connector. External TMDS has extdev=ab, link=3. */
    static const u8 entries[][OUTPUT_BYTES]={
        {0x42,0x53,0x2a,0x01,0x30,0xab,0,0},
        {0x10,0x02,0x00,0x04,0,0,0,0},
        {0x21,0x31,0x00,0x02,0,0,0,0},
        {0x13,0x72,0x00,0x01,0x10,0xee,0,0}
    };
    static const struct gf4_route expected[]={
        {.connector=5,.head=0,.type=GF4_TMDS,.or_mask=1,.i2c_index=4,
         .location=2,.head_mask=3,.ext_type=0xab,.dual_link=true},
        {.head=1,.type=GF4_ANALOG,.or_mask=4,.i2c_index=1,.head_mask=2},
        {.connector=3,.type=GF4_TV,.or_mask=2,.i2c_index=2,.head_mask=1},
        {.connector=7,.head=1,.type=GF4_LVDS,.or_mask=1,.i2c_index=1,.head_mask=2}
    };
    const u32 n=sizeof(expected)/sizeof(expected[0]);
    fresh(0x40,n);
    gf2_host_memcpy(rom+DCB+HEADER,entries,sizeof(entries));
    parse_ok(0); assert(out.nroutes==n);
    for (u32 i=0;i<n;++i) route_equal(&out.routes[i],&expected[i]);
    /* DCB3 does not interpret the DCB4 conf word. */
    rom[DCB]=0x30; parse_ok(0);
    assert(!out.routes[0].dual_link && !out.routes[0].ext_type);
    for (u32 bus=0;bus<BUS_CAPACITY;++bus) assert(out.i2c_rc[bus]==GF4_ENODEV);
    assert(!out.tvdac[0].present && !out.tvdac[1].present);
    /* foreach termination/unused/unsupported entries, dcb.c:228-238. */
    const u32 end_words[]={0, U32_MAX, 0x0000010e};
    for (u32 i=0;i<sizeof(end_words)/sizeof(end_words[0]);++i) {
        fresh(0x40,3); put32(DCB+HEADER,0x01000100);
        put32(DCB+HEADER+OUTPUT_BYTES,end_words[i]);
        put32(DCB+HEADER+2*OUTPUT_BYTES,0x02000202);
        parse_ok(0); assert(out.nroutes==1);
    }
    fresh(0x40,3); put32(DCB+HEADER,0x0000010f);
    put32(DCB+HEADER+OUTPUT_BYTES,0x01000106); /* DP: excluded */
    put32(DCB+HEADER+2*OUTPUT_BYTES,0x01000100);
    parse_ok(0); assert(out.nroutes==1 && out.routes[0].type==GF4_ANALOG);
    fresh(0x41,ROUTE_CAPACITY);
    for (u32 i=0;i<ROUTE_CAPACITY;++i) put32(DCB+HEADER+i*OUTPUT_BYTES,0x01000300);
    parse_ok(0); assert(out.nroutes==ROUTE_CAPACITY);
    rom[DCB+2]=ROUTE_CAPACITY+1;
    put32(DCB+HEADER+ROUTE_CAPACITY*OUTPUT_BYTES,0x01000300);
    unchanged(GF4_ENOSPC,sizeof(rom),0);
}
static void ports(void)
{
    fresh(0x30,1); put32(DCB+HEADER,0x01000100); ccb(0x30,3);
    /* NV04, absent, NV4E, i2c.c:89-104. */
    static const u8 records[]={0x37,0x36,0,0, 0,0,0,0xff, 0,0x55,0,4};
    gf2_host_memcpy(rom+I2C+I2C_HEADER,records,sizeof(records));
    parse_ok(0); assert(out.nroutes==1);
    assert(out.i2c_rc[0]==GF4_OK && out.i2c[0].drive==0x37 && out.i2c[0].sense==0x36);
    assert(out.i2c_rc[1]==GF4_ENODEV && out.i2c_rc[2]==GF4_EUNSUPPORTED);
    assert(out.i2c_rc[3]==GF4_ENODEV);
    /* A present but unused/non-NV04 entry must not fall back to BMP. */
    rom[BMP+BMP_MAJOR]=4; parse_ok(BMP);
    assert(out.i2c_rc[1]==GF4_ENODEV && out.i2c_rc[2]==GF4_EUNSUPPORTED);
    ccb(0x41,2); put32(I2C+I2C_HEADER,0x000003ff);
    put32(I2C+I2C_HEADER+PORT_BYTES,0x000003e1);
    parse_ok(0); assert(out.i2c_rc[0]==GF4_ENODEV && out.i2c_rc[1]==GF4_EUNSUPPORTED);
    /* All 16 legacy records, type uses low 3 bits, dcb.c:56-62;
     * one route then a sentinel; count formula also covers one I2C slot. */
    fresh(0x20,0); put16(DCB+2,DCB+3*OUTPUT_BYTES);
    put32(DCB+4,0x4edcbdcb); put32(DCB+OUTPUT_BYTES,0x01000100);
    u32 at=DCB+3*OUTPUT_BYTES;
    for (u32 i=0;i<LEGACY_PORTS;++i) {
        rom[at+i*PORT_BYTES]=0x3f; rom[at+i*PORT_BYTES+1]=0x3e;
        rom[at+i*PORT_BYTES+3]=0xf8; /* low bits 0 => NV04 */
    }
    rom[at+PORT_BYTES+3]=0xff;
    parse_ok(0); assert(out.nroutes==1);
    assert(out.i2c_rc[0]==GF4_OK && out.i2c_rc[1]==GF4_ENODEV);
    assert(out.i2c_rc[LEGACY_PORTS-1]==GF4_OK);
    assert(out.i2c[LEGACY_PORTS-1].drive==DEFAULT_DRIVE0);
}
static void bmp(void)
{
    fresh(0x30,0); rom[BMP+BMP_MAJOR]=4;
    parse_ok(BMP);
    assert(out.i2c_rc[0]==GF4_OK && out.i2c_rc[1]==GF4_OK);
    assert(out.i2c[0].drive==DEFAULT_DRIVE0 && out.i2c[0].sense==DEFAULT_SENSE0);
    assert(out.i2c[1].drive==DEFAULT_DRIVE1 && out.i2c[1].sense==DEFAULT_SENSE1);
    assert(out.i2c_rc[2]==GF4_ENODEV);
    put16(BMP+BMP_I2C+4,0x6655); put16(BMP+BMP_I2C+6,0x8877);
    parse_ok(BMP); assert(out.i2c[0].drive==0x55 && out.i2c[0].sense==0x66);
    assert(out.i2c[1].drive==0x77 && out.i2c[1].sense==0x88);
    /* BMP fallback also supplies an index beyond a short CCB. */
    ccb(0x30,1); rom[I2C+I2C_HEADER]=0x21; rom[I2C+I2C_HEADER+1]=0x43;
    parse_ok(BMP); assert(out.i2c[0].drive==0x21 && out.i2c[1].drive==0x77);
    put16(DCB+4,0); rom[BMP+BMP_MAJOR]=3;
    put16(BMP_OLD_I2C+4,0x3412); put16(BMP_OLD_I2C+6,0x7856);
    parse_ok(BMP); assert(out.i2c[0].drive==0x12 && out.i2c[0].sense==0x34);
    assert(out.i2c[1].drive==0x56 && out.i2c[1].sense==0x78);
    /* No DCB pointer: no invented route, BMP is still a valid I2C source. */
    put16(DCB_PTR,0); parse_ok(BMP); assert(out.nroutes==0 && out.i2c_rc[0]==GF4_OK);
    unchanged(GF4_EDDC,sizeof(rom),U32_MAX);
    unchanged(GF4_EDDC,BMP+BMP_MAJOR,BMP);
    rom[BMP+BMP_MAJOR]=4; unchanged(GF4_EDDC,BMP+BMP_I2C+7,BMP);
}
static void gpio_equal(u32 n,u8 line,u8 low,u8 high)
{
    assert(out.tvdac[n].present && out.tvdac[n].line==line);
    assert(out.tvdac[n].log[0]==low && out.tvdac[n].log[1]==high);
}
static void gpios(void)
{
    fresh(0x40,0); gpio_table(0x40,3,GPIO40_BYTES);
    /* gpio.c:95-102: tag=0c, line=17, logs=2/1; second tag=2d,
     * line=3, logs=0/3. Duplicate first tag must retain the first match. */
    put32(GPIO+GPIO_HEADER,0x30000c11);
    put32(GPIO+GPIO_HEADER+GPIO40_BYTES,0x60002d03);
    put32(GPIO+GPIO_HEADER+2*GPIO40_BYTES,0x00000c02);
    parse_ok(0); gpio_equal(0,17,2,1); gpio_equal(1,3,0,3);
    gpio_table(0x41,2,GPIO41_BYTES);
    put32(GPIO+GPIO_HEADER,0x00000c2a); rom[GPIO+GPIO_HEADER+4]=0x90;
    put32(GPIO+GPIO_HEADER+GPIO41_BYTES,0x00002d3f);
    rom[GPIO+GPIO_HEADER+GPIO41_BYTES+4]=0xe0;
    parse_ok(0); gpio_equal(0,42,1,2); gpio_equal(1,63,2,3);
    gpio_table(0x10,2,GPIO_OLD_BYTES);
    /* gpio.c:83-92: compact tags and logs: 2/1 on line 17; 0/3 on line 3. */
    put16(GPIO+3,0x3191); put16(GPIO+3+GPIO_OLD_BYTES,0x65a3);
    parse_ok(0); gpio_equal(0,17,2,1); gpio_equal(1,3,0,3);
    /* DCB2.2 preheader pointer and fixed TVDAC0 fallback. */
    fresh(0x22,0); put16(DCB+2,I2C); put32(DCB+4,0x4edcbdcb);
    rom[DCB-1]=0x13; put16(DCB-GPIO_OLD_PTR_BACK,GPIO);
    rom[GPIO]=0x10; rom[GPIO+1]=GPIO_OLD_BYTES; rom[GPIO+2]=1;
    put16(GPIO+3,0x65a3); rom[DCB-GPIO_FIXED_CONF_BACK]=3;
    rom[DCB-GPIO_FIXED_ADDR_BACK]=0xb0;
    parse_ok(0); gpio_equal(0,11,1,0); gpio_equal(1,3,0,3);
    /* The table's TVDAC0 takes precedence over fixed data. */
    put16(GPIO+3,0x3191); parse_ok(0); gpio_equal(0,17,2,1);
    assert(!out.tvdac[1].present);
    rom[DCB-1]=0; rom[DCB-GPIO_FIXED_CONF_BACK]=1;
    parse_ok(0); gpio_equal(0,11,0,1); assert(!out.tvdac[1].present);
    rom[DCB-GPIO_FIXED_CONF_BACK]=0;
    parse_ok(0); assert(!out.tvdac[0].present && !out.tvdac[1].present);
}
static void malformed(void)
{
    assert(gf4_dcb_parse(0,0,0,&out)==GF4_EINVAL);
    assert(gf4_dcb_parse(rom,sizeof(rom),0,0)==GF4_EINVAL);
    fresh(0x40,0); unchanged(GF4_EDDC,DCB_PTR+1,0);
    put16(DCB_PTR,ROM_BYTES); unchanged(GF4_EDDC,sizeof(rom),0);
    const u8 unknown[]={0x14,0x15,0x1f,0x42,0xff};
    for (u32 i=0;i<sizeof(unknown);++i) {
        fresh(unknown[i],0); unchanged(GF4_EUNSUPPORTED,sizeof(rom),0);
    }
    fresh(0x40,1); unchanged(GF4_EDDC,DCB+9,0);
    rom[DCB+6]^=1; unchanged(GF4_EDDC,sizeof(rom),0);
    fresh(0x40,1); rom[DCB+1]=9; unchanged(GF4_EDDC,sizeof(rom),0);
    fresh(0x40,1); rom[DCB+3]=OUTPUT_BYTES-1; unchanged(GF4_EDDC,sizeof(rom),0);
    fresh(0x40,1); unchanged(GF4_EDDC,DCB+HEADER+OUTPUT_BYTES-1,0);
    fresh(0x20,0); put32(DCB+4,0x4edcbdcb); put16(DCB+2,DCB-1);
    unchanged(GF4_EDDC,sizeof(rom),0);
    fresh(0x40,0); ccb(0x30,1);
    unchanged(GF4_EDDC,I2C+I2C_HEADER+PORT_BYTES-1,0);
    rom[I2C+1]=I2C_HEADER-1; unchanged(GF4_EDDC,sizeof(rom),0);
    ccb(0x30,1); rom[I2C+3]=PORT_BYTES-1; unchanged(GF4_EDDC,sizeof(rom),0);
    put16(DCB+4,ROM_BYTES-1); unchanged(GF4_EDDC,sizeof(rom),0);
    fresh(0x40,0); gpio_table(0x41,1,GPIO41_BYTES);
    unchanged(GF4_EDDC,GPIO+GPIO_HEADER+GPIO41_BYTES-1,0);
    rom[GPIO+3]=GPIO41_BYTES-1; unchanged(GF4_EDDC,sizeof(rom),0);
    gpio_table(0x40,1,GPIO40_BYTES); rom[GPIO+1]=GPIO_HEADER-1;
    unchanged(GF4_EDDC,sizeof(rom),0);
    gpio_table(0x42,0,GPIO40_BYTES); unchanged(GF4_EUNSUPPORTED,sizeof(rom),0);
    put16(DCB+10,ROM_BYTES); unchanged(GF4_EDDC,sizeof(rom),0);
}
void _start(void)
{
    gf4_host_init(&host,&state); host.events=events; host.cevents=1;
    routes(); ports(); bmp(); gpios(); malformed();
    gf2_host_exit(0);
}
