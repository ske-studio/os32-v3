/* ======================================================================== */
/*  MAIN.C — OS32 外部シェル エントリ・コマンドルーターロジック         */
/* ======================================================================== */
#include "shell.h"
#include "config.h"
#include "os32/help.h"
#include <stdio.h>

KernelAPI *g_api;

/* ------------------------------------------------------------------------ */
/*  `exit [N]` の終了要求 (票 TASK_EXIT_STATUS §2-3 / §2-5)                  */
/*                                                                          */
/*  **両ビルドで持つ**。以前は sh.bin だけが持っていたので、常駐では          */
/*  `exit 3 | echo tail` の後段が走っていた (往復 2 所見 2)。要求と値を       */
/*  別の変数にしてあるので `exit 0` でも要求が立つ。                          */
/*    sh.bin : shell_run の外側ループが見て端末を閉じる (D2(d))              */
/*    常駐   : 終わらない。いちばん外側の execute_command が 1 行で下ろす    */
/* ------------------------------------------------------------------------ */
int sh_exit_flag = 0;
int sh_exit_code = 0;

/* `$?`。環境変数表には入れない (子に継承させない、票 §2-4)。 */
static int g_last_status = SH_STATUS_OK;

int sh_status_get(void) { return g_last_status; }
void sh_status_set(int status) { g_last_status = status; }

/* 票 §2-3 の写像表。**種別だけ**で決める — 値から種別を作らない。 */
int sh_status_from_kind(int kind, int code)
{
    switch (kind) {
    case EXEC_KIND_EXITED:
        /* 0〜255 に丸める。負値 (exit(-1) = 255 など) も下位 8 ビットで
         * そのまま識別できる。 */
        return code & 0xFF;
    case EXEC_KIND_FAULT:     return SH_STATUS_FAULT;
    case EXEC_KIND_ABORTED:   return SH_STATUS_ABORTED;
    case EXEC_KIND_NOT_FOUND: return SH_STATUS_NOTFOUND;
    case EXEC_KIND_INVALID:   return SH_STATUS_NOEXEC;
    default:                  return SH_STATUS_NOEXEC;   /* NOMEM / FULL / 他 */
    }
}

/* `exit [N]` の引数。0 = 決まった / -1 = 不正 (呼び手は実行せずに 2)。 */
int sh_exit_arg(int argc, char **argv, int *code)
{
    const char *s;
    int v = 0;
    int digits = 0;

    if (argc < 2) { *code = sh_status_get(); return 0; }
    if (argc > 2) {
        g_api->kprintf(ATTR_RED, "%s", "exit: too many arguments\n");
        return -1;
    }
    for (s = argv[1]; *s; s++) {
        if (*s < '0' || *s > '9') {
            g_api->kprintf(ATTR_RED, "exit: %s: numeric argument required\n",
                           argv[1]);
            return -1;
        }
        v = v * 10 + (*s - '0');
        digits++;
        if (v > 255) {
            g_api->kprintf(ATTR_RED, "exit: %s: out of range (0-255)\n",
                           argv[1]);
            return -1;
        }
    }
    if (digits == 0) {
        g_api->kprintf(ATTR_RED, "%s", "exit: numeric argument required\n");
        return -1;
    }
    *code = v;
    return 0;
}

#ifdef SHELL_AS_APP
/* B2: sys_ls の写し取り。glob (このファイル) と ls (cmd_dir.c) が使う。 */
#include "sh_ls.inc"
#endif

static ShellCmd g_cmds[MAX_CMDS];
static int g_cmd_count = 0;

/* タブ補完用 (ui.c から参照される) */
const char *cmd_names[MAX_CMDS + 1];

/* ======================================================================== */
/*  エントリポイント (ファイルの最初にあること)                              */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *api)
{
    g_api = api;

    /* stdout は常に行バッファ。newlib は最初の出力時に isatty で
     * バッファ方式を一度だけ決めるので、最初の printf がリダイレクト中に
     * 走るとその後ずっと全バッファになり出力がコンソールへ遅れて漏れる */
    setvbuf(stdout, (char *)0, _IOLBF, BUFSIZ);

    /* 環境変数の初期化 (コマンド登録より先に) */
    env_init();

    /* 各モジュールのコマンド登録 */
    shell_cmd_base_init();
    shell_cmd_file_init();
    shell_cmd_dir_init();
    shell_cmd_mnt_init();
    shell_cmd_sys_init();
    shell_cmd_pci_init();
    shell_cmd_hdprep_init();
#ifndef SHELL_AS_APP
    /* D2(a): sh.bin はシリアル / rshell を持たない。登録もしないので
     * `serial` `terminal` `rshell` `send` … は最初から表に載らない。 */
    shell_rshell_init();
#endif
    shell_cmd_env_init();
    shell_cmd_script_init();
    shell_cmd_filer_init();

    /* メインループ開始 (ui.c) */
    shell_run();
    /* D2(d): `exit N` の N を端末へ返す (常駐では sh_exit_code は 0 のまま —
     * いちばん外側の execute_command が 1 行ごとに要求を下ろすので、
     * shell_run は `exit` では抜けない)。 */
    return sh_exit_code;
}

/* ======================================================================== */
/*  ヘルパー関数は shell.h でマクロとして定義済み                             */
/* ======================================================================== */
/*  コマンド登録機構                                                        */
/* ======================================================================== */
void shell_register_cmds(const ShellCmd *cmds)
{
    while (cmds->name != 0) {
        if (g_cmd_count < MAX_CMDS) {
            g_cmds[g_cmd_count] = *cmds;
            cmd_names[g_cmd_count] = cmds->name;
            g_cmd_count++;
            cmd_names[g_cmd_count] = (const char *)0; /* 常に終端を付与 */
        }
        cmds++;
    }
}

const ShellCmd *shell_get_cmds(int *count)
{
    *count = g_cmd_count;
    return g_cmds;
}

void shell_print_help(const char *cmd_name)
{
    int count, i;
    const ShellCmd *cmds;

    /* manページを参照 */
    if (os32_help_show(cmd_name) == 0) return;

    /* man が無い内部コマンドは登録テーブルの usage を出す
     * (以前は `unset` / `cp` を引数なしで打つと "No manual entry" だけだった) */
    cmds = shell_get_cmds(&count);
    for (i = 0; i < count; i++) {
        if (str_eq(cmds[i].name, cmd_name)) {
            g_api->kprintf(ATTR_WHITE, "Usage: %s %s\n", cmds[i].name, cmds[i].usage);
            g_api->kprintf(ATTR_WHITE, "  %s\n", cmds[i].description);
            return;
        }
    }
    g_api->kprintf(ATTR_RED, "No manual entry for %s\n", cmd_name);
}

/* ======================================================================== */
/* ======================================================================== */
#include "sh_args.inc"

/* ======================================================================== */
/*  PATH管理                                                                */
/* ======================================================================== */
static char g_path[512] = SYS_DEFAULT_PATH;

const char *shell_get_path(void)
{
    const char *p = env_get("PATH");
    if (p) return p;
    return g_path; /* フォールバック */
}

/* ======================================================================== */
/*  コマンドの起動経路 (has_ext / has_slash / try_exec /                     */
/*  try_exec_from_path / sh_is_cui_only / run_cmd_internal) の実体。         */
/*  ホスト試験がそのまま #include できるように別ファイルにしてある            */
/*  (tools/tests/sh_truncation_host.c)。並びは切り出す前と同一。             */
/* ======================================================================== */
#include "sh_exec.inc"

/* ======================================================================== */
/*  リダイレクト演算子の解析・適用                                           */
/*                                                                          */
/*  argv配列からリダイレクト演算子を検出し、カーネルFDリダイレクトを設定。    */
/*  検出した演算子とそのオペランドをargvから除去して残りのargcを返す。        */
/*                                                                          */
/*  対応構文:                                                               */
/*    cmd > file     stdout を file に上書き                                */
/*    cmd >> file    stdout を file に追記                                  */
/*    cmd < file     stdin を file から読み込み                             */
/*    cmd 2> file    stderr を file に書き込み                              */
/*    cmd > file 2>&1   stdout+stderr を file に                           */
/* ======================================================================== */
static int apply_redirects(int argc, char **argv)
{
    int i, out_idx;
    int new_argc = 0;
    char *new_argv[MAX_ARGS];

    for (i = 0; i < argc; i++) {
        /* ">>" 追記リダイレクト */
        if (argv[i][0] == '>' && argv[i][1] == '>') {
            const char *target;
            if (argv[i][2] != '\0') {
                /* ">>file" (スペースなし) */
                target = &argv[i][2];
            } else if (i + 1 < argc) {
                /* ">> file" */
                target = argv[++i];
            } else {
                g_api->kprintf(ATTR_RED, "%s", "syntax error: missing redirect target\n");
                return -1;
            }
            if (g_api->sys_redirect_fd(1, target, FD_REDIR_APPEND) < 0) {
                g_api->kprintf(ATTR_RED, "redirect: cannot open %s\n", target);
                return -1;
            }
            sh_redirect_mark();
            continue;
        }

        /* ">" 出力リダイレクト (上書き) */
        if (argv[i][0] == '>' && argv[i][1] != '>') {
            const char *target;
            if (argv[i][1] != '\0') {
                target = &argv[i][1];
            } else if (i + 1 < argc) {
                target = argv[++i];
            } else {
                g_api->kprintf(ATTR_RED, "%s", "syntax error: missing redirect target\n");
                return -1;
            }
            if (g_api->sys_redirect_fd(1, target, FD_REDIR_WRITE) < 0) {
                g_api->kprintf(ATTR_RED, "redirect: cannot open %s\n", target);
                return -1;
            }
            sh_redirect_mark();
            continue;
        }

        /* "<" 入力リダイレクト */
        if (argv[i][0] == '<') {
            const char *target;
            if (argv[i][1] != '\0') {
                target = &argv[i][1];
            } else if (i + 1 < argc) {
                target = argv[++i];
            } else {
                g_api->kprintf(ATTR_RED, "%s", "syntax error: missing redirect target\n");
                return -1;
            }
            if (g_api->sys_redirect_fd(0, target, FD_REDIR_READ) < 0) {
                g_api->kprintf(ATTR_RED, "redirect: cannot open %s\n", target);
                return -1;
            }
            sh_redirect_mark();
            continue;
        }

        /* "2>" stderr リダイレクト */
        if (argv[i][0] == '2' && argv[i][1] == '>') {
            const char *target;
            /* "2>&1" — stderr を stdout と同じ先に */
            if (argv[i][2] == '&' && argv[i][3] == '1') {
                /* stdout がリダイレクト済みなら stderr も同じファイルに */
                /* 簡易実装: 2>&1 は無視 (stdout と stderr は同じコンソール) */
                continue;
            }
            if (argv[i][2] != '\0') {
                target = &argv[i][2];
            } else if (i + 1 < argc) {
                target = argv[++i];
            } else {
                g_api->kprintf(ATTR_RED, "%s", "syntax error: missing redirect target\n");
                return -1;
            }
            if (g_api->sys_redirect_fd(2, target, FD_REDIR_WRITE) < 0) {
                g_api->kprintf(ATTR_RED, "redirect: cannot open %s\n", target);
                return -1;
            }
            sh_redirect_mark();
            continue;
        }

        /* 通常の引数 — 保持 */
        new_argv[new_argc++] = argv[i];
    }

    /* リダイレクト演算子を除去した argv を再構築 */
    for (out_idx = 0; out_idx < new_argc; out_idx++) {
        argv[out_idx] = new_argv[out_idx];
    }
    argv[new_argc] = (char *)0;

    return new_argc;
}

/* リダイレクト状態のリセット */
static void reset_all_redirects(void)
{
    g_api->sys_reset_redirect(0);
    g_api->sys_reset_redirect(1);
    g_api->sys_reset_redirect(2);
    sh_redirect_clear();
}

#ifdef SHELL_AS_APP
/* そのコマンド名が内蔵コマンド (または .bat / .sh スクリプト) か。
 * 外部コマンドは要求表経由で WM が起こす**別アプリ**になるので、sh 自身の
 * FD に掛けたリダイレクト / パイプは届かない。 */
static int sh_name_is_builtin(const char *name)
{
    int j;
    if (name[0] == '\0') return 1;   /* 空段は execute_single が黙って捨てる */
    if (has_ext(name, ".bat") || has_ext(name, ".sh")) return 1;
    for (j = 0; j < g_cmd_count; j++) {
        if (str_eq(name, g_cmds[j].name)) return 1;
    }
    return 0;
}

/* パイプの 1 段 (split_pipeline が前後の空白を落とした文字列) の先頭語を見る */
static int sh_stage_is_builtin(const char *seg)
{
    char name[PATH_MAX_LEN];
    int n = 0;

    while (*seg == ' ') seg++;
    while (*seg && *seg != ' ' && *seg != '<' && *seg != '>' &&
           n < PATH_MAX_LEN - 1) {
        name[n++] = *seg++;
    }
    name[n] = '\0';
    return sh_name_is_builtin(name);
}

/* argv にリダイレクト演算子が混じっているか (apply_redirects が見る形と同じ)。
 * リダイレクトを**張る前**に呼ぶこと — 張ってしまうと、外部段を断った後も
 * 親のリダイレクト表を子が閉じる余地が残る (表は全アプリ共有)。 */
static int sh_has_redirect(int argc, char **argv)
{
    int i;
    for (i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '>' || a[0] == '<') return 1;
        if (a[0] == '2' && a[1] == '>') {
            /* "2>&1" は apply_redirects が黙って捨てるだけ (FD を開かない) */
            if (a[2] == '&' && a[3] == '1') continue;
            return 1;
        }
    }
    return 0;
}
#endif

/* ======================================================================== */
/*  コマンド実行エンジン (単一コマンド)                                       */
/* ======================================================================== */
static int execute_single(const char *cmd)
{
    static char tmp_buf[CMD_BUF_SIZE];
    static char *argv[MAX_ARGS];
    static char *allocated_strings[MAX_ARGS];
    int alloc_count = 0;
    int argc = 0, j;
    char *p;
    const char *src;
    /* 票 §2-3「空行・コメントだけの行は `$?` を **変えない**」。何も実行
     * しなかった段はここで前の値を返すので、呼び手が上書きしても同じ値になる。*/
    int status = sh_status_get();

    /* T13: 空行と長大行を同じ扱いにしない。空行は今までどおり黙って戻り、
     * 収まらない行は **断る** (黙って消すと入力が無かったことになる)。 */
    if (strlen(cmd) == 0) return status;
    if (strlen(cmd) >= CMD_BUF_SIZE) {
        sh_refuse("sh: command", CMD_BUF_SIZE - 1);
        return SH_STATUS_USAGE;
    }

    src = cmd;
    p = tmp_buf;
    while (*src) { *p++ = *src++; }
    *p = '\0';

    /* I1: 引数が多すぎる行は一部だけ実行せず丸ごと捨てる */
    if (parse_args_and_glob(tmp_buf, argv, &argc, MAX_ARGS,
                            allocated_strings, &alloc_count) < 0) {
#ifdef SHELL_AS_APP
        sh_glob_failed = 0;
#endif
        for (j = 0; j < alloc_count; j++) {
            g_api->mem_free(allocated_strings[j]);
        }
        return SH_STATUS_USAGE;
    }

#ifdef SHELL_AS_APP
    /* R4: 一致が多すぎて glob を諦めた行は、一部だけ展開して実行しない */
    if (sh_glob_failed) {
        sh_glob_failed = 0;
        for (j = 0; j < alloc_count; j++) {
            g_api->mem_free(allocated_strings[j]);
        }
        return SH_STATUS_USAGE;
    }
#endif

    if (argc > 0) {
#ifdef SHELL_AS_APP
        /* B3: 標準 FD のリダイレクト表は全アプリ共有で read/write/reset が
         * owner を見ないので、外部コマンド (別アプリ) に掛けると出力が親の
         * ファイルへ入り、子の reset が親の FD を閉じる。リダイレクトを
         * **張る前**に断る。内蔵コマンドは sh 自身の文脈で完結するので従来どおり。 */
        if (sh_has_redirect(argc, argv) && !sh_name_is_builtin(argv[0])) {
            g_api->kprintf(ATTR_RED, "%s",
                           "sh: redirect to external command is not supported\n");
            for (j = 0; j < alloc_count; j++) {
                g_api->mem_free(allocated_strings[j]);
            }
            return SH_STATUS_NOEXEC;
        }
#endif
        /* リダイレクト演算子の解析・適用 */
        argc = apply_redirects(argc, argv);
        if (argc < 0) {
            /* 構文エラー / リダイレクト先が開けない — handler へ届かない
             * (票 §2-3 の表)。 */
            status = SH_STATUS_USAGE;
        } else if (argc > 0) {
            status = run_cmd_internal(argc, argv);
        } else {
            /* リダイレクトだけの行 (argc == 0)。開けたのだから 0 (受入 S13)。*/
            status = SH_STATUS_OK;
        }
    }

    /* newlib の stdout バッファをここで吐き出す。リダイレクト解除後に
     * 遅れて flush されると、ファイルに入るはずの出力が次のコマンドの
     * コンソールに混ざる (`ls > file` の後半が欠ける原因の一つ) */
    fflush(stdout);

    for (j = 0; j < alloc_count; j++) {
        g_api->mem_free(allocated_strings[j]);
    }
    return status;
}

/* ======================================================================== */
/*  パイプライン実行エンジン                                                 */
/*                                                                          */
/*  "cmd1 | cmd2 | cmd3" をシーケンシャルに実行:                              */
/*    1. cmd1 の stdout → パイプバッファA に蓄積                             */
/*    2. cmd2 の stdin ← バッファA, stdout → パイプバッファB に蓄積          */
/*    3. cmd3 の stdin ← バッファB, stdout → コンソール                      */
/* ======================================================================== */
#define MAX_PIPE_STAGES 8

/* T5: 段数 (> 0) / -1 = 行ごと断った。
 *
 *  以前は 9 段目以降と空の段を **黙って捨てて** いたので、実行されない段の
 *  失敗が見えなかった。捨てずに断る:
 *    - 段が max_stages を超える      (`a|b|c|d|e|f|g|h|i`)
 *    - 空の段がある                  (`echo ok |` / `| echo` / `a || b`)
 *    - 1 段が seg_size に収まらない  (行全体が CMD_BUF_SIZE 未満なので実際に
 *                                     は届かないが、規則としては同じ)
 *
 *  クォートは **今までどおり見ない**。したがって `echo "a||b"` の中の `|` も
 *  区切りのままで、空の段として断られる。クォートを見る分割は票 §6 で
 *  範囲外と決めてあるので、ここでは分割の規則を変えない。 */
static int split_pipeline(const char *cmd, char *seg_buf, int seg_size, int max_stages)
{
    int count = 0;
    int pos = 0;
    const char *p = cmd;
    char *seg;

    for (;;) {
        if (count >= max_stages) {
            sh_refuse("sh: pipeline", max_stages);
            return -1;
        }
        seg = seg_buf + count * seg_size;
        while (*p == ' ') p++;
        pos = 0;
        while (*p && *p != '|') {
            if (pos >= seg_size - 1) {
                sh_refuse("sh: pipeline stage", seg_size - 1);
                return -1;
            }
            seg[pos++] = *p++;
        }
        while (pos > 0 && seg[pos - 1] == ' ') pos--;
        seg[pos] = '\0';
        if (pos == 0) {
            /* 上限ではなく「段が空」なので sh_refuse の書式には乗らない。
             * 赤字 1 行 + 印だけ立てる (env_expand の断りと同じ形)。 */
            g_api->kprintf(ATTR_RED, "%s", "sh: empty pipeline stage\n");
            sh_refuse_mark();
            return -1;
        }
        count++;
        if (*p != '|') break;
        p++;
    }
    return count;
}


/* ======================================================================== */
/*  「切り詰めたので行を断った」印 (票 TASK_SH_TRUNCATION §2-1)              */
/*                                                                          */
/*  規則と寿命は shell.h の宣言のところに書いてある。ここは実体だけ。        */
/* ======================================================================== */
int sh_refused_flag = 0;

/* execute_command の入れ子の深さ。if / time が組み立てた行やパイプの段から      */
/* 呼ばれた execute_command は印を消さない — 消すと内側の断りが外へ届かない。 */
static int g_exec_depth = 0;

void sh_refuse_mark(void)
{
    sh_refused_flag = 1;
}

void sh_refuse(const char *what, int limit)
{
    /* 「何が上限を超えたか」と「上限」を赤字 1 行で (票 §2 の 2)。
     * 上限は呼び手が定数から渡す ([C4])。 */
    g_api->kprintf(ATTR_RED, "%s too long (max %d)\n", what, limit);
    sh_refused_flag = 1;
}

int sh_refused_take(void)
{
    int r = sh_refused_flag;
    sh_refused_flag = 0;
    return r;
}

/* 読むだけ — **消さない**。パイプの段ループが「この段で断ったか」を見るのに
 * 使う (票 §2 の「行全体を実行しない」)。ここで take してしまうと、断りが
 * script_exec まで届かず後続の**行**が走る。 */
int sh_refused_peek(void)
{
    return sh_refused_flag;
}

/* ======================================================================== */
/*  公開API: execute_command                                                 */
/* ======================================================================== */
static int execute_command_line(const char *cmd)
{
    static char expanded_buf[CMD_BUF_SIZE];
    const char *src;
    int has_pipe = 0;
    /* 空行・コメントだけの行は `$?` を変えない (票 §2-3 の表)。 */
    int status = sh_status_get();

    /* T13: 空行と長大行を同じ扱いにしない (execute_single と同じ規則)。 */
    if (strlen(cmd) == 0) return status;
    if (strlen(cmd) >= CMD_BUF_SIZE) {
        sh_refuse("sh: command line", CMD_BUF_SIZE - 1);
        return SH_STATUS_USAGE;
    }

    /* $VAR / ~ 展開 */
    /* I-2: 展開しきれない行は**切れたまま実行しない**
     * T9: 変数名が ENV_NAME_MAX に収まらない行も同じ扱い。以前は 31 文字で
     *     打ち切って残り (と `}`) をリテラルとして素通しし、展開されない
     *     文字列がコマンド行に混ざっていた。理由が分かるよう文言を分ける。 */
    {
        int er = env_expand(cmd, expanded_buf, CMD_BUF_SIZE);
        if (er == ENV_EXPAND_ERR_NAME) {
            sh_refuse("sh: variable name", ENV_NAME_MAX - 1);
            return SH_STATUS_USAGE;
        }
        if (er < 0) {
            g_api->kprintf(ATTR_RED, "%s", "sh: line too long after expansion\n");
            /* §2-1: これも「断った行」— スクリプト中なら後続行へ落とさない */
            sh_refuse_mark();
            return SH_STATUS_USAGE;
        }
    }
    src = expanded_buf;

    /* パイプの有無を判定 */
    {
        const char *c = src;
        while (*c) { if (*c == '|') { has_pipe = 1; break; } c++; }
    }

    if (!has_pipe) {
        /* パイプなし: 単一コマンド実行 */
        status = execute_single(src);
        reset_all_redirects();
        /* 往復 5 の注意 1: `exit` の handler は値を書いてから要求を立てる。
         * 要求が立っていたらその値が行の値 (`exit 0` でも 0 が入る)。 */
        if (sh_exit_flag) status = sh_exit_code;
        return status;
    }

    /* パイプあり: パイプライン実行 */
    {
        char *seg_buf;
        int stage_count;
        int i;
        int cur_buf, prev_buf;

        /* セグメントバッファを動的確保 */
        seg_buf = (char *)g_api->mem_alloc(MAX_PIPE_STAGES * CMD_BUF_SIZE);
        if (!seg_buf) {
            g_api->kprintf(ATTR_RED, "%s", "pipe: out of memory\n");
            return SH_STATUS_USAGE;
        }

        stage_count = split_pipeline(src, seg_buf, CMD_BUF_SIZE, MAX_PIPE_STAGES);
        /* T5: 分割の時点で断ったら段を 1 つも実行しない (印は split_pipeline
         * が立てている)。パイプバッファはまだ 1 つも取っていない。 */
        if (stage_count < 0) {
            g_api->mem_free(seg_buf);
            return SH_STATUS_USAGE;
        }
        if (stage_count <= 1) {
            status = execute_single(seg_buf);
            reset_all_redirects();
            g_api->mem_free(seg_buf);
            if (sh_exit_flag) status = sh_exit_code;
            return status;
        }

#ifdef SHELL_AS_APP
        /* 外部段が 1 つでもあれば、その段の出力は sh の FD を通らない */
        for (i = 0; i < stage_count; i++) {
            if (!sh_stage_is_builtin(seg_buf + i * CMD_BUF_SIZE)) {
                g_api->kprintf(ATTR_RED, "%s",
                               "sh: pipe to external command is not supported\n");
                g_api->mem_free(seg_buf);
                return SH_STATUS_NOEXEC;
            }
        }
#endif

        /* バッファID: 交互使用 (0, 1, 0, 1, ...) */
        prev_buf = -1;
        {
            u32 saved_len = 0;
            /* パイプバッファを事前確保 (最大2つ: 交互使用) */
            int alloc_buf[2];
            int num_alloc = (stage_count > 2) ? 2 : 1;
            int ai;
            for (ai = 0; ai < num_alloc; ai++) {
                alloc_buf[ai] = sh_pipe_alloc();
                if (alloc_buf[ai] < 0) {
                    g_api->kprintf(ATTR_RED, "%s", "pipe: buffer alloc failed\n");
                    /* 確保済みを解放 */
                    {
                        int aj;
                        for (aj = 0; aj < ai; aj++) {
                            sh_pipe_free(alloc_buf[aj]);
                        }
                    }
                    g_api->mem_free(seg_buf);
                    return SH_STATUS_USAGE;
                }
            }

            sh_pipeline_enter();
            for (i = 0; i < stage_count; i++) {
                int is_first = (i == 0);
                int is_last = (i == stage_count - 1);

                /* stdin のリダイレクト (最初以外) */
                if (!is_first && prev_buf >= 0) {
                    u8 *buf = sh_pipe_get_buf(prev_buf);
                    /* バッファが消えていたら黙って続けない。以前はここが NULL
                     * (前段の exec_exit がパイプを回収していた) でも続行し、
                     * 次段が stdin をキーボードから読んでハングした */
                    if (!buf || g_api->sys_redirect_fd_buf(0, buf, PIPE_BUF_SIZE, saved_len) < 0) {
                        g_api->kprintf(ATTR_RED, "%s", "pipe: stdin buffer lost\n");
                        status = SH_STATUS_USAGE;
                        break;
                    }
                }

                /* stdout のリダイレクト (最後以外) */
                if (!is_last) {
                    cur_buf = alloc_buf[i % num_alloc];
                    {
                        u8 *buf = sh_pipe_get_buf(cur_buf);
                        if (!buf || g_api->sys_redirect_fd_buf(1, buf, PIPE_BUF_SIZE, 0) < 0) {
                            g_api->kprintf(ATTR_RED, "%s", "pipe: stdout buffer lost\n");
                            reset_all_redirects();
                            status = SH_STATUS_USAGE;
                            break;
                        }
                    }
                }

                /* コマンド実行。パイプラインの値は **最後の段の値**
                 * (明示の `exit` が立てばそちらが優先、往復 2 所見 2)。 */
                status = execute_single(seg_buf + i * CMD_BUF_SIZE);

                /* stdout バッファに書き込まれたデータ長を保存 (リセット前に取得) */
                if (!is_last) {
                    saved_len = g_api->sys_redirect_get_buf_len(1);
                }

                /* リダイレクト解除 */
                reset_all_redirects();

                /* 前段のバッファIDを記録 */
                if (!is_last) {
                    prev_buf = cur_buf;
                }

                /* 票 §2「切り詰めたら行全体を実行しない」— 段で断りの印が
                 * 立ったら、**後続の段を実行せずに行を終える**。
                 *
                 * bash の `false | cat` に寄せて段を続けると、
                 * `<断られる段> | tee 重要ファイル` のように**断ったのに
                 * 後段の書き込みが起きる**。後段の `> file` は
                 * apply_redirects が O_TRUNC で開くので、リダイレクト先が
                 * 空で上書きされる。
                 *
                 * 印は**消さない** (§2-1)。消すのはいちばん外側の
                 * execute_command の入口だけで、スクリプト中ならこの行の
                 * 後で script_exec が打ち切る。抜けた後の後始末
                 * (reset_all_redirects / sh_pipe_free / mem_free) は
                 * ループの外と上でそのまま通る。 */
                /* B6 / 往復 2 所見 2: 段の途中で `exit` が立ったらそこで
                 * 打ち切る。**両ビルドで見る** — 以前は sh.bin だけで、しかも
                 * 段の**入口**で見ていたので、常駐では `exit 3 | echo tail` の
                 * 後段が走っていた。見張りはこの 1 か所だけにしてある (入口にも
                 * 置くと、片方を壊しても挙動が変わらず変異に歯が立たない)。
                 *
                 * 往復 5 の注意 1: 値を書いてから要求を立てる — ここで status を
                 * 上書きしておけば、段ループを抜けた後もその値が残る。 */
                if (sh_exit_flag) { status = sh_exit_code; break; }

                if (sh_refused_peek()) { status = SH_STATUS_USAGE; break; }
            }

            sh_pipeline_leave();

            /* パイプバッファを解放 */
            for (ai = 0; ai < num_alloc; ai++) {
                sh_pipe_free(alloc_buf[ai]);
            }
        }
        g_api->mem_free(seg_buf);
    }
    return status;
}

/* 実体は execute_command_line。ここは「断った印」の寿命を 1 行に閉じるための
 * 薄い包み (票 TASK_SH_TRUNCATION §2-1)。
 *
 * 入口で消すのは **いちばん外側** の呼び出しだけ。`if` / `time` が組み立てた
 * 行はこの関数を入れ子で呼ぶので、そこで消すと内側で断ったことが
 * script_exec まで届かなくなる (取りこぼし)。パイプの段は execute_single を
 * 直に呼ぶので入れ子にはならないが、段の中の `time ...` が入れ子になる。 */
int execute_command(const char *cmd)
{
    int status;

    if (g_exec_depth == 0) sh_refused_flag = 0;
    g_exec_depth++;
    status = execute_command_line(cmd);
    g_exec_depth--;

#ifndef SHELL_AS_APP
    /* 常駐シェルは `exit` で終わらない (票 §2-5)。要求は 1 行ぶんで下ろす —
     * 値は下の sh_status_set で `$?` に入るので失われない。sh.bin では
     * shell_run の外側ループが見るので下ろさない。 */
    if (g_exec_depth == 0) sh_exit_flag = 0;
#endif

    sh_status_set(status);
    return status;
}
