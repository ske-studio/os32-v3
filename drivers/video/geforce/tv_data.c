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

/*
 * Copyright 2005-2006 Luc Verhaegen.
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
 */
/* Kernel-only, pure B11-data. Reference: MIT nouveau c03c1a1,
 * dispnv04/tvnv17.c (T), tvmodesnv17.c (M), tvnv17.h (H).
 * No head, connector, GPIO, FP programming or MMIO belongs here. */
#include "../../../hwdev/gf/display/data.h"
/* Temporary aliases until the integration stage adds these to types.h. */
typedef unsigned long long u64;
typedef long long i64;
enum tv_norm { N_PAL, N_PAL_M, N_PAL_N, N_PAL_NC, N_NTSC_M, N_NTSC_J, N_HD480I, N_HD480P, N_HD576I, N_HD576P, N_HD720P, N_HD1080I, N_COUNT };
/* H:56-70 fixes the externally supplied norm numbers, including the
 * interspersed LD HD480I/HD576I entries. M:48-230 supplies these images. */
struct norm_data { u32 width,height,millihz; u8 enc[64];
    struct gf4_timing output; u32 ctv[38]; };
static const struct norm_data norms[N_COUNT] = {
    [N_PAL] = { .width=720, .height=576, .millihz=50000,
        .enc={
            0x2a, 0x9, 0x8a, 0xcb, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x40, 0x8a, 0x35, 0x27, 0x0, 0x34, 0x3,
            0x3e, 0x3, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x9c,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x3,
            0xd3, 0x4, 0xd4, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x1a, 0xff, 0x3, 0x18, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x49, 0x10, 0x0, 0x9b,
            0xbd, 0x15, 0x5, 0x15, 0x3e, 0x3, 0x0, 0x0,
        } },
    [N_PAL_M] = { .width=720, .height=480, .millihz=59940,
        .enc={
            0x21, 0xe6, 0xef, 0xe3, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x44, 0x76, 0x32, 0x25, 0x0, 0x3c, 0x0,
            0x3c, 0x0, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x83,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x1,
            0xc5, 0x4, 0xc5, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x18, 0xff, 0x3, 0x20, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x40, 0x10, 0x0, 0x9c,
            0xc8, 0x15, 0x5, 0x15, 0x3c, 0x0, 0x0, 0x0,
        } },
    [N_PAL_N] = { .width=720, .height=576, .millihz=50000,
        .enc={
            0x2a, 0x9, 0x8a, 0xcb, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x40, 0x8a, 0x32, 0x25, 0x0, 0x3c, 0x0,
            0x3c, 0x0, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x9c,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x1,
            0xc5, 0x4, 0xc5, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x1a, 0xff, 0x3, 0x18, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x49, 0x10, 0x0, 0x9b,
            0xbd, 0x15, 0x5, 0x15, 0x3c, 0x0, 0x0, 0x0,
        } },
    [N_PAL_NC] = { .width=720, .height=576, .millihz=50000,
        .enc={
            0x21, 0xf6, 0x94, 0x46, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x44, 0x8a, 0x35, 0x27, 0x0, 0x34, 0x3,
            0x3e, 0x3, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x9c,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x3,
            0xd3, 0x4, 0xd4, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x1a, 0xff, 0x3, 0x18, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x49, 0x10, 0x0, 0x9b,
            0xbd, 0x15, 0x5, 0x15, 0x3e, 0x3, 0x0, 0x0,
        } },
    [N_NTSC_M] = { .width=720, .height=480, .millihz=59940,
        .enc={
            0x21, 0xf0, 0x7c, 0x1f, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x44, 0x76, 0x48, 0x0, 0x0, 0x3c, 0x0,
            0x3c, 0x0, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x83,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x1,
            0xc5, 0x4, 0xc5, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x16, 0xff, 0x3, 0x20, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x4, 0x10, 0x0, 0x9c,
            0xc8, 0x15, 0x5, 0x15, 0x3c, 0x0, 0x0, 0x0,
        } },
    [N_NTSC_J] = { .width=720, .height=480, .millihz=59940,
        .enc={
            0x21, 0xf0, 0x7c, 0x1f, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x44, 0x76, 0x48, 0x0, 0x0, 0x32, 0x0,
            0x3c, 0x0, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x83,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x1,
            0xcf, 0x4, 0xcf, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x16, 0xff, 0x3, 0x20, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x4, 0x10, 0x0, 0xa4,
            0xc8, 0x15, 0x5, 0x15, 0x3c, 0x0, 0x0, 0x0,
        } },
    [N_HD480I] = { .width=720, .height=480, .millihz=59940,
        .enc={
            0x21, 0xf0, 0x7c, 0x1f, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x44, 0x76, 0x48, 0x0, 0x0, 0x32, 0x0,
            0x3c, 0x0, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x83,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x1,
            0xcf, 0x4, 0xcf, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x16, 0xff, 0x3, 0x20, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x4, 0x10, 0x0, 0xa4,
            0xc8, 0x15, 0x5, 0x15, 0x3c, 0x0, 0x0, 0x0,
        } },
    [N_HD480P] = { .width=720, .height=480,
        .output={27000,720,735,743,858,480,490,494,525,0}, .ctv={
            0x3540000, 0x0, 0x0, 0x314, 0x354003a, 0x40000,
            0x6f0344, 0x18100000, 0x10160004, 0x10060005, 0x1006000c, 0x10060020,
            0x10060021, 0x140e0022, 0x10060202, 0x1802020a, 0x1810020b, 0x10000fff,
            0x10000fff, 0x10000fff, 0x10000fff, 0x10000fff, 0x10000fff, 0x70,
            0x3ff0000, 0x57, 0x2e001e, 0x258012c, 0xa0aa04ec, 0x30,
            0x80960019, 0x12c0300, 0x2019, 0x600, 0x32060019, 0x0,
            0x0, 0x400,
        } },
    [N_HD576I] = { .width=720, .height=576, .millihz=50000,
        .enc={
            0x2a, 0x9, 0x8a, 0xcb, 0x0, 0x0, 0xb, 0x18,
            0x7e, 0x40, 0x8a, 0x35, 0x27, 0x0, 0x34, 0x3,
            0x3e, 0x3, 0x17, 0x21, 0x1b, 0x1b, 0x24, 0x9c,
            0x1, 0x0, 0xf, 0xf, 0x60, 0x5, 0xd3, 0x3,
            0xd3, 0x4, 0xd4, 0x1, 0x2, 0x0, 0xa, 0x5,
            0x0, 0x1a, 0xff, 0x3, 0x18, 0xf, 0x78, 0x0,
            0x0, 0xb4, 0x0, 0x15, 0x49, 0x10, 0x0, 0x9b,
            0xbd, 0x15, 0x5, 0x15, 0x3e, 0x3, 0x0, 0x0,
        } },
    [N_HD576P] = { .width=720, .height=576,
        .output={27000,720,730,738,864,576,581,585,625,0}, .ctv={
            0x3540000, 0x0, 0x0, 0x314, 0x354003a, 0x40000,
            0x6f0344, 0x18100000, 0x10060001, 0x10060009, 0x10060026, 0x10060027,
            0x140e0028, 0x10060268, 0x1810026d, 0x10000fff, 0x10000fff, 0x10000fff,
            0x10000fff, 0x10000fff, 0x10000fff, 0x10000fff, 0x10000fff, 0x69,
            0x3ff0000, 0x57, 0x2e001e, 0x258012c, 0xa0aa04ec, 0x30,
            0x80960019, 0x12c0300, 0x2019, 0x600, 0x32060019, 0x0,
            0x0, 0x400,
        } },
    [N_HD720P] = { .width=1280, .height=720,
        .output={74250,1280,1349,1357,1650,720,725,730,750,0}, .ctv={
            0x1260394, 0x0, 0x0, 0x622, 0x66b0021, 0x6004a,
            0x1210626, 0x8170000, 0x70004, 0x70016, 0x70017, 0x40f0018,
            0x702e8, 0x81702ed, 0xfff, 0xfff, 0xfff, 0xfff,
            0xfff, 0xfff, 0xfff, 0xfff, 0xfff, 0x0,
            0x2e40001, 0x58, 0x2e001e, 0x258012c, 0xa0aa04ec, 0x30,
            0x810c0039, 0x12c0300, 0xc0002039, 0x600, 0x32060039, 0x0,
            0x0, 0x0,
        } },
    [N_HD1080I] = { .width=1920, .height=1080,
        .output={74250,1920,1961,2049,2200,1080,1084,1088,1125,GF4_INTERLACE}, .ctv={
            0xac0420, 0x44c0478, 0x4a4, 0x4fc0868, 0x8940028, 0x60054,
            0xe80870, 0xbf70000, 0xbc70004, 0x70005, 0x70012, 0x70013,
            0x40f0014, 0x70230, 0xbf70232, 0xbf70233, 0x1c70237, 0x70238,
            0x70244, 0x70245, 0x40f0246, 0x70462, 0x1f70464, 0x0,
            0x2e40001, 0x58, 0x2e001e, 0x258012c, 0xa0aa04ec, 0x30,
            0x815f004c, 0x12c0300, 0xc000204c, 0x600, 0x3206004c, 0x0,
            0x0, 0x0,
        } },
};
/* M:254-312: at the fixed flicker=50, flicker terms vanish.
 * The first eight parameters of each row are the original C double
 * constant expressions converted to signed integers (truncation to zero).
 * Retaining literals avoids runtime FP and preserves the original rounding. */
static const i64 filter_params[2][4][8] = {
    {
        {18102131444972928LL, -11123025262576764LL, 1853834447538050LL, 562949953LL,
         220267397LL, 112393908247LL, -18732280818LL, -4294LL},
        {-12544092496372134LL, -19163167132282440LL, 11206102601952914LL, -1128522127725660LL,
         128128353432LL, 214951611242LL, -109006342986LL, 10936799211LL},
        {-15985877032222604LL, 8768954330853465LL, -1461487603400973LL, -844424930LL,
         144058060715LL, -146670135271LL, 24444965278LL, 8589LL},
        {14576968672996204LL, 5970582748216768LL, -5346259709398488LL, 521660389087625LL,
         -178115411911LL, -76346578444LL, 56082914152LL, -4974559971LL},
    },
    {
        {19028265464619256LL, 120561080549731LL, -20074232389050LL, -3377699720LL,
         -14612934734LL, 897648LL, -395136LL, 42949LL},
        {1901545581990413LL, 1644193292258837LL, 413693343420859LL, -42124419114609LL,
         35618679181LL, -5123414947LL, -4070134347LL, 405904474LL},
        {-4160310212499389LL, -4515463234688897LL, 752577393431467LL, 0LL,
         169831502329LL, 24395641874LL, -4065942459LL, 0LL},
        {-7733860784772489LL, -409539335714563LL, 362133601611931LL, 7134264759708LL,
         88851276942LL, 12945044315LL, -2395672628LL, -56307021LL},
    },
};
/* M:315-347: fixed overscan=50 -> 0xe1, flicker=50 -> 0.
 * PTV array order is M:417-430 / data.h; unused/route-owned fields are zero. */
enum { TV_OVERSCAN = 0xe1, TV_FILTER_ONE = 1U << 16,
    TV_FILTER_MASK = 0x8000fe00U, TV_CLOCK_MAX = 70000,
    CTV_CLOCK_MAX = 400000, TV_REFRESH_TOLERANCE = 600 };
enum ptv_index { P200,P204,P208,P20C,P304,P500,P504,P508,
    P600,P604,P608,P60C,P610,P614 };
/* Bounded unsigned long division, with no libgcc division dependency.
 * All callers have a nonzero 32-bit divisor and a 32-bit quotient. */
static u32 quotient(u64 n,u32 d)
{
    u64 r=0; u32 q=0;
    for (u32 bit=64;bit-- > 0;) {
        r=(r<<1)|((n>>bit)&1U);
        if (r>=d) { r-=d; if (bit<32) q|=1U<<bit; }
    }
    return q;
}
static bool is_ctv(u32 norm) { return norms[norm].millihz==0; }
/* Convert a logical modeline once, at its generator (design §3).
 * DBLSCAN expands lines; INTERLACE stores per-field vertical CRTC values. */
static void normalize(struct gf4_timing *t)
{
    if (t->flags&GF4_DBLSCAN) {
        t->vd*=2; t->vs*=2; t->ve*=2; t->vt*=2;
    } else if (t->flags&GF4_INTERLACE) {
        t->vd/=2; t->vs/=2; t->ve/=2; t->vt/=2;
    }
}
/* M:439-467: the first three logical modes also carry DRM CLKDIV2.
 * data.h has no such bit: retain their DBLSCAN classification here; the
 * TV route's hardware translation must supply SR01 bit3 (C:334-335).
 * Clock calculation is T:210-220; the 90MHz LD fixup is T:360-363. */
static const struct gf4_timing ld_modes[] = {
    {0,320,344,392,560,200,200,202,220,GF4_NEG_H|GF4_NEG_V|GF4_DBLSCAN},
    {0,320,344,392,560,240,240,246,263,GF4_NEG_H|GF4_NEG_V|GF4_DBLSCAN},
    {0,400,432,496,640,300,300,303,314,GF4_DBLSCAN},
    {0,640,672,768,880,480,480,492,525,GF4_NEG_H|GF4_NEG_V},
    {0,720,752,872,960,480,480,493,525,GF4_NEG_H|GF4_NEG_V},
    {0,720,776,856,960,576,576,588,597,GF4_NEG_H|GF4_NEG_V},
    {0,800,840,920,1040,600,600,604,618,0},
    {0,1024,1064,1200,1344,768,768,777,806,GF4_NEG_H|GF4_NEG_V}
};
/* T:239-249: HD source sizes. The integer CVT normal-blanking construction
 * below replaces the GPL drm_cvt_mode dependency in T:266-270. Constants
 * are VESA CVT 1.2 normal blanking: 60Hz, cell=8, min V porch=3,
 * min sync/back porch=550us, C'=30, M'=300, H sync=8%, clock step=250kHz.
 * MIT reference xf86cvt.c:92-95,130-209,249-262 (Xorg):
 * https://raw.githubusercontent.com/freedesktop-unofficial-mirror/xorg__xserver/master/hw/xfree86/modes/xf86cvt.c
 * Keep nanoseconds until the final clock quantization; no floating point. */
static const u16 hd_sizes[][2] = {
    {640,400},{640,480},{720,480},{720,576},{800,600},
    {1024,768},{1280,720},{1280,1024},{1920,1080}
};
static struct gf4_timing cvt(u32 w,u32 h,bool interlace)
{
    u32 lines=interlace ? h/2 : h;
    u32 vsync=(w*3==h*4) ? 4 : (w*9==h*16) ? 5 :
        (w*10==h*16) ? 6 : (w*4==h*5 || w*9==h*15) ? 7 : 10;
    u32 rate=interlace ? 120U : 60U;
    u32 period=((1000000000U-550000U*rate)*2U)/
        (2U*(lines+3U)+(interlace ? 1U : 0U))/rate;
    u32 blank=550000U/period+1U;
    if (blank<vsync+3U) blank=vsync+3U;
    u32 duty=30000U-300U*period/1000U;
    if (duty<20000U) duty=20000U;
    u32 hb=(w*duty/(100000U-duty))&~15U, ht=w+hb;
    u32 he=w+hb/2U;
    /* xf86cvt.c:202-205: truncate the difference, then advance one
     * character even if the difference is already character-aligned. */
    u32 hs=((he*100U-ht*8U)/100U/8U+1U)*8U;
    u32 khz=quotient((u64)ht*1000000U,period)/250U*250U;
    u32 vt=lines+blank+3U;
    struct gf4_timing t={khz,w,hs,he,ht,h,h+3U,h+3U+vsync,
        interlace ? vt*2U : vt,GF4_NEG_H|(interlace ? GF4_INTERLACE : 0)};
    return t;
}
i32 gf4_tv_modes(u32 norm,struct gf4_edid_state *out)
{
    if (!out || norm>=N_COUNT) return GF4_EINVAL;
    const struct norm_data *n=&norms[norm];
    struct gf4_edid_state next={.version=GF4_EDID_VERSION,.size=sizeof(next),
        .base_valid=true};
    if (!is_ctv(norm)) {
        /* Put the norm's preferred timing first (B11 card); keep the
         * remaining M table in its original order. */
        u32 first=0;
        for (u32 i=0;i<sizeof(ld_modes)/sizeof(ld_modes[0]);++i)
            if (ld_modes[i].hd==n->width && ld_modes[i].vd==n->height) first=i;
        next.timing[next.ntiming++]=ld_modes[first];
        for (u32 i=0;i<sizeof(ld_modes)/sizeof(ld_modes[0]);++i)
            if (i!=first) next.timing[next.ntiming++]=ld_modes[i];
        for (u32 i=0;i<next.ntiming;++i) {
            struct gf4_timing *t=&next.timing[i];
            t->khz=(n->millihz*t->ht/1000U)*t->vt/1000U;
            if (t->flags&GF4_DBLSCAN) t->khz*=2U;
            normalize(t);
        }
    } else {
        /* T:251-292: preferred native output, then fitting CVT sizes;
         * even the native mode receives the horizontal override. */
        for (u32 pass=0;pass<2;++pass) {
            for (u32 i=0;i<sizeof(hd_sizes)/sizeof(hd_sizes[0]);++i) {
                u32 w=hd_sizes[i][0],h=hd_sizes[i][1];
                if (w>n->width || h>n->height) continue;
                bool native=w==n->width && h==n->height;
                if (native!=(pass==0)) continue;
                struct gf4_timing t=native ? n->output :
                    cvt(w,h,(n->output.flags&GF4_INTERLACE)!=0);
                if (n->width<=720 || n->width>=1920) {
                    t.ht=n->output.ht;
                    t.hs=(w+(t.ht-w)*9U/10U)&~7U; t.he=t.hs+8U;
                }
                if (n->height>=1024) {
                    t.vt=n->output.vt; t.vs=n->output.vs; t.ve=n->output.ve;
                }
                normalize(&t);
                next.timing[next.ntiming++]=t;
            }
        }
    }
    *out=next;
    return GF4_OK;
}
/* T:310-349 on normalized timings. TV permits vs==vd (M:439-467);
 * slave encoder timings need not have VGA's character-clock granularity. */
static bool valid(u32 norm,const struct gf4_timing *t)
{
    if (!t->khz || !t->hd || !t->vd || t->hd>=t->hs || t->hs>=t->he ||
        t->he>t->ht || t->vd>t->vs || t->vs>=t->ve || t->ve>t->vt ||
        (t->flags&~(GF4_NEG_H|GF4_NEG_V|GF4_INTERLACE|GF4_DBLSCAN))) return false;
    const struct norm_data *n=&norms[norm];
    if (is_ctv(norm)) {
        return t->khz<=CTV_CLOCK_MAX && t->hd<=n->width &&
            (u32)t->vd*((t->flags&GF4_INTERLACE) ? 2U : 1U)<=n->height &&
            (t->flags&GF4_INTERLACE)==(n->output.flags&GF4_INTERLACE) &&
            !(t->flags&GF4_DBLSCAN);
    }
    if (t->khz>TV_CLOCK_MAX || (t->flags&GF4_INTERLACE)) return false;
    u32 denominator=(u32)t->ht*t->vt;
    /* T uses drm_mode_vrefresh's rounded integer Hz, then compares mHz. */
    u32 hz=quotient((u64)t->khz*1000U+denominator/2U,denominator);
    u32 refresh=hz*1000U;
    return refresh+TV_REFRESH_TOLERANCE>=n->millihz &&
        refresh<=n->millihz+TV_REFRESH_TOLERANCE;
}
i32 gf4_tv_state_calc(u32 norm,const struct gf4_timing *t,struct gf4_tv_state *out)
{
    if (!t || !out || norm>=N_COUNT) return GF4_EINVAL;
    if (!valid(norm,t)) return GF4_ERANGE;
    const struct norm_data *n=&norms[norm];
    struct gf4_tv_state next={.version=GF4_TV_VERSION,.size=sizeof(next),.norm=norm};
    if (is_ctv(norm)) {
        /* T:536-538. FP timing/rescaler belongs to B11-MMIO. */
        for (u32 i=0;i<sizeof(next.ctv)/sizeof(next.ctv[0]);++i) next.ctv[i]=n->ctv[i];
    } else {
        for (u32 i=0;i<sizeof(next.enc);++i) next.enc[i]=n->enc[i];
        /* T:482-509: NV44 uses the >= Rankine branch. P200's head bit
         * and P204/encoder properties are completed by B11-MMIO. */
        next.ptv[P200]=0x13111100U;
        next.ptv[P208]=0x40U|(TV_OVERSCAN<<8);
        next.ptv[P20C]=0x808010U; next.ptv[P304]=0x2d00000U;
        next.ptv[P500]=0xe8e0U; next.ptv[P504]=0x1710U;
        next.ptv[P508]=n->height==576 ? 0x1200000U : 0xf00000U;
        next.ptv[P610]=0x1e00000U; next.ptv[P614]=n->height==576 ? 0x33U : 0x13U;
        u32 dimension[2]={t->hd,(t->flags&GF4_DBLSCAN) ? t->vd/2U : t->vd};
        for (u32 axis=0;axis<2;++axis) {
            u32 r=quotient((u64)dimension[axis]<<24,
                TV_OVERSCAN*(axis ? n->height : n->width));
            if (r<TV_FILTER_ONE) r=TV_FILTER_ONE;
            for (u32 row=0;row<4;++row) {
                const i64 *p=filter_params[axis][row];
                for (u32 i=0;i<7;++i) {
                    i64 a=((p[3]*i+p[2])*i+p[1])*i+p[0];
                    i64 b=((p[7]*i+p[6])*i+p[5])*i+p[4];
                    u32 c=(u32)((a+b*r+(1LL<<47))>>39)&TV_FILTER_MASK;
                    if (axis) next.vfilter[row][i]=c;
                    else next.hfilter[row][i]=c;
                    /* M:319-320 / 536-538: hfilter2 is never calculated;
                     * do not invent the blob's duplicate coefficients. */
                }
            }
        }
    }
    *out=next;
    return GF4_OK;
}
