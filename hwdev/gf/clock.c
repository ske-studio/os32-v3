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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 * Authors: Ben Skeggs
 */
#include "feature5.h"
#include "command.h"
#include "foundation.h"
#include "../../kdebug/gfinit_pll.h"
#include "../../kdebug/gfinit_decode.h"

/* nouveau nvkm/subdev/clk/nv40.c:39-196; fb/ramnv40.c:34-63. */
#define CLK_CORE 0x4000U
#define CLK_COEF 0x4004U
#define CLK_SHADER 0x4008U
#define CLK_MEM 0x4020U
#define CLK_MUX 0xc040U
#define CLK_ENABLE 0x80000000U
#define CLK_DOUBLE 0x40000000U
#define CLK_SINGLE 0x100U
#define CLK_SETTLE_US 5000U
#define CLK_MUX_MASK 0x333U
#define CLK_CORE_MASK 0xc0070100U
#define CLK_SHADER_MASK 0xc007ffffU
#define CLK_SHARED 0x333U
#define CLK_SEPARATE 0x223U

static i32 result(struct gf5 *g, i32 rc)
{
    if (g) { g->source_rc=rc; g->last_rc=gf5_rc_map(rc); return g->last_rc; }
    return rc;
}
static i32 classified(struct gf5 *g, i32 source, i32 category)
{
    if (g) { g->source_rc=source; g->last_rc=gf5_rc_map(category); return g->last_rc; }
    return gf5_rc_map(category);
}
static struct gf_io *clock_io(struct gf5 *g)
{
    return g && g->ch && g->ch->f ? &g->ch->f->io : 0;
}
static int rd(struct gf_io *io,u32 reg,u32 *v)
{
    return io->read(io->opaque,0,reg,4,v);
}
static int mask(struct gf_io *io,u32 reg,u32 bits,u32 value)
{
    u32 old;
    int rc=rd(io,reg,&old);
    return rc ? rc : io->write(io->opaque,0,reg,4,(old & ~bits)|value);
}
static int read_pll(struct gf_io *io,u32 reg,bool single,u32 *out)
{
    u32 c,n,m,p,v;
    int rc=rd(io,reg,&c);
    if (rc) return rc;
    p=(c>>16)&7U;
    if (single) v=c;
    else { rc=rd(io,reg+4,&v); if (rc) return rc; }
    n=(v>>8)&255U; m=v&255U;
    if (!(c&CLK_ENABLE) || !m) { *out=0; return 0; }
    u32 khz=GF_PLL_REF*n/m;
    if (!single && (c&(CLK_DOUBLE|CLK_SINGLE))==CLK_DOUBLE) {
        n=(v>>24)&255U; m=(v>>16)&255U;
        if (!m) { *out=0; return 0; }
        khz=khz*n/m;
    }
    *out=khz>>p;
    return 0;
}
static int read_source(struct gf_io *io,u32 src,u32 *out)
{
    if (src==3) return read_pll(io,CLK_CORE,false,out);
    if (src==2) return read_pll(io,CLK_SHADER,true,out);
    *out=0; return 0;
}
i32 gf5_clock_read(struct gf5 *g,struct gf5_clock *out)
{
    struct gf_io *io=clock_io(g); struct gf5_clock c; u32 mux;
    if (!io || !io->read || !out) return result(g,GF5_EINVAL);
    int rc=rd(io,CLK_MUX,&mux);
    if (!rc) rc=read_source(io,mux&3U,&c.core_khz);
    if (!rc) rc=read_source(io,(mux>>4)&3U,&c.shader_khz);
    if (!rc) rc=read_pll(io,CLK_MEM,false,&c.mem_khz);
    if (rc) return result(g,rc);
    *out=c;
    return result(g,c.core_khz && c.shader_khz && c.mem_khz ? 0 : GF5_ESTATE);
}
static u32 coefficient(const struct gfinit_pll_solution *s)
{
    u32 v=(s->n1<<8)|s->m1;
    if (s->n2!=s->m2) v|=(s->n2<<24)|(s->m2<<16);
    return v;
}
static i32 solve(struct gf5 *g,u32 reg,u32 khz,bool disable_second,
                 struct gfinit_pll_limits *l,struct gfinit_pll_solution *s)
{
    int rc=gfinit_pll_parse(g->rom.bytes,g->rom.size,GF_INIT_PLL_TABLE,reg,l);
    if (rc) return classified(g,rc,GF5_EBIOS);
    if (disable_second && khz<l->vco1.max_freq) l->vco2.max_freq=0;
    rc=gfinit_pll_solve(l,khz,s);
    return rc ? classified(g,rc,GF5_ERANGE) : 0;
}
i32 gf5_clock_calc(struct gf5 *g,const struct gf5_clock *request,struct gf5_plan *out)
{
    struct gfinit_pll_limits l; struct gfinit_pll_solution s; struct gf5_plan p={0}; i32 rc;
    if (!g || !request || !out || !request->core_khz || !request->mem_khz)
        return result(g,GF5_EINVAL);
    if (!g->rom.bytes) return result(g,GF5_ENOTSUP);
    rc=solve(g,GF_PLL_CORE,request->core_khz,true,&l,&s);
    if (rc) return rc;
    p.nc=CLK_ENABLE|(s.p<<16)|(s.n2==s.m2 ? CLK_SINGLE : CLK_DOUBLE);
    p.nn=coefficient(&s); p.actual.core_khz=s.actual;
    if (request->shader_khz && request->shader_khz!=request->core_khz) {
        /* v2x BIOS parser has no 004008 key (bios/pll.c:46-52,245). */
        return result(g,GF5_EBIOS);
    }
    p.mux=CLK_SHARED; p.actual.shader_khz=p.actual.core_khz;
    rc=solve(g,GF_PLL_MEMORY,request->mem_khz,false,&l,&s);
    if (rc) return rc;
    u32 bias=l.bias_p+s.p; if (bias>l.max_p) bias=l.max_p;
    p.mc=CLK_ENABLE|(s.p<<16)|(bias<<20)|(s.n2==s.m2 ? CLK_SINGLE : CLK_DOUBLE);
    p.mn=coefficient(&s); p.actual.mem_khz=s.actual;
    *out=p; return result(g,0);
}
i32 gf5_clock_prog(struct gf5 *g,const struct gf5_plan *p)
{
    struct gf_io *io=clock_io(g);
    if (!io || !io->read || !io->write || !io->timing.wait_us || !p)
        return result(g,GF5_EINVAL);
    int rc=mask(io,CLK_MUX,CLK_MUX_MASK,0);
    if (!rc) rc=io->write(io->opaque,0,CLK_COEF,4,p->nn);
    if (!rc) rc=mask(io,CLK_CORE,CLK_CORE_MASK,p->nc);
    if (!rc) rc=mask(io,CLK_SHADER,CLK_SHADER_MASK,p->sc);
    for (u32 remaining=CLK_SETTLE_US; !rc && remaining; ) {
        u32 chunk=remaining>GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : remaining;
        rc=io->timing.wait_us(io->opaque,chunk);
        remaining-=chunk;
    }
    if (!rc) rc=mask(io,CLK_MUX,CLK_MUX_MASK,p->mux);
    if (rc) return result(g,rc);
    return result(g,0);
}
