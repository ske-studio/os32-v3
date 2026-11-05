/* ======================================================================== */
/*  SERIALFS.H — シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B)         */
/*                                                                          */
/*  セッションを開けるのは常駐シェルの `sfs run <コマンド行>` だけ          */
/*  (KAPI sfs_begin / sfs_end)。`mount /host COM1 serialfs` の単独実行は     */
/*  mount の口が断る (セッションの中の vfs_mount だけが通る)。              */
/*                                                                          */
/*  fs/serialfs/serialfs.c        … VfsOps (ホスト試験でもそのまま組む)              */
/*  fs/serialfs/serialfs_session.c … セッション (ゲート・HELLO・マウント・隔離・     */
/*                          ログのフレーム)。カーネルだけ                    */
/* ======================================================================== */
#ifndef FS_SERIALFS_H
#define FS_SERIALFS_H

#include "types.h"
#include "../vfs.h"
#include "sfs_client.h"

#define SERIALFS_NAME     "serialfs"
#define SERIALFS_PREFIX   "/host"
#define SERIALFS_DEVNAME  "COM1"

/* VfsOps (vfs_register_fs に渡す) */
VfsOps *serialfs_get_ops(void);
int     serialfs_init(void);

/* LAN resident lifetime (NET_HOSTSVC_DESIGN §3). Persistent code owns this
 * client and mount generation; UART retains the shell-scoped lifetime below.
 * sid renewal on idle/same-generation ALIVE does not invalidate existing FDs. */
struct SfsPersistent {
    SfsClient client;
    u32 generation, last_use, alive_seen;
};
/* Absolute deadline for the whole operation (1000ms including HELLO/TX/retry).
 * No sid / 30s idle / new ALIVE / seq>0xF000 requires HELLO.
 * A timeout fails this request with HOSTDOWN; the next request tries again.
 * A timed-out write must not be replayed under a new sid automatically. */
int sfs_ensure_session(u32 deadline);
int sfs_request_deadline(u8 op, const void *in, u16 n, void *out, u32 deadline);
/* Non-consuming internal observation; only schedules reconnection. */
void sfs_alive_observe(u32 host_generation);

/* セッションが mount の口を開け閉めする。c = このセッションの受け手。
 * 開いているあいだだけ ops->mount が c を返す (それ以外は NULL)。 */
void    serialfs_permit(SfsClient *c);
void    serialfs_forbid(void);
int     serialfs_is_mounted(void);

/* ---- セッション (fs/serialfs/serialfs_session.c、KAPI の実体) ----
 * sfs_begin: 0 = /host にマウントした /
 *   OS32_ERR_INVAL … 常駐シェル以外から / シリアル未初期化 / 待てない (IF=0)
 *   OS32_ERR_BUSY  … セッション中 / /host が使われている
 *   OS32_ERR_IO    … HELLO に答えが無い (ゲートは下ろして戻る)
 *   その他の負値   … vfs_mount の失敗 (BYE してゲートを下ろして戻る)
 * sfs_end: 0 = 閉じた / 1 = 閉じたが線が静まらなかった (警告済み) /
 *   OS32_ERR_INVAL … セッションが無い・常駐シェル以外から */
int     serialfs_session_begin(void);
int     serialfs_session_end(int exit_code);
/* kdebug の peek -l の宛先 (TASK_KPEEK §2-8)。out は 6 バイト。
 * 0 = ペアの MAC を写した / -1 = ペアが無い (out は変えない)。 */
int     sfs_lan_peer_mac(u8 *out);

#endif /* FS_SERIALFS_H */
