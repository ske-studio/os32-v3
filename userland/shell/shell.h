/* ======================================================================== */
/*  SHELL.H — OS32 外部シェル 共通ヘッダ                                     */
/* ======================================================================== */
#ifndef SHELL_H
#define SHELL_H

#include "os32api.h"
#include <string.h>

#define str_eq(a, b) (strcmp((a), (b)) == 0)
#define str_startswith(a, b) (strncmp((a), (b), strlen(b)) == 0)
#define str_len(a) strlen(a)

/* ------------------------------------------------------------------------ */
/*  構造体・型定義                                                          */
/* ------------------------------------------------------------------------ */

#define CMD_BUF_SIZE 4096
#define HIST_LINE_MAX 512
#define MAX_ARGS OS32_MAX_ARGS
#define MAX_CMDS 128
#define PATH_MAX_LEN OS32_MAX_PATH

/* スクリプトエンジン定数 */
#define SCRIPT_MAX_LINES  128   /* スクリプト最大行数 */
#define SCRIPT_MAX_LINE   256   /* 1行の最大長 */
#define SCRIPT_MAX_DEPTH  4     /* source ネスト上限 */

/* `if VAL1 == VAL2` の比較値の幅 (cmd_script.c)。実効は IF_VALUE_MAX - 1。
 * [C4] 断りのメッセージにもこの定数から上限を出す。 */
#define IF_VALUE_MAX      256

/* 組み立てた行の幅。実効長はコードのループが見る値と同じにしてある —
 * [C4] のとおり断りのメッセージにもここから上限を出す (票 §5 の段 3)。
 *   TIME_CMD_MAX      cmd_base.c の `time` (cmd_buf[512]、実効 510)
 *   EXEC_CMDLINE_MAX  cmd_mnt.c の `exec` (cmdline[256]、実効 255)
 *   GLOB_PATTERN_MAX  sh_args.inc の glob パターン部 (実効 255) */
#define TIME_CMD_MAX      512
#define EXEC_CMDLINE_MAX  256
#define GLOB_PATTERN_MAX  256

/* 環境変数の幅 (cmd_env.c)。実効は名前 31 / 値 255。cmd_script.c の `ask` と
 * main.c の展開エラーの文言もここから上限を出す ([C4]、票 §5 の段 4)。 */
#define ENV_NAME_MAX      32
#define ENV_VALUE_MAX     256

/* `ask` のプロンプト / 入力の幅 (cmd_script.c)。実効はどちらも MAX - 2。 */
#define ASK_PROMPT_MAX    256
#define ASK_INPUT_MAX     256

/* rshell (rshell.c)。1 行の受信バッファ幅 (実効 RSHELL_LINE_MAX - 2) と、
 * `host:` を /host/ へ直したパスの幅 (実効 - 1)。 */
#define RSHELL_LINE_MAX       128
#define RSHELL_HOST_PATH_MAX  256

/* env_expand の戻り値。負はすべて「展開できなかった」で、-2 は
 * **変数名が ENV_NAME_MAX に収まらなかった**ことを表す (T9)。
 * -1 は行が dst に収まらなかった (I-2 の従来の意味)。 */
#define ENV_EXPAND_ERR_NAME (-2)

/* script_source_file の戻り値 (票 TASK_EXIT_STATUS §2-5-1 で確定):
 *   >= 0                最後に実行した行の状態 (= source の `$?`)。
 *                       実行する行が 1 つも無ければ 0 (受入 S13)。
 *   -1                  開けない / 読めない / 確保できない / 深すぎる
 *   SCRIPT_ERR_REFUSED  行を断って打ち切った (票 TASK_SH_TRUNCATION §2-1)
 * 呼び手 (`source` / 暗黙の .sh / .bat) は負をまとめて SH_STATUS_USAGE に
 * 写し、SCRIPT_ERR_REFUSED のときだけ印を立て直す。 */
#define SCRIPT_ERR_REFUSED (-2)

/* ------------------------------------------------------------------------ */
/*  `$?` に入る値 (票 TASK_EXIT_STATUS §2-3 の写像表、[C4])                   */
/*                                                                          */
/*  数字は bash と同じ意味づけ。**印 (sh_refused_flag) とは役割が違う** —    */
/*  印は「実行を拒否したかどうか」の制御信号、こちらはその結果の状態値。      */
/* ------------------------------------------------------------------------ */
#define SH_STATUS_OK        0     /* 成功 */
#define SH_STATUS_ERROR     1     /* 実行はしたが失敗した (一般) */
#define SH_STATUS_USAGE     2     /* 実行しなかった (構文 / 引数 / 断り) */
#define SH_STATUS_NOEXEC    126   /* 見つかったが起こせなかった */
#define SH_STATUS_NOTFOUND  127   /* 見つからなかった */
#define SH_STATUS_ABORTED   130   /* CTRL+STOP / ESC で畳んだ (128 + 2) */
#define SH_STATUS_FAULT     139   /* 例外で畳んだ (128 + 11) */

/* コマンドハンドラ関数の型。**int** (決裁 E2) — 「赤字のエラーを出す分岐 =
 * 非 0」。取りこぼしは登録表の初期化子の型不一致で捕まる。 */
typedef int (*CmdHandler)(int argc, char **argv);

/* コマンド登録用構造体 */
typedef struct {
    const char *name;
    CmdHandler handler;
    const char *usage;
    const char *description;
} ShellCmd;

/* ------------------------------------------------------------------------ */
/*  グローバル変数 (main.cで定義)                                           */
/* ------------------------------------------------------------------------ */
extern KernelAPI *g_api;

/* ------------------------------------------------------------------------ */
/*  関数プロトタイプ                                                        */
/* ------------------------------------------------------------------------ */

/* コマンド登録機構 (main.c) */
void shell_register_cmds(const ShellCmd *cmds);

/* コマンド実行エンジン (main.c)。戻り値は `$?` に入る値。
 * 呼ぶたびに sh_status_set() も済ませてあるので、呼び手は戻り値を捨ててよい。 */
int execute_command(const char *cmd);

/* ------------------------------------------------------------------------ */
/*  `$?` — 直前のコマンドの状態 (main.c)                                     */
/*                                                                          */
/*  初期値は 0。環境変数表には**書かない** (子に継承させない) ので、          */
/*  `set ?=5` で作った変数があっても env_expand の特別扱いが先に効いて隠れる。*/
/* ------------------------------------------------------------------------ */
int  sh_status_get(void);
void sh_status_set(int status);

/* EXEC_KIND_* → `$?` の写像 (票 §2-3 の表)。1 か所にまとめてある [C4]。 */
int  sh_status_from_kind(int kind, int code);

/* ------------------------------------------------------------------------ */
/*  `exit [N]` の終了要求 (票 §2-3 / §2-5)                                   */
/*                                                                          */
/*  **「要求が立ったか」と「終了値」は別の変数** — 真偽値に値を入れる作りだと */
/*  `exit 0` で終われない。要求はパイプの段ループと script_exec が見る。      */
/*  sh.bin では shell_run の外側ループも見て端末を閉じる (D2(d))。            */
/*  常駐は終わらないので、いちばん外側の execute_command が 1 行ぶんで下ろす。*/
/* ------------------------------------------------------------------------ */
extern int sh_exit_flag;   /* 終了要求が立ったか */
extern int sh_exit_code;   /* そのときの終了値 */

/* `exit [N]` の引数を読む。0 = *code に値が入った / -1 = 不正 (実行しない)。
 * 引数なしは直前の `$?`。非数値・255 超・2 つ以上は -1 (呼び手は 2 を返す)。 */
int  sh_exit_arg(int argc, char **argv, int *code);

/* ------------------------------------------------------------------------ */
/*  「切り詰めたので行を断った」印 (票 TASK_SH_TRUNCATION §2-1、main.c)      */
/*                                                                          */
/*  切り詰めを見つけた側は sh_refuse() で赤字 1 行を出し、同時に印を立てる。 */
/*  組み込み handler は void のままなので (int 化は TASK_EXIT_STATUS の範囲)、*/
/*  断ったことはこのグローバル 1 本だけで伝える。                            */
/*                                                                          */
/*  印の寿命は **1 行ぶん**:                                                 */
/*    - いちばん外側の execute_command が入口で消す                          */
/*      (入れ子 = if / time が組み立てた行、パイプの段 では消さない。         */
/*       消すと内側の断りが外へ届かない)                                     */
/*    - script_exec が 1 行ごとに sh_refused_take() で読んで消し、            */
/*      立っていたらスクリプトを打ち切る (goto のラベル無しと同じ扱い)        */
/*    - 対話 / rshell は誰も読まないので、断った行の次の行は今までどおり動く  */
/*    - パイプの段ループは sh_refused_peek() で **読むだけ**。印が立って  */
/*      いたら後続の段を実行せずに行を終える (票 §2「行全体を実行しない」)  */
/* ------------------------------------------------------------------------ */
extern int sh_refused_flag;
void sh_refuse(const char *what, int limit);  /* 赤字 1 行 + 印 */
void sh_refuse_mark(void);                    /* 印だけ (伝播用) */
int  sh_refused_take(void);                   /* 読んで消す */
int  sh_refused_peek(void);                   /* 読むだけ (消さない) */
extern const char *cmd_names[];  /* タブ補完用 */
const ShellCmd *shell_get_cmds(int *count);
void shell_print_help(const char *cmd_name);

/* メインループ・UI制御 (ui.c) */
void shell_run(void);
void hist_save(void);
void hist_load(void);

/* 各モジュールの初期化関数 (コマンド登録用) */
void shell_cmd_base_init(void);
void shell_cmd_file_init(void);
void shell_cmd_dir_init(void);
void shell_cmd_mnt_init(void);
void shell_cmd_sys_init(void);
void shell_cmd_pci_init(void);   /* cmd_pci.c: lspci / pcidump */
void shell_cmd_hdprep_init(void); /* cmd_hdprep.c: hdprep (票 TASK_HDD_INSTALL 段 1) */
void shell_rshell_init(void);
void shell_cmd_env_init(void);
void shell_cmd_script_init(void);
void shell_cmd_filer_init(void);

/* 環境変数 (cmd_env.c) */
void env_init(void);
const char *env_get(const char *name);
void env_set(const char *name, const char *value);
void env_unset(const char *name);
int  env_expand(const char *src, char *dst, int max);

/* PATH検索 (main.c) */
const char *shell_get_path(void);

/* ------------------------------------------------------------------------ */
/*  sh_launch — 外部プログラムの唯一の起動口 (票 T9 D3a)                     */
/*                                                                          */
/*  常駐 shell.bin (SHELL_AS_APP 未定義) は従来どおり入れ子 exec_run。        */
/*  マクロなので展開後のトークンは以前の g_api->exec_run(...) と同一で、      */
/*  常駐のコード生成は 1 バイトも変わらない (受入 S7 の SHA-256 一致)。       */
/*                                                                          */
/*  sh.bin (SHELL_AS_APP) は入れ子 exec_run を使えない — その子は park でき   */
/*  ず協調型 GUI 全体が止まる (K5b D9-8)。代わりにカーネルの要求表に載せ、     */
/*  WM に起動してもらって sys_yield で譲りながら launch_poll で待つ。         */
/*  実体は sh_launch.inc (main.c が #include)。exec_run への参照はこのヘッダ  */
/*  の #else 側 1 か所だけ。                                                  */
/*                                                                          */
/*  **2026-09-16 (票 TASK_EXIT_STATUS §2-2)**: 起動口を sh_exec_result に     */
/*  まとめた。常駐は exec_run の**直後**に exec_last_result を読み、sh.bin は */
/*  要求表の状態を自分で写す (カーネルの記録は読まない — その子は自分の子とは */
/*  限らない)。この票で常駐のコード生成が変わるので、以前あった「SHA-256 が   */
/*  一致する」という注記は成り立たない。                                      */
/* ------------------------------------------------------------------------ */

/* 外部プログラムを起こし、結果を「種別 + 値」で返す (票 §2-2)。
 * 戻り値は従来の EXEC_* (互換のため残す)。呼び手が分岐に使うのは *kind だけ。
 *   常駐   : exec_run → 直後に exec_last_result (KAPI v55)
 *   sh.bin : 要求表の DONE / FAILED を写す (LAUNCH_ST_DONE → EXITED 0)
 * 起こさずに断った場合 (シェル側の組み立て失敗) は印 (sh_refused_flag) が
 * 立つので、呼び手は印を先に見ること — 種別は EXEC_KIND_NONE のままになる。 */
int sh_exec_result(const char *cmdline, int *kind, int *code);
#ifdef SHELL_AS_APP
int sh_launch(const char *cmdline);
/* B4: パイプの段を回している間だけ立てる入れ子カウンタ。sh_launch の入口で
 * 見て、外部プログラムの起動を断る (先頭語だけの事前判定では
 * `exec /bin/sh.bin | echo tail` や `source x.sh | echo tail` を通してしまう。
 * 最終起動口で確かめれば経路を問わず捕まえられる)。 */
void sh_pipeline_enter(void);
void sh_pipeline_leave(void);
/* R2: sh がリダイレクトを張っている間だけ立てる印。apply_redirects で立て、
 * reset_all_redirects で下ろす。sh_launch の入口で見て外部起動を断る —
 * 内蔵の `exec` / `if` / `time` / `source` はリダイレクトを張った**後**に
 * 外部へ行けてしまい、事前判定 (execute_single) をすり抜けるため。 */
void sh_redirect_mark(void);
void sh_redirect_clear(void);
#else
#define sh_launch(cmdline)  (g_api->exec_run(cmdline))
#define sh_pipeline_enter()  ((void)0)
#define sh_pipeline_leave()  ((void)0)
#define sh_redirect_mark()   ((void)0)
#define sh_redirect_clear()  ((void)0)
#endif

/* ------------------------------------------------------------------------ */
/*  B2: sys_ls のコールバックからは KAPI を呼ばない (SHELL_AS_APP)           */
/*                                                                          */
/*  CPL=3 で `sys_ls` のコールバックから KAPI (int 0x80) を呼ぶと落ちる      */
/*  (カーネル側の欠陥、別票)。名前と種別を写すだけのコールバックを使い、     */
/*  `sys_ls` が戻ってから表示 / mem_alloc を行う。実体は sh_ls.inc。         */
/* ------------------------------------------------------------------------ */
#ifdef SHELL_AS_APP
void sh_ls_reset(void);
void sh_ls_set_filter(int (*f)(const char *name));
/* R4: 一致が多すぎて glob を諦めた行の印 (execute_single が見て捨てる) */
extern int sh_glob_failed;
void sh_ls_collect_cb(const DirEntry_Ext *entry, void *ctx);
int  sh_ls_count_get(void);
int  sh_ls_dropped(void);
void sh_ls_fill(int i, DirEntry_Ext *out);
#endif

/* B5: 行末の 1 文字を画面からも消す (端末の BS は 1 セル左へ動くだけ)。
 * ui.c の行編集と cmd_script.c の `ask` が共有する。実体は sh_redraw.inc。
 * 常駐 (CUI) では console が BS で消すので何もしない。 */
#ifdef SHELL_AS_APP
void sh_erase_cells(char removed);
#else
#define sh_erase_cells(removed) ((void)0)
#endif

/* ------------------------------------------------------------------------ */
/*  パイプバッファの出どころ (Codex 往復 5 の blocker)                       */
/*                                                                          */
/*  常駐 (CPL=0) はカーネルの `sys_pipe_*` をそのまま使う — マクロなので      */
/*  展開後のトークンは以前と同一で、常駐のコード生成は変わらない。           */
/*  sh.bin (CPL=3) はカーネル帯のポインタを `sys_redirect_fd_buf` へ渡せない */
/*  (`ring3_ptr_ok` に落ちて fault kill) ので、sh 自身の .bss から配る。      */
/*  実体は sh_pipe.inc (main.c が #include)。                                */
/* ------------------------------------------------------------------------ */
#ifdef SHELL_AS_APP
int  sh_pipe_alloc(void);
u8  *sh_pipe_get_buf(int slot);
void sh_pipe_free(int slot);
#else
#define sh_pipe_alloc()        (g_api->sys_pipe_alloc())
#define sh_pipe_get_buf(slot)  (g_api->sys_pipe_get_buf(slot))
#define sh_pipe_free(slot)     (g_api->sys_pipe_free(slot))
#endif

/* ------------------------------------------------------------------------ */
/*  sh_gfx_restore — 子がグラフィクスを使った後の後始末                      */
/*                                                                          */
/*  CUI では子が VRAM を握ったまま戻ることがあるので表示をテキストへ戻す。   */
/*  GUI 中に画面を持っているのは WM で、CPL=3 の sh.bin が gfx_shutdown を    */
/*  呼ぶと (所有者検査が無いので) GUI ごと表示が止まる。だから sh.bin では    */
/*  何もしない。常駐側の展開は以前の g_api->gfx_shutdown() と同一トークン。  */
/* ------------------------------------------------------------------------ */
#ifdef SHELL_AS_APP
#define sh_gfx_restore() ((void)0)
#else
#define sh_gfx_restore() (g_api->gfx_shutdown())
#endif

/* スクリプトエンジン (cmd_script.c) */
int script_source_file(const char *path);

/* `set -e` / `set +e` (票 §2-5)。`set` は cmd_env.c に 1 本しか登録しない —
 * cmd_set の**先頭**で -e / +e を拾ってここへ流す (二重登録しない)。
 * 旗は入れ子の source を抜けるときに script_abort_flag と同じく save/restore。*/
void script_errexit_set(int on);
int  script_errexit_get(void);

/* いまスクリプトを実行中か (`exit` が打ち切るかどうかの判定)。 */
int  script_in_script(void);
/* 起動スクリプト (/etc/profile, $HOME/.profile) 用。断られても起動は止めず、
 * メッセージを出して既定値で続ける (票 TASK_SH_TRUNCATION §2-1 末尾 / R2)。 */
void script_source_profile(const char *path);

#endif /* SHELL_H */
