/* ======================================================================== */
/*  IRQ.H — 割り込みの動的登録 (票 TASK_HAL_WIRING §1-1)                    */
/*                                                                          */
/*  PCI の装置は BIOS が IRQ を割り当て、複数の装置が 1 本を共有し得る。     */
/*  起動時に読んだ IRQ 番号でハンドラを結ぶ口がこれ。                       */
/*                                                                          */
/*  受けるのは**共通スタブに結んだ線だけ** (3/5/6/8/9/10/14/15)。固定スタブ */
/*  の線 (0/1/2/4/7/11/12/13) は IRQ_ERR_NOTSUP で断る。断られた装置は       */
/*  この票では利用不可 (ポーリング稼働はこの層では持たない)。               */
/*                                                                          */
/*  登録する driver の契約 (票 §1-1。この層は検査しない — 静的な driver しか */
/*  居ないうちはレビューと W3 で担保する):                                  */
/*   - ハンドラは**有界**。装置待ちのスピン禁止。予算超過は IRQ_DEFERRED。   */
/*   - IRQ_DEFERRED を返すなら、装置側の割り込みを**マスクしたまま**返し、   */
/*     自分の tick フックで残件を回収して自分でマスクを外す。               */
/*   - **SHARED か排他かを問わず、全登録者が定期回収 (tick フック) を持つ**。 */
/*     共有線のエッジ喪失は有限の走査では防げない (往復 5 B1) し、線の隔離で */
/*     巻き添えになった登録者も tick だけで動き続ける必要がある (往復 7 B2)。 */
/*     IRQ は加速器、tick が保証。                                          */
/*   - ISR の中から登録・解除しない (IRQ_ERR_CTX で断る)。                   */
/*   - 共有者が勝手に irq_disable を呼ばない (マスクは登録数で持つ)。        */
/* ======================================================================== */

#ifndef __IRQ_H
#define __IRQ_H

#include "types.h"
#include "irq_math.h"   /* IRQ_NONE/HANDLED/DEFERRED, IRQ_F_*, IRQ_ERR_*, 表の型 */

/* 登録 / 解除。戻り 0 = 成功、負 = IRQ_ERR_*。
 * 最初の登録で PIC のマスクを開け、最後の解除で閉じる (登録数で持つ)。 */
int irq_register(unsigned int irq, irq_handler_fn fn, void *arg,
                 unsigned int flags);
int irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg);

/* 共通スタブ irq_stub_common_%1 から呼ばれる入口 (IF=0、ネスト無し)。
 * 走査 → ストーム勘定 → irq_finish の順で、**EOI はここの下でだけ**送る。 */
void irq_dispatch(unsigned int irq);

/* 動的経路の唯一の終了経路。IRQ15 のスプリアス検査は handled とは独立。 */
void irq_finish(unsigned int irq, int handled_any);

/* 線をまるごと隔離する (noisy / 巻き添えの打ち切り)。PIC でマスクし、
 * 以後その線への irq_register は IRQ_ERR_BUSY。**登録数の再計算では解けない**。
 * 戻り 0 = 隔離した / IRQ_ERR_INVAL = 動的でない線・範囲外 (0xFF など)。 */
int irq_quarantine_line(unsigned int irq);

/* 観測点 (kernel.map から読む。KAPI にはしない)。 */
extern struct irq_line irq_lines[IRQ_DYN_COUNT];  /* 添字は irq_dyn_index() */
extern u32 irq_unexpected;         /* 誰も受けなかったディスパッチ数 */
extern u32 irq_storm_masked;       /* ストームでマスクした線のビット (sticky) */
extern u32 irq_line_quarantined;   /* 隔離した線のビット (sticky) */
extern u32 irq_ctx_violations;     /* ISR 文脈からの登録・解除を断った回数 */
extern volatile int irq_in_irq;    /* 共通スタブの入れ子深さ (契約の検査用) */

/* 数えたものの読み口 (添字を知らなくてよい)。不明な線は 0。 */
u32 irq_deferred_count(unsigned int irq);
u32 irq_shared_dispatch(unsigned int irq);

/* **試験専用**。kselftest が隔離とストームの試験を終えたあと、残りの起動に
 * 影響を残さないために線を初期状態へ戻す。これ以外から呼んではいけない
 * (隔離が sticky であることの意味が消える)。 */
void irq_test_reset_line(unsigned int irq);

/* **試験専用**。1 の間、ストーム・隔離・誰も受けなかった IRQ の kprintf を
 * 出さない (数え方もマスクも変わらない。黙るのは表示だけ)。kselftest の
 * IRQ 試験は IRQ3 でわざとストームと隔離を起こすので、起動画面に
 * 「[irq] storm on IRQ3」「IRQ3 quarantined」「[isr] unclaimed IRQ3」が
 * 本物の障害と同じ形で出ていた (実機 Ra266、2026-09-24)。さらに
 * isr_unexpected_report は線ごとに 1 回しか出さないので、試験が IRQ3 の
 * 1 回を使い切ると、後で本物の装置 (82557 等) が IRQ3 を誰にも受けられず
 * 上げても何も出なかった。これ以外から立ててはいけない。 */
extern int irq_test_quiet;

#endif /* __IRQ_H */
