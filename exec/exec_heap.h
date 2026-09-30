/* ======================================================================== */
/*  EXEC_HEAP.H — プログラム専用ヒープ (カーネルヒープと完全分離)           */
/*                                                                          */
/*  0x500000〜 にFirst-Fit動的アロケータを配置。                            */
/*  exec_run 開始時に初期化。                                               */
/*  alloc/freeによる動的な確保・解放・再利用が可能。                         */
/*  終了/クラッシュ時にresetで全域破棄。                                     */
/* ======================================================================== */

#ifndef __EXEC_HEAP_H
#define __EXEC_HEAP_H

#include "types.h"

#include "memmap.h"

/* exec_heap のベースアドレスとサイズは exec_run() 内で動的に計算される */

/* ヒープ初期化 (exec_run開始時に呼ぶ)
 * base: ヒープの開始アドレス
 * size: 確保するヒープサイズ (バイト)
 * 全体を1つの空きブロックとして初期化する */
void exec_heap_init_at(u32 base, u32 size);

/* ヒープからメモリ確保 (4バイトアラインメント)
 * ファーストフィット方式で空きブロックを検索・分割
 * 戻り値: ポインタ, NULL=空き不足 */
void *exec_heap_alloc(u32 size);

/* メモリ解放
 * ブロックをフリーに戻し、前後のフリーブロックと結合（マージ）する */
void exec_heap_free(void *ptr);

/* ヒープ全域破棄 (exec_run終了時に呼ぶ)
 * 全体を1つのフリーブロックに戻す。O(1) */
void exec_heap_reset(void);

/* スナップショット保存 (exec_run 開始前に呼ぶ)
 * 現在の管理変数を out_used に退避する。
 * ヒープのメタデータ (BlkHdr) はメモリ上にそのまま残る。 */
void exec_heap_save_state(u32 *out_used);

/* スナップショット復元 (子プロセス終了後に呼ぶ)
 * exec_heap_init_at と異なり、ヒープのメタデータを破壊しない。
 * 管理変数 (base/size/used) だけを元に戻す。
 * 凍結モデル前提: 親プロセスのヒープ領域は子が書き換えないことを保証。 */
void exec_heap_restore_state(u32 base, u32 size, u32 used);

/* 統計 */
u32 exec_heap_total(void);
u32 exec_heap_used(void);

#endif /* __EXEC_HEAP_H */
