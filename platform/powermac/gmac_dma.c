/* OpenBSD sys/dev/ic/gem.c:146-175: coherent control DMA allocation. */
#include "lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "ppc_memmap.h"
#include "mm/paging.h"

#define DMA_BLOCK 0x10000u
#define DMA_BYTES (3u * DMA_BLOCK)
#define DMA_BUFFER 2048u
#define TX_RING 0u
#define RX_RING 0x1000u

_Static_assert(GEM_RING_COUNT * GEM_DESC_SIZE <= DMA_BLOCK, "TX ring boundary");
_Static_assert(RX_RING + GEM_RING_COUNT * GEM_DESC_SIZE <= DMA_BLOCK, "RX ring boundary");
_Static_assert(DMA_BLOCK % DMA_BUFFER == 0, "buffer boundary");
_Static_assert(GEM_RING_COUNT * DMA_BUFFER == DMA_BLOCK, "buffer region");
_Static_assert(TX_RING % 4096 == 0 && RX_RING % 4096 == 0, "ring alignment");
static u8 pool[DMA_BYTES] __attribute__((aligned(DMA_BLOCK)));
static struct pm_gmac_boot saved;
static struct gem_dma result;
static int attempted, status;

void ppc_dcbf_line(void *line);
void ppc_sync(void);

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
