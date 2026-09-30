/* ======================================================================== */
/*  DMA_POOL.H — DMA に渡せるメモリの小さな池 (固定番地)                    */
/*                                                                          */
/*  82557 の CB/RFD (≒16KB) と CS4231 の PCM リング (16KB) を置く場所。      */
/*  普通のカーネルヒープ (kmalloc) では駄目な理由が 3 つある:                */
/*    (a) 64KB バンクをまたぐと 8237 が折り返す ([HW2])                      */
/*    (b) 割り込み文脈から取る / 返すので、動的確保を挟めない                */
/*    (c) 番地が全 PD で同じでなければならない (割り込みは**そのときの       */
/*        CR3**、つまりアプリの PD で走る。共有されるのは PDE 0 だけ)        */
/*  だから **0〜4MB の中の固定番地** (include/memmap.h の MEM_DMA_POOL_*)    */
/*  を 4KB × 16 ページのビットマップで配るだけの池にする。                   */
/*                                                                          */
/*  表を触る算数は kernel/dma_pool_math.c (ホスト試験つき)。ここと           */
/*  kernel/dma_pool.c は「唯一の池」と irq_save の殻だけ。                   */
/*                                                                          */
/*  外への口は dma_alloc / dma_free / dma_mark_leaked (struct dma_buf の     */
/*  {pa, va} の組と物理の上限 limit、TASK_T1_LEDGER §3-7)。池は台帳の owner */
/*  の対象外 (固定の DMA 区間の内側を span 表で配るだけ、§4-8)。            */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-3、TASK_T1_LEDGER §3-7 (T1c)  */
/*  記録: tools/tests/dma_pool_tdd.md                                       */
/* ======================================================================== */

#ifndef DMA_POOL_H
#define DMA_POOL_H

#include "types.h"
#include "memmap.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* (エラーは共通体系) */
#include "dma8237.h"            /* dma_range_ok / DMA_PHYS_LIMIT */

#define DMA_POOL_PAGE_SIZE   0x1000UL
#define DMA_POOL_PAGES       ((u32)(MEM_DMA_POOL_SIZE / DMA_POOL_PAGE_SIZE))
#define DMA_POOL_SPANS       16

/* 受付範囲。**32KB より大きい要求は空の池でも必ず失敗する** —
 * 池は 0x2F0000 の 64KB 境界をまたぐので、またがずに取れる最大は
 * 境界の片側ぶん = 32KB。33〜64KB を「断片化のせい」と読まれないよう
 * 入口で断る (票 §1-3)。 */
#define DMA_POOL_MAX_BYTES   0x8000UL    /* 32KB */
#define DMA_POOL_MIN_BYTES   1UL
#define DMA_POOL_DEF_ALIGN   DMA_POOL_PAGE_SIZE

/* span の状態 */
#define DMA_SPAN_FREE        0
#define DMA_SPAN_USED        1
#define DMA_SPAN_LEAKED      2   /* 装置が止まった証拠が無い。**再利用しない** */

/* エラーは 8237 と同じ体系 (OS32_ERR_*)。 */
#define DMA_POOL_ERR_ARG     OS32_ERR_INVAL   /* -9  引数 / 途中ポインタ / 二重解放 */
#define DMA_POOL_ERR_STATE   OS32_ERR_NOSYS   /* -10 dma_pool_init より前 */
#define DMA_POOL_ERR_NOSPC   OS32_ERR_NOSPC   /* -4  置ける隙間が無い (断片化 / span 満杯) */

/* ------------------------------------------------------------------------ */
/*  表そのもの (kernel/dma_pool_math.c が触る唯一の型)                      */
/*                                                                          */
/*  base は池の先頭の**物理番地** (表は物理で持ち、va は P2V で作る)。      */
/*  ホスト試験は合成した base で同じ算数を回せる。                          */
/* ------------------------------------------------------------------------ */
struct dma_pool_span {
    u8 first;     /* 先頭ページ番号 */
    u8 npages;
    u8 state;     /* DMA_SPAN_* */
};

struct dma_pool_state {
    u32 base;
    u32 npages;
    u32 bad_free;
    u32 leaked;
    u8  used[DMA_POOL_PAGES];       /* 1 = USED か LEAKED */
    struct dma_pool_span span[DMA_POOL_SPANS];
    u8  ready;
};

/* ------------------------------------------------------------------------ */
/*  DMA の受け皿 (TASK_T1_LEDGER §3-7)                                      */
/*                                                                          */
/*  pa は装置へ渡す物理番地、va は CPU が触る番地 (va = P2V(pa))。両方を     */
/*  組で返すのは、呼び手に「どちらを装置へ渡すのか」を考えさせないため。    */
/* ------------------------------------------------------------------------ */
struct dma_buf {
    u32   pa;
    void *va;
    u32   size;
};

/* ------------------------------------------------------------------------ */
/*  純粋関数 (kernel/dma_pool_math.c) — I/O も割り込み禁止も無い            */
/* ------------------------------------------------------------------------ */
void dma_pool_state_init(struct dma_pool_state *s, u32 base, u32 npages);

/* 最初適合 + 整列 + 候補ごとの dma_range_ok (64KB 非またぎと limit、
 * drivers/dma8237.h)。
 *   0 = *out に先頭番地 / 負 = 取れない (*out = 0 — **この出力を外へ直接
 *   つながない**。外への口は dma_pool_state_alloc_buf)
 * align は 2 の冪で DMA_POOL_MAX_BYTES 以下。0 は DMA_POOL_DEF_ALIGN。 */
int dma_pool_state_alloc(struct dma_pool_state *s, u32 bytes, u32 align,
                         u32 limit, u32 *out);

/* dma_alloc の中身。局所変数で受けて**成功したときだけ** *out を写す
 * (失敗時 *out は不変)。out が NULL なら DMA_POOL_ERR_ARG。 */
int dma_pool_state_alloc_buf(struct dma_pool_state *s, u32 size, u32 align,
                             u32 limit, struct dma_buf *out);

/* span の**先頭と一致するときだけ**解放。途中ポインタ・二重解放・範囲外は
 * DMA_POOL_ERR_ARG を返して bad_free を数える。 */
int dma_pool_state_free(struct dma_pool_state *s, u32 pa);

/* span を LEAKED にする。以後その span は配らない。 */
int dma_pool_state_mark_leaked(struct dma_pool_state *s, u32 pa);

/* ------------------------------------------------------------------------ */
/*  唯一の池 (kernel/dma_pool.c)。全部 irq_save の短い区間                  */
/*  割り込み文脈から呼べる (動的確保を挟まない)。                            */
/* ------------------------------------------------------------------------ */

/* paging_init と memory_boot_init の後、pci_bind_all の前に 1 回だけ。 */
void dma_pool_init(void);

/* 0 = *out に {pa, va, size} / 負 (OS32_ERR_*) = 取れない (**out は不変**)。
 * limit は物理の上限 (ISA の 8237 は DMA_PHYS_LIMIT)。64KB 境界をまたがない
 * ことは常に条件。**dma_pool_init より前は必ず失敗** (DMA_POOL_ERR_STATE)。 */
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out);

/* 0 / 負。**装置がそのメモリへの DMA を止めた証拠を持ってから**呼ぶ
 * (証拠の定義は装置ごと。票 TASK_HAL_WIRING §1-3 の「解放の契約」)。
 * b->pa が span の先頭でなければ断って bad_free を数える。 */
int dma_free(const struct dma_buf *b);

/* 止まった証拠が取れなかった失敗経路で driver が明示的に呼ぶ。 */
int dma_mark_leaked(const struct dma_buf *b);

/* 診断のカウンタ (kselftest と起動後の報告が読む)。 */
u32 dma_pool_bad_free(void);
u32 dma_pool_leaked(void);
u32 dma_pool_free_pages(void);

#endif /* DMA_POOL_H */
