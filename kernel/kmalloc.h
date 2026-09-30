/* ======================================================================== */
/*  KMALLOC.H — カーネルメモリアロケータ                                    */
/*                                                                          */
/*  シンプルなファーストフィット方式の動的メモリ管理                          */
/*  ヒープ領域はBSS末尾からスタック手前まで                                  */
/* ======================================================================== */

#ifndef __KMALLOC_H
#define __KMALLOC_H

#include "types.h"

/* ======== ヒープ記述子 (共通アロケータ実装) ======== */
/* kmalloc (カーネルヒープ) と exec_heap (プログラムヒープ) は
 * この記述子を取る kheap_* 共通実装の 2 インスタンス。 */
typedef struct {
    u8         *base;   /* ヒープ先頭 (0 = 未初期化/空) */
    u32         size;   /* ヒープ総サイズ */
    u32         used;   /* 使用中バイト数 (ヘッダ込み) */
    const char *name;   /* 診断表示用の名前 */
} KHeap;

void  kheap_init(KHeap *h, void *base, u32 size, const char *name);
void *kheap_alloc(KHeap *h, u32 size);
void  kheap_free(KHeap *h, void *ptr);
void  kheap_reset(KHeap *h);
u32   kheap_block_size(KHeap *h, void *ptr);

/* ======== API ======== */

/* ヒープ初期化 (起動時に1回だけ呼ぶ) */
/* heap_start: ヒープ開始アドレス, heap_size: バイト数 */
void kmalloc_init(void *heap_start, u32 heap_size);

/* メモリ確保 (アライメント: 4バイト) */
void *kmalloc(u32 size);

/* ゼロクリア付き確保 (kmalloc + kmemset の手書きはこれに置き換える) */
void *kzalloc(u32 size);

/* メモリ解放 */
void kfree(void *ptr);

/* メモリ再確保 (kmalloc + memcpy + kfree) */
void *krealloc(void *ptr, u32 new_size);

/* ヒープ情報 */
u32 kmalloc_total(void);  /* ヒープ総サイズ */
u32 kmalloc_used(void);   /* 使用中サイズ */
extern u32 kmalloc_peak_bytes;   /* 使用中サイズの最大値 (起動から)。KHEAP_SIZE の根拠 */
u32 kmalloc_free(void);   /* 空き容量 */

/* ブロックのデータ部サイズを取得 (テスト用) */
u32 kmalloc_block_size(void *ptr);

/* ======== libc互換ラッパー ======== */
/* カーネル空間でのみリンクされる。外部プログラムの newlib malloc には影響しない。 */
void *malloc(u32 size);
void  free(void *ptr);
void *realloc(void *ptr, u32 new_size);

#endif /* __KMALLOC_H */
