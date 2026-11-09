/* Real palette state and PC-98 provider; replace only hardware I/O. */
#include "types.h"
#include "io.h"
#include "palette.h"
static unsigned ports[128], values[128], count;
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void expect(int ok) { if (!ok) finish(1); }
static void port_write(unsigned p, unsigned v)
{
    expect(count < 128);
    ports[count] = p; values[count++] = v;
}
static unsigned reads;
static unsigned port_read(unsigned p)
{
    expect(p == 0xa0 && count < 128);
    unsigned v = (reads == 0 || reads == 3) ? 0x20 : 0;
    reads++;
    ports[count] = p; values[count++] = v;
    return v;
}
#define inp(p) port_read(p)
#define outp(p,v) port_write(p,v)
#include "display_source.inc"
#include "palette_source.inc"
#include "scroll_provider_source.inc"
#include "scroll_source.inc"
u8 *bb_b;
int gfx_current_height = 400, gfx_flip_enabled, gfx_display_page;
/* Literal pre-separation wire format: command, VSYNC high/low/low/high,
 * then two little-endian SAD/SL partitions. No product body is copied. */
static void scroll_trace(int y, int height, int flip, int page)
{
    unsigned base = flip ? page * height * 40 : 0;
    unsigned sad = base + y * 40;
    unsigned first = (height-y) * (400/height), second = y * (400/height);
    unsigned expected[] = {0x70,0x20,0,0,0x20,
        sad&255, sad>>8, (first&15)<<4, (first>>4)&63,
        base&255, base>>8, (second&15)<<4, (second>>4)&63};
    expect(count == 13);
    for (unsigned i=0;i<13;i++) {
        expect(ports[i] == (i == 0 ? 0xa2 : 0xa0));
        expect(values[i] == expected[i]);
    }
}
static void scroll_cases(void)
{
    static const int lines[] = {-1, 0, 1, -401, -400, -399, -201, -200,
        -199, 199, 200, 201, 399, 400, 401, 2147483647, -2147483647-1};
    for (int height=200;height<=400;height+=200)
    for (int flip=0;flip<2;flip++) for (int page=0;page<2;page++) {
        gfx_current_height=height; gfx_flip_enabled=flip; gfx_display_page=page;
        bb_b=(u8 *)1;
        vram_scroll_y=77; count=reads=0;
        gfx_scroll_init(); expect(vram_scroll_y == 0);
        scroll_trace(0,height,flip,page);
        int y=0;
        for (unsigned i=0;i<sizeof(lines)/sizeof(lines[0]);i++) {
            /* Unsigned magnitude oracle also handles INT_MIN without overflow. */
            unsigned magnitude = lines[i]<0 ? 0U-(unsigned)lines[i] : (unsigned)lines[i];
            int delta = (int)(magnitude % (unsigned)height);
            y += lines[i]<0 ? -delta : delta;
            if (y<0) y+=height;
            if (y>=height) y-=height;
            count=reads=0; gfx_hardware_scroll(lines[i]);
            expect(vram_scroll_y == y); scroll_trace(y,height,flip,page);
        }
        bb_b=0; count=reads=0; gfx_hardware_scroll(-1);
        expect(count == 0 && vram_scroll_y == y);
    }
}
/* Fixed pre-separation index/G/R/B sequence, with no waits or other I/O. */
static void color(unsigned n, unsigned index, unsigned r, unsigned g, unsigned b)
{
    expect(ports[n] == 0xa8 && values[n] == index);
    expect(ports[n+1] == 0xaa && values[n+1] == g);
    expect(ports[n+2] == 0xac && values[n+2] == r);
    expect(ports[n+3] == 0xae && values[n+3] == b);
}
static void entry(int index, unsigned r, unsigned g, unsigned b)
{
    u8 rr=99, gg=99, bb=99;
    palette_get(index, &rr, &gg, &bb);
    expect(rr == r && gg == g && bb == b);
    const PaletteEntry *all = palette_get_all();
    expect(all[index].r == r && all[index].g == g && all[index].b == b);
    palette_get(index, 0, 0, 0);
}
void _start(void)
{
    for (int i=0; i<16; i++) entry(i,0,0,0);
    palette_init();
    expect(count == 64);
    for (int i=0; i<16; i++) {
        unsigned v=i<8 ? 7 : 15;
        unsigned r=i&2 ? v : 0, g=i&4 ? v : 0, b=i&1 ? v : 0;
        color(i*4,i,r,g,b); entry(i,r,g,b);
    }
    for (int i=0; i<16; i++) for (unsigned v=0; v<256; v++) {
        count=0;
        palette_set(i,v,v+1,v+2);
        expect(count == 4); color(0,i,v&15,(v+1)&15,(v+2)&15);
        entry(i,v&15,(v+1)&15,(v+2)&15);
        count=0;
        /* PEGC already wrote its native 8-bit DAC; update only legacy shadow. */
        palette_shadow_set(i,v+3,v+4,v+5);
        expect(count == 0); entry(i,(v+3)&15,(v+4)&15,(v+5)&15);
    }
    static const int invalid[] = {-2147483647-1,-1,16,17,2147483647};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        u8 r=21,g=22,b=23;
        count=0;
        palette_set(invalid[i],255,255,255);
        palette_shadow_set(invalid[i],255,255,255);
        palette_get(invalid[i],&r,&g,&b);
        expect(count == 0 && r == 21 && g == 22 && b == 23);
        for (int j=0;j<16;j++) entry(j,2,3,4);
    }
    count=0;
    display_palette4_write(7,0xf1,0xe2,0xd3);
    expect(count == 4); color(0,7,1,2,3);
    count=0; palette_init(); expect(count == 64);
    for (int i=0;i<16;i++) {
        unsigned v=i<8 ? 7 : 15;
        entry(i,i&2 ? v : 0,i&4 ? v : 0,i&1 ? v : 0);
    }
    scroll_cases();
    finish(0);
}
