/*
 * Copyright 2012 Nouveau Community
 * Copyright 2013 Red Hat Inc.
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
 *
 * Authors of the reference: Martin Peres, Ben Skeggs
 */
#include "feature5.h"
#define GF_ROM_BIT_ONLY
#include "rom.inc"
#undef GF_ROM_BIT_ONLY

/* nouveau c03c1a1 nvkm/subdev/bios/perf.c:30-145. Offsets here are
 * byte offsets in the ROM, not device addresses. BMP signature:
 * nvkm/subdev/bios/base.c:181-182. Only the NV4a decode families are used. */
enum {
    PERF_BIT_PTR_BYTES=4, PERF_BIT_HEADER=6, PERF_BMP_HEADER=4,
    PERF_BMP_MINOR=6, PERF_BMP_MIN_MINOR=0x25, PERF_BMP_PTR=0x94,
    PERF_OLD_MIN_ENTRY=0x38, PERF_OLD_VOLT=0x38,
    PERF_NV4_MIN_ENTRY=0x0d, PERF_NEW_MIN_ENTRY=0x0e,
    PERF_KHZ_PER_MHZ=1000, PERF_NV4_MEMORY_SCALE=2000,
    PERF_OLD_CORE_SCALE=10, PERF_OLD_MEMORY_SCALE=20
};
struct perf_table { u32 base, header, count, length, stride; u8 version; };

static bool span(const struct gf5_rom_view *r,u32 off,u32 bytes)
{
    return off<=r->size && bytes<=r->size-off;
}
static u32 le16(const u8 *p) { return (u32)p[0] | (u32)p[1]<<8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p+2)<<16; }
static i32 finish(struct gf5 *g,i32 rc)
{
    if (g) { g->source_rc=rc; g->last_rc=gf5_rc_map(rc); }
    return gf5_rc_map(rc);
}

static i32 table_read(struct gf5 *g,struct perf_table *t)
{
    const struct gf5_rom_view *r=&g->rom;
    if (!r->bytes || !r->size) return GF5_ENOTSUP;
    u32 data=0,length=0,base=0;
    u8 version=0;
    int found=gf_rom_bit_entry(r->bytes,r->size,'P',&data,&length,&version);
    if (found<0) return GF5_EBIOS;
    if (!found && (version==1 || version==2)) {
        if (length<PERF_BIT_PTR_BYTES) return GF5_EBIOS;
        base=le32(r->bytes+data);
        if (base) {
            if (!span(r,base,PERF_BIT_HEADER)) return GF5_EBIOS;
            const u8 *p=r->bytes+base;
            if (p[0]<0x20 || p[0]>=0x40) return GF5_ENOTSUP;
            *t=(struct perf_table){base,p[1],p[2],p[3],p[3]+p[4]*p[5],p[0]};
            if (t->header<PERF_BIT_HEADER) return GF5_EBIOS;
        }
    }
    if (!base) {
        static const u8 signature[]={0xff,0x7f,'N','V',0};
        for (u32 off=0;span(r,off,sizeof(signature));++off) {
            u32 i=0;
            while (i<sizeof(signature) && r->bytes[off+i]==signature[i]) ++i;
            if (i!=sizeof(signature)) continue;
            if (!span(r,off,PERF_BMP_MINOR+1)) return GF5_EBIOS;
            if (r->bytes[off+PERF_BMP_MINOR]<PERF_BMP_MIN_MINOR) return GF5_ENOTSUP;
            if (!span(r,off,PERF_BMP_PTR+2)) return GF5_EBIOS;
            base=le16(r->bytes+off+PERF_BMP_PTR);
            if (!base) return GF5_ENOTSUP;
            if (!span(r,base,PERF_BMP_HEADER)) return GF5_EBIOS;
            const u8 *p=r->bytes+base;
            *t=(struct perf_table){base,p[0],p[2],p[3],p[3],p[1]};
            if (t->header<PERF_BMP_HEADER) return GF5_EBIOS;
            break;
        }
        if (!base) return GF5_ENOTSUP;
    }
    u32 min_entry;
    switch (t->version) {
    case 0x12: case 0x13: case 0x14: min_entry=PERF_OLD_MIN_ENTRY; break;
    case 0x21: case 0x23: case 0x24: min_entry=PERF_NV4_MIN_ENTRY; break;
    case 0x25: case 0x30: case 0x35: min_entry=PERF_NEW_MIN_ENTRY; break;
    default: return GF5_ENOTSUP;
    }
    if (t->length<min_entry || !span(r,base,t->header+t->count*t->stride))
        return GF5_EBIOS;
    return GF5_OK;
}

/* clk/base.c:421-425: disabled pstates never enter the public list. */
static bool entry_valid(const u8 *p)
{
    return p[0]!=0xff;
}

static i32 entry_read(struct gf5 *g,u32 index,struct gf5_perf_entry *out)
{
    struct perf_table t;
    i32 rc=table_read(g,&t);
    if (rc) return rc;
    u32 bios_index=0, valid_index=0;
    const u8 *p=g->rom.bytes+t.base+t.header;
    for (;bios_index<t.count;++bios_index,p+=t.stride) {
        if (!entry_valid(p)) continue;
        if (valid_index==index) break;
        ++valid_index;
    }
    if (bios_index==t.count) return GF5_EINVAL;
    struct gf5_perf_entry e={0};
    e.pstate=p[0]; e.bios_index=(u8)bios_index;
    switch (t.version) {
    case 0x12: case 0x13: case 0x14:
        if (le32(p+1)>(u32)0xffffffffU/PERF_OLD_CORE_SCALE ||
            le32(p+5)>(u32)0xffffffffU/PERF_OLD_MEMORY_SCALE) return GF5_EBIOS;
        e.clock.core_khz=le32(p+1)*PERF_OLD_CORE_SCALE;
        e.clock.mem_khz=le32(p+5)*PERF_OLD_MEMORY_SCALE;
        e.fanspeed=p[0x37];
        if (t.length>PERF_OLD_VOLT) e.voltage=p[PERF_OLD_VOLT];
        break;
    case 0x21: case 0x23: case 0x24: {
        e.fanspeed=p[4]; e.voltage=p[5];
        e.clock.shader_khz=le16(p+6)*PERF_KHZ_PER_MHZ;
        i32 core=(i32)e.clock.shader_khz+(i32)(signed char)p[8]*PERF_KHZ_PER_MHZ;
        if (core<0) return GF5_EBIOS;
        e.clock.core_khz=(u32)core;
        e.clock.mem_khz=le16(p+0x0b)*PERF_NV4_MEMORY_SCALE;
        break;
    }
    case 0x25:
        e.fanspeed=p[4]; e.voltage=p[5];
        e.clock.core_khz=le16(p+6)*PERF_KHZ_PER_MHZ;
        e.clock.shader_khz=le16(p+0x0a)*PERF_KHZ_PER_MHZ;
        e.clock.mem_khz=le16(p+0x0c)*PERF_KHZ_PER_MHZ;
        break;
    case 0x30: case 0x35:
        e.fanspeed=p[6]; e.voltage=p[7];
        e.clock.core_khz=le16(p+8)*PERF_KHZ_PER_MHZ;
        e.clock.shader_khz=le16(p+0x0a)*PERF_KHZ_PER_MHZ;
        e.clock.mem_khz=le16(p+0x0c)*PERF_KHZ_PER_MHZ;
        break;
    }
    *out=e;
    return GF5_OK;
}

i32 gf5_perf_count(struct gf5 *g,u32 *out)
{
    if (!g || !out) return finish(g,GF5_EINVAL);
    struct perf_table t;
    i32 rc=table_read(g,&t);
    if (!rc) {
        u32 count=0;
        const u8 *p=g->rom.bytes+t.base+t.header;
        for (u32 i=0;i<t.count;++i,p+=t.stride)
            if (entry_valid(p)) ++count;
        *out=count;
    }
    return finish(g,rc);
}
i32 gf5_perf_get(struct gf5 *g,u32 index,struct gf5_perf_entry *out)
{
    if (!g || !out) return finish(g,GF5_EINVAL);
    return finish(g,entry_read(g,index,out));
}

/* service owns busy/phase admission checks and the outer absolute deadline.
 * Do not replace that deadline here: restore may nest performance work.
 * PM decision 15: clock_calc fills mc/mn too; RAM only programs the plan.
 * PM decision 21: RAM owns its pause/unpause and admission preservation.
 * In particular, nested restore must keep admission closed until publication.
 * Ordering: nouveau nvkm/subdev/clk/base.c:159-215,266-305; OS32 aborts
 * on intermediate error instead of following the reference's lower path. */
i32 gf5_perf_select(struct gf5 *g,u8 index)
{
    if (!g) return GF5_EINVAL;
    struct gf5_perf_entry e;
    struct gf5_plan plan={0};
    bool programming=false;
    i32 rc=entry_read(g,index,&e), absent=0;
    if (rc) return finish(g,rc);
    g->source_rc=0;
    rc=gf5_clock_calc(g,&e.clock,&plan);
    if (rc) {
        g->last_rc=gf5_rc_map(rc);
        if (!g->source_rc) g->source_rc=rc;
        return g->last_rc; /* No hardware change or FAULT on calc failure. */
    }
#define STEP(call) do { g->source_rc=0; rc=(call); if (rc) goto fault; } while (0)
#define OPTIONAL(call) do { \
    g->source_rc=0; rc=(call); \
    if (gf5_rc_map(rc)==GF5_ENODEV) { absent=g->source_rc ? g->source_rc : rc; } \
    else if (rc) goto fault; \
} while (0)
    /* clk/base.c:178-192 only tolerates a missing component. Resolve all
     * support before RAM changes hardware; unsupported tables are errors. */
    OPTIONAL(gf5_fan_check_cstate(g,e.fanspeed));
    OPTIONAL(gf5_volt_check_id(g,e.voltage,e.voltage));
    programming=true;
    STEP(gf5_ram_prog(g,&plan));
    OPTIONAL(gf5_fan_cstate(g,e.fanspeed,+1));
    OPTIONAL(gf5_volt_id(g,e.voltage,e.voltage,+1));
    STEP(gf5_clock_prog(g,&plan));
    OPTIONAL(gf5_volt_id(g,e.voltage,e.voltage,-1));
    OPTIONAL(gf5_fan_cstate(g,e.fanspeed,-1));
#undef OPTIONAL
#undef STEP
    /* Keep absence visible while returning success for available features. */
    (void)finish(g,absent);
    return GF5_OK;
fault: {
        i32 source=g->source_rc ? g->source_rc : rc;
        i32 mapped=gf5_rc_map(rc);
        if (programming) { g->phase=GF5_FAULT; g->accepting=false; }
        g->source_rc=source; g->last_rc=mapped;
        return mapped;
    }
}
