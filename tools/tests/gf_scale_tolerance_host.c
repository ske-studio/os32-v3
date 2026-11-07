/* Fake read-only VRAM, independent boundary fixtures; no GPU emulation. */
#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_accel_test.c"

static u8 vram[TEST_BYTES];
static struct gf_foundation foundation;
static struct gf2_chan channel;
static struct gf3_ctx context;
static const struct gf3_surface test_surface = {.off=0x10000, .bytes=TEST_BYTES};
static char output[65536];
static u32 output_used;
static bool perturb_rgb;
void serial_puts_polled(const char *s)
{
    while (*s) { assert(output_used + 1 < sizeof(output)); output[output_used++] = *s++; }
    output[output_used] = 0;
}
static u32 occurrences(const char *needle)
{
    u32 count = 0;
    for (u32 i = 0; i < output_used; i++) {
        u32 j = 0;
        while (needle[j] && output[i+j] == needle[j]) j++;
        if (!needle[j]) count++;
    }
    return count;
}
static int map(struct gf_vram_view *v, struct gf2_chan *ch, u32 off, u32 bytes,
               volatile u8 **va)
{
    assert(ch == &channel && off == test_surface.off && bytes == TEST_BYTES);
    assert(!v->mapped); v->mapped = true; *va = vram; return 0;
}
static int unmap(struct gf_vram_view *v)
{ assert(v->mapped); v->mapped = false; return 0; }
static int clock_now(void *owner, u32 *now)
{ assert(owner == &foundation); *now=100; return 0; }
static int upload(struct gf3_ctx *c, const struct gf3_surface *s,
                  struct gf3_rect r, const struct gf3_ram *ram, bool force, u32 dl)
{
    (void)c; (void)s; (void)r; (void)force; (void)dl;
    for (u32 i=0; i<TEST_BYTES; i++) vram[i]=((const u8 *)ram->ptr)[i];
    return 0;
}
/* Oracle loopback only to exercise scale_case's classification and logging.
 * Deliberate YUV RGB perturbations stand for unspecified hardware precision;
 * hand-selected raw samples above/below test the diagnostic independently. */
static int scale(struct gf3_ctx *c, const struct gf3_surface *s,
                 const struct gf3_surface *d, struct gf3_rect sr,
                 struct gf3_rect dr, struct gf3_rect clip, bool filter, u32 dl)
{
    (void)c; (void)dl;
    u32 du=(sr.w<<20)/dr.w, dv=(sr.h<<20)/dr.h, n=cpp(d->fmt);
    for (u32 y=clip.y; y<clip.y+clip.h; y++)
    for (u32 x=clip.x; x<clip.x+clip.w; x++) {
        u32 v=filtered(s->fmt,(sr.x<<20)+(x-dr.x)*du,
                       (sr.y<<20)+(y-dr.y)*dv,filter);
        u32 r=v&255U, g=(v>>8)&255U, b=(v>>16)&255U;
        u32 packed=n==2 ? ((r>>3)<<11)|((g>>2)<<5)|(b>>3) :
                         (v&0xff000000U)|(r<<16)|(g<<8)|b;
        if (filter && sr.x==0 && (s->fmt==GF3_YUY2 || s->fmt==GF3_UYVY))
            packed = (packed & ~255U) | (b <= 252U ? b + 3U : b - 3U);
        if (perturb_rgb && filter && sr.x==0 && s->fmt<=GF3_ARGB8888)
            packed ^= 2U; /* Two native blue LSBs, including RGB565. */
        put_pixel(vram+TEST_HALF+y*d->pitch+x*n,n,packed);
    }
    return 0;
}
static const struct gf3_test_ops ops = {
    .ctx=&context, .test_surface=&test_surface, .map=map, .unmap=unmap,
    .upload=upload, .scale=scale
};
const struct gf3_test_ops *geforce_accel_ops(void) { return &ops; }
static void clear(void)
{
    output_used=0; output[0]=0;
    for (u32 i=0; i<TEST_BYTES; i++) expected[i]=vram[i]=0;
    xrgb_first=xrgb_second=0;
    scale_compare_clip=(struct gf3_rect){1,2,3,4};
    first_bad=TEST_FULL_MASK;
}
static void pixel(u32 n, u32 e, u32 a, bool tolerant, int rc)
{
    clear();
    scale_compare_fmt=n == 2 ? GF3_RGB565 : GF3_ARGB8888;
    scale_compare_pitch=32*n;
    u32 off=TEST_HALF+2*scale_compare_pitch+n;
    put_pixel(expected+off,n,e); put_pixel(vram+off,n,a);
    assert(compare(false,tolerant)==rc);
    assert(!test_view.view.mapped);
}
void _start(void)
{
    channel.f=&foundation; channel.state=GF2_RUN; channel.epoch=1;
    context.ch=&channel; context.ready=true; context.epoch=1;
    for (u32 f=GF3_RGB565; f<=GF3_UYVY; f++) {
        bool converted=f==GF3_RGB565 || f==GF3_YUY2 || f==GF3_UYVY;
        assert(scale_tolerant(f,false,false)==converted);
        assert(scale_tolerant(f,true,false));
        assert(scale_tolerant(f,true,true)==(f==GF3_RGB565));
    }
    /* Both signs, every 8-bit channel including ARGB alpha. */
    for (u32 b=0; b<4; b++) {
        u32 bit=1U<<(8*b);
        pixel(4,0x80808080U,0x80808080U+bit,true,0);
        pixel(4,0x80808080U,0x80808080U-bit,true,0);
        pixel(4,0x80808080U,0x80808080U+2*bit,true,GF2_EIO);
        pixel(4,0x80808080U,0x80808080U-2*bit,true,GF2_EIO);
        pixel(4,0x80808080U,0x80808080U+bit,false,GF2_EIO);
    }
    /* Native R5/G6/B5 steps; green crosses a packed-byte carry (224). */
    const u32 steps[]={1,32,2048};
    for (u32 b=0; b<3; b++) {
        pixel(2,0x4208,0x4208+steps[b],true,0);
        pixel(2,0x4208,0x4208-steps[b],true,0);
        pixel(2,0x4208,0x4208+2*steps[b],true,GF2_EIO);
        pixel(2,0x4208,0x4208-2*steps[b],true,GF2_EIO);
    }
    pixel(2,0x00e0,0x0100,true,0);
    pixel(2,0,0xffff,true,GF2_EIO); /* no wraparound distance */
    /* XRGB unused byte stays ignored; raw differences remain observable. */
    pixel(4,0x80808080,0x80808081,true,0);
    assert(differences && crc_expected!=crc_actual);
    xrgb_second=32*scale_compare_pitch;
    vram[TEST_HALF+2*scale_compare_pitch+4+3]=0;
    assert(compare(false,true)==0);
    /* One byte outside the clip, in source, padding or tail stays fatal. */
    const u32 outside[]={0,TEST_HALF,TEST_HALF+2*128,TEST_HALF+2*128+16,
                         TEST_HALF+6*128+4,TEST_BYTES-1};
    for (u32 i=0; i<sizeof(outside)/sizeof(outside[0]); i++) {
        clear(); scale_compare_fmt=GF3_ARGB8888; scale_compare_pitch=128;
        vram[outside[i]]=1;
        assert(compare(false,true)==GF2_EIO);
    }
    /* cl0077.h CENTER/FOH leaves conversion rounding unspecified. A
     * nonuniform YUV result beyond LSB1 remains evidence, while the source,
     * clip border and padding are still exact specification controls. */
    for (u32 fmt=GF3_YUY2; fmt<=GF3_UYVY; fmt++) {
        clear();
        scale_seed(fmt,false);
        for (u32 i=0; i<TEST_BYTES; i++) vram[i]=expected[i];
        scale_compare_fmt=GF3_XRGB8888; scale_compare_pitch=128;
        xrgb_second=32*128;
        for (u32 y=2; y<6; y++)
        for (u32 x=1; x<4; x++) {
            u32 off=TEST_HALF+y*128+x*4;
            put_pixel(expected+off,4,0x00808080);
            put_pixel(vram+off,4,0x00808083);
        }
        bool read=false;
        assert(compare_impl(false,true,true,&read)==0 && read);
        assert(occurrences("result=PASS")==1);
        scale_observe(fmt,(struct gf3_rect){0,0,16,16},
                          (struct gf3_rect){1,2,24,24},
                          (16U<<20)/24, (16U<<20)/24);
        assert(occurrences("GF3 yuv pixel")==8);
        assert(occurrences("GF3 yuv source")==32);
        assert(occurrences("outside_tolerance_pixels=12 shown=8 limit=8")==1);
        assert(occurrences("expected_rgb=00808080 actual_rgb=00808083")==8);
        assert(occurrences("fx20=00000000 fy20=00000000")==1);
        /* Independent raw first sample, same YUV in either packed order. */
        assert(occurrences("sample=0 x=0 y=0 Y=32 U=96 V=176")>=1);
        assert(occurrences("sample=1 x=1 y=0 Y=40 U=96 V=176")>=1);
        assert(occurrences("sample=2 x=0 y=1 Y=35 U=96 V=176")>=1);
        assert(occurrences("sample=3 x=1 y=1 Y=43 U=96 V=176")>=1);
        /* Edge evidence clamps all four source points to the last texel. */
        scale_compare_clip=(struct gf3_rect){24,25,1,1};
        u32 edge=TEST_HALF+25*128+24*4;
        put_pixel(expected+edge,4,0); put_pixel(transfer+edge,4,3);
        output_used=0; output[0]=0;
        scale_observe(fmt,(struct gf3_rect){0,0,16,16},
                          (struct gf3_rect){1,2,24,24},
                          (16U<<20)/24, (16U<<20)/24);
        assert(occurrences("x=15 y=15 Y=197 U=96 V=176")==4);
        scale_compare_clip=(struct gf3_rect){1,2,3,4};
        for (u32 i=0; i<sizeof(outside)/sizeof(outside[0]); i++) {
            u32 off=outside[i];
            vram[off]^=1;
            output_used=0; output[0]=0;
            assert(compare_impl(false,true,true,&read)==GF2_EIO && read);
            assert(occurrences("result=FAIL")==1);
            vram[off]^=1;
        }
    }
    /* Independent RGB fixtures: count pixels once, retain native LSB1
     * acceptance and log full-surface edge coordinates like the YUV probe.
     * X/nv04_xv_blit.c:141-154 submits CENTER/FOH; no new pixel rule. */
    for (u32 fmt=GF3_RGB565; fmt<=GF3_ARGB8888; fmt++) {
        clear();
        scale_compare_fmt=fmt; scale_compare_pitch=32*cpp(fmt);
        scale_compare_clip=(struct gf3_rect){22,22,3,4};
        if (fmt==GF3_XRGB8888) xrgb_second=32*scale_compare_pitch;
        u32 e=fmt==GF3_RGB565 ? 0x4208 : 0x7d808080;
        u32 a=fmt==GF3_RGB565 ? 0x420a : 0x7d808083;
        for (u32 y=22; y<26; y++)
        for (u32 x=22; x<25; x++) {
            u32 off=TEST_HALF+y*scale_compare_pitch+x*cpp(fmt);
            put_pixel(expected+off,cpp(fmt),e);
            put_pixel(vram+off,cpp(fmt),a);
        }
        bool read=false;
        assert(compare_impl(false,true,false,&read)==GF2_EIO && read);
        scale_observe(fmt,(struct gf3_rect){0,0,16,16},
                      (struct gf3_rect){1,2,24,24},0xaaaaa,0xaaaaa);
        assert(occurrences("GF3 rgb pixel")==8);
        assert(occurrences("GF3 yuv source")==0);
        assert(occurrences("outside_tolerance_pixels=12 shown=8 limit=8")==1);
        assert(occurrences("x=22 y=22 fx20=00dffff2 fy20=00d55548")==1);
        assert(occurrences(fmt==GF3_RGB565 ?
            "expected_rgb=00424142 actual_rgb=00424152" :
            "expected_rgb=00808080 actual_rgb=00808083")==8);
        /* Exact and one-native-LSB controls produce no rejected pixels. */
        for (u32 i=0; i<TEST_BYTES; i++) vram[i]=expected[i];
        u32 off=TEST_HALF+22*scale_compare_pitch+22*cpp(fmt);
        put_pixel(vram+off,cpp(fmt),e+1);
        assert(compare_impl(false,true,false,&read)==0 && read);
        output_used=0; output[0]=0;
        scale_observe(fmt,(struct gf3_rect){0,0,16,16},
                      (struct gf3_rect){1,2,24,24},0xaaaaa,0xaaaaa);
        assert(occurrences("GF3 rgb pixel")==0);
        assert(occurrences("outside_tolerance_pixels=0 shown=0 limit=8")==1);
    }
    clear();
    foundation.io.opaque=&foundation; foundation.io.timing.clock_us=clock_now;
    assert(scale_case()==0);
    assert(occurrences("GF3 rgb observation")==3);
    assert(occurrences("outside_tolerance_pixels=0 shown=0 limit=8")==3);
    assert(occurrences("GF3 yuv observation")==2);
    assert(occurrences("outside_tolerance_pixels=576 shown=8 limit=8")==2);
    clear(); perturb_rgb=true;
    assert(scale_case()==GF2_EIO);
    assert(occurrences("GF3 rgb observation")==3);
    assert(occurrences("GF3 rgb pixel")==24);
    assert(occurrences("outside_tolerance_pixels=576 shown=8 limit=8")==5);
    gf2_host_exit(0);
}
