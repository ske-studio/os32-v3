#include "../../../hwdev/gf/command.h"
#include "../../../mm/dma_pool.h"

/* Translate fields explicitly: the two buffer types have different layouts. */
static int geforce_dma_alloc(u32 bytes, u32 align, u32 mask,
                             struct gf2_dma_buf *out)
{
    struct dma_buf buf;
    int rc = dma_alloc(bytes, align, mask, &buf);
    if (rc == 0) {
        out->pa = buf.pa;
        out->size = buf.size;
        out->va = buf.va;
    }
    return rc;
}

static int geforce_dma_free(struct gf2_dma_buf *buf)
{
    const struct dma_buf pool_buf = { .pa = buf->pa, .va = buf->va,
                                      .size = buf->size };
    return dma_free(&pool_buf);
}

static void geforce_dma_leak(struct gf2_dma_buf *buf)
{
    const struct dma_buf pool_buf = { .pa = buf->pa, .va = buf->va,
                                      .size = buf->size };
    (void)dma_mark_leaked(&pool_buf);
}

const struct gf2_dma_ops *geforce_dma_ops(void)
{
    static const struct gf2_dma_ops ops = {
        .alloc = geforce_dma_alloc,
        .free = geforce_dma_free,
        .leak = geforce_dma_leak,
    };
    return &ops;
}
