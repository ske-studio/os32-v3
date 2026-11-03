/* Layer 2 partial BAR1 mapping and resident DMA adapter boundary.
 * Push placement: GF_FEAT2_DESIGN.md sections 3 and 4. */
#include "command.h"

int gf2_map_vram(struct gf2_chan *ch, u32 off, u32 len, volatile u32 **va)
{
    struct recover_bar_extent extent;
    volatile u8 *ptr;

    if (!ch || !va || !ch->map_ops || !ch->map_ops->map ||
        ch->mapped || ch->vram_reserved || !ch->bar1.valid || !len ||
        (off & 3U) || (len & 3U) || off > ch->bar1.length ||
        len > ch->bar1.length - off || off > U32_MAX - ch->bar1.base ||
        len - 1U > U32_MAX - (ch->bar1.base + off))
        return GF2_EMAP;

    extent = ch->bar1;
    extent.base += off;
    extent.length = len;
    if (ch->map_ops->map(ch->map_owner, &extent, len, &ptr))
        return GF2_EMAP;
    ch->push_extent = extent;
    ch->map_bytes = len;
    ch->va = (volatile u32 *)ptr;
    ch->mapped = true;
    ch->vram_reserved = true;
    *va = ch->va;
    return GF2_OK;
}

int gf2_unmap_vram(struct gf2_chan *ch)
{
    if (!ch || !ch->mapped || !ch->map_ops || !ch->map_ops->unmap ||
        ch->map_ops->unmap(ch->map_owner, (volatile u8 *)ch->va,
                           ch->map_bytes))
        return GF2_EMAP;
    ch->mapped = false;
    ch->vram_reserved = false;
    ch->va = 0;
    ch->map_bytes = 0;
    ch->push_extent = (struct recover_bar_extent){0};
    return GF2_OK;
}

int gf2_dma_get(struct gf2_chan *ch)
{
    if (!ch || !ch->dma_ops || !ch->dma_ops->alloc || ch->staging_live)
        return GF2_ESTATE;
    ch->dma_rc = ch->dma_ops->alloc(GF2_STAGING_BYTES, GF2_STAGING_ALIGN,
                                    GF2_STAGING_MASK, &ch->staging);
    if (ch->dma_rc) return GF2_ENOSPC;
    ch->staging_live = true;
    return GF2_OK;
}

void gf2_dma_put(struct gf2_chan *ch)
{
    if (!ch || !ch->staging_live) return;
    ch->dma_rc = ch->dma_ops->free(&ch->staging);
    if (ch->dma_rc) ch->dma_ops->leak(&ch->staging);
    ch->staging_live = false;
}
