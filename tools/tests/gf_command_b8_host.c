/* B8 fixture: map and DMA adapters have the A2-compatible ops signatures. */
#include <assert.h>
#include "../../hwdev/gf/command.h"

static struct recover_bar_extent mapped_extent;
static u32 mapped_bytes, storage[GF2_PUSH_WORDS];
static int map_fail, unmap_fail, alloc_fail, free_fail, leaked;
static int map_stub(void *owner, const struct recover_bar_extent *bar,
                    u32 bytes, volatile u8 **out)
{
    (void)owner; if (map_fail) return map_fail;
    mapped_extent = *bar; mapped_bytes = bytes;
    *out = (volatile u8 *)storage; return 0;
}
static int unmap_stub(void *owner, volatile u8 *ptr, u32 bytes)
{
    (void)owner; assert(ptr == (volatile u8 *)storage);
    assert(bytes == mapped_bytes); return unmap_fail;
}
static int alloc_stub(u32 bytes, u32 align, u32 mask, struct gf2_dma_buf *b)
{
    assert(bytes == GF2_STAGING_BYTES && align == GF2_STAGING_ALIGN);
    assert(mask == GF2_STAGING_MASK);
    if (alloc_fail) return alloc_fail;
    *b = (struct gf2_dma_buf){0x1000U, bytes, storage}; return 0;
}
static int free_stub(struct gf2_dma_buf *b) { (void)b; return free_fail; }
static void leak_stub(struct gf2_dma_buf *b) { (void)b; leaked++; }
static const struct gf_instmem_map_ops map_ops = {map_stub, unmap_stub};
static const struct gf2_dma_ops dma_ops = {alloc_stub, free_stub, leak_stub};

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
