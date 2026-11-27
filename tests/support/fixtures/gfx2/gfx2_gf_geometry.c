/* Compare GART range admission and KEEP fills using the product validators. */
#include "sys/dev/video/geforce/accel.h"
static int rops, colors, rectangles;
int gf2_fault_check(struct gf2_chan *ch) { return 0; }
int gf_vram_match(struct gf2_chan *ch, const void *owner, u32 id, u32 gen,
                  u32 off, u32 bytes, struct gf_vram_reservation *out)
{
    return id == 1 && gen == 2 ? 0 : GF3_ERANGE;
}
static int range(void *ctx, u32 dma, u32 off, u32 bytes)
{
    return off <= 8192 && bytes <= 8192 - off ? 0 : GF3_ERANGE;
}
int gf3_rop_set(struct gf3_ctx *c, u8 subc, u8 alu, u32 mask, enum gf3_fmt fmt, u32 dl)
{
    rops++;
    return 0;
}
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc, const u32 *data,
             u16 n, u32 dl)
{
    if (subc == GF3_SUBC_GDI && method == 0x03fc && n == 1 && data[0] == 5) colors++;
    if (subc == GF3_SUBC_GDI && method == 0x0400 && n == 2) rectangles++;
    return 0;
}
static int run(void)
{
    static struct gf2_chan ch = {.state = GF2_RUN};
    struct gf3_ctx c = {.ready = true, .ch = &ch};
    struct gf3_surface s = {.bytes = 8192, .pitch = 64, .width = 64, .height = 128,
                            .fmt = GF3_INDEX8, .dma = GF3_HANDLE_GART_DMA};
    struct gf3_rect r = {0, 0, 1, 1};
    struct gf3_format f;
    struct gf3_span span;
    if (gf3_surface_check(&c, &s, r, GF3_USE_BYTES, &f, &span) != GF3_EINVAL) return 1;
    c.range_q = range;
    if (gf3_surface_check(&c, &s, r, GF3_USE_BYTES, &f, &span) ||
        gf3_surface_check(&c, &s, r, GF3_USE_GDI, &f, &span) != GF3_EUNSUPPORTED) return 2;
    s.off = 4096;
    if (gf3_surface_check(&c, &s, r, GF3_USE_BYTES, &f, &span) != GF3_ERANGE) return 3;
    s.off = 0xfffff000;
    if (gf3_surface_check(&c, &s, r, GF3_USE_BYTES, &f, &span) != GF3_ERANGE) return 3;
    s.dma = 0; s.off = 0; s.reservation_id = 1; s.generation = 2;
    if (gf3_fill_emit(&c, &s, r, 5, GF3_ALU_KEEP, 255, 9) ||
        rops || colors != 1 || rectangles != 1) return 4;
    if (gf3_fill_emit(&c, &s, r, 5, 3, 255, 9) ||
        rops != 1 || colors != 2 || rectangles != 2) return 5;
    return 0;
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
