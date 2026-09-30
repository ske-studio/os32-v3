/* ======================================================================== */
/*  KMALLOC.C — ヒープアロケータ共通実装 + カーネルヒープ                    */
/*                                                                          */
/*  Best-fit 方式の簡易アロケータ                                            */
/*  連結リストでフリーブロックを管理、隣接ブロック結合付き                    */
/*                                                                          */
/*  実装はヒープ記述子 (KHeap) をパラメータに取る kheap_* 共通関数群で、      */
/*  カーネルヒープ (kmalloc) と exec ヒープ (exec/exec_heap.c) は同じ         */
/*  実装の 2 インスタンス。かつては exec_heap.c がこのファイルのクローンで、  */
/*  バグ修正が二重管理になっていた (R1)。                                    */
/*                                                                          */
/*  ブロックヘッダ構造 (8バイト):                                            */
/*    u32 size    — データ部サイズ (ヘッダ含まず)                            */
/*    u32 magic   — 確保済み=0xA110CA7E, 解放済み=0xFEEEFEEE                */
/*    [data...]   — ユーザデータ (8バイトアライン)                           */
/*                                                                          */
/*  不変条件:                                                               */
/*   1. 隣接する 2 つのフリーブロックは存在しない (free 時に必ず結合する)。   */
/*      これにより free の結合は前後 1 個ずつを見るだけで足りる。            */
/*   2. **アロケータは再入不可**。内部の走査と h->used の更新は割り込みで    */
/*      保護していないので、ISR から kmalloc/kfree を呼んではならない。      */
/*      (保護すると O(n) の走査中ずっと割り込み禁止になり遅延が跳ねる)       */
/* ======================================================================== */

#include "kmalloc.h"
#include "kstring.h"
#include "kprintf.h"

/* ======== ブロックヘッダ ======== */
#define BLK_MAGIC_USED  0xA110CA7EUL  /* "ALLOCATE" */
#define BLK_MAGIC_FREE  0xFEEEFEEEUL
#define BLK_HDR_SIZE    8             /* sizeof(BlkHdr) */

/* 8バイトアライメント。
 * SQLite は double を含む構造体をこのヒープ上に置く (malloc ラッパー経由)。
 * 4 バイト境界だと i386 では動くが未定義動作寄りで、SSE 命令を使う
 * コードが混ざった途端に落ちる。ヘッダも 8 バイトなので、ベースさえ
 * 8 バイト境界なら全データポインタが 8 バイト境界になる。 */
#define BLK_ALIGN       8

typedef struct BlkHdr {
    u32 size;      /* データ部サイズ */
    u32 magic;     /* マジックナンバー */
} BlkHdr;

/* ======================================================================== */
/*  共通実装 (kheap_*)                                                      */
/* ======================================================================== */

void kheap_init(KHeap *h, void *base, u32 size, const char *name)
{
    BlkHdr *first;
    u32 addr = (u32)base;
    u32 aligned = (addr + BLK_ALIGN - 1) & ~(u32)(BLK_ALIGN - 1);

    h->name = name;

    /* ベースを BLK_ALIGN 境界へ切り上げる。ここが揃っていないと
     * 以降のデータポインタが全てずれる (呼び出し元はページ境界を
     * 渡してくるので通常は no-op)。 */
    if (aligned - addr < size) {
        size -= (aligned - addr);
    } else {
        size = 0;
    }
    /* 末尾も切り捨てて全体を BLK_ALIGN の倍数にする */
    size &= ~(u32)(BLK_ALIGN - 1);

    /* ヘッダすら入らないサイズなら空ヒープとして初期化する
     * (first->size = size - BLK_HDR_SIZE のアンダーフロー防止) */
    if (size < BLK_HDR_SIZE + BLK_ALIGN) {
        h->base = (u8 *)0;
        h->size = 0;
        h->used = 0;
        return;
    }

    h->base = (u8 *)aligned;
    h->size = size;
    h->used = 0;

    /* ヒープ全体を1つのフリーブロックにする */
    first = (BlkHdr *)h->base;
    first->size  = size - BLK_HDR_SIZE;
    first->magic = BLK_MAGIC_FREE;
}

void *kheap_alloc(KHeap *h, u32 size)
{
    u8 *p;
    BlkHdr *blk;
    BlkHdr *best_blk = (BlkHdr *)0;
    u8 *best_p = (u8 *)0;
    u32 best_size = 0xFFFFFFFF;

    /* size > h->size の除外はアライン時の整数オーバーフロー
     * (0xFFFFFFFD〜 が 0 に化ける) も同時に防ぐ */
    if (size == 0 || size > h->size) return (void *)0;

    /* 4バイトアライメント */
    size = (size + BLK_ALIGN - 1) & ~(BLK_ALIGN - 1);

    /* Best-fit 探索 */
    p = h->base;
    while (p + BLK_HDR_SIZE <= h->base + h->size) {
        blk = (BlkHdr *)p;

        /* 壊れたブロック検出 (マジック不正、またはサイズがヒープ外を指す) */
        if (blk->magic != BLK_MAGIC_FREE && blk->magic != BLK_MAGIC_USED) {
            return (void *)0;   /* ヒープ破損 */
        }
        if (blk->size > (u32)(h->base + h->size - p) - BLK_HDR_SIZE) {
            return (void *)0;   /* ヒープ破損 */
        }

        if (blk->magic == BLK_MAGIC_FREE && blk->size >= size) {
            /* 要求を満たす最小のブロックを探す */
            if (blk->size < best_size) {
                best_size = blk->size;
                best_blk = blk;
                best_p = p;
                if (best_size == size) {
                    break; /* 完全一致 (無駄ゼロ) なら即探索終了 */
                }
            }
        }

        /* 次のブロックへ */
        p += BLK_HDR_SIZE + blk->size;
    }

    if (best_blk != (BlkHdr *)0) {
        blk = best_blk;
        p = best_p;

        /* 分割可能か？ (残り >= ヘッダ+最小データ8バイト) */
        if (blk->size >= size + BLK_HDR_SIZE + 8) {
            BlkHdr *next;
            u32 remain = blk->size - size - BLK_HDR_SIZE;

            /* 後半を新しいフリーブロックに */
            next = (BlkHdr *)(p + BLK_HDR_SIZE + size);
            next->size  = remain;
            next->magic = BLK_MAGIC_FREE;

            blk->size = size;
        }

        blk->magic = BLK_MAGIC_USED;
        h->used += blk->size + BLK_HDR_SIZE;
        if (h->used > kmalloc_peak_bytes) kmalloc_peak_bytes = h->used;
        return (void *)(p + BLK_HDR_SIZE);
    }

    return (void *)0;   /* メモリ不足 */
}

void kheap_free(KHeap *h, void *ptr)
{
    BlkHdr *blk;
    u8 *p;

    if (ptr == (void *)0) return;

    /* ポインタ検証: ヒープ範囲外・アライメント不正はヘッダを読む前に弾く。
     * 検証せずに ptr-8 を読むと、任意アドレスの free でヒープ管理が壊れる */
    if ((u8 *)ptr < h->base + BLK_HDR_SIZE ||
        (u8 *)ptr >= h->base + h->size ||
        (((u32)ptr) & (BLK_ALIGN - 1)) != 0) {
        kprintf(0xC1, "[%s] invalid free %x\n", h->name, (u32)ptr);
        return;
    }

    blk = (BlkHdr *)((u8 *)ptr - BLK_HDR_SIZE);

    /* マジック検証 (double free / ヘッダ破壊の検出) */
    if (blk->magic != BLK_MAGIC_USED) {
        kprintf(0xC1, "[%s] bad magic %x at %x (double free?)\n",
                h->name, blk->magic, (u32)ptr);
        return;
    }

    blk->magic = BLK_MAGIC_FREE;
    h->used -= blk->size + BLK_HDR_SIZE;

    /* ---- 結合は解放したブロックの前後 1 個だけ見れば足りる ----
     *
     * 不変条件 1 (隣接フリーブロックは存在しない) が保たれているので、
     * このブロックの直前と直後さえ調べれば「結合漏れ」は起きない。
     *
     * かつては free のたびにヒープ全域を走査して全隣接ペアを結合しており、
     * ブロック数 n に対して free 1 回が O(n)、n 個を解放すると O(n^2) に
     * なっていた。 */

    /* 前方結合: 直後がフリーなら取り込む (O(1)) */
    {
        u8 *next_p = (u8 *)blk + BLK_HDR_SIZE + blk->size;
        if (next_p + BLK_HDR_SIZE <= h->base + h->size) {
            BlkHdr *next = (BlkHdr *)next_p;
            if (next->magic == BLK_MAGIC_FREE &&
                next->size <= (u32)(h->base + h->size - next_p) - BLK_HDR_SIZE) {
                blk->size += BLK_HDR_SIZE + next->size;
            }
        }
    }

    /* 後方結合: 直前のブロックを探す。
     * 依然として先頭からの線形探索だが、見つけた時点で打ち切るので
     * 全域走査にはならない。O(1) にするにはブロック末尾にサイズを
     * 書くフッタ (boundary tag) が要る — 必要になったら入れる。 */
    if ((u8 *)blk == h->base) return;

    p = h->base;
    while (p + BLK_HDR_SIZE <= h->base + h->size) {
        BlkHdr *cur = (BlkHdr *)p;
        u8 *next_p;

        if (cur->magic != BLK_MAGIC_FREE && cur->magic != BLK_MAGIC_USED) {
            break;  /* ヒープ破損 */
        }
        if (cur->size > (u32)(h->base + h->size - p) - BLK_HDR_SIZE) {
            break;  /* ヒープ破損 */
        }

        next_p = p + BLK_HDR_SIZE + cur->size;
        if (next_p <= p) break;         /* size 破損時の無限ループ防止 */

        if (next_p == (u8 *)blk) {
            if (cur->magic == BLK_MAGIC_FREE) {
                cur->size += BLK_HDR_SIZE + blk->size;
            }
            return;                     /* 直前が見つかった時点で終了 */
        }
        if (next_p > (u8 *)blk) break;  /* 通り越した = 連鎖が壊れている */

        p = next_p;
    }
}

void kheap_reset(KHeap *h)
{
    if (h->size > 0) {
        BlkHdr *first = (BlkHdr *)h->base;
        first->size  = h->size - BLK_HDR_SIZE;
        first->magic = BLK_MAGIC_FREE;
    }
    h->used = 0;
}

u32 kheap_block_size(KHeap *h, void *ptr)
{
    BlkHdr *blk;
    if (ptr == (void *)0) return 0;
    if ((u8 *)ptr < h->base + BLK_HDR_SIZE ||
        (u8 *)ptr >= h->base + h->size ||
        (((u32)ptr) & (BLK_ALIGN - 1)) != 0) return 0;
    blk = (BlkHdr *)((u8 *)ptr - BLK_HDR_SIZE);
    if (blk->magic != BLK_MAGIC_USED) return 0;
    return blk->size;
}

/* ======================================================================== */
/*  カーネルヒープ インスタンス                                              */
/* ======================================================================== */
static KHeap kernel_heap;

void kmalloc_init(void *heap_start, u32 heap_size)
{
    kheap_init(&kernel_heap, heap_start, heap_size, "kheap");
}

void *kmalloc(u32 size)
{
    return kheap_alloc(&kernel_heap, size);
}

/* ======================================================================== */
/*  kzalloc — ゼロクリア付き確保                                            */
/*  「kmalloc して kmemset」の手書きは未ゼロ化の漏れを生むのでこれを使う。   */
/* ======================================================================== */
void *kzalloc(u32 size)
{
    void *p = kmalloc(size);
    if (p) {
        kmemset(p, 0, size);
    }
    return p;
}

void kfree(void *ptr)
{
    kheap_free(&kernel_heap, ptr);
}

u32 kmalloc_block_size(void *ptr)
{
    return kheap_block_size(&kernel_heap, ptr);
}

/* ======================================================================== */
/*  krealloc — メモリ再確保                                                  */
/*  新しいブロックを確保し、既存データをコピーして旧ブロックを解放する。       */
/*  ptr==NULL の場合は kmalloc(new_size) と同等。                             */
/*  new_size==0 の場合は kfree(ptr) と同等で NULL を返す。                    */
/* ======================================================================== */
void *krealloc(void *ptr, u32 new_size)
{
    void *new_ptr;
    u32 old_size;

    if (ptr == (void *)0) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return (void *)0; }

    old_size = kmalloc_block_size(ptr);
    if (old_size == 0) return (void *)0; /* 無効なポインタ */

    /* 縮小・同サイズは同じブロックを返す (ブロック分割はしない)。
     * libc の realloc と同じく「戻り値のブロックは new_size 以上」を
     * 満たすので契約違反ではない。 */
    if (old_size >= new_size) return ptr;

    new_ptr = kmalloc(new_size);
    if (new_ptr == (void *)0) return (void *)0;

    /* ここに来る時点で old_size < new_size が確定しているので、
     * コピー量は常に old_size (旧実装の min() は死んだ分岐だった) */
    kmemcpy(new_ptr, ptr, old_size);
    kfree(ptr);

    return new_ptr;
}

/* ======================================================================== */
/*  ヒープ情報                                                              */
/* ======================================================================== */
/* 使用中バイト数の最大値 (起動から)。KHEAP_SIZE を切り直す根拠にする — シェルには
 * 出さず、NP21/W の /api/mem か実機のシリアルで kernel.map の番地を読む。 */
u32 kmalloc_peak_bytes = 0;

u32 kmalloc_total(void) { return kernel_heap.size; }
u32 kmalloc_used(void)  { return kernel_heap.used; }

/* 実際に確保可能なバイト数。
 * size - used はフリーブロックのヘッダ分を数えないため、フリーブロックが
 * 増えるほど過大申告になっていた。フリーブロックのデータ部を実際に
 * 合計する (統計表示用なので O(n) 走査でよい)。 */
u32 kmalloc_free(void)
{
    KHeap *h = &kernel_heap;
    u8 *p = h->base;
    u32 total = 0;

    while (p != (u8 *)0 && p + BLK_HDR_SIZE <= h->base + h->size) {
        BlkHdr *cur = (BlkHdr *)p;
        if (cur->magic != BLK_MAGIC_FREE && cur->magic != BLK_MAGIC_USED) break;
        if (cur->size > (u32)(h->base + h->size - p) - BLK_HDR_SIZE) break;
        if (cur->magic == BLK_MAGIC_FREE) total += cur->size;
        p += BLK_HDR_SIZE + cur->size;
    }
    return total;
}

/* ======================================================================== */
/*  libc互換ラッパー                                                         */
/*  カーネル空間でのみリンクされる。SQLite等の外部コードが暗黙に使用する       */
/*  malloc/free/realloc を kmalloc ベースで提供する。                         */
/*  外部プログラムの newlib-nano malloc とは完全に独立。                      */
/* ======================================================================== */
void *malloc(u32 size)               { return kmalloc(size); }
void  free(void *ptr)                { kfree(ptr); }
void *realloc(void *ptr, u32 size)   { return krealloc(ptr, size); }
