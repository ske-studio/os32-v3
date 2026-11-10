#include "platform/powermac/lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "mm/paging.h"
static unsigned lines, syncs, maps, failmap, sync_lines, ptes, bad_pte;
static u32 pte_addresses[16];
static u32 base, flags;
static unsigned dma_calls;
static void *dma_cpu;
static u32 dma_bytes;
static enum dma_sync dma_op;
int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op) {
    dma_calls++; dma_cpu=cpu; dma_bytes=bytes; dma_op=op;
    return op == DMA_POSTREAD && ((u32)cpu & 31u || bytes & 31u) ? LAN_EIO : 42;
}
void ppc_dcbf_line(void *p) { if (!lines) base=(u32)p; if ((u32)p != base+lines*32) lines=99999; else lines++; }
void ppc_sync(void) { sync_lines=lines; syncs++; }
int paging_map_range(u32 a,u32 b,u32 c,u32 f) { maps++; flags=f; if (a!=base || b!=a+65536 || c!=a || lines!=2048 || syncs!=1) return -1; return failmap ? -1 : 0; }
u32 paging_pte_raw(u32 a) {
    if (ptes < 16) pte_addresses[ptes]=a;
    ptes++;
    return a | (ptes == bad_pte ? flags & ~PTE_PCD : flags);
}
static void output(const char *s) { unsigned n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(s),"d"(n) : "memory"); }
static int test(void) {
    struct pm_gmac_boot b={0}; struct gem_dma d; u32 bus;
    output("REACHED host\n");
    b.profile=PM_GMAC_PROFILE_UNINORTH; b.dma_bytes=0xffffffffu;
#if CASE == 6
    if (pm_gmac_dma_init(&b,&d)) return 1;
    unsigned before=syncs;
    if (pm_gmac_dma_sync((u8 *)d.cpu+16,16,DMA_POSTREAD) ||
        pm_gmac_dma_sync(d.cpu,16,DMA_PREWRITE) || dma_calls || syncs!=before+2) {
        output("H:descriptor\n"); return 1;
    }
    output("REACHED descriptor\n");
    if (pm_gmac_dma_sync((u8 *)d.cpu+0xffff,1,DMA_PREWRITE) || dma_calls || syncs!=before+3 ||
        pm_gmac_dma_sync((u8 *)d.cpu+0x10000,32,DMA_PREWRITE)!=42 || dma_calls!=1 ||
        dma_cpu!=(u8 *)d.cpu+0x10000 || dma_bytes!=32 || dma_op!=DMA_PREWRITE || syncs!=before+3) {
        output("H:boundary\n"); return 1;
    }
    return 0;
#elif CASE == 1
    b.dma_cpu_base=0x08000000u; b.dma_bus_base=0x18000000u; b.dma_bytes=0x10000000u;
    if (pm_gmac_dma_init(&b,&d) || pm_dma_bus(0x08001000u,16,&bus) || bus!=0x18001000u) { output("H:bus\n"); return 1; }
    if (pm_dma_bus(0x18000000u,1,&bus)!=LAN_ERANGE) { output("H:window\n"); return 1; }
    return 0;
#elif CASE == 2
    b.profile=99;
    if (pm_gmac_dma_init(&b,&d)!=LAN_EUNSUPPORTED || pm_dma_bus(0,1,&bus)!=LAN_EUNSUPPORTED) return 1;
    return 0;
#elif CASE == 3
    failmap=1;
    if (pm_gmac_dma_init(&b,&d)>=0 || pm_gmac_dma_init(&b,&d)>=0 || maps!=1) return 1;
    return 0;
#elif CASE == 5
    bad_pte=8;
    if (pm_gmac_dma_init(&b,&d)!=LAN_EIO || ptes!=8) { output("H:pte-error\n"); return 1; }
    for (unsigned i=0; i<ptes; i++)
        if (pte_addresses[i]!=base+i*4096u) { output("H:pte-address\n"); return 1; }
    return 0;
#elif CASE == 4
    b.dma_bus_base=0xffff0000u;
    if (pm_gmac_dma_init(&b,&d)!=LAN_ERANGE) return 1;
    return 0;
#endif
    int rc = pm_gmac_dma_init(&b,&d);
    if (syncs != 1 || sync_lines != 2048) { output("H:A4 dma-unpublished\n"); return 1; }
    if (flags != (PAGE_RW | PTE_PCD)) { output("H:pcd\n"); return 1; }
    if (ptes!=16) { output("H:pte-count\n"); return 1; }
    for (unsigned i=0; i<ptes; i++)
        if (pte_addresses[i]!=base+i*4096u) { output("H:pte-address\n"); return 1; }
    if (rc || (d.pa & 65535) || d.bytes!=196608 || d.bus!=d.pa || (u32)d.cpu!=d.pa) { output("H:layout\n"); return 1; }
    if (lines!=2048 || syncs!=1) { output("H:A4 dma-unpublished\n"); return 1; }
    if (flags!=(PAGE_RW|PTE_PCD)) { output("H:pcd\n"); return 1; }
    if (pm_gmac_dma_init(&b,&d) || maps!=1 || syncs!=1 || lines!=2048) { output("H:N3\n"); return 1; }
    if (pm_dma_bus(0x1000,16,&bus) || bus!=0x1000) { output("H:bus\n"); return 1; }
    if (pm_dma_bus(0xffffffffu,2,&bus)!=LAN_ERANGE) { output("H:window\n"); return 1; }
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1),"b"(rc)); __builtin_unreachable(); }
