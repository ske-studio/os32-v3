/* ======================================================================== */
/*  EXEC_HEAP.C — プログラム専用 動的メモリアロケータ                       */
/*                                                                          */
/*  カーネルヒープ(kmalloc)とは完全に独立した領域。                          */
/*  バグプログラムがバッファオーバーランしてもカーネルヒープを破壊しない。    */
/*                                                                          */
/*  resident は単一 KHeap、USER は AS extent から検証付き一時 view を作る。  */
/* ======================================================================== */

#include "exec_heap.h"
#include "kmalloc.h"
#include "appmem.h"
#include "paging.h"
#include "pgalloc.h"
#include "io.h"

static KHeap exec_heap;

static int user_arena(const struct appmem_extent *e)
{
    return e->kind == APPMEM_EXEC_INITIAL || e->kind == APPMEM_EXEC_ARENA;
}

/* Validate a candidate arena before letting kheap mutate any byte. IRQs stay enabled
 * during the linear walk; only the individual owner/PTE reads are protected.
 * No USER execution or AS switch can occur inside this non-reentrant entry. */
static int user_view(struct addrspace *as, u32 size, u32 ptr, KHeap *out)
{
    out->base = 0;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &as->appmem.e[i];
        u32 used = 0, match = 0;
        if (!user_arena(e)) continue;
        if (ptr && (ptr < e->base || ptr >= e->end)) continue;
        if (e->base < MEM_EXEC_HEAP_BASE || e->end <= e->base ||
            e->end > as->appmem_layout.guard_b ||
            ((e->base | e->end) & (PAGE_SIZE - 1))) goto bad;
        for (u32 va = e->base; va < e->end; va += PAGE_SIZE) {
            u32 pa;
            unsigned int flags = irq_save();
            int ok = as_access_page(as, va, 1, &pa);
            irq_restore(flags);
            if (!ok) goto bad;
        }
        u32 va = e->base;
        while (va < e->end) {
            if ((va & (BLK_ALIGN - 1)) || e->end - va < BLK_HDR_SIZE)
                goto bad;
            const BlkHdr *b = (const BlkHdr *)va;
            if (b->size < BLK_ALIGN || (b->size & (BLK_ALIGN - 1)) ||
                b->size > ~(u32)0 - va - BLK_HDR_SIZE ||
                b->size > e->end - va - BLK_HDR_SIZE ||
                (b->magic != BLK_MAGIC_USED && b->magic != BLK_MAGIC_FREE))
                goto bad;
            u32 next = va + BLK_HDR_SIZE + b->size;
            if (next <= va) goto bad;
            if (b->magic == BLK_MAGIC_USED) {
                used += next - va;
                if (ptr == va + BLK_HDR_SIZE) match = 1;
            } else if (size && b->size >= size) match = 1;
            va = next;
        }
        if (va != e->end) goto bad;
        if (match) {
            *out = (KHeap){(u8 *)e->base, e->end - e->base, used, "exec_heap"};
            return 1;
        }
    }
    return 1;
bad:
    as->exec_heap_used = ~(u32)0;
    return 0;
}

void exec_heap_user_init(struct addrspace *as)
{
    KHeap view;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &as->appmem.e[i];
        if (e->kind == APPMEM_EXEC_INITIAL)
            kheap_init(&view, (void *)e->base, e->end - e->base, "exec_heap");
    }
    as->exec_heap_used = 0;
}

void *exec_heap_user_alloc(struct addrspace *as, u32 size)
{
    KHeap view;
    u32 base, bytes, before, request = size;
    if (kctx_irq_depth || kctx_exc_depth || !as || as->appmem_poisoned ||
        as->exec_heap_used == ~(u32)0 || !size ||
        size > ~(u32)0 - (BLK_ALIGN - 1) - BLK_HDR_SIZE)
        return 0;
    size = (size + BLK_ALIGN - 1) & ~(BLK_ALIGN - 1);
    if (request >= MEM_EXEC_HEAP_MIN) {
        if (!appmem_map(as, &as->appmem, &as->appmem_layout, request, 0,
                        APPMEM_MAP_TOPDOWN, APPMEM_EXEC_LARGE, 0, &base)) {
            as->exec_heap_used += PAGE_ALIGN_UP(request);
            return (void *)base;
        }
        if (as->appmem_poisoned) return 0;
    }
    if (!user_view(as, size, 0, &view)) return 0;
    if (!view.base) {
        if (request >= MEM_EXEC_HEAP_MIN) return 0; /* Existing arenas only on failure. */
        bytes = PAGE_ALIGN_UP(size + BLK_HDR_SIZE);
        if (bytes < MEM_EXEC_HEAP_MIN) bytes = MEM_EXEC_HEAP_MIN;
        int rc = appmem_map(as, &as->appmem, &as->appmem_layout, bytes,
                            as->appmem_layout.exec_heap_cur_end,
                            APPMEM_MAP_EXACT, APPMEM_EXEC_ARENA, 0, &base);
        if (rc) {
            if (appmem_map(as, &as->appmem, &as->appmem_layout, bytes, 0,
                           APPMEM_MAP_TOPDOWN, APPMEM_EXEC_ARENA, 0, &base))
                return 0;
        } else as->appmem_layout.exec_heap_cur_end = base + bytes;
        kheap_init(&view, (void *)base, bytes, "exec_heap");
        /* Publish may have merged adjacent ARENAs; derive the new view. */
        if (!user_view(as, size, 0, &view) || !view.base) return 0;
    }
    before = view.used;
    void *result = kheap_alloc(&view, size);
    as->exec_heap_used += view.used - before;
    return result;
}

void exec_heap_user_free(struct addrspace *as, void *ptr)
{
    KHeap view;
    u32 va = (u32)ptr, member = 0;
    if (kctx_irq_depth || kctx_exc_depth || !as || as->appmem_poisoned ||
        as->exec_heap_used == ~(u32)0 || !ptr) return;
    /* Never even read a candidate header outside an owned EXEC arena. */
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) {
        const struct appmem_extent *e = &as->appmem.e[i];
        if (e->kind == APPMEM_EXEC_LARGE && va == e->base) {
            u32 bytes = e->end - e->base;
            if (!appmem_exec_unmap(as, va, bytes)) as->exec_heap_used -= bytes;
            return;
        }
        if (user_arena(e) && va >= e->base && va < e->end) member = 1;
        if ((e->kind == APPMEM_ANON || e->kind == APPMEM_EXEC_LARGE) &&
            va >= e->base && va < e->end) {
            as->exec_heap_used = ~(u32)0;
            return; /* A forged header in this AS's mem_map is not an allocation. */
        }
    }
    if (!member) return;
    if (!user_view(as, 0, va, &view)) return;
    if (!view.base) { as->exec_heap_used = ~(u32)0; return; }
    u32 before = view.used;
    kheap_free(&view, ptr);
    as->exec_heap_used -= before - view.used;
}

u32 exec_heap_user_trim(struct addrspace *as)
{
    KHeap view;
    u32 pages = 0;
    if (kctx_irq_depth || kctx_exc_depth || !as || as->appmem_poisoned ||
        as->exec_heap_used == ~(u32)0 || paging_current_cr3() != as->pd_phys)
        return 0;
    /* No match requested: validate ALL arenas, including INITIAL, first. */
    if (!user_view(as, 0, 0, &view)) return 0;
    /* Descend so whole returns may compact the table without skipping entries. */
    for (u32 i = APPMEM_EXTENT_MAX; i-- > 0;) {
        const struct appmem_extent e = as->appmem.e[i];
        if (e.kind != APPMEM_EXEC_ARENA) continue;
        u32 va = e.base, next;
        BlkHdr *tail;
        do {
            tail = (BlkHdr *)va;
            next = va + BLK_HDR_SIZE + tail->size;
            if (next == e.end) break;
            va = next;
        } while (1);
        if (tail->magic != BLK_MAGIC_FREE) continue;
        /* Keep the header and the minimum payload, even across a page edge. */
        u32 end = va == e.base ? e.base : PAGE_ALIGN_UP(va + BLK_HDR_SIZE + BLK_ALIGN);
        if (end == e.end) continue;
        if (appmem_exec_trim(as, end, e.end - end)) break;
        if (end != e.base) tail->size = end - va - BLK_HDR_SIZE;
        if (as->appmem_layout.exec_heap_cur_end == e.end)
            as->appmem_layout.exec_heap_cur_end = end;
        pages += (e.end - end) / PAGE_SIZE;
    }
    return pages;
}

/* ======================================================================== */
/*  初期化                                                                  */
/* ======================================================================== */
void exec_heap_init_at(u32 base, u32 size)
{
    kheap_init(&exec_heap, (void *)base, size, "exec_heap");
}

/* ======================================================================== */
/*  確保 / 解放                                                             */
/* ======================================================================== */
void *exec_heap_alloc(u32 size)
{
    if (kctx_irq_depth || kctx_exc_depth) return 0;
    return kheap_alloc(&exec_heap, size);
}

void exec_heap_free(void *ptr)
{
    if (kctx_irq_depth || kctx_exc_depth) return;
    kheap_free(&exec_heap, ptr);
}

/* ======================================================================== */
/*  exec_heap_reset — 全域破棄 (exec_run終了時)                             */
/*  全体を1つのフリーブロックに戻す。                                       */
/* ======================================================================== */
void exec_heap_reset(void)
{
    kheap_reset(&exec_heap);
}

/* ======================================================================== */
/*  exec_heap_save_state — 管理変数のスナップショット保存                    */
/*  ヒープのメタデータ (BlkHdr) はメモリ上にそのまま残す。                   */
/* ======================================================================== */
void exec_heap_save_state(u32 *out_used)
{
    if (out_used) {
        *out_used = exec_heap.used;
    }
}

/* ======================================================================== */
/*  exec_heap_restore_state — 管理変数のスナップショット復元                 */
/*                                                                          */
/*  exec_heap_init_at() と異なり、ヒープメモリの内容(BlkHdr)を破壊しない。   */
/*  管理変数 (base, size, used) だけを親プロセスの値に戻す。                 */
/*  凍結モデル前提: 親のヒープ領域は子プロセスが書き換えない。               */
/* ======================================================================== */
void exec_heap_restore_state(u32 base, u32 size, u32 used)
{
    /* WM allocations made while a USER child ran are already accounted for. */
    if (exec_heap.base == (u8 *)base && exec_heap.size == size) return;
    exec_heap.base = (u8 *)base;
    exec_heap.size = size;
    exec_heap.used = used;
    exec_heap.name = "exec_heap";
}

/* ======================================================================== */
/*  統計情報                                                                */
/* ======================================================================== */
u32 exec_heap_total(void) { return exec_heap.size; }
u32 exec_heap_used(void)  { return exec_heap.used; }
