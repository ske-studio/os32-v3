/*
 * Independent EDID byte fixtures; libc-free A1 UNIT 4, linked with edid.c.
 * Sources (numeric facts, not product-generated expectations):
 * E-EDID A2 tables 3.19/3.21/3.26-3.28, Appendix B; DMT 1.0 rev13 §4;
 * GTF 1.1 §7.3/7.6; CVT 1.2 §5.2/5.3. PDFs:
 * https://glenwing.github.io/docs/VESA-EEDID-A2.pdf
 * https://glenwing.github.io/docs/VESA-DMT-1.13.pdf
 * https://glenwing.github.io/docs/VESA-GTF-1.1.pdf
 * https://glenwing.github.io/docs/VESA-CVT-1.2.pdf
 * CEA identifiers checked against MIT drm_edid.c (Linux v4.4):641-987;
 * https://github.com/torvalds/linux/blob/v4.4/drivers/gpu/drm/drm_edid.c
 * Recreate: encode the named standard's active/blank/porch/sync fields with
 * EDID table 3.21 or standard-code table 3.19, then complement byte sum.
 * Formula expectations below give independent intermediate rational values.
 * No test reads external references or includes product implementation.
 */
#define GF4_HOST_UNIT 4
#include "gf4_display_fake.h"
static struct gf4_edid_state state, before;
static u8 b[128];
enum {
    HEADER_END=8, CHECKSUM=127, EXTENSIONS=126, DTD=54, DESC_BYTES=18,
    STANDARD=38, RANGE=DTD+DESC_BYTES, EDID_VERSION=18, EDID_REVISION=19,
    EDID_INPUT=20, EDID_FEATURES=24,
    CTA_TAG=2, CTA_REV=3, CTA_DATA_END=6, CTA_VIDEO_ONE=0x41,
    CTA_NATIVE=0x80, VIC_480P60=1, VIC_720P60=4, VIC_1080I60=5,
    VIC_REPEATED_480I60=6, VIC_1080P60=16, LAST_VIC=64,
    NO_PREFERRED=~0U, SECONDARY_GTF=2, CVT_SUPPORT=4, RANGE_LIMITS_ONLY=1
};
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* DMT ID04, nearest representable 10kHz DTD clock, E-EDID table 3.21. */
static const struct gf4_timing vga_dtd={
    25170,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V
};
/* CEA VIC4/VIC5, drm_edid.c:658/663; §3 CRTC field-normalized values. */
static const struct gf4_timing hd720={74250,1280,1390,1430,1650,720,725,730,750,0};
static const struct gf4_timing hd1080i={
    74250,1920,2008,2052,2200,540,542,547,562,GF4_INTERLACE
};
/* Independent GTF §7.3 evaluation for 1024x768@61:
 * H_PERIOD_EST=966450000/46909 ns, VSYNC+BP=27 lines, VT=796,
 * corrected H frequency=48556 Hz; blank rounds to 320, sync to 104,
 * pixel frequency=1344*48556 Hz=65.259264 MHz, nearest kHz=65259.
 * Secondary M=360,C=40,K=128,J=20 changes blank to 368 and sync to
 * 112; clock=1392*48556 Hz=67.589952 MHz, nearest kHz=67590.
 * drm_modes.c:451-538 identifies the steps; do not use its approximate
 * H_FREQ_EST clock as the oracle for the corrected specification formula. */
static const struct gf4_timing gtf61={
    65259,1024,1080,1184,1344,768,769,772,796,GF4_NEG_H
};
static const struct gf4_timing secondary61={
    67590,1024,1096,1208,1392,768,769,772,796,GF4_NEG_V
};
/* CVT §5.3 for the same input: H_PERIOD_EST=966450000/47031 ns,
 * VSYNC+BP=27, VT=798; blank=320; H frequency=47031e6/966450 Hz,
 * clock=65.403... MHz, floor to 0.25MHz=65.25 MHz. 4:3 sync=4 lines.
 * drm_modes.c:218-337 is a separate integer cross-reference. */
static const struct gf4_timing cvt61={
    65250,1024,1080,1184,1344,768,771,775,798,GF4_NEG_H
};
/* CVT 256x192@60: estimated H period=967000000/11700 ns;
 * minimum sync+back is 4+6=10 lines, VT=205; duty clamps to 20%;
 * blank=64, sync=24, clock=3.871... MHz -> 3.75 MHz. */
static const struct gf4_timing cvt_minimum={
    3750,256,264,288,320,192,195,199,205,GF4_NEG_H
};
static void equal(const struct gf4_timing *a,const struct gf4_timing *e)
{
    assert(a->khz==e->khz && a->hd==e->hd && a->hs==e->hs &&
           a->he==e->he && a->ht==e->ht && a->vd==e->vd &&
           a->vs==e->vs && a->ve==e->ve && a->vt==e->vt && a->flags==e->flags);
}
static void seal(void)
{
    u32 sum=0; b[CHECKSUM]=0;
    for(u32 i=0;i<CHECKSUM;i++) sum+=b[i];
    b[CHECKSUM]=(u8)(0U-sum);
}
static void empty_base(void)
{
    gf2_host_memset(b,0,sizeof(b));
    for(u32 i=1;i<HEADER_END-1;i++) b[i]=255;
    b[EDID_VERSION]=1; b[EDID_REVISION]=4; b[EDID_INPUT]=0x80;
    b[EDID_FEATURES]=2; b[EXTENSIONS]=2;
    for(u32 i=STANDARD;i<DTD;i+=2) b[i]=b[i+1]=1;
}
static void vga(u32 offset)
{
    /* Table 3.21 pack: 640/160 pixels, 480/45 lines, H porch16/sync96,
     * V porch10/sync2; 2517 units of 10kHz, separate negative syncs. */
    static const u8 bytes[DESC_BYTES]={
        0xd5,0x09,0x80,0xa0,0x20,0xe0,0x2d,0x10,16,96,0xa2,0,0,0,0,0,0,0x18
    };
    gf2_host_memcpy(b+offset,bytes,sizeof(bytes));
}
static void setup(void) { empty_base(); vga(DTD); seal(); }
static void limits(u32 type)
{
    b[RANGE+3]=0xfd;
    b[RANGE+5]=30; b[RANGE+6]=200; b[RANGE+7]=1; b[RANGE+8]=200;
    b[RANGE+9]=30; b[RANGE+10]=(u8)type; b[EDID_FEATURES]|=1;
    if(type==CVT_SUPPORT) {
        b[RANGE+11]=0x11; b[RANGE+14]=0xf8; b[RANGE+15]=0x18;
    }
}
static void std_code(u32 w,u32 aspect,u32 hz)
{
    b[STANDARD]=(u8)(w/8U-31U);
    b[STANDARD+1]=(u8)((aspect<<6)|(hz-60U));
}
static void cta_one(u32 vic)
{
    gf2_host_memset(b,0,sizeof(b));
    b[0]=CTA_TAG; b[1]=CTA_REV; b[2]=CTA_DATA_END;
    b[4]=CTA_VIDEO_ONE; b[5]=(u8)vic; seal();
}
static void accepted_base(void)
{
    seal(); assert(gf4_edid_modes(b,0,&state)==0);
    assert(state.base_valid && state.version==GF4_EDID_VERSION && state.size==sizeof(state));
}
static void unchanged_timings(void)
{
    assert(state.ntiming==before.ntiming && state.preferred==before.preferred &&
           state.truncated==before.truncated && state.base_valid==before.base_valid &&
           state.dtd_seen==before.dtd_seen);
    for(u32 i=0;i<before.ntiming;i++) equal(&state.timing[i],&before.timing[i]);
}
static void base_and_errors(void)
{
    setup(); accepted_base();
    assert(state.digital && state.ntiming==1 && state.preferred==0 &&
           state.extensions==2 && state.next_block==1 && !state.last_rc && state.dtd_seen==1);
    equal(&state.timing[0],&vga_dtd);
    before=state;
    assert(gf4_edid_modes(NULL,0,&state)==GF4_EINVAL);
    assert(gf4_edid_modes(b,0,NULL)==GF4_EINVAL);
    unchanged_timings();
    assert(gf4_edid_modes(b,2,&state)==GF4_ESTATE && state.next_block==1);
    unchanged_timings();
    assert(state.last_rc==GF4_ESTATE);
    for(u32 i=0;i<HEADER_END;i++) {
        setup(); b[i]^=1; seal();
        assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid && !state.ntiming);
    }
    setup(); b[10]++; /* Break checksum only. */
    assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && state.last_rc==GF4_EEDID);
    setup(); b[EDID_VERSION]=2; seal();
    assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid);
    setup(); b[EDID_REVISION]=5; seal();
    assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid);
    setup(); b[DTD+9]=255; seal(); /* H sync extends beyond blanking. */
    assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.ntiming);
    setup(); b[EDID_INPUT]=0; accepted_base(); assert(!state.digital);
    setup(); b[EDID_REVISION]=3; b[EDID_FEATURES]=0; accepted_base();
    assert(state.preferred==NO_PREFERRED);
    setup(); b[EDID_REVISION]=3; accepted_base();
    assert(state.preferred==0); /* EDID 1.3 honors the preferred bit. */
    setup(); b[EDID_FEATURES]=0; accepted_base();
    assert(state.preferred==0); /* EDID 1.4 always prefers the first DTD. */
    empty_base(); vga(DTD+DESC_BYTES); accepted_base();
    assert(state.ntiming==1 && state.preferred==NO_PREFERRED && state.dtd_seen==1);
    /* A descriptor in the first slot does not promote a later base DTD. */
    setup(); vga(DTD+DESC_BYTES); accepted_base();
    assert(state.ntiming==1 && state.preferred==0 && state.dtd_seen==2); /* duplicate DTD */
    /* block0 restarts a previous parse and clears the sticky diagnostic. */
    state.truncated=true; state.last_rc=GF4_EEDID; state.dtd_seen=99;
    setup(); accepted_base();
    assert(!state.truncated && !state.last_rc && state.ntiming==1 && state.dtd_seen==1);
}
/* PM B4 decision: CTA native counts never set preference. The original
 * DTD sequence still counts unsupported and duplicate entries, across
 * blocks; monitor descriptors and SVDs do not occupy positions. */
static void dtd_order_and_preference(void)
{
    /* A later CTA count cannot promote a nonpreferred legacy base DTD. */
    setup(); b[EDID_REVISION]=3; b[EDID_FEATURES]=0; accepted_base();
    assert(state.preferred==NO_PREFERRED);
    /* Empty CTA DTD area: remove the previous video-block header. */
    cta_one(0); b[2]=4; b[3]=1; b[4]=b[5]=0; seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.preferred==NO_PREFERRED &&
           state.dtd_seen==1);
    equal(&state.timing[0],&vga_dtd);
    /* Unsupported base DTD is counted without promoting the extension. */
    setup(); b[DTD+17]=0; accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED && state.dtd_seen==1);
    cta_one(0); b[2]=4; b[3]=1; vga(4); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==2);
    equal(&state.timing[0],&vga_dtd);
    /* Unsupported CTA DTD is counted without promoting its successor. */
    empty_base(); accepted_base();
    cta_one(0); b[2]=4; b[3]=1; vga(4); b[4+17]=0;
    vga(4+DESC_BYTES); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && state.last_rc==GF4_EUNSUPPORTED &&
           state.dtd_seen==2);
    /* Changing native counts across extensions has no effect on priority. */
    empty_base(); accepted_base();
    cta_one(0); b[2]=4; vga(4); b[4+17]=0; seal();
    assert(gf4_edid_modes(b,1,&state)==0 && !state.ntiming && state.dtd_seen==1);
    cta_one(0); b[2]=4; b[3]=1; vga(4); seal();
    assert(gf4_edid_modes(b,2,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==2);
    /* A later native count cannot promote a previous extension's DTD.
     * Distinct DTDs are appended in their original order. */
    empty_base(); accepted_base(); cta_one(0); b[2]=4; vga(4); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.preferred==NO_PREFERRED);
    b[3]=1; b[4]++; seal();
    assert(gf4_edid_modes(b,2,&state)==0 && state.ntiming==2 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==2);
    equal(&state.timing[0],&vga_dtd);
    assert(state.timing[1].khz==vga_dtd.khz+10);
    /* Unsupported duplicate DTDs each contribute to the serial count. */
    setup(); b[EDID_REVISION]=3; b[EDID_FEATURES]=0;
    b[DTD+17]=0; vga(DTD+DESC_BYTES); b[DTD+DESC_BYTES+17]=0;
    accepted_base();
    cta_one(0); b[2]=4; b[3]=2; vga(4); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==3);
    /* A duplicate supported DTD likewise consumes a position. */
    setup(); b[EDID_REVISION]=3; b[EDID_FEATURES]=0;
    vga(DTD+DESC_BYTES); accepted_base();
    cta_one(0); b[2]=4; b[3]=3; vga(4); b[4]++; seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==2 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==3);
    equal(&state.timing[0],&vga_dtd);
}
static void extensions(void)
{
    setup(); accepted_base();
    cta_one(VIC_720P60|CTA_NATIVE);
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==2 && state.preferred==0);
    equal(&state.timing[1],&hd720);
    before=state;
    assert(gf4_edid_modes(b,1,&state)==GF4_ESTATE && state.next_block==2);
    unchanged_timings();
    b[10]++; assert(gf4_edid_modes(b,2,&state)==GF4_EEDID);
    assert(state.next_block==3 && state.last_rc==GF4_EEDID); unchanged_timings();
    assert(gf4_edid_modes(b,3,&state)==GF4_ESTATE && state.next_block==3);
    setup(); accepted_base(); before=state;
    cta_one(VIC_720P60); b[2]=5; seal(); /* VDB overruns DBC. */
    assert(gf4_edid_modes(b,1,&state)==GF4_EEDID && state.next_block==2);
    unchanged_timings();
    cta_one(VIC_720P60); assert(gf4_edid_modes(b,2,&state)==0 && state.ntiming==2);
    equal(&state.timing[1],&hd720);
    setup(); accepted_base(); before=state;
    cta_one(VIC_720P60); b[2]=8; b[6]=CTA_VIDEO_ONE+1; seal();
    assert(gf4_edid_modes(b,1,&state)==GF4_EEDID); unchanged_timings();
    setup(); accepted_base(); before=state;
    cta_one(VIC_720P60); vga(CTA_DATA_END); b[CTA_DATA_END+9]=255; seal();
    assert(gf4_edid_modes(b,1,&state)==GF4_EEDID); unchanged_timings();
    for(u32 end=1;end<4;end++) {
        setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[2]=(u8)end; seal();
        assert(gf4_edid_modes(b,1,&state)==GF4_EEDID); unchanged_timings();
    }
    setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[2]=128; seal();
    assert(gf4_edid_modes(b,1,&state)==GF4_EEDID); unchanged_timings();
    setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[2]=0; seal();
    assert(gf4_edid_modes(b,1,&state)==0); unchanged_timings();
    setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[2]=127;
    gf2_host_memset(b+4,0,123); seal();
    assert(gf4_edid_modes(b,1,&state)==0); unchanged_timings();
    setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[0]=0x70; seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.last_rc==GF4_EUNSUPPORTED);
    unchanged_timings();
    setup(); accepted_base(); before=state; cta_one(VIC_720P60); b[1]=4; seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.last_rc==GF4_EUNSUPPORTED);
    unchanged_timings();
    /* An extension DTD is not preferred, even with a native count. */
    empty_base(); accepted_base(); cta_one(0); b[2]=4; b[3]=1; vga(4); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==1);
    equal(&state.timing[0],&vga_dtd);
    /* Multiple extension DTDs retain their source order, regardless of native count. */
    empty_base(); accepted_base(); cta_one(0); b[2]=4; b[3]=2;
    vga(4); b[4]++; vga(4+DESC_BYTES); seal();
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==2 &&
           state.preferred==NO_PREFERRED && state.dtd_seen==2);
    assert(state.timing[0].khz==vga_dtd.khz+10);
    equal(&state.timing[1],&vga_dtd);
    /* Native SVDs also leave preference unset when there is no base DTD. */
    empty_base(); accepted_base(); cta_one(VIC_720P60|CTA_NATIVE);
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1 &&
           state.preferred==NO_PREFERRED && !state.dtd_seen);
    equal(&state.timing[0],&hd720);
    setup(); accepted_base(); before=state; state.size--;
    cta_one(VIC_720P60); assert(gf4_edid_modes(b,1,&state)==GF4_ESTATE);
    assert(state.next_block==1); unchanged_timings();
}
static void interlace_and_sync(void)
{
    /* EDID table 3.21 high-bit packing, deliberately non-cell-aligned
     * sync edges: parser must preserve the descriptor rather than round. */
    static const u8 wide_offsets[DESC_BYTES]={
        0x10,0x27,0,0,0x84,0,0,0x41,44,4,0x41,0x55,0,0,0,0,0,0x1e
    };
    static const struct gf4_timing wide={
        100000,2048,2348,2608,3072,1024,1044,1061,1280,0
    };
    empty_base(); gf2_host_memcpy(b+DTD,wide_offsets,sizeof(wide_offsets));
    accepted_base(); assert(state.ntiming==1); equal(&state.timing[0],&wide);
    static const u8 interlaced[DESC_BYTES]={
        0x01,0x1d,0x80,0x18,0x71,0x1c,0x16,0x20,88,44,0x25,0,0,0,0,0,0,0x9e
    };
    empty_base(); gf2_host_memcpy(b+DTD,interlaced,sizeof(interlaced));
    accepted_base(); assert(state.ntiming==1); equal(&state.timing[0],&hd1080i);
    cta_one(VIC_1080I60);
    assert(gf4_edid_modes(b,1,&state)==0 && state.ntiming==1); /* normalized once */
    for(u32 polarity=0;polarity<4;polarity++) {
        setup(); b[DTD+17]=(u8)(0x18|(polarity<<1)); accepted_base();
        assert(state.timing[0].flags==((polarity&1 ? 0U : GF4_NEG_H)|
                                      (polarity&2 ? 0U : GF4_NEG_V)));
    }
    setup(); b[DTD+17]=0; accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    setup(); b[DTD+17]|=0x20; accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
}
struct dmt_fixture { u16 w,h,hz; u8 aspect; struct gf4_timing t; };
/* DMT rev13 §4 named timing rows; independent values also located at
 * drm_edid.c:161-502. Include all standard-code aspect ratios and >1280.
 * Regenerate each row from its named DMT horizontal/vertical timing boxes,
 * never from edid.c. 1280x800 normal and reduced blanking are separate rows. */
static const struct dmt_fixture dmt_cases[]={
    {640,480,60,1,{25175,640,656,752,800,480,490,492,525,GF4_NEG_H|GF4_NEG_V}},
    {640,480,85,1,{36000,640,696,752,832,480,481,484,509,GF4_NEG_H|GF4_NEG_V}},
    {800,600,60,1,{40000,800,840,968,1056,600,601,605,628,0}},
    {1024,768,85,1,{94500,1024,1072,1168,1376,768,769,772,808,0}},
    {1152,864,75,1,{108000,1152,1216,1344,1600,864,865,868,900,0}},
    {1280,720,60,3,{74250,1280,1390,1430,1650,720,725,730,750,0}},
    {1280,800,60,0,{83500,1280,1352,1480,1680,800,803,809,831,GF4_NEG_H}},
    {1280,960,60,1,{108000,1280,1376,1488,1800,960,961,964,1000,0}},
    {1280,1024,60,2,{108000,1280,1328,1440,1688,1024,1025,1028,1066,0}},
    {1400,1050,60,1,{121750,1400,1488,1632,1864,1050,1053,1057,1089,GF4_NEG_H}},
    {1440,900,60,0,{106500,1440,1520,1672,1904,900,903,909,934,GF4_NEG_H}},
    {1600,900,60,3,{108000,1600,1624,1704,1800,900,901,904,1000,0}},
    {1600,1200,60,1,{162000,1600,1664,1856,2160,1200,1201,1204,1250,0}},
    {1680,1050,60,0,{146250,1680,1784,1960,2240,1050,1053,1059,1089,GF4_NEG_H}},
    {1792,1344,60,1,{204750,1792,1920,2120,2448,1344,1345,1348,1394,GF4_NEG_H}},
    {1856,1392,60,1,{218250,1856,1952,2176,2528,1392,1393,1396,1439,GF4_NEG_H}},
    {1920,1080,60,3,{148500,1920,2008,2052,2200,1080,1084,1089,1125,0}}, /* DMT p89: +H/+V, unlike v4.4 table */
    {1920,1200,60,0,{193250,1920,2056,2256,2592,1200,1203,1209,1245,GF4_NEG_H}},
    {2048,1152,60,3,{162000,2048,2074,2154,2250,1152,1153,1156,1200,0}}
};
static void dmt_and_established(void)
{
    for(u32 i=0;i<COUNT(dmt_cases);i++) {
        const struct dmt_fixture *f=&dmt_cases[i];
        empty_base(); std_code(f->w,f->aspect,f->hz); accepted_base();
        assert(state.ntiming==1 && !state.last_rc); equal(&state.timing[0],&f->t);
        assert(state.timing[0].vd==f->h);
    }
    /* DMT 1B 1280x800@60 RB, drm_edid.c:258; EDID CVT RB support bit. */
    static const struct gf4_timing rb={71000,1280,1328,1360,1440,800,803,809,823,GF4_NEG_V};
    empty_base(); limits(CVT_SUPPORT); std_code(1280,0,60); accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&rb);
    /* Established table 3.18: each individual bit must decode. */
    for(u32 i=0;i<17;i++) {
        empty_base(); u32 byte=i<8 ? 35 : i<16 ? 36 : 37;
        u32 bit=i<16 ? 7U-(i&7U) : 7U; b[byte]=(u8)(1U<<bit);
        accepted_base(); assert(state.ntiming==1);
        if(i==0) assert(state.timing[0].hd==720 && state.timing[0].khz==28320);
        if(i==1) assert(state.timing[0].flags==(GF4_NEG_H|GF4_NEG_V));
        if(i==11) assert(state.timing[0].flags==GF4_INTERLACE &&
                         state.timing[0].vd==384 && state.timing[0].ve==388 &&
                         state.timing[0].vt==408);
        if(i==16) assert(state.timing[0].hd==1152 && state.timing[0].vd==870);
    }
    setup(); b[35]=1U<<5; b[STANDARD]=(u8)(640/8-31); b[STANDARD+1]=1U<<6;
    accepted_base(); /* 25170 DTD differs from the 25175 DMT clock. */
    assert(state.ntiming==2 && state.preferred==0);
    /* Additional standard timing descriptor, E-EDID table 3.30. */
    empty_base(); b[DTD+3]=0xfa;
    for(u32 i=5;i<17;i+=2) b[DTD+i]=b[DTD+i+1]=1;
    b[DTD+5]=(u8)(800/8-31); b[DTD+6]=1U<<6; accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&dmt_cases[2].t);
}
static void formula_and_range(void)
{
    empty_base(); limits(0); std_code(1024,1,61); accepted_base();
    assert(state.ntiming==1 && state.range_valid && !state.last_rc);
    equal(&state.timing[0],&gtf61);
    empty_base(); limits(SECONDARY_GTF); std_code(1024,1,61);
    b[RANGE+12]=10; b[RANGE+13]=80; b[RANGE+14]=0x68; b[RANGE+15]=1;
    b[RANGE+16]=128; b[RANGE+17]=40; accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&secondary61);
    b[RANGE+12]=30; accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&gtf61);
    b[RANGE+12]=10; b[RANGE+14]=255; b[RANGE+15]=255; accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_ERANGE);
    empty_base(); limits(CVT_SUPPORT); std_code(1024,1,61); accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&cvt61); /* normal even if RB supported */
    empty_base(); limits(CVT_SUPPORT); std_code(256,1,60); accepted_base();
    assert(state.ntiming==1); equal(&state.timing[0],&cvt_minimum);
    /* Limit fields from table 3.26: reject each frequency axis and clock. */
    static const struct {u8 byte,value;} reject[]={
        {5,62},{6,60},{7,50},{8,40},{9,6}
    };
    for(u32 i=0;i<COUNT(reject);i++) {
        setup(); limits(0); std_code(1024,1,61);
        b[RANGE+reject[i].byte]=reject[i].value; accepted_base();
        assert(state.ntiming==1 && state.last_rc==GF4_ERANGE);
        equal(&state.timing[0],&vga_dtd);
    }
    empty_base(); limits(CVT_SUPPORT); std_code(1024,1,61);
    b[RANGE+12]=4; b[RANGE+9]=7; accepted_base();
    assert(state.max_khz==69750); /* 70MHz - 1 * 0.25MHz, table 3.28. */
    b[RANGE+13]=100; accepted_base(); /* 800-pixel horizontal limit. */
    assert(!state.ntiming && state.last_rc==GF4_ERANGE);
    empty_base(); limits(0); b[RANGE+4]=15; b[RANGE+5]=1; b[RANGE+6]=2;
    b[RANGE+7]=3; b[RANGE+8]=4; accepted_base();
    assert(state.min_v_hz==256 && state.max_v_hz==257 &&
           state.min_h_khz==258 && state.max_h_khz==259);
    b[EDID_REVISION]=3; accepted_base(); /* offsets only exist in 1.4 */
    assert(state.min_v_hz==1 && state.max_v_hz==2 && state.min_h_khz==3);
    empty_base(); limits(0); b[RANGE+5]=201; seal();
    assert(gf4_edid_modes(b,0,&state)==GF4_EEDID && !state.base_valid);
    /* E-EDID Appendix B 1.3/1.4: no range descriptor is required. */
    for(u32 revision=2;revision<=4;revision++) {
        empty_base(); b[EDID_REVISION]=(u8)revision;
        std_code(1024,1,61); accepted_base();
        assert(state.ntiming==1 && !state.range_valid && !state.last_rc);
        equal(&state.timing[0],revision>=4 ? &cvt61 : &gtf61);
    }
    empty_base(); b[EDID_REVISION]=1; std_code(1024,1,61); accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    empty_base(); limits(RANGE_LIMITS_ONLY); std_code(1024,1,61); accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    empty_base(); limits(3); std_code(1024,1,61); accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    empty_base(); limits(0); b[EDID_REVISION]=1; std_code(1024,1,61); accepted_base();
    assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    /* Legacy 1:1 aspect uses GTF, modern aspect00 means 16:10. */
    empty_base(); limits(0); b[EDID_REVISION]=2; std_code(640,0,60); accepted_base();
    assert(state.ntiming==1 && state.timing[0].vd==640);
}
/* CEA-861-E tables 2/3/4 (pp16-27): each named VIC row supplies
 * pixel kHz, H active/front/sync/back, V active/front/sync/back, polarity.
 * https://source.netsyms.com/skylarmt/glenwing.github.io_withhdmi/raw/commit/30782ab3ada02d7b1d86d6752171bbd8d7245bb6/docs/CEA-861-E.pdf
 * Table 3 lists interlaced vertical intervals in field lines; the
 * half-line in each field total is discarded for integer CRTC totals.
 * These porch/sync inputs are independent of the product's edge table;
 * MIT cross-reference is drm_edid.c:642-987 (per-VIC locations below).
 * Omitted entries require pixel repetition, absent from gf4_timing.
 * Array indices name the table's VIC, including aspect-ratio aliases. */
#define VIC_ROW(k,ha,hf,hw,hb,va,vf,vw,vb,f) \
    {k,ha,(ha)+(hf),(ha)+(hf)+(hw),(ha)+(hf)+(hw)+(hb), \
     va,(va)+(vf),(va)+(vf)+(vw),(va)+(vf)+(vw)+(vb),f}
#define NEG_SYNC (GF4_NEG_H|GF4_NEG_V)
static const struct gf4_timing vic_expected[LAST_VIC+1]={
 [1]=VIC_ROW(25175,640,16,96,48,480,10,2,33,NEG_SYNC), /* drm_edid.c:642 */
 [2]=VIC_ROW(27000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:647 */
 [3]=VIC_ROW(27000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:652 */
 [4]=VIC_ROW(74250,1280,110,40,220,720,5,5,20,0), /* drm_edid.c:657 */
 [5]=VIC_ROW(74250,1920,88,44,148,540,2,5,15,GF4_INTERLACE), /* drm_edid.c:662 */
 [10]=VIC_ROW(54000,2880,76,248,228,240,4,3,15,NEG_SYNC|GF4_INTERLACE), /* drm_edid.c:692 */
 [11]=VIC_ROW(54000,2880,76,248,228,240,4,3,15,NEG_SYNC|GF4_INTERLACE), /* drm_edid.c:698 */
 [12]=VIC_ROW(54000,2880,76,248,228,240,4,3,15,NEG_SYNC), /* drm_edid.c:704 */
 [13]=VIC_ROW(54000,2880,76,248,228,240,4,3,15,NEG_SYNC), /* drm_edid.c:709 */
 [14]=VIC_ROW(54000,1440,32,124,120,480,9,6,30,NEG_SYNC), /* drm_edid.c:714 */
 [15]=VIC_ROW(54000,1440,32,124,120,480,9,6,30,NEG_SYNC), /* drm_edid.c:719 */
 [16]=VIC_ROW(148500,1920,88,44,148,1080,4,5,36,0), /* drm_edid.c:724 */
 [17]=VIC_ROW(27000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:729 */
 [18]=VIC_ROW(27000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:734 */
 [19]=VIC_ROW(74250,1280,440,40,220,720,5,5,20,0), /* drm_edid.c:739 */
 [20]=VIC_ROW(74250,1920,528,44,148,540,2,5,15,GF4_INTERLACE), /* drm_edid.c:744 */
 [25]=VIC_ROW(54000,2880,48,252,276,288,2,3,19,NEG_SYNC|GF4_INTERLACE), /* drm_edid.c:774 */
 [26]=VIC_ROW(54000,2880,48,252,276,288,2,3,19,NEG_SYNC|GF4_INTERLACE), /* drm_edid.c:780 */
 [27]=VIC_ROW(54000,2880,48,252,276,288,2,3,19,NEG_SYNC), /* drm_edid.c:786 */
 [28]=VIC_ROW(54000,2880,48,252,276,288,2,3,19,NEG_SYNC), /* drm_edid.c:791 */
 [29]=VIC_ROW(54000,1440,24,128,136,576,5,5,39,NEG_SYNC), /* drm_edid.c:796 */
 [30]=VIC_ROW(54000,1440,24,128,136,576,5,5,39,NEG_SYNC), /* drm_edid.c:801 */
 [31]=VIC_ROW(148500,1920,528,44,148,1080,4,5,36,0), /* drm_edid.c:806 */
 [32]=VIC_ROW(74250,1920,638,44,148,1080,4,5,36,0), /* drm_edid.c:811 */
 [33]=VIC_ROW(74250,1920,528,44,148,1080,4,5,36,0), /* drm_edid.c:816 */
 [34]=VIC_ROW(74250,1920,88,44,148,1080,4,5,36,0), /* drm_edid.c:821 */
 [35]=VIC_ROW(108000,2880,64,248,240,480,9,6,30,NEG_SYNC), /* drm_edid.c:826 */
 [36]=VIC_ROW(108000,2880,64,248,240,480,9,6,30,NEG_SYNC), /* drm_edid.c:831 */
 [37]=VIC_ROW(108000,2880,48,256,272,576,5,5,39,NEG_SYNC), /* drm_edid.c:836 */
 [38]=VIC_ROW(108000,2880,48,256,272,576,5,5,39,NEG_SYNC), /* drm_edid.c:841 */
 [39]=VIC_ROW(72000,1920,32,168,184,540,23,5,57,GF4_NEG_V|GF4_INTERLACE), /* drm_edid.c:846 */
 [40]=VIC_ROW(148500,1920,528,44,148,540,2,5,15,GF4_INTERLACE), /* drm_edid.c:852 */
 [41]=VIC_ROW(148500,1280,440,40,220,720,5,5,20,0), /* drm_edid.c:858 */
 [42]=VIC_ROW(54000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:863 */
 [43]=VIC_ROW(54000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:868 */
 [46]=VIC_ROW(148500,1920,88,44,148,540,2,5,15,GF4_INTERLACE), /* drm_edid.c:885 */
 [47]=VIC_ROW(148500,1280,110,40,220,720,5,5,20,0), /* drm_edid.c:891 */
 [48]=VIC_ROW(54000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:896 */
 [49]=VIC_ROW(54000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:901 */
 [52]=VIC_ROW(108000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:918 */
 [53]=VIC_ROW(108000,720,12,64,68,576,5,5,39,NEG_SYNC), /* drm_edid.c:923 */
 [56]=VIC_ROW(108000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:939 */
 [57]=VIC_ROW(108000,720,16,62,60,480,9,6,30,NEG_SYNC), /* drm_edid.c:945 */
 [60]=VIC_ROW(59400,1280,1760,40,220,720,5,5,20,0), /* drm_edid.c:962 */
 [61]=VIC_ROW(74250,1280,2420,40,220,720,5,5,20,0), /* drm_edid.c:967 */
 [62]=VIC_ROW(74250,1280,1760,40,220,720,5,5,20,0), /* drm_edid.c:972 */
 [63]=VIC_ROW(297000,1920,88,44,148,1080,4,5,36,0), /* drm_edid.c:977 */
 [64]=VIC_ROW(297000,1920,528,44,148,1080,4,5,36,0), /* v6.12 drm_edid.c:1090 */
};
#undef NEG_SYNC
#undef VIC_ROW
static void vic_and_capacity(void)
{
    for(u32 vic=1;vic<=LAST_VIC;vic++) {
        empty_base(); accepted_base(); cta_one(vic);
        assert(gf4_edid_modes(b,1,&state)==0);
        if(!vic_expected[vic].khz)
            assert(!state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
        else {
            assert(state.ntiming==1 && !state.last_rc);
            equal(&state.timing[0],&vic_expected[vic]);
        }
    }
    empty_base(); accepted_base(); cta_one(127);
    assert(gf4_edid_modes(b,1,&state)==0 && !state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    empty_base(); accepted_base(); cta_one(0);
    assert(gf4_edid_modes(b,1,&state)==0 && !state.ntiming && state.last_rc==GF4_EUNSUPPORTED);
    /* Build capacity through real successive CTA blocks; unique timings are
     * enumerated by VIC, aspect-ratio aliases must deduplicate. */
    empty_base(); b[EXTENSIONS]=LAST_VIC; accepted_base();
    bool saw_full=false;
    for(u32 vic=1;vic<=LAST_VIC;vic++) {
        u32 previous=state.ntiming;
        cta_one(vic);
        assert(gf4_edid_modes(b,vic,&state)==0 && state.ntiming<=GF4_TIMINGS);
        if(previous==GF4_TIMINGS && state.truncated) saw_full=true;
    }
    assert(saw_full && state.ntiming==GF4_TIMINGS && state.truncated);
    /* Native SVDs cannot replace entries in a full table or set preference.
     * Full table is a value fixture, not a private parser entry point. */
    setup(); accepted_base();
    for(u32 i=state.ntiming;i<GF4_TIMINGS;i++) {
        state.timing[i]=vga_dtd; state.timing[i].khz+=i;
    }
    state.ntiming=GF4_TIMINGS; before=state; cta_one(VIC_720P60|CTA_NATIVE);
    assert(gf4_edid_modes(b,1,&state)==0 && state.truncated);
    assert(state.preferred==0); equal(&state.timing[0],&vga_dtd);
    for(u32 i=0;i<GF4_TIMINGS;i++) equal(&state.timing[i],&before.timing[i]);
    setup(); accepted_base(); state.ntiming=GF4_TIMINGS; state.preferred=NO_PREFERRED;
    for(u32 i=0;i<GF4_TIMINGS;i++) {
        state.timing[i]=vga_dtd; state.timing[i].khz+=i;
    }
    before=state; cta_one(VIC_720P60|CTA_NATIVE);
    assert(gf4_edid_modes(b,1,&state)==0 && state.truncated &&
           state.preferred==NO_PREFERRED);
    for(u32 i=0;i<GF4_TIMINGS;i++) equal(&state.timing[i],&before.timing[i]);
    before=state; cta_one(VIC_720P60|CTA_NATIVE);
    assert(gf4_edid_modes(b,2,&state)==0); unchanged_timings();
}
void _start(void)
{
    base_and_errors(); extensions(); dtd_order_and_preference(); interlace_and_sync();
    dmt_and_established(); formula_and_range(); vic_and_capacity();
    gf2_host_exit(0);
}
