/*
 * Copyright 2012, 2013 Red Hat Inc.
 * Copyright 2012 Nouveau Community
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
/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
/* NV44A GPIO/VID voltage requests (not an ADC measurement).
 * MIT nouveau c03c1a1 nvkm/subdev/{bios/{bit,gpio,volt,vmap},
 * gpio/{base,nv10},volt/{base,gpio,nv40}}.c, cited below.
 * Tables borrow the immutable F5 ROM; no persistent parser state/allocations. */
#include "feature5.h"
#include "command.h"
#include "foundation.h"
#include "internal.h"

/* bios/bit.c:36-57; bios/volt.c:29-100; bios/gpio.c:32-118. */
enum {
    BIT_HEADER=12, BIT_ENTRY=6, BIT_P_VOLT1=0x10, BIT_P_VOLT2=0x0c,
    BIT_P_VMAP=0x20, DCB_POINTER=0x36, DCB_SIGNATURE=0x4edcbdcb,
    DCB_GPIO_POINTER=0x0a, DCB_GPIO_HEADER=0x0c,
    DCB_OLD_GPIO_BACK=0x0f, DCB_OLD_PREFIX=0x13,
    VOLT_UNIT_UV=10000, VID_BITS=8,
    GPIO_LOW=0x600818, GPIO_MID=0x60081c, GPIO_HIGH=0x600850,
    GPIO_BAR=0, GPIO_WIDTH=4, GPIO_LINES=14
};
struct table { u32 off, hdr, len, cnt; u8 ver, mask; };
struct pin { u8 line, log[2]; };
static bool span(const struct gf5 *g,u32 off,u32 n)
{ return g->rom.bytes && off<=g->rom.size && n<=g->rom.size-off; }
static u32 le16(const struct gf5 *g,u32 off)
{ return g->rom.bytes[off] | ((u32)g->rom.bytes[off+1]<<8); }
static u32 le32(const struct gf5 *g,u32 off)
{ return le16(g,off) | (le16(g,off+2)<<16); }
static i32 done(struct gf5 *g,i32 rc)
{
    if (g) {
        if (!rc || !g->source_rc) g->source_rc=rc;
        g->last_rc=gf5_rc_map(rc);
        return g->last_rc;
    }
    return GF5_EINVAL;
}
static i32 table_span(const struct gf5 *g,const struct table *t,u32 hdr,u32 len)
{
    if (t->hdr<hdr || t->len<len || !span(g,t->off,t->hdr) ||
        !span(g,t->off+t->hdr,t->cnt*t->len)) return GF5_EBIOS;
    return 0;
}
/* bios/base.c BIT signature search; bios/bit.c:36-57. */
static i32 power(const struct gf5 *g,u32 *off,u32 *size,u8 *ver)
{
    if (!g->rom.bytes) return GF5_ENOTSUP;
    for (u32 b=0;span(g,b,5);++b) {
        const u8 *r=g->rom.bytes+b;
        if (r[0]!=0xff || r[1]!=0xb8 || r[2]!='B' || r[3]!='I' || r[4]!='T') continue;
        if (!span(g,b,BIT_HEADER)) return GF5_EBIOS;
        u32 hdr=r[8],len=r[9],cnt=r[10];
        if (hdr<BIT_HEADER || len<BIT_ENTRY || !span(g,b,hdr) ||
            !span(g,b+hdr,cnt*len)) return GF5_EBIOS;
        for (u32 i=0;i<cnt;++i) {
            u32 e=b+hdr+i*len;
            if (g->rom.bytes[e]!='P') continue;
            *ver=g->rom.bytes[e+1]; *size=le16(g,e+2); *off=le16(g,e+4);
            return span(g,*off,*size) ? 0 : GF5_EBIOS;
        }
        return GF5_ENODEV;
    }
    return GF5_ENOTSUP;
}
static i32 power_pointer(const struct gf5 *g,bool vmap,u32 *out)
{
    u32 p,n; u8 ver;
    i32 rc=power(g,&p,&n,&ver);
    if (vmap && (rc==GF5_ENODEV || rc==GF5_ENOTSUP)) { *out=0; return 0; }
    if (rc) return rc;
    if (vmap && ver!=2) { *out=0; return 0; }
    if (ver!=1 && ver!=2) return GF5_ENOTSUP;
    if (vmap && ver==1) { *out=0; return 0; }
    u32 field=vmap ? BIT_P_VMAP : (ver==1 ? BIT_P_VOLT1 : BIT_P_VOLT2);
    /* Short P v2 predates vmap: absence, not an out-of-bounds read. */
    if (vmap && n<field+4) { *out=0; return 0; }
    if (n<field+4) return GF5_EBIOS;
    *out=le32(g,p+field);
    return 0;
}
/* bios/gpio.c:32-118. First matching DCB function, no XPIO on NV44A. */
static i32 gpio_find(struct gf5 *g,u8 tag,u8 *line,u8 log[2])
{
    if (!g || !line || !log) return GF5_EINVAL;
    if (!g->rom.bytes) return GF5_ENOTSUP;
    if (!span(g,DCB_POINTER,2)) return GF5_EBIOS;
    u32 d=le16(g,DCB_POINTER),p=0;
    if (!d) return GF5_ENODEV;
    if (!span(g,d,1)) return GF5_EBIOS;
    u8 ver=g->rom.bytes[d];
    if (ver>=0x42 || ver<0x20) return GF5_ENOTSUP;
    if (ver>=0x30) {
        if (!span(g,d,10) || le32(g,d+6)!=DCB_SIGNATURE) return GF5_EBIOS;
        u32 hdr=g->rom.bytes[d+1];
        if (hdr<10 || !span(g,d,hdr)) return GF5_EBIOS;
        if (hdr>=DCB_GPIO_HEADER) p=le16(g,d+DCB_GPIO_POINTER);
    } else {
        if (!span(g,d,8) || le32(g,d+4)!=DCB_SIGNATURE) return GF5_EBIOS;
        if (ver>=0x22) {
            if (d<DCB_OLD_GPIO_BACK) return GF5_EBIOS;
            if (g->rom.bytes[d-1]>=DCB_OLD_PREFIX) p=le16(g,d-DCB_OLD_GPIO_BACK);
        }
    }
    if (!p) return GF5_ENODEV;
    if (!span(g,p,3)) return GF5_EBIOS;
    struct table t={.off=p,.ver=g->rom.bytes[p]};
    if (t.ver>0x41) return GF5_ENOTSUP;
    t.cnt=g->rom.bytes[p+2];
    if (t.ver<0x30) { t.hdr=3; t.len=g->rom.bytes[p+1]; }
    else {
        if (!span(g,p,4)) return GF5_EBIOS;
        t.hdr=g->rom.bytes[p+1]; t.len=g->rom.bytes[p+3];
    }
    i32 rc=table_span(g,&t,t.ver<0x30 ? 3 : 4,t.ver<0x40 ? 2 : (t.ver==0x40 ? 4 : 5));
    if (rc) return rc;
    for (u32 i=0;i<t.cnt;++i) {
        u32 e=p+t.hdr+i*t.len;
        u32 v=t.ver<0x40 ? le16(g,e) : le32(g,e);
        u8 func=t.ver<0x40 ? (v>>5)&0x3f : (v>>8)&0xff;
        if (func!=tag) continue;
        u8 l=v&(t.ver==0x41 ? 0x3f : 0x1f),a,b;
        if (l>=GPIO_LINES) return GF5_EINVAL;
        if (t.ver<0x40) { a=(v>>11)&3; b=(v>>13)&3; }
        else if (t.ver==0x40) { a=(v>>27)&3; b=(v>>29)&3; }
        else { a=(g->rom.bytes[e+4]>>4)&3; b=(g->rom.bytes[e+4]>>6)&3; }
        *line=l; log[0]=a; log[1]=b;
        return 0;
    }
    return GF5_ENODEV;
}
i32 gf5_gpio_find(struct gf5 *g,u8 tag,u8 *line,u8 log[2])
{
    if (g) g->source_rc=0;
    return done(g,gpio_find(g,tag,line,log));
}
/* gpio/nv10.c:29-79; gpio/base.c:80-108. */
static u32 gpio_reg(u8 line,u32 *shift)
{
    if (line<2) { *shift=16U*line; return GPIO_LOW; }
    if (line<10) { *shift=4U*(line-2); return GPIO_MID; }
    *shift=4U*(line-10); return GPIO_HIGH;
}
static i32 io(struct gf5 *g,u32 reg,u32 *value,bool write)
{
    if (!g->ch || !g->ch->f) return GF5_EINVAL;
    const struct gf_io *ops=&g->ch->f->io;
    if ((write && !ops->write) || (!write && !ops->read)) return GF5_EINVAL;
    i32 rc=write ? ops->write(ops->opaque,GPIO_BAR,reg,GPIO_WIDTH,*value) :
                   ops->read(ops->opaque,GPIO_BAR,reg,GPIO_WIDTH,value);
    if (rc) { g->source_rc=rc; return GF5_EIO; }
    return 0;
}
i32 gf5_gpio_get(struct gf5 *g,u8 line,const u8 log[2],bool *out)
{
    if (g) g->source_rc=0;
    if (!g || !log || !out || line>=GPIO_LINES) return done(g,GF5_EINVAL);
    u32 shift,v,reg=gpio_reg(line,&shift);
    i32 rc=io(g,reg,&v,false);
    if (!rc) *out=!!(v&(1U<<(shift+(line<2 ? 8 : 2))))==!!(log[1]&1);
    return done(g,rc);
}
i32 gf5_gpio_set(struct gf5 *g,u8 line,const u8 log[2],bool value)
{
    if (g) g->source_rc=0;
    if (!g || !log || line>=GPIO_LINES) return done(g,GF5_EINVAL);
    u32 shift,old,reg=gpio_reg(line,&shift);
    i32 rc=io(g,reg,&old,false);
    if (!rc) {
        u32 state=log[value],dir=(state>>1)&1,out=state&1;
        u32 mask=line<2 ? 0x11 : 3;
        u32 data=line<2 ? (dir<<4)|out : (dir<<1)|out;
        old=(old&~(mask<<shift))|(data<<shift);
        rc=io(g,reg,&old,true);
    }
    return done(g,rc);
}
/* bios/volt.c:29-100,137-150. Whole table validated before MMIO. */
static i32 voltage_table(struct gf5 *g,struct table *t,struct pin pins[VID_BITS])
{
    i32 rc=power_pointer(g,false,&t->off);
    if (rc) return rc;
    u32 p=t->off;
    if (!p) return GF5_ENODEV;
    if (!span(g,p,1)) return GF5_EBIOS;
    t->ver=g->rom.bytes[p];
    if (t->ver!=0x12 && t->ver!=0x20 && t->ver!=0x30) return GF5_ENOTSUP;
    u32 hdr=t->ver==0x20 ? 6 : 5;
    if (!span(g,p,hdr)) return GF5_EBIOS;
    t->hdr=t->ver==0x12 ? 5 : g->rom.bytes[p+1];
    t->cnt=g->rom.bytes[p+(t->ver==0x30 ? 3 : 2)];
    t->len=g->rom.bytes[p+(t->ver==0x12 ? 1 : (t->ver==0x20 ? 3 : 2))];
    t->mask=g->rom.bytes[p+(t->ver==0x20 ? 5 : 4)];
    rc=table_span(g,t,hdr,2);
    if (rc) return rc;
    /* volt/base.c:195-223,312-317 retains an empty VID table. */
    /* volt/gpio.c:30-97, bios/gpio.h:15-22. */
    static const u8 tags[VID_BITS]={0x04,0x05,0x06,0x1a,0x73,0x74,0x75,0x76};
    for (u32 i=0;i<VID_BITS;++i) if (t->mask&(1U<<i)) {
        rc=gpio_find(g,tags[i],&pins[i].line,pins[i].log);
        if (rc==GF5_ENODEV) t->mask&=~(1U<<i);
        else if (rc) return rc;
    }
    return 0;
}
static u8 entry_vid(const struct gf5 *g,const struct table *t,u32 i)
{
    u8 vid=g->rom.bytes[t->off+t->hdr+i*t->len+1];
    if (t->ver==0x30) vid >>= 2;
    /* bios/volt.c:143-150, volt/base.c:212-214: keep the table VID.
     * volt/gpio.c:84-93 masks only GPIO reads, never table entries. */
    return vid;
}
static u32 entry_uv(const struct gf5 *g,const struct table *t,u32 i)
{ return (u32)g->rom.bytes[t->off+t->hdr+i*t->len]*VOLT_UNIT_UV; }
static i32 voltage_get(struct gf5 *g,u32 *out)
{
    struct table t={0}; struct pin pins[VID_BITS]; u8 vid=0;
    i32 rc=voltage_table(g,&t,pins);
    if (rc) return rc;
    if (g->vid.get) {
        rc=g->vid.get(g->vid.owner,&vid);
        if (rc) { g->source_rc=rc; return gf5_rc_map(rc); }
        vid&=t.mask;
    } else for (u32 i=0;i<VID_BITS;++i) if (t.mask&(1U<<i)) {
        bool value;
        rc=gf5_gpio_get(g,pins[i].line,pins[i].log,&value);
        if (rc) return rc;
        if (value) vid|=1U<<i;
    }
    for (u32 i=0;i<t.cnt;++i) if (entry_vid(g,&t,i)==vid) {
        *out=entry_uv(g,&t,i); return 0;
    }
    return GF5_ESTATE;
}
i32 gf5_volt_get(struct gf5 *g,u32 *out)
{
    if (g) g->source_rc=0;
    if (!g || !out) return done(g,GF5_EINVAL);
    return done(g,voltage_get(g,out));
}
static i32 voltage_choice(const struct gf5 *g,const struct table *t,u32 uv,u8 *vid)
{
    u32 best=t->cnt,best_uv=U32_MAX;
    /* volt/base.c:51-78: equal nonzero errors replace, exact exits first. */
    for (u32 i=0;i<t->cnt;++i) {
        u32 candidate=entry_uv(g,t,i);
        if (candidate<uv || candidate>best_uv) continue;
        best=i; best_uv=candidate;
        if (candidate==uv) break;
    }
    if (best==t->cnt) return GF5_ERANGE;
    *vid=entry_vid(g,t,best);
    return 0;
}
static i32 voltage_set(struct gf5 *g,u32 uv)
{
    struct table t={0}; struct pin pins[VID_BITS]; u8 vid;
    i32 rc=voltage_table(g,&t,pins);
    if (!rc) rc=voltage_choice(g,&t,uv,&vid);
    if (rc) return rc;
    if (g->vid.set) {
        rc=g->vid.set(g->vid.owner,vid);
        if (rc) g->source_rc=rc;
        return gf5_rc_map(rc);
    }
    for (u32 i=0;i<VID_BITS;++i) if (t.mask&(1U<<i)) {
        rc=gf5_gpio_set(g,pins[i].line,pins[i].log,!!(vid&(1U<<i)));
        if (rc) return rc;
    }
    return 0;
}
i32 gf5_volt_set(struct gf5 *g,u32 uv)
{
    if (!g) return GF5_EINVAL;
    g->source_rc=0;
    return done(g,voltage_set(g,uv));
}
/* bios/vmap.c:29-53,93-153; volt/base.c:104-153,233-239.
 * nv40 has no speedo_read. The handoff checks speedo BEFORE every mode,
 * including constant modes: a present valid entry is always ENOTSUP here. */
static i32 voltage_map_request(struct gf5 *g,u8 id,u32 *uv,bool optional_zero)
{
    u32 p=0;
    i32 rc=power_pointer(g,true,&p);
    if (rc) return rc;
    if (p) {
        if (!span(g,p,1)) return GF5_EBIOS;
        /* bios/vmap.c:29-53: unknown versions mean no map table. */
        if (g->rom.bytes[p]!=0x10 && g->rom.bytes[p]!=0x20) p=0;
    }
    if (p) {
        if (!span(g,p,4)) return GF5_EBIOS;
        struct table t={.off=p,.ver=g->rom.bytes[p],.hdr=g->rom.bytes[p+1],
            .len=g->rom.bytes[p+2],.cnt=g->rom.bytes[p+3]};
        rc=table_span(g,&t,4,t.ver==0x10 ? 0x14 : 0x22);
        if (rc) return rc;
        if (id<t.cnt) return GF5_ENOTSUP;
    }
    /* A zero minimum without a vmap entry means no extra floor.  A real
     * entry (including index zero) still requires speedo above. */
    if (!id && !optional_zero) return GF5_ENODEV;
    *uv=(u32)id*VOLT_UNIT_UV;
    return 0;
}
static i32 voltage_map(struct gf5 *g,u8 id,u32 *uv)
{ return voltage_map_request(g,id,uv,false); }
i32 gf5_volt_check_id(struct gf5 *g,u8 id,u8 min_id)
{
    if (!g) return GF5_EINVAL;
    g->source_rc=0;
    struct table t={0}; struct pin pins[VID_BITS]; u8 vid;
    u32 request,minimum;
    /* volt/base.c:168-182: a failed map never reaches get/set. */
    i32 rc=voltage_map(g,id,&request);
    if (!rc) rc=voltage_table(g,&t,pins);
    if (!rc) {
        rc=voltage_map_request(g,min_id,&minimum,true);
        if (!rc && minimum>request) request=minimum;
    }
    if (!rc) rc=voltage_choice(g,&t,request,&vid);
    return done(g,rc);
}
i32 gf5_volt_id(struct gf5 *g,u8 id,u8 min_id,i32 condition)
{
    if (!g) return GF5_EINVAL;
    g->source_rc=0;
    u32 request,prev,minimum;
    i32 rc=voltage_map(g,id,&request);
    if (rc) return done(g,rc);
    rc=voltage_get(g,&prev);
    /* volt/base.c:156-182: compare request to prev BEFORE mapping min_id. */
    if (!condition || rc || (condition<0 && request<prev) ||
        (condition>0 && request>prev)) {
        if (!voltage_map(g,min_id,&minimum) && minimum>request) request=minimum;
        g->source_rc=0; /* A failed previous read does not fail a successful set. */
        return done(g,voltage_set(g,request));
    }
    return done(g,0);
}
