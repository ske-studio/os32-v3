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
/* Pure value adaptation of nouveau c03c1a1 nvkm/subdev/bios/{dcb,i2c,gpio}.c.
 * No ROM pointer escapes; the caller's image changes only on full success. */
#include "data.h"

/* dcb.c:28-95,121-193,221-244; include/nvkm/subdev/bios/dcb.h:5-12. */
enum {
    DCB_POINTER = 0x36, DCB_V20 = 0x20, DCB_V22 = 0x22,
    DCB_V30 = 0x30, DCB_V40 = 0x40, DCB_V41 = 0x41, DCB_UNKNOWN = 0x42,
    DCB_SIGNATURE = 0x4edcbdcb, DCB_OLD_HEADER = 8, DCB_OUTPUT_BYTES = 8,
    DCB_EOL = 0x0e, DCB_UNUSED = 0x0f, DCB_NIBBLE = 0x0f,
    DCB_HEADS_SHIFT = 8, DCB_I2C_SHIFT = 4, DCB_CONNECTOR_SHIFT = 12,
    DCB_LOCATION_SHIFT = 20, DCB_LOCATION_MASK = 3, DCB_OR_SHIFT = 24,
    DCB_EXT_SHIFT = 8, DCB_EXT_MASK = 0xff,
    DCB_LINK_SHIFT = 4, DCB_LINK_MASK = 3, DCB_LINK_BOTH = 3
};
struct dcb_table { u32 off,header,count,stride,version; };
struct rom_view { const u8 *bytes; u32 size; };

static bool span(struct rom_view r,u32 off,u32 bytes)
{
    return off<=r.size && bytes<=r.size-off;
}
/* Readers are called only after the enclosing span has been checked. */
static u32 rd16(struct rom_view r,u32 off)
{
    return (u32)r.bytes[off] | ((u32)r.bytes[off+1]<<8);
}
static u32 rd32(struct rom_view r,u32 off)
{
    return rd16(r,off) | (rd16(r,off+2)<<16);
}
static i32 table_span(struct rom_view r,const struct dcb_table *t,u32 minimum)
{
    if (t->stride<minimum || !span(r,t->off,t->header)) return GF4_EDDC;
    /* Counts/strides are byte-sized in ROM (old DCB count is <= 8191). */
    return span(r,t->off+t->header,t->count*t->stride) ? GF4_OK : GF4_EDDC;
}

/* D0-a: dcb.c:28-95. Pre-2.0 has no output decoder in dcb_outp_parse. */
static i32 dcb_table_read(struct rom_view r,struct dcb_table *t)
{
    if (!span(r,DCB_POINTER,2)) return GF4_EDDC;
    t->off=rd16(r,DCB_POINTER);
    if (!t->off) return GF4_OK;
    if (!span(r,t->off,1)) return GF4_EDDC;
    t->version=r.bytes[t->off];
    if (t->version<DCB_V20 || t->version>=DCB_UNKNOWN) return GF4_EUNSUPPORTED;
    if (t->version>=DCB_V30) {
        if (!span(r,t->off,10)) return GF4_EDDC;
        if (rd32(r,t->off+6)!=DCB_SIGNATURE) return GF4_EDDC;
        t->header=r.bytes[t->off+1]; t->count=r.bytes[t->off+2];
        t->stride=r.bytes[t->off+3];
        if (t->header<10) return GF4_EDDC;
    } else {
        if (!span(r,t->off,DCB_OLD_HEADER)) return GF4_EDDC;
        if (rd32(r,t->off+4)!=DCB_SIGNATURE) return GF4_EDDC;
        u32 i2c=rd16(r,t->off+2);
        if (i2c<t->off+DCB_OLD_HEADER) return GF4_EDDC;
        t->header=DCB_OLD_HEADER; t->stride=DCB_OUTPUT_BYTES;
        t->count=(i2c-t->off)/DCB_OUTPUT_BYTES;
    }
    return table_span(r,t,DCB_OUTPUT_BYTES);
}

/* D0-a: dcb.c:121-193 and outp_foreach:221-244. Unsupported output
 * types (DP/WFD) do not become display4 routes. No synthetic CRT route. */
static i32 routes_read(struct rom_view r,const struct dcb_table *t,
                       struct gf4_dcb_values *v)
{
    for (u32 i=0;i<t->count;++i) {
        u32 off=t->off+t->header+i*t->stride,conn=rd32(r,off);
        u8 type=conn&DCB_NIBBLE;
        if (!conn || conn==U32_MAX || type==DCB_EOL) break;
        if (type==DCB_UNUSED || type>GF4_LVDS) continue;
        if (v->nroutes>=sizeof(v->routes)/sizeof(v->routes[0])) return GF4_ENOSPC;
        struct gf4_route *route=&v->routes[v->nroutes++];
        route->type=type;
        route->or_mask=(conn>>DCB_OR_SHIFT)&DCB_NIBBLE;
        route->location=(conn>>DCB_LOCATION_SHIFT)&DCB_LOCATION_MASK;
        route->connector=(conn>>DCB_CONNECTOR_SHIFT)&DCB_NIBBLE;
        route->head_mask=(conn>>DCB_HEADS_SHIFT)&DCB_NIBBLE;
        route->i2c_index=(conn>>DCB_I2C_SHIFT)&DCB_NIBBLE;
        /* head is the first selectable NV4x head, not an expansion of entry. */
        route->head=(route->head_mask&1U) ? 0 : 1;
        if (t->version>=DCB_V40 && (type==GF4_TMDS || type==GF4_LVDS)) {
            u32 conf=rd32(r,off+4);
            route->dual_link=((conf>>DCB_LINK_SHIFT)&DCB_LINK_MASK)==DCB_LINK_BOTH;
            if (route->location) route->ext_type=(conf>>DCB_EXT_SHIFT)&DCB_EXT_MASK;
        }
        /* max_khz=0: no encoder-specific ceiling (PM 2026-11-04).
         * tv_norm=0 is PAL, dispnv04/tvnv17.h:56-70. */
    }
    return GF4_OK;
}

/* D0-b: i2c.c:29-67,71-104,136-163. Only NV04 bitbang is
 * exposed to B3; other hardware types retain a per-index error. */
enum {
    I2C_ENTRY_BYTES = 4, I2C_LEGACY_COUNT = 16, I2C_NV04 = 0,
    I2C_UNUSED = 0xff, I2C_OLD_TYPE_MASK = 7, I2C_PMGR_UNUSED = 0x3ff,
    BMP_VERSION = 5, BMP_V4 = 4, BMP_OLD_I2C = 0x48, BMP_I2C = 0x36,
    BMP_BUS_BYTES = 8, BMP_BUSES = 2,
    BMP_DRIVE0 = 0x3f, BMP_SENSE0 = 0x3e,
    BMP_DRIVE1 = 0x37, BMP_SENSE1 = 0x36
};
static i32 bmp_port(struct rom_view r,u32 bmp,u32 index,struct gf4_i2c_port *out)
{
    if (!bmp || index>=BMP_BUSES) return GF4_ENODEV;
    if (!span(r,bmp,BMP_VERSION+1)) return GF4_EDDC;
    bool old=r.bytes[bmp+BMP_VERSION]<BMP_V4;
    if (!old && !span(r,bmp,BMP_I2C+BMP_BUS_BYTES)) return GF4_EDDC;
    u32 off=old ? BMP_OLD_I2C : bmp+BMP_I2C;
    if (!span(r,off,BMP_BUS_BYTES)) return GF4_EDDC;
    off+=4+index*2;
    out->drive=r.bytes[off]; out->sense=r.bytes[off+1];
    if (!out->drive) out->drive=index ? BMP_DRIVE1 : BMP_DRIVE0;
    if (!out->sense) out->sense=index ? BMP_SENSE1 : BMP_SENSE0;
    return GF4_OK;
}
static i32 i2c_read(struct rom_view r,const struct dcb_table *d,u32 bmp,
                    struct gf4_dcb_values *v)
{
    struct dcb_table t={0};
    if (d->off) t.off=rd16(r,d->off+(d->version>=DCB_V30 ? 4 : 2));
    if (t.off) {
        if (d->version>=DCB_V30) {
            if (!span(r,t.off,I2C_ENTRY_BYTES)) return GF4_EDDC;
            t.version=r.bytes[t.off]; t.header=r.bytes[t.off+1];
            t.count=r.bytes[t.off+2]; t.stride=r.bytes[t.off+3];
            if (t.header<I2C_ENTRY_BYTES) return GF4_EDDC;
        } else {
            t.version=d->version; t.count=I2C_LEGACY_COUNT; t.stride=I2C_ENTRY_BYTES;
        }
        i32 rc=table_span(r,&t,I2C_ENTRY_BYTES);
        if (rc) return rc;
    }
    for (u32 i=0;i<sizeof(v->i2c)/sizeof(v->i2c[0]);++i) {
        if (!t.off || i>=t.count) {
            i32 rc=bmp_port(r,bmp,i,&v->i2c[i]);
            if (rc==GF4_EDDC) return rc;
            v->i2c_rc[i]=rc;
            continue;
        }
        u32 off=t.off+t.header+i*t.stride;
        u32 type=r.bytes[off+3];
        if (t.version>=DCB_V41) {
            /* i2c.c:80-87: unused only when both I2C/AUX ports are 31. */
            v->i2c_rc[i]=(rd32(r,off)&I2C_PMGR_UNUSED)==I2C_PMGR_UNUSED ?
                GF4_ENODEV : GF4_EUNSUPPORTED;
            continue;
        }
        if (t.version<DCB_V30) {
            type&=I2C_OLD_TYPE_MASK;
            if (type==I2C_OLD_TYPE_MASK) type=I2C_UNUSED;
        }
        if (type==I2C_NV04) {
            v->i2c[i].drive=r.bytes[off];
            v->i2c[i].sense=r.bytes[off+1];
            v->i2c_rc[i]=GF4_OK;
        } else v->i2c_rc[i]=type==I2C_UNUSED ? GF4_ENODEV : GF4_EUNSUPPORTED;
    }
    return GF4_OK;
}

/* D0-c: gpio.c:30-57,78-115,119-149; bios/gpio.h:8,10.
 * Retain the first match, as dcb_gpio_match does. XPIO is not searched. */
enum {
    GPIO_DCB_HEADER = 0x0c, GPIO_POINTER = 0x0a,
    GPIO_OLD_PREFIX = 0x13, GPIO_OLD_POINTER_BACK = 0x0f,
    GPIO_TVDAC0 = 0x0c, GPIO_TVDAC1 = 0x2d,
    GPIO_OLD_HEADER = 3, GPIO_NEW_HEADER = 4,
    GPIO_OLD_BYTES = 2, GPIO_V40_BYTES = 4, GPIO_V41_BYTES = 5,
    GPIO_LINE_MASK = 0x1f, GPIO_V41_LINE_MASK = 0x3f,
    GPIO_OLD_FUNC_SHIFT = 5, GPIO_OLD_FUNC_MASK = 0x3f,
    GPIO_FUNC_SHIFT = 8, GPIO_FUNC_MASK = 0xff,
    GPIO_OLD_LOG0_SHIFT = 11, GPIO_OLD_LOG1_SHIFT = 13,
    GPIO_V40_LOG0_SHIFT = 27, GPIO_V40_LOG1_SHIFT = 29,
    GPIO_V41_LOG0_SHIFT = 4, GPIO_V41_LOG1_SHIFT = 6, GPIO_LOG_MASK = 3,
    GPIO_FIXED_CONF_BACK = 5, GPIO_FIXED_ADDR_BACK = 4,
    GPIO_FIXED_PRESENT = 1, GPIO_FIXED_INVERT = 2
};
static i32 gpio_read(struct rom_view r,const struct dcb_table *d,
                     struct gf4_dcb_values *v)
{
    struct dcb_table t={0};
    if (!d->off) return GF4_OK;
    if (d->version>=DCB_V30 && d->header>=GPIO_DCB_HEADER)
        t.off=rd16(r,d->off+GPIO_POINTER);
    else if (d->version>=DCB_V22 && d->version<DCB_V30 &&
             r.bytes[d->off-1]>=GPIO_OLD_PREFIX) {
        if (d->off<GPIO_OLD_POINTER_BACK) return GF4_EDDC;
        t.off=rd16(r,d->off-GPIO_OLD_POINTER_BACK);
    }
    if (t.off) {
        if (!span(r,t.off,1)) return GF4_EDDC;
        t.version=r.bytes[t.off];
        if (t.version>DCB_V41) return GF4_EUNSUPPORTED;
        u32 minimum;
        if (t.version<DCB_V30) {
            if (!span(r,t.off,GPIO_OLD_HEADER)) return GF4_EDDC;
            t.header=GPIO_OLD_HEADER; t.count=r.bytes[t.off+2];
            t.stride=r.bytes[t.off+1];
        } else {
            if (!span(r,t.off,GPIO_NEW_HEADER)) return GF4_EDDC;
            t.header=r.bytes[t.off+1]; t.count=r.bytes[t.off+2];
            t.stride=r.bytes[t.off+3];
            if (t.header<GPIO_NEW_HEADER) return GF4_EDDC;
        }
        minimum=t.version<DCB_V40 ? GPIO_OLD_BYTES :
            (t.version<DCB_V41 ? GPIO_V40_BYTES : GPIO_V41_BYTES);
        i32 rc=table_span(r,&t,minimum);
        if (rc) return rc;
        for (u32 i=0;i<t.count;++i) {
            u32 off=t.off+t.header+i*t.stride;
            u32 info=t.version<DCB_V40 ? rd16(r,off) : rd32(r,off);
            u32 func,log0,log1,line;
            if (t.version<DCB_V40) {
                line=info&GPIO_LINE_MASK;
                func=(info>>GPIO_OLD_FUNC_SHIFT)&GPIO_OLD_FUNC_MASK;
                log0=info>>GPIO_OLD_LOG0_SHIFT; log1=info>>GPIO_OLD_LOG1_SHIFT;
            } else {
                func=(info>>GPIO_FUNC_SHIFT)&GPIO_FUNC_MASK;
                if (t.version<DCB_V41) {
                    line=info&GPIO_LINE_MASK;
                    log0=info>>GPIO_V40_LOG0_SHIFT; log1=info>>GPIO_V40_LOG1_SHIFT;
                } else {
                    line=info&GPIO_V41_LINE_MASK;
                    log0=r.bytes[off+4]>>GPIO_V41_LOG0_SHIFT;
                    log1=r.bytes[off+4]>>GPIO_V41_LOG1_SHIFT;
                }
            }
            if (func!=GPIO_TVDAC0 && func!=GPIO_TVDAC1) continue;
            struct gf4_gpio_value *g=&v->tvdac[func==GPIO_TVDAC1];
            if (g->present) continue;
            *g=(struct gf4_gpio_value){true,line,
                {log0&GPIO_LOG_MASK,log1&GPIO_LOG_MASK}};
        }
    }
    /* gpio.c:134-149: only TVDAC0 has a DCB2.2 fixed-data fallback. */
    if (!v->tvdac[0].present && d->version>=DCB_V22 && d->version<DCB_V30) {
        if (d->off<GPIO_FIXED_CONF_BACK) return GF4_EDDC;
        u8 conf=r.bytes[d->off-GPIO_FIXED_CONF_BACK];
        u8 addr=r.bytes[d->off-GPIO_FIXED_ADDR_BACK];
        if (conf&GPIO_FIXED_PRESENT) v->tvdac[0]=(struct gf4_gpio_value){
            true,addr>>4,{!!(conf&GPIO_FIXED_INVERT),!(conf&GPIO_FIXED_INVERT)}};
    }
    return GF4_OK;
}

i32 gf4_dcb_parse(const u8 *rom,u32 size,u32 bmp_offset,struct gf4_dcb_values *out)
{
    if (!rom || !out) return GF4_EINVAL;
    struct rom_view r={rom,size};
    struct dcb_table t={0};
    struct gf4_dcb_values v={0};
    i32 rc=dcb_table_read(r,&t);
    if (rc) return rc;
    for (u32 i=0;i<sizeof(v.i2c_rc)/sizeof(v.i2c_rc[0]);++i) v.i2c_rc[i]=GF4_ENODEV;
    rc=routes_read(r,&t,&v);
    if (rc) return rc;
    rc=i2c_read(r,&t,bmp_offset,&v);
    if (rc) return rc;
    rc=gpio_read(r,&t,&v);
    if (rc) return rc;
    *out=v;
    return GF4_OK;
}
