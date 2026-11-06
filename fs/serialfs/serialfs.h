/* ======================================================================== */
/*  SERIALFS.H — シリアル越しの /host (票 TASK_SERIAL_HOSTFS 部品 B)         */
/*                                                                          */
/*  UART は常駐シェルの `sfs run <コマンド行>` 内だけ /host を公開する。    */
/*  LAN は起動時に /host を常駐公開し、最初の操作で HELLO する。           */
/*  KAPI sfs_begin / sfs_end は LAN では scope の開始 / 終了になる。        */
/*                                                                          */
/*  fs/serialfs/serialfs.c        … VfsOps (ホスト試験でもそのまま組む)              */
/*  fs/serialfs/serialfs_session.c … セッション (ゲート・HELLO・マウント・隔離・     */
/*                          ログのフレーム)。カーネルだけ                    */
/*  fs/serialfs/serialfs_persistent.c … LAN 常駐 client・mount 世代          */
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
/* Absolute SfsIo.now deadline: 10ms ticks, entry now+100, retry every 10.
 * Use the same deadline through ensure/HELLO/RPC; compare with signed
 * (i32)(now-deadline)>=0 and keep differences below half the u32 range.
 * The whole operation has 1000ms including HELLO/TX/retry.
 * No sid / 30s idle / new ALIVE / seq>0xF000 requires HELLO.
 * A timeout fails this request with HOSTDOWN; the next request tries again.
 * out is required and follows SfsReply ownership/copy rules in sfs_client.h;
 * success returns the complete response including its undecoded wire status.
 * A timed-out write must not be replayed under a new sid automatically. */
int sfs_ensure_session(u32 deadline);
int sfs_request_deadline(u8 op, const void *in, u16 n, struct SfsReply *out, u32 deadline);
/* Non-consuming internal observation; only schedules reconnection. */
void sfs_alive_observe(u32 host_generation);

/* mount のゲート。UART セッション / LAN 常駐の所有者が開け閉めする。
 * c = 共有 client (LAN では C1 だけが所有する)。
 * 開いているあいだだけ ops->mount が c を返す (それ以外は NULL)。 */
void    serialfs_permit(SfsClient *c);
void    serialfs_forbid(void);
int     serialfs_is_mounted(void);

/* ---- セッション (fs/serialfs/serialfs_session.c、KAPI の実体) ----
 * LAN 常駐時は scope_begin/end へ分岐し、BYE/unmount しない。
 * 以下の戻り値と shell owner の制約は UART 経路の契約。
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

/* C1 owns the sole resident client and mount generation. cfg is copied;
 * io/ctx are borrowed for boot lifetime. start publishes /host without HELLO,
 * even with no peer response; an existing HostDrv mount returns BUSY. */
struct SfsPersistentConfig {
    const SfsIo *io;
    void *io_ctx;
    int lan_enabled;
};
int sfs_persistent_start(const struct SfsPersistentConfig *cfg);
/* NULL before start, otherwise borrowed read-only (sid/seq observation).
 * Mutations and communication go through ensure/request above. */
const SfsClient *sfs_persistent_client(void);
u32 sfs_persistent_generation(void);
/* Unknown sid (reported by C3) or a new host generation invalidates old FDs.
 * Ordinary idle/same-generation ALIVE sid renewal preserves mount generation. */
void sfs_persistent_stale(void);
int sfs_persistent_enabled(void);
/* Paired LAN scopes; end never sends BYE or unmounts the resident /host. */
int sfs_persistent_scope_begin(void);
int sfs_persistent_scope_end(int exit_code);

#endif /* FS_SERIALFS_H */
