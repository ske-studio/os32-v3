/*
 * Copyright 2005-2006 Erik Waling
 * Copyright 2006 Stephane Marchesin
 * Copyright 1993-2003 NVIDIA, Corporation
 * Copyright 2007-2009 Stuart Bennett
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
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/*
 * Copyright (C) 2010 Francisco Jerez.
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
/* NV44A-only adaptation of nouveau c03c1a1 MIT bios/pll.c:301-333,
 * clk/pllnv04.c:28-247 and devinit/nv04.c:200-353. Data tables only.
 * Arithmetic is u32 throughout; no libc, floating point or runtime helpers. */
#include "gfinit_pll.h"
#include <stdbool.h>
STATIC_ASSERT(GF_PLL_REF <= 0xffffffffU / GF_PLL_NM_MAX / GF_PLL_NM_MAX, pll_product);
static int pll_span(u32 size, u32 at, u32 n)
{ return at <= size && n <= size-at ? 0 : -1; }
static u32 pll_le(const u8 *r, u32 at, u32 n)
{
    u32 v=0;
    for (u32 i=0;i<n;i++) v |= (u32)r[at+i] << (8*i);
    return v;
}
static int vco_ok(const struct gfinit_pll_vco *v)
{
    return v->min_freq && v->min_freq <= v->max_freq &&
        v->min_input && v->min_input <= v->max_input &&
        v->min_n && v->min_n <= v->max_n && v->max_n <= GF_PLL_NM_MAX &&
        v->min_m && v->min_m <= v->max_m && v->max_m <= GF_PLL_NM_MAX;
}
static int limits_ok(const struct gfinit_pll_limits *l)
{
    return l && l->ref == GF_PLL_REF && l->max_p <= 7 && l->bias_p <= 7 &&
        vco_ok(&l->vco1) && (!l->vco2.max_freq || vco_ok(&l->vco2));
}
int gfinit_pll_parse(const u8 *r, u32 size, u32 table, u32 reg, struct gfinit_pll_limits *o)
{
    if (!r || !o || pll_span(size,table,4) || r[table]!=0x21 || r[table+1]<4 ||
        r[table+2]<31 || r[table+3]!=4) return -1;
    u32 header=r[table+1], len=r[table+2], count=r[table+3];
    if (pll_span(size,table,header+len*count)) return -1;
    u32 key=reg;
    if (reg==GF_PLL_CORE || reg==GF_PLL_MEMORY) key-=4;
    else if (reg!=GF_PLL_VIDEO) return -1;
    u32 found=0;
    for (u32 i=0;i<count;i++) {
        u32 at=table+header+i*len;
        if (pll_le(r,at,4)!=key) continue;
        if (found++) return -1;
        *o=(struct gfinit_pll_limits){0};
        o->vco1=(struct gfinit_pll_vco){pll_le(r,at+4,2)*1000,pll_le(r,at+6,2)*1000,
            pll_le(r,at+12,2)*1000,pll_le(r,at+16,2)*1000,r[at+20],r[at+21],r[at+22],r[at+23]};
        o->vco2=(struct gfinit_pll_vco){pll_le(r,at+8,2)*1000,pll_le(r,at+10,2)*1000,
            pll_le(r,at+14,2)*1000,pll_le(r,at+18,2)*1000,r[at+24],r[at+25],r[at+26],r[at+27]};
        o->max_p=r[at+29]; o->bias_p=r[at+30];
        o->ref=len>34 ? pll_le(r,at+31,4) : 0;
        if (!o->ref) o->ref=GF_PLL_REF;
        if (!limits_ok(o)) return -1;
    }
    return found==1 ? 0 : -1;
}
static void pll_best(struct gfinit_pll_solution *s,u32 clk,u32 actual,
                     u32 n1,u32 m1,u32 n2,u32 m2,u32 p)
{
    u32 error=actual>clk?actual-clk:clk-actual;
    if (actual && (!s->actual || error<s->error))
        *s=(struct gfinit_pll_solution){n1,m1,n2,m2,p,actual,error};
}
int gfinit_pll_solve(const struct gfinit_pll_limits *l,u32 clk,struct gfinit_pll_solution *s)
{
    if (!s || !limits_ok(l) || !clk || clk>(0xffffffffU>>GF_PLL_P_MAX)) return -1;
    *s=(struct gfinit_pll_solution){0};
    const struct gfinit_pll_vco *a=&l->vco1,*b=&l->vco2;
    u32 max=a->max_freq,min=a->min_freq;
    if (!b->max_freq) {
        if ((clk<<GF_PLL_P_MAX)<min) { min=clk*GF_PLL_P_MAX; max=min*2; }
        if (clk+clk/200>max) max=clk+clk/200;
        for (u32 p=0;p<=GF_PLL_P_MAX;p++) {
            u32 div=1U<<p, cp=clk<<p;
            if (cp<min) continue;
            if (cp>max) break;
            for (u32 m=a->min_m;m<=a->max_m;m++) {
                if (GF_PLL_REF/m<a->min_input) break;
                if (GF_PLL_REF/m>a->max_input) continue;
                if (cp>(0xffffffffU-GF_PLL_REF/2)/m) return -1;
                u32 n=(cp*m+GF_PLL_REF/2)/GF_PLL_REF;
                if (n<a->min_n) continue;
                if (n>a->max_n) break;
                u32 actual=((n*GF_PLL_REF+div/2)/div+m/2)/m;
                pll_best(s,clk,actual,n,m,1,1,p);
                if (s->actual && !s->error) return 0;
            }
        }
    } else {
        u32 p=0, half=(b->max_freq-b->max_freq/200)/2;
        while (p<GF_PLL_P_MAX && clk<=(half>>p)) p++;
        u32 cp=clk<<p, max2=b->max_freq;
        if (max2<clk+clk/200) max2=clk+clk/200;
        bool fixed=b->min_m==b->max_m && b->min_n==b->max_n;
        for (u32 m1=a->min_m;m1<=a->max_m;m1++) {
            if (GF_PLL_REF/m1<a->min_input) break;
            if (GF_PLL_REF/m1>a->max_input) continue;
            for (u32 n1=a->min_n;n1<=a->max_n;n1++) {
                u32 c1=GF_PLL_REF*n1/m1;
                if (c1<a->min_freq) continue;
                if (c1>a->max_freq) break;
                for (u32 m2=b->min_m;m2<=b->max_m;m2++) {
                    if (c1/m2<b->min_input) break;
                    if (c1/m2>b->max_input) continue;
                    if (cp>(0xffffffffU-c1/2)/m2) return -1;
                    u32 n2=(cp*m2+c1/2)/c1;
                    if (n2<b->min_n) continue;
                    if (n2>b->max_n) break;
                    u32 c2=c1;
                    if (!fixed) {
                        if (n2/m2<4 || n2/m2>10) continue;
                        c2=c1*n2/m2;
                        if (c2<b->min_freq) break;
                        if (c2>max2) continue;
                    }
                    pll_best(s,clk,c2>>p,n1,m1,n2,m2,p);
                    if (s->actual && !s->error) return 0;
                }
            }
        }
    }
    return s->actual ? 0 : -1;
}
static void pll_write(struct gfinit_pll_sequence *q,u32 reg,u32 source,u32 keep,u32 value)
{ q->writes[q->count++]=(struct gfinit_pll_write){reg,source,keep,value}; }
int gfinit_pll_sequence(u32 reg,const struct gfinit_pll_limits *l,
                        const struct gfinit_pll_solution *s,struct gfinit_pll_sequence *q)
{
    if (!q || !s || !limits_ok(l) || !s->actual || !s->m1 || !s->m2 ||
        !s->n1 || !s->n2 || s->n1>255 || s->n2>255 || s->m1>255 || s->m2>255 || s->p>GF_PLL_P_MAX) return -1;
    q->count=0;
    u32 nm1=s->n1<<8|s->m1,nm2=s->n2<<8|s->m2;
    bool single=s->n2==s->m2;
    if (reg==GF_PLL_VIDEO) {
        pll_write(q,GF_PLL_GATE,GF_PLL_GATE,~(3U<<16),0);
        pll_write(q,GF_PLL_VIDEO_STAGE,GF_PLL_VIDEO_STAGE,~(1U<<8),single?1U<<8:0);
        pll_write(q,GF_PLL_VIDEO_COEFF,GF_PLL_VIDEO_COEFF,0x7fff0000U,0x80000000U|nm2|(single?0x011fU:0));
        pll_write(q,reg,reg,0xfff80000U,s->p<<16|nm1);
        pll_write(q,GF_PLL_GATE,GF_PLL_GATE,~0U,0);
    } else if (reg==GF_PLL_CORE || reg==GF_PLL_MEMORY) {
        bool mem=reg==GF_PLL_MEMORY;
        u32 preg=reg-4, keep=mem?~(0x77U<<16):~(7U<<16);
        u32 bits=0xc0000000U|s->p<<16;
        if (mem) {
            u32 bias=s->p+l->bias_p;
            if (bias>l->max_p) bias=l->max_p;
            bits|=1U<<28|bias<<20;
            pll_write(q,GF_PLL_MEMORY_POWER,GF_PLL_MEMORY_POWER,~0U,8U<<28);
        }
        if (single) bits|=mem?1U<<12:1U<<8;
        pll_write(q,preg,preg,~0U,1U<<28);
        pll_write(q,preg,preg,keep&~(4U<<28),bits&~(4U<<28));
        if (mem) {
            bits|=8U<<20;
            pll_write(q,preg,preg,keep&~(0xcU<<28),bits&~(0xcU<<28));
            pll_write(q,GF_PLL_MEMORY_MIRROR,preg,keep&~(0xcU<<28),bits&~(0xcU<<28));
        }
        pll_write(q,GF_PLL_GATE,GF_PLL_GATE,mem?~(3U<<14):~0x333U,0);
        pll_write(q,reg,0,0,nm2<<16|nm1);
        if (mem) pll_write(q,GF_PLL_MEMORY_COEFF,0,0,nm2<<16|nm1);
        pll_write(q,preg,preg,keep,bits);
        if (mem) {
            bits&=~(8U<<20); keep&=~(8U<<20);
            pll_write(q,preg,preg,keep,bits);
            pll_write(q,GF_PLL_MEMORY_MIRROR,preg,keep,bits);
            pll_write(q,GF_PLL_MEMORY_POWER,GF_PLL_MEMORY_POWER,~0U,0);
        }
        pll_write(q,GF_PLL_GATE,GF_PLL_GATE,~0U,0);
        if (mem) {
            pll_write(q,preg,preg,keep&~(1U<<28),bits&~(1U<<28));
            pll_write(q,GF_PLL_MEMORY_MIRROR,preg,keep&~(1U<<28),bits&~(1U<<28));
        }
    } else return -1;
    return 0;
}
