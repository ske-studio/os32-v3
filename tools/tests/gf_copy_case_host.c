/* Execute the real foreground copy table. Device adapters model linear VRAM
 * and reject pitches outside nouveau_exa.c:435's 64-byte-aligned domain.
 * No GPU result is claimed; snapshot-before-write models overlapping blits. */
#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_accel_test.c"

static u8 vram[TEST_BYTES], saved_pixels[TEST_BYTES];
static char output[32768];
static u32 output_used, calls, maps, fail_sample;
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
static int upload(struct gf3_ctx *c, const struct gf3_surface *s,
                  struct gf3_rect r, const struct gf3_ram *ram, bool force, u32 dl)
{
    assert(c==&context && force && s->bytes==sizeof(vram));
    assert(r.w*r.h==sizeof(vram) && ram->bytes==sizeof(vram) && dl>100);
    gf2_host_memcpy(vram,ram->ptr,sizeof(vram));
    return 0;
}
static int blit(struct gf3_ctx *c, const struct gf3_surface *s,
                const struct gf3_surface *d, struct gf3_rect r,
                u32 x, u32 y, u8 alu, u32 mask, u32 dl)
{
    assert(c==&context && alu==3 && mask==0xffffffffU && dl>100);
    /* Independent oracle, not TEST_COPY_DST_PITCH: all four formats must
     * stay within EXA's pitch domain and exercise different src/dst strides. */
    assert(!(s->pitch & 63U) && !(d->pitch & 63U));
    u32 direction=calls%11, format=calls/11;
    assert(s->fmt==format && d->fmt==format);
    const u32 bytes_per_pixel[]={1,2,4,4};
    u32 n=bytes_per_pixel[format];
    if (direction==10) {
        assert(d->pitch!=s->pitch && d->pitch>=32*n);
        assert(d->off==s->off+12288 && d->pitch*32<=8192);
        /* Input has to be visible even when this callback fails. */
        assert(gf2_host_strstr(output,"GF3 copy input direction=10 fmt=0"));
        assert(gf2_host_strstr(output," src_pitch=64 dst_pitch=192"));
    }
    ++calls;
    if (calls==fail_sample) return GF2_EFAULT;
    gf2_host_memcpy(saved_pixels,vram,sizeof(vram));
    u32 src=s->off-test_surface.off, dst=d->off-test_surface.off;
    for(u32 row=0;row<r.h;row++)
    for(u32 b=0;b<r.w*n;b++) {
        u32 from=src+(r.y+row)*s->pitch+r.x*n+b;
        u32 to=dst+(y+row)*d->pitch+x*n+b;
        assert(from<sizeof(vram) && to<sizeof(vram));
        vram[to]=saved_pixels[from];
    }
    return 0;
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
    .ctx=&context, .test_surface=&test_surface, .copy=blit,
    .upload=upload, .map=map, .unmap=unmap
};
const struct gf3_test_ops *geforce_accel_ops(void) { return &ops; }
void _start(void)
{
    foundation.io.opaque=&foundation;
    foundation.io.timing.clock_us=clock_now;
    channel.f=&foundation; channel.state=GF2_RUN; channel.epoch=1;
    context.ch=&channel; context.ready=true; context.epoch=1;
    first_bad=0xffffffffU;
    assert(copy_case()==0);
    assert(calls==44 && maps==44 && differences==0);
    /* Inject the original failure location: its input must survive. */
    calls=maps=output_used=0; output[0]=0; fail_sample=11;
    assert(copy_case()==GF2_EFAULT && calls==11 && maps==10);
    assert(gf2_host_strstr(output,"GF3 copy input direction=10 fmt=0"));
    gf2_host_exit(0);
}
