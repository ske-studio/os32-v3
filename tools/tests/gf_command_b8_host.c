/* Link command_platform.c only; A2 supplies map and DMA ops. */
#include <assert.h>
#include "../../hwdev/gf/command.h"

#define GF2_HOST_B8
#include "gf_command_host.h"

int main(void)
{
    struct gf2_chan ch = {0}; volatile u32 *va = 0;
    ch.map_ops = &map_ops; ch.dma_ops = &dma_ops;
    ch.bar1 = (struct recover_bar_extent){0x90000008U, 0x90000000U, 0x20000U, 1};
    map_fail = 1; assert(gf2_map_vram(&ch, 0x10000U, GF2_PUSH_BYTES, &va) == GF2_EMAP);
    assert(!ch.mapped && !ch.vram_reserved && !va);
    map_fail = 0; assert(gf2_map_vram(&ch, 0x10000U, GF2_PUSH_BYTES, &va) == GF2_OK);
    assert(mapped_extent.base == 0x90010000U && mapped_extent.length == GF2_PUSH_BYTES);
    assert(mapped_extent.raw == ch.bar1.raw && mapped_bytes == GF2_PUSH_BYTES);
    assert(va == storage && ch.mapped && ch.vram_reserved);
    unmap_fail = 1; assert(gf2_unmap_vram(&ch) == GF2_EMAP);
    assert(ch.mapped && ch.vram_reserved && ch.va == va);
    unmap_fail = 0; assert(gf2_unmap_vram(&ch) == GF2_OK);
    assert(!ch.mapped && !ch.vram_reserved);
    assert(gf2_map_vram(&ch, 0x10004U, GF2_PUSH_BYTES, &va) == GF2_EMAP);
    ch.bar1.base = 0xfffff000U;
    assert(gf2_map_vram(&ch, 0x10000U, GF2_PUSH_BYTES, &va) == GF2_EMAP);
    alloc_fail = -7; assert(gf2_dma_get(&ch) == GF2_ENOSPC && ch.dma_rc == -7);
    alloc_fail = 0; assert(gf2_dma_get(&ch) == GF2_OK && ch.staging_live);
    free_fail = -8; gf2_dma_put(&ch);
    assert(leaked == 1 && ch.dma_rc == -8 && !ch.staging_live);
    return 0;
}
