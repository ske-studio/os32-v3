/* Compare the shipped guest reference with the software renderer, freestanding. */
#include "gfx2_sw.h"
#define main guest_main
#include "../../userland/tests/gfx2_rh.c"
#undef main
static u8 dst[64*16*2], src[64*16*2];
int gfxsync_read_rows(GfxSyncCtx *s, u32 x, u32 y, u32 w, u32 h, u8 *p, u32 pitch)
{ (void)s;(void)x;(void)y;(void)w;(void)h;(void)p;(void)pitch; return -1; }
int gfxsync_write_rows(GfxSyncCtx *s, u32 x, u32 y, u32 w, u32 h, const u8 *p, u32 pitch)
{ (void)s;(void)x;(void)y;(void)w;(void)h;(void)p;(void)pitch; return -1; }
static void stop(int rc)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
void _start(void)
{
    static const struct { u8 op, rop, pat; } cases[] = {
        {GFX2_OP_FILL,0x5a,1},{GFX2_OP_FILL,0xa0,2},
        {GFX2_OP_FILL,0x0f,0},{GFX2_OP_FILL,0x55,0},
        {GFX2_OP_COPY,0x66,0},{GFX2_OP_COPY,0x33,0},
        {GFX2_OP_COPY,0xb8,1},{GFX2_OP_COPY,0xca,2}};
    if (check_cases[2].expected_gf != GFX2_HOW_NOSUPPORT) stop(1);
    for (u32 fmt=GFX2_FMT_INDEX8; fmt<=GFX2_FMT_RGB565; fmt++) {
        u32 cpp=fmt==GFX2_FMT_INDEX8?1:2, mask=cpp==1?255:65535;
        u32 fg=cpp==1?0x2d:0xf81f, bg=cpp==1?0x96:0x07e0;
        struct gfx2_pattern pat={0};
        for (u32 i=0;i<8;i++) pat.mono[i]=mono[i];
        for (u32 i=0;i<64;i++) pat.color[i]=color[i]=cpp==1?(i*37+11)&255:(i*0x421+0x1234)&65535;
        pat.fg.v=fg; pat.bg.v=bg;
        for (u32 c=0;c<8;c++) {
            for (u32 y=0;y<16;y++) for (u32 x=0;x<64;x++) {
                put(dst,y*64+x,cpp,initial(x,y,fmt,0));
                put(src,y*64+x,cpp,initial(x,y,fmt,1));
            }
            pat.kind=cases[c].pat;
            GFX2_Op op={.op=cases[c].op,.rop=cases[c].rop,.flags=pat.kind?GFX2_F_PAT:0,
                .dx=3,.dy=5,.dw=37,.dh=9,.sx=9,.sy=2,.sw=37,.sh=9,
                .fg={.v=cpp==1?0x4b:0x1234}};
            struct gfx2_exec ex={.op=&op,.dv={.base=dst,.pitch=64*cpp,.fmt=fmt},
                .sv={.base=src,.pitch=64*cpp,.fmt=fmt},.clip={3,5,37,9},.pat=&pat};
            struct gfx2_rect touched;
            if (gfx2_sw_rop(&ex,&touched)) stop(1);
            for (u32 y=0;y<16;y++) for (u32 x=0;x<64;x++) {
                u32 want=initial(x,y,fmt,0);
                if (x>=3 && x<40 && y>=5 && y<14) {
                    u32 p=ref_pixel(x,y,pat.kind,fg,bg,op.fg.v);
                    want=ref_rop3(p,initial(x-3+9,y-5+2,fmt,1),want,op.rop)&mask;
                }
                if (get(dst,y*64+x,cpp)!=want) stop(1);
            }
        }
    }
    stop(0);
}
