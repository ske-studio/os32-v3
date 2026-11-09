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
#define outp(p,v) port_write(p,v)
#include "display_source.inc"
#include "palette_source.inc"
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
    finish(0);
}
