/* ======================================================================== */
/*  LAUNCH.H — 起動要求表 (GUI 中の外部プログラム起動をカーネルが仲介する)   */
/*                                                                          */
/*  票: docs/archive/gui_v13/TASK_T9_sh.md §1 D3 / §1a (K 側 = カーネル +      */
/*      KAPI v49 の 8 本)                                                    */
/*                                                                          */
/*  GUI 中の CPL=3 アプリは入れ子 exec_run を使えない (子が park できず、     */
/*  協調型の全体が止まる — 票 §0)。そこで「外部プログラムを起動したい」と     */
/*  「この子を畳みたい」を **カーネルの表** に置き、owner 1 (WM) が           */
/*  top-level で取りに来て exec_start / exec_kill を実行し、結果を表へ返す。  */
/*                                                                          */
/*  表は **要求者 ID ごとに 1 本** (ID 2〜5 の 4 本)。1 本の中で              */
/*                                                                          */
/*    phase  IDLE → PENDING → TAKEN → RUNNING → DONE / FAILED               */
/*    kind   LAUNCH (cmdline) / KILL (arg = 畳む ID)                         */
/*    child  この表が所有する子 (0 = 無し)                                    */
/*                                                                          */
/*  と分けてあるのが要点で、**配送状態 (phase/kind) と子の所有 (child) は     */
/*  別物**。取消 (launch_cancel) や要求者の退場の途中でも child を消さないの  */
/*  は、消すと「誰も畳まない孤児」ができるため (票 §9 blocker 3)。            */
/*                                                                          */
/*  照合は要求者 ID ではなく **token** で行う。ID は再利用されるので、        */
/*  古い要求の poll / cancel が新しい住人の表に当たってしまう (票 §9 4)。     */
/*                                                                          */
/*  ワイヤ側の定数 (LAUNCH_KIND_* / LAUNCH_ST_* / LAUNCH_CMDLINE_MAX /       */
/*  LAUNCH_TOKEN_MAX) は sh・端末・WM と共有するので os32_kapi_shared.h が    */
/*  正典 ([C4] の 3 層管理)。ここにはカーネル内部だけが使う宣言を置く。       */
/* ======================================================================== */

#ifndef __LAUNCH_H
#define __LAUNCH_H

#include "types.h"

/* ---- phase (表の中だけ。KAPI には LAUNCH_ST_* の形で出る) -------------- */
#define LAUNCH_PHASE_IDLE     0
#define LAUNCH_PHASE_PENDING  1
#define LAUNCH_PHASE_TAKEN    2
#define LAUNCH_PHASE_RUNNING  3
#define LAUNCH_PHASE_DONE     4
#define LAUNCH_PHASE_FAILED   5

/* 要求者が退場した後の印 (票 §10 non-blocker 1 の「孤児回収」)。
 * この印が付いた表は poll されないので、完了したら DONE ではなく IDLE に
 * 落とす — 落とさないと同じ ID の次の住人が永久に OS32_ERR_FULL を食う。 */
#define LAUNCH_REQ_ORPHAN     (-1)

/* ---- 入退場 ------------------------------------------------------------ */
/* 表を空にする (exec_init から 1 回。token の種は 1 に戻さない)。 */
void launch_init(void);

/* ---- KAPI v49 の実体 (§1a の ABI 表がそのまま仕様) --------------------- */

/* 起動要求を積む。戻り値 = token (> 0) / 負。
 *   宣言 OS32X_FLAG_LAUNCHER を持つ CPL=3 アプリだけ (hdr_flags を見る)。
 *   GUI 外 (con_sink 無効) / 入れ子 exec_run の子 (gui == 0 の非シェル) は
 *   OS32_ERR_INVAL。cmdline は NUL 終端 1〜255B (空 / 超過は INVAL)。
 *   自分の表が IDLE でない (孤児回収中を含む) → OS32_ERR_FULL。 */
i32 launch_req(const char *cmdline);

/* PENDING の要求数。誰でも呼べる (WM の should_park の材料)。 */
i32 launch_pending(void);

/* PENDING を 1 本取って TAKEN にする。owner 1 専用、要求者 ID 昇順。
 * 戻り値 = token / 0 (無し) / 負。**出力ポインタは buf を含めて NULL 可**
 * (書かないだけ)。cap を見るのは buf が非 NULL のときだけで、そのとき
 * cap < LAUNCH_CMDLINE_MAX は INVAL。失敗時は 1 つも書かない。
 * requester には孤児回収の表なら LAUNCH_REQ_ORPHAN が入る。 */
i32 launch_take(char *buf, u32 cap, i32 *requester, i32 *kind, i32 *arg);

/* 結果を表へ返す。owner 1 専用。TAKEN 以外は OS32_ERR_STALE。
 *   LAUNCH: rc > 0 → child = rc, RUNNING (生きている非シェル ID でなければ
 *           OS32_ERR_INVAL) / rc == 0 → DONE / rc < 0 → FAILED(rc)
 *   KILL  : rc は無視。**取得済みの印 (TAKEN) を消して RUNNING へ戻すだけ**
 *           で、child は落とさない — 落とすと「生きている子の所有が誰の表
 *           からも消え、以後の退場でも回収されない」孤児ができる。DONE を
 *           付けるのは常に child の回収通知 (launch_owner_exit) なので、
 *           正常な順序では先に DONE になっていて STALE が返る (票 §12 2)。
 *           要求者が再度 cancel すれば KILL(child) がまた PENDING になる。 */
i32 launch_report(i32 token, i32 rc);

/* 要求者だけが自分の要求の状態を読む。status は LAUNCH_ST_*。
 * DONE / FAILED を返した時点で表は IDLE に戻る (再 poll は STALE)。 */
i32 launch_poll(i32 token, i32 *status);

/* 要求者だけが取り消す。RUNNING → KILL(child) の PENDING (child は保持)。
 * PENDING / TAKEN → OS32_ERR_AGAIN、DONE / FAILED / **要求者の不一致**
 * (別 ID からの cancel、孤児回収中の表を再利用 ID が指した旧 token) →
 * OS32_ERR_STALE。AGAIN だけが「次のタイマで再試行」の合図。 */
i32 launch_cancel(i32 token);

/* その ID の表が所有する子 (phase を問わず)。誰でも。不正 ID は 0。
 * WM が CTRL+STOP の宛先を連鎖の末尾へ解決するのに使う (票 D8)。 */
i32 launch_child(i32 id);

/* ---- 連鎖 (票 D8。exec_kill が使う) ------------------------------------ */
/* head から child を末尾まで辿って out[] に並べる (out[0] = head)。
 * 戻り値 = 並べた本数 (>= 1、head が不正なら 0)。同じ ID は 2 度入れない
 * (壊れた表が環を作っても止まる)。exec_kill はこれを **末尾から** 畳む。 */
int launch_chain(int head, int *out, int max);

/* ---- 回収 (exec_reclaim_owned から、con_sink_owner_exit と同じ位置) ---- */
/* 使うのは ID だけ (AppSlot の欄は読まない — 正常終了は解放の後、exec_kill は
 * 前にここを通るため)。
 *   child == id の表  : DONE + child = 0 + 取得済みの印を掃除
 *   requester == id の表: child があれば孤児回収 (KILL(child) の PENDING)、
 *                         無ければ IDLE (票 §10 non-blocker 1) */
void launch_owner_exit(int id);

/* ---- 自己診断 (kernel/kselftest.c) ------------------------------------- */
/* 表の遷移を踏む。ビット 0..n が落ちた項目 (0 = 全部通った)。
 * 呼んだ後の表は空に戻る。 */
u32 launch_selftest(void);

#endif /* __LAUNCH_H */
