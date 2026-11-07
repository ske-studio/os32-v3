/* S02 construction/row-stride regression, no hardware result claimed. */
#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_accel_test.c"

static u8 vram[TEST_BYTES];
static char output[32768];
static u32 output_used, calls, maps;
static struct gf_foundation foundation;
static struct gf2_chan channel;
static struct gf3_ctx context;
static const struct gf3_surface test_surface = {
    .off=0x10000, .bytes=TEST_BYTES, .reservation_id=1, .generation=1
};
static int clock_now(void *owner, u32 *now)
{ assert(owner == &foundation); *now=100; return 0; }
void serial_puts_polled(const char *s)
{
    while (*s) { assert(output_used+1 < sizeof(output)); output[output_used++]=*s++; }
    output[output_used]=0;
}

/* Model only linear byte movement; drawing alignment is checked at the same
 * boundary as the real IFC/SIFM. RAM rows retain their +3B gaps. */
static int move(const struct gf3_surface *s, struct gf3_rect r,
                const struct gf3_ram *ram, bool down)
{
    const u32 sizes[]={1,2,4,4,1,2,2};
    u32 n=sizes[s->fmt], off=s->off-test_surface.off;
    for (u32 y=0;y<r.h;y++)
    for (u32 b=0;b<r.w*n;b++) {
        u32 gpu=off+(r.y+y)*s->pitch+r.x*n+b, cpu=y*ram->pitch+b;
        assert(gpu<sizeof(vram) && cpu<ram->bytes);
        if (down) ((u8 *)ram->ptr)[cpu]=vram[gpu];
        else vram[gpu]=((const u8 *)ram->ptr)[cpu];
    }
    return 0;
}
static int upload(struct gf3_ctx *c, const struct gf3_surface *s,
                  struct gf3_rect r, const struct gf3_ram *ram, bool force, u32 dl)
{
    assert(c==&context && dl>100);
    if (!force) {
        assert(!(s->pitch&63U) && !(s->off&255U));
        assert(s->width==63 && (s->pitch==128 || s->pitch==256));
        ++calls;
    }
    return move(s,r,ram,false);
}
static int download(struct gf3_ctx *c, const struct gf3_surface *s,
                    struct gf3_rect r, struct gf3_ram *ram, u32 dl)
{ assert(c==&context && dl>100); return move(s,r,ram,true); }
static int ifc(struct gf3_ctx *c, const struct gf3_surface *s,
               struct gf3_rect r, const struct gf3_ram *ram, u32 dl)
{
    if (s->fmt==GF3_INDEX8) return GF3_EFORMAT;
    return upload(c,s,r,ram,false,dl);
}
static int scale(struct gf3_ctx *c, const struct gf3_surface *s,
                 const struct gf3_surface *d, struct gf3_rect sr,
                 struct gf3_rect dr, struct gf3_rect clip, bool filter, u32 dl)
{
    (void)sr; (void)dr; (void)clip; (void)filter;
    assert(c==&context && dl>100 && s->pitch==64 && !(d->pitch&63U));
    assert(!(s->off&255U) && !(d->off&255U));
    ++calls;
    /* Stop before rendering: this test verifies construction and seed
     * addressing; it does not claim to emulate SIFM interpolation. */
    return GF2_EFAULT;
}
static int map(struct gf_vram_view *v, struct gf2_chan *ch, u32 off, u32 bytes,
               volatile u8 **va)
{
    assert(ch==&channel && off==test_surface.off && bytes==sizeof(vram));
    assert(!v->mapped); v->mapped=true; *va=vram; ++maps; return 0;
}
static int unmap(struct gf_vram_view *v)
{ assert(v->mapped); v->mapped=false; return 0; }

static const struct gf3_test_ops ops={
    .ctx=&context, .test_surface=&test_surface, .upload=upload,
    .download=download, .ifc=ifc,
    .scale=scale, .map=map, .unmap=unmap
};
const struct gf3_test_ops *geforce_accel_ops(void) { return &ops; }
void _start(void)
{
    foundation.io.opaque=&foundation; foundation.io.timing.clock_us=clock_now;
    channel.f=&foundation; channel.state=GF2_RUN; channel.epoch=1;
    context.ch=&channel; context.ready=true; context.epoch=1;
    first_bad=0xffffffffU;
    assert(transfer_case(TEST_IFC)==0 && calls==2);
    calls=0;
    assert(transfer_case(TEST_UPLOAD)==0 && calls==2);
    assert(transfer_case(TEST_DOWNLOAD)==0);
    /* Independent sample (x=7,y=9) of the documented seed and BT.601
     * ideal: RGB=(112,144,224), Y=128, packed YUV=(Y115,U96,V176).
     * Constants include RGB565 bit replication. */
    const u32 colors[]={0xffe79273U,0xffe09070U,0x7de09070U,
                        0xff808080U,0xff3359c0U,0xff3359c0U};
    const u32 raw[]={0x749cU,0x7d7090e0U,0x7d7090e0U,128U,0xb073U,0x73b0U};
    const u32 sizes[]={2,4,4,1,2,2};
    for (u32 f=GF3_RGB565;f<=GF3_UYVY;f++) {
        u32 i=f-GF3_RGB565;
        scale_seed((enum gf3_fmt)f,false,false);
        assert(get_pixel(expected+9*64+7*sizes[i],sizes[i])==raw[i]);
        assert(rgba((enum gf3_fmt)f,7,9)==colors[i]);
    }
    calls=0;
    assert(scale_case()==GF2_EFAULT && calls==1);
    gf2_host_exit(0);
}
