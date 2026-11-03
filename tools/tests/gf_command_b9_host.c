/* B9 links the resident adapter with these DMA pool stubs; A2 is not needed. */
#include <assert.h>
#include "../../hwdev/gf/command.h"
#include "../../mm/dma_pool.h"

static int alloc_rc, free_rc, leak_calls;
static u32 requested_bytes, requested_align, requested_mask;
static struct dma_buf seen;
static int storage;

int dma_alloc(u32 bytes, u32 align, u32 mask, struct dma_buf *out)
{
    requested_bytes = bytes; requested_align = align; requested_mask = mask;
    if (alloc_rc) return alloc_rc;
    *out = (struct dma_buf){ .pa = 0x12345000U, .va = &storage, .size = bytes };
    return 0;
}
int dma_free(const struct dma_buf *buf) { seen = *buf; return free_rc; }
int dma_mark_leaked(const struct dma_buf *buf) { seen = *buf; ++leak_calls; return 0; }

int main(void)
{
    const struct gf2_dma_ops *ops = geforce_dma_ops();
    struct gf2_dma_buf buf = { .pa = 7, .size = 9, .va = &storage };
    assert(ops == geforce_dma_ops());
    alloc_rc = -4;
    assert(ops->alloc(GF2_STAGING_BYTES, GF2_STAGING_ALIGN, GF2_STAGING_MASK, &buf) == -4);
    assert(buf.pa == 7 && buf.size == 9 && buf.va == &storage);
    alloc_rc = 0;
    assert(ops->alloc(GF2_STAGING_BYTES, GF2_STAGING_ALIGN, GF2_STAGING_MASK, &buf) == 0);
    assert(requested_bytes == GF2_STAGING_BYTES && requested_align == GF2_STAGING_ALIGN &&
           requested_mask == GF2_STAGING_MASK);
    assert(buf.pa == 0x12345000U && buf.size == GF2_STAGING_BYTES && buf.va == &storage);
    free_rc = -9;
    assert(ops->free(&buf) == -9);
    assert(seen.pa == buf.pa && seen.va == buf.va && seen.size == buf.size);
    ops->leak(&buf);
    assert(leak_calls == 1 && seen.pa == buf.pa && seen.va == buf.va && seen.size == buf.size);
    free_rc = 0;
    assert(ops->free(&buf) == 0);
    return 0;
}
