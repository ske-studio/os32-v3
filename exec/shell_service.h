/* ========================================================================
 *  shell_service.h — 常駐 shell (gshell) だけが使う内部入口の表 (T7b-b2)
 *
 *  設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §3 (票 docs/tasks/v3/
 *  TASK_T7_AND_FOLLOWUPS.md §2-1〜2-4)。
 *
 *  表 g_shell_service は kernel BSS (supervisor、USER なし) に 1 つだけ置き、
 *  KAPI / trampoline / import 表には載せない。常駐 shell には entry frame の
 *  4 語目 (CRT0 `_start_c` の第 4 引数 → `__os32_aux`) で番地を渡し、
 *  app には 0 を渡す (結線は S3b、exec/exec.c)。
 *
 *  入口 5 本は exec/shell_service.c の wrapper で、呼ぶたびに
 *    A 戻り番地が現行 shell text の範囲内 (かつ登録中)
 *    B 呼出し文脈 (open/close は shell の通常文脈だけ、read/validate/
 *      font_guard_run は通常文脈か登録済み WM 区間)
 *  を確かめてから kernel/font_source.c の shell 側実体 (段 C registry・
 *  段 D VFS) か kernel/font_guard.c の font_guard_run へ渡す。外れたら
 *  OS32_ERR_INVAL、kill はしない。
 *
 *  version 2 (T7b-b3 S2、設計 ~/os32-tmp/run/t7bb34/DESIGN.final.md §2-5):
 *  末尾に font_guard_run を追記。gshell は version < 2 の表なら font を
 *  開かず KCG だけで描く (KcgOnly(OldShell))。lease の open/close は guard の
 *  外 (段 B の open 文脈)、Face の読込みと miss の raster+commit は callback
 *  の中で行い、callback の中では read_at 以外の入口・KAPI を呼ばない。
 *
 *  定数 SHELL_SERVICE_MAGIC / VERSION / SIZE は gshell の build.rs が
 *  この header から読む ([C4]、userland/gshell/src/ffi.rs)。書式
 *  (`#define 名前 値` の 1 行) を変えないこと。
 * ======================================================================== */
#ifndef OS32_SHELL_SERVICE_H
#define OS32_SHELL_SERVICE_H

#include "types.h"   /* u32 / i32 / __cdecl / STATIC_ASSERT */

/* 表の印 ("SVS1" の little endian) と版。欄の追加・意味の変更で版を上げる。 */
#define SHELL_SERVICE_MAGIC 0x31535653u
#define SHELL_SERVICE_VERSION 2u
/* i386 での表の大きさ (u32 3 語 + 関数番地 5 語)。 */
#define SHELL_SERVICE_SIZE 32u

/* shell_service_reject_last の理由番号 (どの段で落ちたか)。 */
#define SHELL_SVC_REJ_INACTIVE  1   /* 段 A: 登録されていない (retire 後・publish 前) */
#define SHELL_SVC_REJ_TEXT      2   /* 段 A: 戻り番地が現行 shell text の外 */
#define SHELL_SVC_REJ_CTX_OPEN  3   /* 段 B: open/close の文脈でない (WM 中・app 由来など) */
#define SHELL_SVC_REJ_CTX_READ  4   /* 段 B: read/validate の文脈でない */
#define SHELL_SVC_REJ_CTX_GUARD 5   /* 段 B: font_guard_run の文脈でない (read と同じ条件) */

typedef struct {
    u32 magic, version;            /* SHELL_SERVICE_MAGIC / SHELL_SERVICE_VERSION */
    u32 generation;                /* shell 登録世代 (単調増加、0 = 未登録) */
    i32 (__cdecl *font_source_open)(const char *path, u32 *out_id, u32 *out_size);
    i32 (__cdecl *font_source_read_at)(u32 id, u32 offset, void *dst, u32 len);
    i32 (__cdecl *font_source_validate)(u32 id);
    i32 (__cdecl *font_source_close)(u32 id);
    /* version 2: x87 guard の中で call(ctx) を 1 回呼び、その戻り値を返す
     * (kernel/font_guard.h)。FPU 無しは OS32_ERR_NOSYS (以後 KCG)、
     * 再入・IRQ/例外文脈は OS32_ERR_BUSY (この呼出しだけ KCG)。 */
    i32 (__cdecl *font_guard_run)(int (*call)(void *), void *ctx);
} ShellService;

#if defined(__i386__)
STATIC_ASSERT(sizeof(ShellService) == SHELL_SERVICE_SIZE, shell_service_size);
#endif

/* 表の実体。番地は entry frame の 4 語目で常駐 shell へ渡す (S3b)。 */
extern ShellService g_shell_service;

/* WM 区間の世代付きの印。ring3_wm_enter で深さが 1 になったとき登録中なら
 * 現 generation、深さが 0 に戻る・ring3_context_clear・dispatch 入口で 0
 * (結線は S3b)。カーネルシンボル (emu_read_mem で読む)。 */
extern volatile u32 ring3_wm_shell_gen;

/* 入口 wrapper が断った数と最後の理由 (SHELL_SVC_REJ_*)。カーネルシンボル。 */
extern volatile u32 shell_service_reject_count;
extern volatile u32 shell_service_reject_last;

/* 常駐 shell の画像を確定した直後 (appslot_shell_commit の後) に呼ぶ。
 * generation を進め、text 範囲 [load_addr, load_addr + text_size) を控え、
 * 関数 5 本を埋めて登録中にする。範囲が空か桁あふれなら登録しない。 */
void shell_service_publish(u32 load_addr, u32 text_size);

/* 常駐 shell の終了 (exec_finish の shell 分岐、exec_reclaim_owned の後)。
 * shell の lease を全部閉じ、登録を外し、text 範囲を 0 にする。
 * generation は進めない (次の publish で進む)。 */
void shell_service_retire(void);

/* ring3_wm_enter で深さが 1 になった直後に呼ぶ (S3b)。登録中なら
 * ring3_wm_shell_gen に現 generation を書く。 */
void shell_service_wm_mark(void);

#endif /* OS32_SHELL_SERVICE_H */
