/* ======================================================================== */
/*  SERIALFS.H — シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B)         */
/*                                                                          */
/*  セッションを開けるのは常駐シェルの `sfs run <コマンド行>` だけ          */
/*  (KAPI sfs_begin / sfs_end)。`mount /host COM1 serialfs` の単独実行は     */
/*  mount の口が断る (セッションの中の vfs_mount だけが通る)。              */
/*                                                                          */
/*  fs/serialfs.c        … VfsOps (ホスト試験でもそのまま組む)              */
/*  fs/serialfs_session.c … セッション (ゲート・HELLO・マウント・隔離・     */
/*                          ログのフレーム)。カーネルだけ                    */
/* ======================================================================== */
#ifndef FS_SERIALFS_H
#define FS_SERIALFS_H

#include "types.h"
#include "vfs.h"
#include "sfs_client.h"

#define SERIALFS_NAME     "serialfs"
#define SERIALFS_PREFIX   "/host"
#define SERIALFS_DEVNAME  "COM1"

/* VfsOps (vfs_register_fs に渡す) */
VfsOps *serialfs_get_ops(void);
void    serialfs_init(void);

/* セッションが mount の口を開け閉めする。c = このセッションの受け手。
 * 開いているあいだだけ ops->mount が c を返す (それ以外は NULL)。 */
void    serialfs_permit(SfsClient *c);
void    serialfs_forbid(void);
int     serialfs_is_mounted(void);

/* ---- セッション (fs/serialfs_session.c、KAPI の実体) ----
 * sfs_begin: 0 = /host にマウントした /
 *   OS32_ERR_INVAL … 常駐シェル以外から / シリアル未初期化 / 待てない (IF=0)
 *   OS32_ERR_BUSY  … セッション中 / /host が使われている
 *   OS32_ERR_IO    … HELLO に答えが無い (ゲートは下ろして戻る)
 *   その他の負値   … vfs_mount の失敗 (BYE してゲートを下ろして戻る)
 * sfs_end: 0 = 閉じた / 1 = 閉じたが線が静まらなかった (警告済み) /
 *   OS32_ERR_INVAL … セッションが無い・常駐シェル以外から */
int     serialfs_session_begin(void);
int     serialfs_session_end(int exit_code);

#endif /* FS_SERIALFS_H */
