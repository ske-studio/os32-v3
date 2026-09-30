/* ======================================================================== */
/*  RING3_STR.H — KAPI が CPL=3 へ **返す** 文字列の置き場                   */
/*                                                                          */
/*  票: docs/archive/gui_v13/TASK_T9_sh.md §12 R1 (Codex 網羅レビュー 往復 7)  */
/*                                                                          */
/*  KAPI の引数として来るポインタはディスパッチャが範囲検証する              */
/*  (`ring3_ptr_ok`) が、**戻り値のポインタ** は誰も見ていなかった。         */
/*  `sys_getcwd` は `fs/vfs.c` の static `cwd` (カーネル帯 = PDE0 の PTE) を */
/*  そのまま返す。`kernel/paging.c` はカーネル帯を `phys | PAGE_RW`          */
/*  (USER なし) で張るので、CPL=3 の呼び手がその番地を読むと #PF → fault     */
/*  kill になる。RO+USER になっているのは **KAPI トランポリンページの 1 枚**  */
/*  だけ (`exec/exec.c` の `ring3_trampoline_init`)。                        */
/*                                                                          */
/*  直し方は「そのページの空きへ写して、写したポインタを返す」。ABI も       */
/*  KAPI の本数も変わらない (`sdk/kapi.json` の `sys_getcwd` の target を    */
/*  `vfs_cwd` → `vfs_cwd_user` に差し替えるだけ)。                           */
/*                                                                          */
/*  ページの中身 (`ring3_trampoline_init` が組む順):                         */
/*                                                                          */
/*    +0                      : ユーザ可視の KernelAPI 表 (magic/version/    */
/*                              関数ポインタ KAPI_FUNC_CAPACITY 本/データ 2 個) */
/*    +RING3_USTR_STUB_OFF    : int 0x80 スタブ 8 B × KAPI_FUNC_CAPACITY    */
/*                              (予約スロット込み、票 TASK_KAPI_DATA_FIELDS)  */
/*    +RING3_USTR_OFF         : ここ (写し場 RING3_USTR_CAP バイト)          */
/*                                                                          */
/*  写しは **呼ばれるたびに上書き**する。呼び手は次の KAPI 呼び出しより前に   */
/*  読み切ること (仕様。docs/KAPI_SPEC.md の sys_getcwd の行に注記)。        */
/* ======================================================================== */

#ifndef __RING3_STR_H
#define __RING3_STR_H

#include "types.h"
#include "os32_kapi_shared.h"   /* KernelAPI / KAPI_FUNC_COUNT / OS32_MAX_PATH */

/* 表の直後 (4B 整列) からスタブ。`ring3_trampoline_init` の stub_base と
 * **同じ式**でなければならない。 */
#define RING3_USTR_STUB_OFF  ((u32)((sizeof(KernelAPI) + 3u) & ~3u))
/* スタブの終わり = 写し場の先頭 (4B 整列)。 */
#define RING3_USTR_OFF       ((u32)((RING3_USTR_STUB_OFF + \
                                     (u32)KAPI_FUNC_CAPACITY * 8u + 3u) & ~3u))
/* 写し場の大きさ。パス 1 本が入れば足りる。いま写しを返すのは 4 本
 * (sys_getcwd = vfs_cwd_user、vfs_devname_user、path_get_drive_user、
 * path_get_cwd_user — exec/exec.c の tramp_copy)。どれも OS32_MAX_PATH 以内。 */
#define RING3_USTR_CAP       ((u32)OS32_MAX_PATH)

/* カーネル帯の文字列を CPL=3 の呼び手へ返してよい形に直す。
 *   in_syscall : `ring3_in_syscall` (1 = int 0x80 ディスパッチ中 = CPL=3 由来)
 *   scratch    : 写し先 (トランポリンページ内)。0 なら写さない
 *   cap        : 写し先のバイト数 (NUL 込み)
 *   src        : カーネル帯の文字列 (0 可)
 * 戻り値: CPL=3 由来なら scratch、そうでなければ src (CPL=0 の常駐シェルは
 * カーネル帯をそのまま読めるので、写す意味も余地もない)。
 * **純関数に近い** (書くのは scratch だけ) のでホストでそのまま試験できる
 * — tools/tests/ring3_str_host.c。 */
const char *ring3_user_str(int in_syscall, char *scratch, u32 cap,
                           const char *src);

/* ======================================================================== */
/*  CPL=3 へ**書き込む**前の判定 (票 TASK_HAL_WIRING、Codex 往復 10)         */
/*                                                                          */
/*  OS32 は **CR0.WP = 0** で走る (arch/x86/arch_cpu.h。kernel/shlib.c が     */
/*  「カーネルからは読み取り専用ページにも書ける」ことに依存している)。      */
/*  そのため CPL=0 の KAPI ラッパは、アプリが渡した読み取り専用 USER ページ   */
/*  — 具体的には**共有ライブラリの .text/.rodata**、全アプリで同じ物理 —     */
/*  にも #PF を起こさずに書けてしまう。既存の早期検査 (`kapi_argptr` →       */
/*  `ring3_ptr_ok`) は**帯しか見ない**ので、ここは素通りする。               */
/*  (`exec/exec.c` の `ring3_ptr_ok` にあった「.text への書き込みは PTE が    */
/*   RO なのでハードウェアの #PF で捕まる」という注記は CR0.WP = 0 では誤り) */
/*                                                                          */
/*  判定の**純粋な部分**だけをここに置く。実際にどの番地がどう張られている    */
/*  かを引くのは exec/exec.c の `ring3_user_range_writable`。                */
/* ======================================================================== */

/* PTE の下位 12bit から「CPL=3 が書けるページか」を決める表。
 * present + RW + USER の**3 つとも**立っていなければ書いてはいけない。
 * (i386 の実効権限は PDE と PTE の論理積だが、呼び手が両方を AND してから
 *  渡す約束にしてここは 1 語だけ見る。) */
int ring3_pte_writable_ok(u32 pte_flags);

/* PDE (下位 12bit で足りる) から「この PDE の下を CPL=3 が書けるか」。
 *   - present + RW + USER が**3 つとも**要る。i386 の実効権限は PDE と PTE の
 *     論理積なので、PTE が RW + USER でも PDE が supervisor / RO なら
 *     アプリは書けない (Approve 後の注意 4)。
 *   - **PS (4MB ページ) は拒否** — その PDE の下に PT は無い。いまの OS32 に
 *     4MB PDE を作る経路は 1 つも無いので、見えたら表の読み違い (アプリ CR3 の
 *     まま歩いた等) を疑うべき状態。 */
int ring3_pde_walkable_ok(u32 pde_flags);

/* [p, p + len) と [base, end) が重なるか。len = 0 は重ならない。
 * **加算の桁あふれでも素通しにしない** (p + len が巻き戻ると、帯の外を
 * 指すポインタが「帯に重ならない」と答えてしまう)。 */
int ring3_range_overlaps(u32 p, u32 len, u32 base, u32 end);

/* ======================================================================== */
/*  ring3_guard_active — 「いまの KAPI 呼び出しは CPL=3 のアプリ由来か」     */
/*                        (票 TASK_KAPI_OUTPUT_GUARD の追補、2026-09-26)      */
/*                                                                          */
/*  ring3_user_range_ok / ring3_user_ranges_writable / tramp_copy の 3 つの   */
/*  門は、これまで `ring3_in_syscall` だけを見て「1 = アプリ由来」としていた。*/
/*  ところが WM (gshell、CPL=0 の常駐シェル) は**アプリの syscall の中でしか  */
/*  走らない** (契約 T8: X1 ハンドラ / X3 OP_WAIT / X4 ポンプ)。その間も      */
/*  `ring3_in_syscall` は 1 のままなので、WM が自分のスタックの MouseInfo を   */
/*  `mouse_poll` に渡すと「アプリの出力先」として PTE を見られ、シェル帯には  */
/*  USER が無いので拒否 → **アプリが kill** された (filer が窓も出さずに消え  */
/*  る、2026-09-26)。                                                        */
/*                                                                          */
/*  そこで「カーネルが WM のコードへ入っている深さ」(`ring3_wm_depth`、      */
/*  gui_call / ポンプ / owner_exit の入口で +1、出口で -1) を渡し、          */
/*  **深さが 0 のときだけ** ガードを効かせる。                                */
/*                                                                          */
/*  安全性 (**門はポインタの由来で決める**):                                 */
/*   - **WM が選んだポインタは素通し** — 深さが 1 以上なのはカーネルが WM の  */
/*     ハンドラ / ポンプを同期的に実行しているあいだで、gshell が契約 T1 を   */
/*     守る限り (OWNER_EXIT ハンドラから exec_start / exec_resume を呼ばない) */
/*     その間 CPL=3 は走らない。呼んでも入れ子の syscall はディスパッチャの   */
/*     入口で深さ 0 に戻るので門の穴にはならない (戻った後の対の崩れは        */
/*     ring3_wm_depth_underflow が数える)。KAPI に渡るポインタは WM が選ぶ    */
/*     (gshell は `arg` をポインタとして解釈しない — 入力は SHM のスロット経由)。*/
/*   - **アプリが登録したポインタは深さに関係なく歩く** — 前に控えたアプリの  */
/*     ポインタ (fd_redirect_to_buffer のバッファ) を WM の文脈で書くときは、  */
/*     この判定を通さず ring3_user_ranges_writable_always で表を歩く          */
/*     (fs/fd_redirect.c の user_origin、2026-09-26 代行レビュー P2)。         */
/*  longjmp で WM を抜けた (park / kill) ときの深さの立ち直しは exec/exec.c   */
/*  (ディスパッチャの入口で 0 に戻す)。                                       */
/*                                                                          */
/*  `ring3_in_syscall` の意味 (#PF/#GP の帰属) は**変えない** — WM の中で     */
/*  落ちたときの扱いは従来どおり。純関数なのでホストで表を固定する            */
/*  (tools/tests/ring3_guard_host.c)。                                       */
/*  戻り値: 1 = ガードを効かせる (CPL=3 由来) / 0 = 常駐側の直呼び扱い。       */
/* ======================================================================== */
int ring3_guard_active(int in_syscall, int wm_depth);

#endif /* __RING3_STR_H */
