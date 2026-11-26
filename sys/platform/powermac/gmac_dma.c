/* OpenBSD sys/dev/ic/gem.c:146-175: coherent control DMA allocation. */
#include "lan_boot.h"
#include "sys/dev/net/gem/gem.h"
#include "platform_ppc_memmap.h"
#include "sys/kern/mm/paging.h"

#define DMA_BLOCK 0x10000u
#define DMA_BYTES GEM_POOL_SIZE

_Static_assert(GEM_POOL_TX_RING + GEM_TX_RING_COUNT * GEM_DESC_SIZE <= GEM_POOL_RX_RING, "TX ring boundary");
_Static_assert(GEM_POOL_RX_RING + GEM_RX_RING_COUNT * GEM_DESC_SIZE <= DMA_BLOCK, "RX ring boundary");
_Static_assert(GEM_POOL_TX_RING % 4096 == 0 && GEM_POOL_RX_RING % 4096 == 0, "ring alignment");
_Static_assert(DMA_BLOCK % GEM_BUF_SIZE == 0, "buffer boundary");
_Static_assert(GEM_POOL_TX_BUF == DMA_BLOCK && GEM_POOL_RX_BUF % DMA_BLOCK == 0, "buffer alignment");
_Static_assert(GEM_POOL_TX_BUF + GEM_TX_RING_COUNT * GEM_BUF_SIZE == GEM_POOL_RX_BUF, "TX region");
_Static_assert(GEM_POOL_RX_BUF + GEM_RX_RING_COUNT * GEM_BUF_SIZE == DMA_BYTES, "RX region");
static u8 pool[DMA_BYTES] __attribute__((aligned(DMA_BLOCK)));
static struct pm_gmac_boot saved;
static struct gem_dma result;
static int attempted, status;

void ppc_dcbf_line(void *line);
void ppc_sync(void);
int ppc_dma_sync(void *cpu, u32 bytes, enum dma_sync op);

int pm_gmac_dma_sync(void *cpu, u32 bytes, enum dma_sync op)
{
    if ((unsigned long)cpu - (unsigned long)pool < DMA_BLOCK) {
        ppc_sync();
        return 0;
    }
    return ppc_dma_sync(cpu, bytes, op);
}

int pm_dma_bus(u32 pa, u32 bytes, u32 *bus)
{
    u32 off, address;
    if (saved.profile != PM_GMAC_PROFILE_UNINORTH) return LAN_EUNSUPPORTED;
    if (!bus || !bytes) return LAN_EINVAL;
    if (pa < saved.dma_cpu_base) return LAN_ERANGE;
    off = pa - saved.dma_cpu_base;
    if (off >= saved.dma_bytes || bytes > saved.dma_bytes - off ||
        bytes - 1u > 0xffffffffu - pa) return LAN_ERANGE;
    if (off > 0xffffffffu - saved.dma_bus_base) return LAN_ERANGE;
    address = saved.dma_bus_base + off;
    if (bytes - 1u > 0xffffffffu - address) return LAN_ERANGE;
    *bus = address;
    return LAN_OK;
}

int pm_gmac_dma_init(const struct pm_gmac_boot *boot, struct gem_dma *out)
{
    u32 pa, bus;
    if (!boot || !out) return LAN_EINVAL;
    if (attempted) {
        if (status == LAN_OK) *out = result;
        return status;
    }
    attempted = 1;
    saved = *boot;
    pa = pm_v2p_ram(pool);
    status = pm_dma_bus(pa, DMA_BYTES, &bus);
    if (status != LAN_OK) return status;
    for (u32 off = 0; off < DMA_BLOCK; off += PPC_CACHE_LINE)
        ppc_dcbf_line(pool + off);
    ppc_sync();
    if (paging_map_range(pa, pa + DMA_BLOCK, pa, PAGE_RW | PTE_PCD) != 0) {
        status = LAN_EIO;
        return status;
    }
    for (u32 off = 0; off < DMA_BLOCK; off += 4096u) {
        if ((paging_pte_raw(pa + off) & (0xfffff000u | PAGE_RW | PTE_PCD)) !=
            ((pa + off) | PAGE_RW | PTE_PCD)) {
            status = LAN_EIO;
            return status;
        }
    }
    result.cpu = pool; result.pa = pa; result.bus = bus; result.bytes = DMA_BYTES;
    *out = result;
    return LAN_OK;
}
