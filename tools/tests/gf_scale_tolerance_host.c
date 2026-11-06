/* Fake read-only VRAM, independent boundary fixtures; no GPU emulation. */
#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_accel_test.c"

static u8 vram[TEST_BYTES];
static struct gf_foundation foundation;
static struct gf2_chan channel;
static struct gf3_ctx context;
static const struct gf3_surface test_surface = {.off=0x10000, .bytes=TEST_BYTES};
void serial_puts_polled(const char *s) { (void)s; }
static int map(struct gf_vram_view *v, struct gf2_chan *ch, u32 off, u32 bytes,
               volatile u8 **va)
{
    assert(ch == &channel && off == test_surface.off && bytes == TEST_BYTES);
    assert(!v->mapped); v->mapped = true; *va = vram; return 0;
}
static int unmap(struct gf_vram_view *v)
{ assert(v->mapped); v->mapped = false; return 0; }
static const struct gf3_test_ops ops = {
    .ctx=&context, .test_surface=&test_surface, .map=map, .unmap=unmap
};
const struct gf3_test_ops *geforce_accel_ops(void) { return &ops; }
static void clear(void)
{
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
    gf2_host_exit(0);
}
