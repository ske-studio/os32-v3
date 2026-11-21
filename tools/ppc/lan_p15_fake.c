/* PowerMac GEM layout (2026-11-21): OpenBSD gemvar.h RX count,
 * four reserved descriptors, and OS32 HW2 64 KiB DMA boundaries. */
#include "gem.h"
static u8 pool[0x60000] __attribute__((aligned(4096)));
static struct gem g;
static unsigned n, syncs, master;
static const u32 regs[] = {0x200c,0x2008,0x4008,0x4004,0x10,0x6024,0x6020,0x6028,0x2004,0x2000,0x4000,0x4020,0x4108};
static const u32 vals[] = {0,0x100000,0,0x101000,~0u,5,0xffff,0,0x13fc01,0,0x4000805,0x100300,0x6006};
static int crosses_boundary, bad, fail_once = 1, master_fail_once = 1;
static void output(const char *s, unsigned len) { __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory"); }
#define SAY(s) output(s, sizeof(s)-1)
static u32 rd(void *c, u32 r) { (void)c; if (r != 0x4120) bad=1; return 1024; }
static void wr(void *c, u32 r, u32 v) { (void)c; if (n>=13 || r!=regs[n] || v!=vals[n] || (n>=8 && !master)) bad=1; n++; }
static void fail(const char *s, unsigned len) { output(s,len); __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable(); }
#define FAIL(s) fail(s,sizeof(s)-1)
static int sy(void *p, u32 bytes, enum dma_sync op) {
    u8 *q=p;
    u32 first = g.dma.bus + (u32)(q - pool);
    if (!bytes || (first >> 16) != ((first + bytes - 1) >> 16))
        crosses_boundary = 1;
    if (syncs==0) { if(q!=pool || bytes!=512 || op!=DMA_PREWRITE) bad=1; }
    else if(syncs<=124) { if(q!=pool+0x20000+(syncs-1)*2048 || bytes!=2048 || op!=DMA_PREREAD || pool[0x1000+(syncs-1)*16+3]) bad=1; }
    else if(syncs==125) { if(q!=pool+0x1000 || bytes!=2048 || op!=DMA_PREWRITE) bad=1; }
    else bad=1;
    if (syncs == 5 && fail_once) { fail_once = 0; return -1; }
    syncs++; return 0;
}
static int ma(void *c) { (void)c; if(n!=8 || syncs!=126) bad=1; master++; if (master_fail_once) { master_fail_once = 0; return -1; } return 0; }
void _start(void) {
    struct gem_dma d={pool,0x100000,0x100000,sizeof(pool)};
    g.io.read32=rd; g.io.write32=wr; g.io.sync=sy; g.io.master_enable=ma;
    SAY("REACHED host\n");
    for (unsigned i = 0; i < 128 * 16; i++) pool[0x1000+i] = 0xa5;
    d.bytes--;
    if (gem_ring_prepare(&g,&d) != GEM_EINVAL) FAIL("H:length\n");
    d.bytes++;
    d.bus += 0xfc00; /* The first 2 KiB TX buffer would cross a 64 KiB boundary. */
    if (gem_ring_prepare(&g,&d) != GEM_EINVAL) FAIL("H:boundary\n");
    d.bus -= 0xfc00;
    if(gem_ring_prepare(&g,&d) != GEM_EIO || syncs != 5 || gem_ring_prepare(&g,&d) || gem_ring_prepare(&g,&d) || syncs!=126) FAIL("H:sync\n");
    if (crosses_boundary) FAIL("H:dma-span\n");
    for(unsigned i=0;i<128;i++) for(unsigned j=0;j<16;j++) {
        u32 addr=0x120000+i*2048; u8 expected=0;
        if(i<124) { if(j==2) expected=0; if(j==3) expected=0x88; if(j>=8 && j<12) expected=(u8)(addr >> ((j-8)*8)); }
        if(pool[0x1000+i*16+j]!=expected) FAIL("H:descriptor\n");
    }
    if(gem_ring_publish(&g) != GEM_EIO || n!=8 || master!=1 || bad) FAIL("H:publish\n");
    if(gem_ring_publish(&g) || gem_ring_publish(&g) || n!=13 || master!=2 || bad) FAIL("H:publish\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); __builtin_unreachable();
}
