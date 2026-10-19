/* ========================================================================
 *  font_source.h — font の read lease 表 (T7b-b1、設計 §2-1・§2-3・§3-2)
 *
 *  設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md (票 docs/tasks/v3/
 *  TASK_T7_AND_FOLLOWUPS.md §2-1〜2-4)。
 *
 *  VFS の font FD (fs/vfs.h の VfsFontLease) を lease 表 g_font_leases に
 *  1 枠ずつ持ち、呼び手には FD 番号でなく公開 id を渡す:
 *      id = (id_gen << FONT_LEASE_IDX_BITS) | idx
 *  id_gen は枠ごとに単調増加 (close / 回収で進む)。満了した枠は二度と
 *  使わない (FD の generation と同じ規則)。app (KAPI) と常駐 shell
 *  (exec/shell_service.c の内部入口) は kind で名前空間を分け、同じ id でも
 *  kind が違えば INVAL。
 *
 *  app の入口 font_lease_app_* は public KAPI font_source_open / read_at /
 *  close (sdk/kapi.json、v77) の実体。shell の入口 font_lease_shell_* は
 *  shell_service.c の wrapper が段 A/B を確かめてから呼ぶ (段 C/D)。
 * ======================================================================== */
#ifndef OS32_FONT_SOURCE_H
#define OS32_FONT_SOURCE_H

#include "types.h"
#include "vfs.h"     /* VfsFontLease / VFS_MAX_OPEN_FILES */

enum { FONT_LEASE_FREE = 0, FONT_LEASE_APP = 1, FONT_LEASE_SHELL = 2 };

/* 公開 id の下位 FONT_LEASE_IDX_BITS ビットが枠番号、残りが id_gen。 */
#define FONT_LEASE_IDX_BITS 4u
#define FONT_LEASE_IDX_MASK ((1u << FONT_LEASE_IDX_BITS) - 1u)
/* 枠の数 = VFS の FD 表の大きさ (font FD より多くは要らない)。 */
#define FONT_LEASE_SLOTS    VFS_MAX_OPEN_FILES
/* id_gen は 28 ビット。ここに達した枠は retire (再利用しない)。 */
#define FONT_LEASE_GEN_MAX  (0xffffffffu >> FONT_LEASE_IDX_BITS)

STATIC_ASSERT(FONT_LEASE_SLOTS <= (1u << FONT_LEASE_IDX_BITS), font_lease_slots_fit);

typedef struct {
    u8  kind;          /* FONT_LEASE_FREE / APP / SHELL */
    u8  pad[3];
    u32 id_gen;        /* 公開 id の上位。0 = まだ使っていない枠 */
    VfsFontLease vfs;  /* VFS の font FD (owner は app の owner / GUI_SHELL_OWNER) */
    u32 owner_gen;     /* app: caller の AS 世代、shell: shell 登録世代 */
    u32 file_size;     /* open 時の長さ */
} FontLease;

/* font_lease_reject_last の理由番号 (app の門・token の判定表のどこで落ちたか)。 */
#define FONT_LEASE_REJ_NOT_USER   1   /* 門 1: CPL=3 の dispatch 中でない (WM 中・CPL0 直呼び) */
#define FONT_LEASE_REJ_CALLER     2   /* 門 2: caller が USER でない・owner が res_owner と違う */
#define FONT_LEASE_REJ_IDX        3   /* 3a: 枠番号が範囲外 */
#define FONT_LEASE_REJ_ID_GEN     4   /* 3b: id_gen 不一致 (close 済み・再 close) */
#define FONT_LEASE_REJ_KIND       5   /* 3c: kind が APP でない */
#define FONT_LEASE_REJ_OWNER      6   /* 3d: 他 owner の id */
#define FONT_LEASE_REJ_OWNER_GEN  7   /* 3e: 同 owner 番号の別の app (AS 世代違い) */
#define FONT_LEASE_REJ_PATH       8   /* open: path を写せない (kill 済み) */

/* app の門・token で断った数と最後の理由。カーネルシンボル (emu_read_mem)。 */
extern volatile u32 font_lease_reject_count;
extern volatile u32 font_lease_reject_last;

/* ---- app (public KAPI v77 の実体) ----
 * 門: ring3_call_from_user() != 0、caller が USER かつ caller.owner ==
 * res_owner_get()。外れたら OS32_ERR_INVAL。
 * open: owner ごとの上限 VFS_FONT_FD_MAX_APP・合計 VFS_FONT_FD_MAX_APP_TOTAL・
 *   枠 / FD 枯渇は OS32_ERR_NOSPC。成功で 0 と *out_id / *out_size。
 * read_at: 戻り = 実読込長 (短読はそのまま) / 負。範囲外は INVAL。
 * close: 0。close 済み id の再 close は OS32_ERR_STALE。 */
i32 font_lease_app_open(const char *path, u32 *out_id, u32 *out_size);
i32 font_lease_app_read_at(u32 id, u32 offset, void *dst, u32 len);
i32 font_lease_app_close(u32 id);

/* app の終了 / fault 回収 (exec_reclaim_resources の vfs_close_owned の直前、
 * 結線は S3b)。kind==APP かつ owner 一致の lease を閉じて枠を空ける。 */
void font_lease_close_owned(int owner);

/* ---- 常駐 shell (exec/shell_service.c の wrapper から、段 C / 段 D) ----
 * shell_gen は呼んだ時点の g_shell_service.generation。open は lease の
 * owner_gen としてこれを持ち、read_at / validate / close は照合する
 * (不一致は OS32_ERR_STALE)。VFS の owner は GUI_SHELL_OWNER 固定、
 * open flags は VFS_FONT_OPEN_PROTECT | VFS_FONT_OPEN_PIN。上限は
 * VFS_FONT_FD_MAX_SHELL (超えると OS32_ERR_NOSPC)。 */
i32 font_lease_shell_open(const char *path, u32 shell_gen, u32 *out_id, u32 *out_size);
i32 font_lease_shell_read_at(u32 id, u32 shell_gen, u32 offset, void *dst, u32 len);
i32 font_lease_shell_validate(u32 id, u32 shell_gen);
i32 font_lease_shell_close(u32 id, u32 shell_gen);
/* kind==SHELL の lease を全部 vfs_close_font (protect を無視) して枠を空ける
 * (shell_service_retire から)。 */
void font_lease_close_shell(void);

#endif /* OS32_FONT_SOURCE_H */
