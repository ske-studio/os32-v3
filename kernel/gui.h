/* ======================================================================== */
/*  GUI.H — GUI シェル v1.1 のカーネル背骨 (K レーン K1)                     */
/*                                                                          */
/*  アプリ (CPL=3) が gshell の WM を呼ぶ唯一の入口 gui_call と、WM が        */
/*  自分を登録する gui_register を提供する。カーネルは op の意味を解釈せず、  */
/*  登録ハンドラへ転送 + owner 付与だけを行う (契約 T1)。                     */
/*                                                                          */
/*  設計: docs/tasks/gui/API_CONTRACTS.md (T1 / T4 / T9)、TASK_K1_gui_call.md */
/* ======================================================================== */

#ifndef __GUI_H
#define __GUI_H

#include "os32_gui_shared.h"   /* GUI_OP_OWNER_EXIT, 型 */

/* WM ハンドラの C 署名 (契約 T4 の所有者タグ付き)。
 * owner = 呼び出し元の exec ネスト段 (res_owner_get())。 */
typedef i32 (*GuiHandler)(u32 op, u32 arg, int owner);

/* 入力ポンプの C 署名 (契約 T6 / T8 の X4)。引数なし・戻り値なし。
 * gshell 側の実体は Rust の `gshell_gui_pump()` (userland/gshell/src/pump.rs)。 */
typedef void (*GuiPump)(void);

/* シェル帯の実行レベル (exec_exit の注記どおり level 1 = シェル)。
 * WM (gshell) 自身の owner。K2 の syscall 境界ポンプもこれで自分を除外する。 */
#define GUI_SHELL_OWNER  1

/* KAPI: アプリ → WM の唯一の入口。未登録なら OS32_ERR_NOSYS。 */
i32 gui_call(u32 op, u32 arg);

/* KAPI: WM (gshell) が自分のハンドラを登録する。shell 帯 (owner 1, CPL=0)
 * からのみ許可。それ以外は OS32_ERR_INVAL。pump は K2 が使う (今は保存だけ)。 */
i32 gui_register(void *handler, void *pump);

/* 所有者による回収の口 (契約 T4 / U8)。exec_exit() が owner 別回収で呼ぶ。
 * ハンドラが登録済みなら GUI_OP_OWNER_EXIT を渡して WM に回収させる。 */
void gui_owner_exit(int owner);

/* KAPI ime_set_render の入口 (sdk/kapi.json の target)。shell 帯 (owner 1) の
 * CPL=0 の直呼びだけを通し、アプリ由来 (ring3_call_from_user) は黙って断って
 * gui_ime_render_rejected を数える。 */
void gui_ime_set_render(void *table);
extern volatile u32 gui_ime_render_rejected;

/* K2 用: 登録された入力ポンプ (gui_pump) を返す。未登録なら NULL。
 * 実体の型は GuiPump (void (*)(void))。 */
void *gui_get_pump(void);

/* KAPI: 次に起動するシェルのパスをカーネルに記録する (契約 T9、K4 の入口)。
 * shell 帯 (owner 1) からのみ。呼んだシェルは自分で後始末して exit する。
 * 実際のシェル入れ替えはカーネルのシェル起動ループ (K4) が行う。 */
i32 sys_switch_shell(const char *path);

/* K4 用: sys_switch_shell で記録された次シェルのパス (無ければ NULL)。 */
const char *gui_next_shell(void);
int gui_take_next_shell(char *out, int cap);   /* 取り出して消す (K4 の起動ループ用) */

#endif /* __GUI_H */
