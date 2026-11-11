#include "gfx2_int.h"

int gfx2_heap_init(struct gfx2_heap *h, u32 base, u32 size)
{
    if (size > (u32)~0u - base)
        return OS32_ERR_INVAL;
    h->base = base;
    h->size = size;
    h->n = size ? 1 : 0;
    if (size) {
        h->blk[0].off = base;
        h->blk[0].bytes = size;
        h->blk[0].used = 0;
    }
    return 0;
}

int gfx2_heap_alloc(struct gfx2_heap *h, u32 bytes, u32 align, u32 *off)
{
    u32 i, j, start, end, prefix, suffix, extra;
    if (!bytes || !align || (align & (align - 1)))
        return OS32_ERR_INVAL;
    for (i = 0; i < h->n; i++) {
        if (h->blk[i].used || h->blk[i].off > (u32)~0u - (align - 1))
            continue;
        start = (h->blk[i].off + align - 1) & ~(align - 1);
        end = h->blk[i].off + h->blk[i].bytes;
        if (start > end || bytes > end - start)
            continue;
        prefix = start - h->blk[i].off;
        suffix = end - start - bytes;
        extra = (prefix != 0) + (suffix != 0);
        if (extra > GFX2_HEAP_BLOCKS - h->n)
            return OS32_ERR_NOSPC;
        for (j = h->n; j > i + 1; j--)
            h->blk[j + extra - 1] = h->blk[j - 1];
        h->n += extra;
        if (prefix) {
            h->blk[i].bytes = prefix;
            i++;
        }
        h->blk[i].off = start;
        h->blk[i].bytes = bytes;
        h->blk[i].used = 1;
        if (suffix) {
            h->blk[i + 1].off = start + bytes;
            h->blk[i + 1].bytes = suffix;
            h->blk[i + 1].used = 0;
        }
        *off = start;
        return 0;
    }
    return OS32_ERR_NOSPC;
}

int gfx2_heap_free(struct gfx2_heap *h, u32 off)
{
    u32 i, j;
    for (i = 0; i < h->n; i++)
        if (h->blk[i].off == off && h->blk[i].used)
            break;
    if (i == h->n)
        return OS32_ERR_INVAL;
    h->blk[i].used = 0;
    if (i && !h->blk[i - 1].used) {
        h->blk[i - 1].bytes += h->blk[i].bytes;
        for (j = i; j + 1 < h->n; j++)
            h->blk[j] = h->blk[j + 1];
        h->n--;
        i--;
    }
    if (i + 1 < h->n && !h->blk[i + 1].used) {
        h->blk[i].bytes += h->blk[i + 1].bytes;
        for (j = i + 1; j + 1 < h->n; j++)
            h->blk[j] = h->blk[j + 1];
        h->n--;
    }
    return 0;
}
