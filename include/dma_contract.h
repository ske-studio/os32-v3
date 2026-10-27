/* Shared DMA contract; device registers remain in drivers/. */
#ifndef DMA_CONTRACT_H
#define DMA_CONTRACT_H
#include "types.h"
/*  1 回の転送で積める上限。カウントレジスタは 16bit で、積むのは
 *  「バイト数 - 1」。0 バイトは積めない (65536 の意味になる)。 */
#define DMA_XFER_MIN_BYTES  1UL
#define DMA_XFER_MAX_BYTES  0x10000UL   /* 64KB */

/*  64KB バンク。バンクレジスタが上位 8bit を持つので、転送は 1 つの
 *  64KB の中に収まらなければならない ([HW2])。 */
#define DMA_BANK_SIZE       0x10000UL
#define DMA_BANK_MASK       0x0FFFFUL
#define DMA_BANK_SHIFT      16

/*  拡張バンク (0E05h〜) はこの票で扱わない。バンクレジスタ 8bit で届く
 *  のは 24bit = 16MB まで。 */
#define DMA_PHYS_LIMIT      0x1000000UL  /* 16MB (この番地以上は拒否) */

/* [phys, phys+bytes) が 64KB バンクをまたぐか。1 = またぐ。
 * bytes == 0 は「またがない」ではなく **またぐ扱い (1)** — 0 バイトの転送は
 * そもそも積めないので、ここで拾えば呼び手の検査漏れが止まる。 */
int dma_crosses_64k(u32 phys, u32 bytes);

/* [pa, pa + size) を装置に渡してよいか: 1 = よい / 0 = 駄目。
 * **終端**が limit 以下 (limit は物理の上限で、この番地以上は拒否 — ISA の
 * 8237 は DMA_PHYS_LIMIT) で、64KB 境界をまたがない ([HW2])。size 0 は駄目。
 * dma_alloc (kernel/dma_pool_math.c) の候補の検査と、池の外の固定バッファ
 * (FDC の BSS) の検査が**同じこの関数**を使う (TASK_T1_LEDGER §3-7 / §4-3)。 */
int dma_range_ok(u32 pa, u32 size, u32 limit);


#endif
