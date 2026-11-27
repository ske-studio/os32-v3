#include "sys/hal/gfx/v2/gfx2_int.h"
static struct gfx2_heap h, saved;
static void save(void)
{
    u32 i;
    saved.base = h.base; saved.size = h.size; saved.n = h.n;
    for (i = 0; i < h.n; i++) {
        saved.blk[i].off = h.blk[i].off; saved.blk[i].bytes = h.blk[i].bytes; saved.blk[i].used = h.blk[i].used;
    }
}
static int same(void)
{
    u32 i;
    if (h.base != saved.base || h.size != saved.size || h.n != saved.n) return 0;
    for (i = 0; i < h.n; i++)
        if (h.blk[i].off != saved.blk[i].off || h.blk[i].bytes != saved.blk[i].bytes || h.blk[i].used != saved.blk[i].used) return 0;
    return 1;
}
static int probe(void)
{
    u32 a, b, i;
    if (gfx2_heap_init(&h, 0x1000, 0x10000)) return 1;
    if (gfx2_heap_alloc(&h, 0x1000, 0x1000, &a) || a != 0x1000) return 1;
    if (gfx2_heap_alloc(&h, 0x100, 0x4000, &b) || b != 0x4000 || h.blk[1].off != 0x2000 || h.blk[1].bytes != 0x2000) return 1;
    if (gfx2_heap_free(&h, b) || gfx2_heap_free(&h, a) || h.n != 1 || h.blk[0].bytes != 0x10000) return 2;
    save();
    if (gfx2_heap_free(&h, a) != OS32_ERR_INVAL || !same() || gfx2_heap_free(&h, 42) != OS32_ERR_INVAL || !same()) return 3;
    if (gfx2_heap_alloc(&h, 0, 1, &a) != OS32_ERR_INVAL || gfx2_heap_alloc(&h, 1, 0, &a) != OS32_ERR_INVAL || gfx2_heap_alloc(&h, 1, 3, &a) != OS32_ERR_INVAL || !same()) return 4;
    if (gfx2_heap_init(&h, 0xfffff000u, 0x2000) != OS32_ERR_INVAL || !same()) return 5;
    if (gfx2_heap_init(&h, 17, 0) || gfx2_heap_alloc(&h, 1, 1, &a) != OS32_ERR_NOSPC) return 6;
    if (gfx2_heap_init(&h, 0, 4096)) return 9;
    for (i = 0; i < 2046; i++)
        if (gfx2_heap_alloc(&h, 1, 1, &a) || a != i) return 9;
    if (h.n != 2047 || gfx2_heap_alloc(&h, 1, 1, &a) ||
        a != 2046 || h.n != 2048) return 10;
    save();
    if (gfx2_heap_alloc(&h, 1, 1, &a) != OS32_ERR_NOSPC || !same()) return 7;
    if (gfx2_heap_init(&h, 0xfffffff0u, 15) || gfx2_heap_alloc(&h, 1, 32, &a) != OS32_ERR_NOSPC) return 8;
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
