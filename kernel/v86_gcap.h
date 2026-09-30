/* ======================================================================== */
/*  V86_GCAP.H — `v86 -g`: 実機の ROM の INT 18h AH=31h/30h を V86 で呼び、  */
/*  その間の I/O を記録する管理者用の診断                                   */
/*                                                                          */
/*  票 docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1。記録の形・判定は    */
/*  v86_gcap_math.h (ホスト試験で実物のまま回す)。ここはセッションの段取り、 */
/*  #GP / I/O 経路の差し込み口、CUI の作り直し。                            */
/* ======================================================================== */

#ifndef __V86_GCAP_H
#define __V86_GCAP_H

#include "types.h"
#include "os32_kapi_shared.h"   /* V86Gcap */

/* KAPI v68 v86_gdc_capture の本体。mode = V86G_MODE_ROM / V86G_MODE_SELFTEST。
 * 記録を out へ写し、status (V86G_ST_*、>= 0) を返す。始められなければ負
 * (OS32_ERR_INVAL / OS32_ERR_BUSY / OS32_ERR_NOSPC、out は触らない)。 */
int v86_gdc_capture(int mode, V86Gcap *out);

/* 採取中か (v86_io.c の方針の切り替えに使う) */
int v86_gcap_active(void);

/* ゲストの INT n を IF を落とさずに流すか (v86_inject_int)。ROM の呼び出しと
 * その見切りの自己試験の間だけ 1。ROM の 1 呼び出しを GCAP_TICK_LIMIT で
 * 見切り、脱出ホットキーを効かせるため (タイマ・キーボードの実 IRQ は
 * ゲストの IF = 実 IF が立っていないと来ない)。 */
int v86_gcap_keep_if(void);

/* ---- #GP / I/O 経路からの差し込み口。採取中でなければ何もせず 0 ---- */

/* #GP で捕まえた命令を採取中に扱えないなら 1 (打ち切り。理由と CS:IP を
 * 記録する)。opsize16 = 0 は 66h 前置。 */
int  v86_gcap_insn_abort(int opsize16, u8 opcode);

/* 実機へ通すポートなら幅どおりに読み書きして記録し 1。そうでなければ 0
 * (呼び手は通常の仮想化へ進む)。 */
int  v86_gcap_pass_in(u16 port, int size, u32 *value);
int  v86_gcap_pass_out(u16 port, int size, u32 value);

/* 通常の仮想化で済ませた (実機へ通さなかった) I/O を記録する。
 * 実機へ通すポートは pass_* が記録済みなので、ここでは数えない。 */
void v86_gcap_note_emul_in(u16 port, int size, u32 value);
void v86_gcap_note_emul_out(u16 port, int size, u32 value);

#endif /* __V86_GCAP_H */
