/* Link geforce_dma.c only; A2 supplies DMA pool dependencies. */
#include <assert.h>
#include "../../hwdev/gf/command.h"

#define GF2_HOST_B9
#include "gf_command_host.h"

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
