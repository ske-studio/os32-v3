/* ======================================================================== */
/*  DMA_POOL.C — 唯一の DMA プール。表の算数は kernel/dma_pool_math.c       */
/*                                                                          */
/*  ここにあるのは「1 つしかない池」と irq_save の殻だけ。探索も表の更新も   */
/*  動的確保を挟まないので、**割り込み文脈から呼べる** (82557 の巻き戻しは   */
/*  IRQ callback の中から dma_free / dma_mark_leaked を呼ぶ)。              */
/*                                                                          */
/*  写像は kernel/paging.c が張る (present / RW / supervisor)。ここは        */
/*  番地を配るだけで CR3 にも PTE にも触らない。                            */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_HAL_WIRING.md §1-3、TASK_T1_LEDGER §3-7          */
/* ======================================================================== */

#include "dma_pool.h"
#include "io.h"

/* 池は 1 つ。**静的に持つ** — kmalloc から取ると、割り込み文脈で表を
 * 触れなくなるうえ番地が PD ごとに動く理屈が増える。 */
static struct dma_pool_state s_pool;

void dma_pool_init(void)
{
    unsigned int flags;

    flags = irq_save();
    dma_pool_state_init(&s_pool, (u32)MEM_DMA_POOL_BASE, DMA_POOL_PAGES);
    irq_restore(flags);
}

/* 失敗時 *out は不変 (中身は dma_pool_state_alloc_buf、TASK_T1_LEDGER §3-7)。 */
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    unsigned int flags;
    int rc;

    flags = irq_save();
    rc = dma_pool_state_alloc_buf(&s_pool, size, align, limit, out);
    irq_restore(flags);
    return rc;
}

int dma_free(const struct dma_buf *b)
{
    unsigned int flags;
    int rc;

    if (!b) return DMA_POOL_ERR_ARG;
    flags = irq_save();
    rc = dma_pool_state_free(&s_pool, b->pa);
    irq_restore(flags);
    return rc;
}

int dma_mark_leaked(const struct dma_buf *b)
{
    unsigned int flags;
    int rc;

    if (!b) return DMA_POOL_ERR_ARG;
    flags = irq_save();
    rc = dma_pool_state_mark_leaked(&s_pool, b->pa);
    irq_restore(flags);
    return rc;
}

u32 dma_pool_bad_free(void)
{
    return s_pool.bad_free;
}

u32 dma_pool_leaked(void)
{
    return s_pool.leaked;
}

u32 dma_pool_free_pages(void)
{
    u32 i, n = 0;

    for (i = 0; i < s_pool.npages; i++) {
        if (!s_pool.used[i]) n++;
    }
    return n;
}
