#include "../../hwdev/gf/foundation.h"
#include "../../mm/paging.h"
#include "../../include/memmap.h"
static u32 present, map_fail, unmap_fail, reserved_fail, calls, start_seen, end_seen, flags_seen;
int geforce_range_reserved(u32 base, u32 bytes) { (void)base; (void)bytes; return reserved_fail; }
u32 paging_pte_raw(u32 addr) { return addr == present ? PTE_PRESENT : 0; }
int paging_map_phys(u32 v, u32 p, u32 n, u32 flags)
{ calls++; start_seen=v; end_seen=p; flags_seen=flags; return map_fail || n != 2; }
int paging_set_not_present(u32 start, u32 end)
{ start_seen=start; end_seen=end; return unmap_fail; }
static void fail(int n) { __asm__ volatile("int $0x80" : : "a"(1), "b"(n)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
void _start(void)
{
    struct gf_foundation f = {0};
    struct recover_bar_extent b = {0, 0x25000000, 0x4000, 1};
    f.map_ops = &gf_production_instmem_map_ops;
    CHECK(gf_instmem_map(&f, &b, 0x5000) == GF1_EMAP && !calls);
    b.valid=0; CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_EMAP && !calls); b.valid=1;
    b.base++; CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_EMAP && !calls); b.base--;
    reserved_fail=1; CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_EMAP && !calls); reserved_fail=0;
    present=b.base+0x1000; CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_EMAP && !calls); present=0;
    map_fail=1; CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_EMAP && !f.mapped && !f.bar2); map_fail=0;
    for (int i=0; i<2; i++) {
        CHECK(gf_instmem_map(&f, &b, 0x2000) == GF1_OK && f.mapped);
        CHECK(start_seen == b.base && end_seen == b.base && flags_seen == (PAGE_RW | PTE_PCD));
        CHECK(f.imem.aperture_bytes == 0x2000 && V2P(f.bar2) == b.base);
        unmap_fail=1; CHECK(gf_instmem_unmap(&f) == GF1_EMAP && f.mapped && f.bar2 && f.imem.aperture_bytes == 0x2000);
        unmap_fail=0; CHECK(gf_instmem_unmap(&f) == GF1_OK && !f.mapped && !f.bar2 && !f.imem.aperture_bytes);
        CHECK(start_seen == b.base && end_seen == b.base + 0x1fff);
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
