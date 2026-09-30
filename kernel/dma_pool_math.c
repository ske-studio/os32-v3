/* ======================================================================== */
/*  DMA_POOL_MATH.C — DMA プールの表だけ。I/O も割り込み禁止も無い          */
/*                                                                          */
/*  ホストで回す (tools/tests/test_dma_pool.py)。**エミュレータでは踏め     */
/*  ない**のは 64KB またぎの飛ばしで、NP21/W は 8237 の折り返しを模擬        */
/*  しないので「またいでも読めてしまう」(§4-51 と同じ型)。                  */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-3                             */
/*  記録: tools/tests/dma_pool_tdd.md                                       */
/* ======================================================================== */

#include "dma_pool.h"
#include "dma8237.h"   /* dma_crosses_64k — 境界の規則は 8237 側が正典 */

/* ------------------------------------------------------------------------ */
/*  内部の小物                                                              */
/* ------------------------------------------------------------------------ */
static u32 dma_pool_pages_for(u32 bytes)
{
    return (bytes + DMA_POOL_PAGE_SIZE - 1) / DMA_POOL_PAGE_SIZE;
}

static int dma_pool_align_ok(u32 align)
{
    if (align == 0) return 1;                      /* 既定へ倒す */
    if (align > DMA_POOL_MAX_BYTES) return 0;
    return ((align & (align - 1)) == 0) ? 1 : 0;   /* 2 の冪 */
}

/* 空き span を 1 本取る。無ければ -1。 */
static int dma_pool_span_slot(struct dma_pool_state *s)
{
    int i;

    for (i = 0; i < DMA_POOL_SPANS; i++) {
        if (s->span[i].state == DMA_SPAN_FREE) return i;
    }
    return -1;
}

/* 先頭ページ番号で span を引く。無ければ -1。 */
static int dma_pool_span_at(struct dma_pool_state *s, u32 page)
{
    int i;

    for (i = 0; i < DMA_POOL_SPANS; i++) {
        if (s->span[i].state == DMA_SPAN_FREE) continue;
        if ((u32)s->span[i].first == page) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------------ */
void dma_pool_state_init(struct dma_pool_state *s, u32 base, u32 npages)
{
    u32 i;

    if (!s) return;
    s->base = base;
    s->npages = (npages > DMA_POOL_PAGES) ? DMA_POOL_PAGES : npages;
    s->bad_free = 0;
    s->leaked = 0;
    for (i = 0; i < DMA_POOL_PAGES; i++) s->used[i] = 0;
    for (i = 0; i < DMA_POOL_SPANS; i++) {
        s->span[i].first = 0;
        s->span[i].npages = 0;
        s->span[i].state = DMA_SPAN_FREE;
    }
    s->ready = 1;
}

/* ------------------------------------------------------------------------ */
/*  最初適合。**候補ごとに 64KB またぎを見る** — 池は 0x2F0000 をまたぐ     */
/*  ので、空いていても置けない場所がある。またぐ候補は飛ばすだけで、        */
/*  そこで諦めない (後半にまだ置ける)。                                     */
/* ------------------------------------------------------------------------ */
int dma_pool_state_alloc(struct dma_pool_state *s, u32 bytes, u32 align,
                         u32 *out)
{
    u32 need, page, i, addr;
    int slot;

    if (out) *out = 0;
    if (!s || !s->ready) return DMA_POOL_ERR_STATE;
    if (bytes < DMA_POOL_MIN_BYTES || bytes > DMA_POOL_MAX_BYTES)
        return DMA_POOL_ERR_ARG;
    if (!dma_pool_align_ok(align)) return DMA_POOL_ERR_ARG;
    if (align == 0) align = DMA_POOL_DEF_ALIGN;

    need = dma_pool_pages_for(bytes);
    if (need > s->npages) return DMA_POOL_ERR_ARG;

    for (page = 0; page + need <= s->npages; page++) {
        addr = s->base + page * DMA_POOL_PAGE_SIZE;
        if (addr % align != 0) continue;
        /* **またぐ候補は飛ばす** (諦めない)。 */
        if (dma_crosses_64k(addr, bytes)) continue;
        for (i = 0; i < need; i++) {
            if (s->used[page + i]) break;
        }
        if (i != need) continue;

        slot = dma_pool_span_slot(s);
        if (slot < 0) return DMA_POOL_ERR_NOSPC; /* span 表が満杯 */

        for (i = 0; i < need; i++) s->used[page + i] = 1;
        s->span[slot].first = (u8)page;
        s->span[slot].npages = (u8)need;
        s->span[slot].state = DMA_SPAN_USED;
        if (out) *out = addr;
        return 0;
    }
    /* 置ける隙間が無い。**引数が悪いのとは別**に読めるようにする
     * (33KB の要求は ERR_ARG、断片化は ERR_NOSPC)。 */
    return DMA_POOL_ERR_NOSPC;
}

/* ------------------------------------------------------------------------ */
/*  解放 — **span の先頭と一致するときだけ**                                */
/*                                                                          */
/*  隣り合う span を「番地が池の中」だけで解放すると、16KB の次に置いた      */
/*  8KB の先頭を渡されたときにどちらを外すかが決まらない。先頭一致に        */
/*  限ることで、途中ポインタも二重解放も同じ 1 本の規則で弾ける。           */
/* ------------------------------------------------------------------------ */
int dma_pool_state_free(struct dma_pool_state *s, u32 virt)
{
    u32 off, page, i;
    int slot;

    if (!s || !s->ready) return DMA_POOL_ERR_STATE;
    if (virt < s->base) { s->bad_free++; return DMA_POOL_ERR_ARG; }
    off = virt - s->base;
    if (off >= s->npages * DMA_POOL_PAGE_SIZE) {
        s->bad_free++;
        return DMA_POOL_ERR_ARG;
    }
    if (off % DMA_POOL_PAGE_SIZE != 0) { s->bad_free++; return DMA_POOL_ERR_ARG; }

    page = off / DMA_POOL_PAGE_SIZE;
    slot = dma_pool_span_at(s, page);
    if (slot < 0) { s->bad_free++; return DMA_POOL_ERR_ARG; }
    /* LEAKED は**戻さない**。装置がまだ書いているかもしれない。 */
    if (s->span[slot].state != DMA_SPAN_USED) {
        s->bad_free++;
        return DMA_POOL_ERR_ARG;
    }

    for (i = 0; i < (u32)s->span[slot].npages; i++) s->used[page + i] = 0;
    s->span[slot].first = 0;
    s->span[slot].npages = 0;
    s->span[slot].state = DMA_SPAN_FREE;
    return 0;
}

/* ------------------------------------------------------------------------ */
int dma_pool_state_mark_leaked(struct dma_pool_state *s, u32 virt)
{
    u32 off, page;
    int slot;

    if (!s || !s->ready) return DMA_POOL_ERR_STATE;
    if (virt < s->base) { s->bad_free++; return DMA_POOL_ERR_ARG; }
    off = virt - s->base;
    if (off >= s->npages * DMA_POOL_PAGE_SIZE) {
        s->bad_free++;
        return DMA_POOL_ERR_ARG;
    }
    if (off % DMA_POOL_PAGE_SIZE != 0) { s->bad_free++; return DMA_POOL_ERR_ARG; }

    page = off / DMA_POOL_PAGE_SIZE;
    slot = dma_pool_span_at(s, page);
    if (slot < 0 || s->span[slot].state != DMA_SPAN_USED) {
        s->bad_free++;
        return DMA_POOL_ERR_ARG;
    }
    s->span[slot].state = DMA_SPAN_LEAKED;
    s->leaked++;
    return 0;
}
