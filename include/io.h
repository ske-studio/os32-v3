/* ======================================================================== */
/*  IO.H — ベアメタル I/O の **契約**                                        */
/*                                                                          */
/*  ポート I/O・割り込み制御・CPU 停止・特権命令について、カーネルが          */
/*  「何を保証されるか」だけをここに書く。**実装は一切ここに無い。**          */
/*                                                                          */
/*  実装は 2 本の軸に分かれ、include パスで選ぶ (build/config.mk):           */
/*                                                                          */
/*    arch/$(ARCH)/arch_io.h          CPU に属するもの                      */
/*                                    割り込みの許可/禁止・保存/復元・        */
/*                                    CPU 停止・IDT のロード                 */
/*                                    既定 ARCH = x86                        */
/*                                                                          */
/*    platform/$(PLATFORM)/platform_io.h                                    */
/*                                    機種に属するもの                       */
/*                                    ポート I/O と I/O ウェイト             */
/*                                    既定 PLATFORM = pc98                   */
/*                                                                          */
/*  取り込む名前は **固定** (`arch_io.h` / `platform_io.h`) で、どの実装が    */
/*  来るかは `-Iarch/$(ARCH)` / `-Iplatform/$(PLATFORM)` が決める。          */
/*  アーキテクチャや機種を足す作業は「ディレクトリを 1 つ足す」だけで済み、    */
/*  既存のファイルに `#ifdef` が増えない。手順は arch/README.md。            */
/*                                                                          */
/*  カーネル側の C ソース (kernel/ drivers/ exec/ fs/ kapi/ lib/ net/ gfx/)  */
/*  は hlt / cli / sti を直接 asm で書かない。ここの原始命令だけを使う。      */
/*  番人は tools/check_arch_asm.py (make check)。                            */
/*                                                                          */
/*  下の宣言に付いた註は「何を保証するか」の契約であって x86 の説明ではない。 */
/*  別アーキテクチャへ移すときは、命令列ではなく **契約** を再現すること。    */
/*  宣言そのものも契約の一部で、実装の型が食い違えばコンパイルが止まる。      */
/* ======================================================================== */

#ifndef IO_H
#define IO_H

/* ======================================================================== */
/*  1. ポート I/O — platform/$(PLATFORM)/platform_io.h                      */
/* ======================================================================== */

/* 8-bit ポートから 1 バイト読む / 書く。
 * 契約: `port` は機種の I/O 空間での番地。副作用のあるレジスタを叩くので
 *       コンパイラに順序を入れ替えさせてはいけない。 */
static inline unsigned int inp(unsigned int port);
static inline void outp(unsigned int port, unsigned int value);

/* 16-bit ポートから 1 ワード読む / 書く。
 * 契約: 転送はリトルエンディアンの 1 語として扱う (PC-98 の機器はすべて
 *       LE。移植先で BE の CPU に載せるならここで入れ替えること)。 */
static inline unsigned int inpw(unsigned int port);
static inline void outpw(unsigned int port, unsigned int value);

/* 32-bit ポートから 1 ダブルワード読む / 書く。
 * 契約: 転送はリトルエンディアンの 1 語。**1 回のバスサイクルで 32 ビット
 *       まるごと**であること — 8/16 ビットに割っては**いけない**。
 * これが要るのは PCI のコンフィギュレーションアドレスレジスタ (0CF8h)。
 * バイト/ワードで叩くと「通常の I/O アクセス」として扱われ、PCI ではなく
 * チップセットの別レジスタを触る (`docs/hw/undocumented/io_pci.md` 456 行)。
 * I/O 空間を持たない移植先では、対応する 32 ビットのレジスタ読み書きになる。 */
static inline unsigned long inpd(unsigned int port);
static inline void outpd(unsigned int port, unsigned long value);

/* 16-bit ポートから `count` ワードを `buf` へ連続で読む。
 * 契約: `buf` は `count * 2` バイト以上。読んだ順にそのまま並べる
 *       (inpw を count 回呼ぶのと同じ結果)。メモリを書き換えるので
 *       "memory" clobber 相当の保証が要る。 */
static inline void insw_rep(unsigned int port, void *buf, unsigned int count);

/* 機器が次の I/O を受け付けるまでの短い待ち (PC-98 で約 0.6µs)。
 * 契約: 「直前の I/O が落ち着くまで待つ」。時間の長さそのものではなく、
 *       **連続アクセスのあいだに置く** ことに意味がある。各ドライバは
 *       自前の空ループではなくこれを使うこと。 */
static inline void io_wait(void);

/* io_wait() を n 回。手書きの io_wait() 連打はこれを使う。 */
static inline void io_wait_n(int n);

/* ======================================================================== */
/*  2. 割り込み制御と CPU 停止 — arch/$(ARCH)/arch_io.h                     */
/* ======================================================================== */

/* 割り込みを許可する。
 * 契約: 戻った時点で割り込みは受理される状態。呼び出し前の状態は問わない
 *       (すでに許可されていても無害)。
 * 注意: 「許可して眠る」を意図するなら _idle() を使うこと。_enable() の
 *       直後に _halt() を並べてはいけない (_idle() の註を参照)。 */
static inline void _enable(void);

/* 割り込みを禁止する。
 * 契約: 戻った時点で割り込みは受理されない。**元の状態は覚えない**ので、
 *       禁止区間が入れ子になり得る場所では irq_save()/irq_restore() を使う。 */
static inline void _disable(void);

/* 盲目的な cli/sti ペアの代替。呼び出し時点の割り込み許可状態を保存して
 * 禁止し、irq_restore() で元の状態 (有効/無効) に戻す。割り込み禁止区間が
 * ネストしても安全 (内側の restore が外側の禁止状態を壊さない)。
 * 契約: irq_save() の戻り値は irq_restore() にだけ渡す**不透明な値**。
 *       中身 (x86 なら EFLAGS) を読んだり組み立てたりしてはいけない。 */
static inline unsigned int irq_save(void);
static inline void irq_restore(unsigned int flags);

/* いま割り込みが許可されているかを **何も変えずに** 答える。
 * 契約: 戻り 1 = 許可、0 = 禁止。呼んでも割り込み状態は変わらない
 *       (irq_save() は cli する副作用があるので、ただ知りたいだけの
 *       場所で使ってはいけない — しかも戻り値は不透明という契約)。
 * これが要るのは「IF=0 なら _halt() してはいけない」を自分で判断する
 * コード (drivers/serial.c の送信ループ)。IF=0 の hlt は二度と起きない。 */
static inline int _irq_enabled(void);

/* 割り込み記述子表をロードする。
 * 契約: `ptr` はその CPU が期待する形の記述子表ポインタ。中身の形は
 *       arch 固有 (x86 なら limit+base の 6 バイト)。 */
static inline void _lidt(void *ptr);

/* 次の割り込みまで CPU を止める。
 * 契約: 割り込みが **すでに許可されている** ことを呼び手が保証する。IF=0 の
 *       文脈で呼ぶと永久に起きない (drivers/serial.c の panic 経路が
 *       スピン待ちにしてあるのはこの理由)。
 *       割り込みを 1 つ処理して戻る、が期待する使い方。ただし NMI や
 *       他の外部事象でも戻り得るので、待ち条件は必ずループで再検査する。 */
static inline void _halt(void);

/* 割り込みを許可して、次の割り込みまで CPU を止める。**不可分**。
 * 契約: 「許可する」と「眠る」のあいだに割り込みが入って取りこぼす窓が
 *       無いこと。割り込み禁止区間の中で条件を確かめ、成立していなければ
 *       そのまま眠る — という書き方はこの不可分性だけに依存できる。
 *
 * ★ _enable(); _halt(); に分けてはいけない ★
 *   x86 の sti は直後の 1 命令のあいだ割り込みを遅らせるので
 *   "sti; hlt" は分割不能だが、C の 2 文に分けると最適化や将来の挿入で
 *   両者のあいだが開き、そこへ来た割り込みを処理したあとに hlt へ入って
 *   「起こすはずだった割り込みを使い切ったまま眠る」ことが起こり得る。
 *   移植先でも、この 2 つは 1 つの原始命令として実装すること
 *   (ARMv7/v8 なら cpsie i / msr daifclr + wfi を同じ規則で並べる。
 *    条件判定 → wfi の窓を閉じるのは wfi 自身の wake-up event 保持)。
 *
 * "memory" clobber も契約の一部。眠るまえに (cli 区間の中で) 読んだ変数は
 * 割り込みハンドラが書き換え得るので、起きたあとに必ず再読させる。
 * net/link.c の LINK_IDLE() はこれに依存している。落としてはいけない。 */
static inline void _idle(void);

/* 割り込みを禁じて CPU を止める。「ここで終わり」を表す。
 * 契約: 通常の割り込みでは戻らない。ただし NMI / SMI / デバッグ例外では
 *       戻り得るので、**呼び手は必ず for (;;) で囲む**。
 *       panic / 到達不能点の行き止まりに使う (アイドル待ちではない)。 */
static inline void _stop(void);

/* ======================================================================== */
/*  3. 実装の取り込み                                                       */
/*                                                                          */
/*  固定名を引き、-I で実装を選ぶ。ファイル名が "io.h" と違うのは、          */
/*  実装側が誤ってこの契約ヘッダ自身を引き込まないようにするため。           */
/* ======================================================================== */

#include "arch_io.h"        /* arch/$(ARCH)/arch_io.h */
#include "platform_io.h"    /* platform/$(PLATFORM)/platform_io.h */

/* ======================================================================== */
/*  4. arch にも platform にも依らない小物                                  */
/* ======================================================================== */

/* ---- リングバッファの取り出し (割り込み保護付き) ----
 * ISR がエンキューするリングバッファから 1 要素取り出す共通イディオム。
 * 要素型が u16/int などバッファごとに違うためマクロで提供する。
 * 呼び出し前に count > 0 を確認しておくこと。 */
#define RING_DEQUEUE(entry, buf, head, count, bufsize)      \
    do {                                                    \
        unsigned int ring_flags_ = irq_save();              \
        (entry) = (buf)[(head)];                            \
        (head) = ((head) + 1) % (bufsize);                  \
        (count)--;                                          \
        irq_restore(ring_flags_);                           \
    } while (0)

#endif /* IO_H */
