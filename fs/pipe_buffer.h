/* ======================================================================== */
/*  PIPE_BUFFER.H -- パイプバッファ管理                                       */
/*                                                                          */
/*  シングルタスクOS向けのシーケンシャルパイプ実装。                          */
/*  cmd1 の stdout をバッファに蓄積 → cmd2 の stdin から読み取る。           */
/*  2つのバッファを交互に使うことで多段パイプに対応 (A→B→A→…)。             */
/* ======================================================================== */

#ifndef PIPE_BUFFER_H
#define PIPE_BUFFER_H

#include "types.h"

/* PIPE_BUF_SIZE は os32_kapi_shared.h で定義 (SSoT) */
#ifndef PIPE_BUF_SIZE
#define PIPE_BUF_SIZE   (64 * 1024)
#endif

/* パイプバッファの個数 (交互使用: 2個) */
#define PIPE_BUF_COUNT  2

/* ======== API ======== */

/* 初期化 (kmalloc でバッファ確保) — カーネル起動時に1回呼ぶ */
void pipe_buffer_init(void);

/* パイプバッファの確保 (使用中フラグを立てる)
 * 戻り値: バッファID (0 or 1), -1=空きなし */
int pipe_alloc(void);

/* パイプバッファの解放 */
void pipe_free(int id);

/* 指定所有者 (res_owner_get() の値で確保時にタグ付け) のバッファだけ解放。
 * exec_exit の安全網用: シェルが張ったパイプラインのバッファを、途中段の
 * 外部プログラム終了で巻き込まないため */
void pipe_free_owned(int owner);

/* バッファのポインタを取得 */
u8 *pipe_get_buf(int id);

/* バッファの容量を取得 */
u32 pipe_get_capacity(int id);

/* バッファの書き込み済みデータ長を取得/設定 */
u32 pipe_get_len(int id);
void pipe_set_len(int id, u32 len);

/* バッファをクリア (len=0にリセット) */
void pipe_clear(int id);

#endif /* PIPE_BUFFER_H */
