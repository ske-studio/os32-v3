/* ======================================================================== */
/*  CMD_ENV.C — OS32 シェル 環境変数管理モジュール                           */
/* ======================================================================== */
#include "shell.h"
#include "config.h"

/* ======================================================================== */
/*  環境変数テーブル                                                         */
/* ======================================================================== */

#define MAX_ENV_VARS 32
/* ENV_NAME_MAX / ENV_VALUE_MAX は shell.h ([C4])。`ask` と main.c の
 * 展開エラーの文言も同じ定数から上限を出すので 1 か所に置いてある。 */

typedef struct {
    char name[ENV_NAME_MAX];
    char value[ENV_VALUE_MAX];
    int  used;
} EnvVar;

static EnvVar env_vars[MAX_ENV_VARS];

/* ======================================================================== */
/*  内部ヘルパー                                                             */
/* ======================================================================== */

static int env_find(const char *name)
{
    int i;
    for (i = 0; i < MAX_ENV_VARS; i++) {
        if (!env_vars[i].used) continue;
        if (strcmp(name, env_vars[i].name) == 0) return i;
    }
    return -1;
}

static void str_copy(char *dst, const char *src, int max)
{
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

void env_init(void)
{
    int i;
    for (i = 0; i < MAX_ENV_VARS; i++) env_vars[i].used = 0;

    env_set("PATH",  SYS_DEFAULT_PATH);
    env_set("HOME",  SYS_DEFAULT_HOME);
    env_set("SHELL", SYS_DEFAULT_SHELL);
    env_set("USER",  "user");
}

const char *env_get(const char *name)
{
    int idx = env_find(name);
    if (idx < 0) return (const char *)0;
    return env_vars[idx].value;
}

void env_set(const char *name, const char *value)
{
    int idx;

    /* T8: 収まらない名前 / 値は**切って登録しない**。壊れた PATH が黙って
     * 入ると、その後の起動が全部別のディレクトリを見る (票 §2 の 1)。
     * 登録口はここ 1 本なので、set / export / ask / 直呼びの全部に効く。 */
    if ((int)strlen(name) > ENV_NAME_MAX - 1) {
        sh_refuse("set: variable name", ENV_NAME_MAX - 1);
        return;
    }
    if ((int)strlen(value) > ENV_VALUE_MAX - 1) {
        sh_refuse("set: variable value", ENV_VALUE_MAX - 1);
        return;
    }

    idx = env_find(name);
    if (idx >= 0) {
        str_copy(env_vars[idx].value, value, ENV_VALUE_MAX);
        return;
    }
    /* 新規追加 */
    {
        int i;
        for (i = 0; i < MAX_ENV_VARS; i++) {
            if (!env_vars[i].used) {
                env_vars[i].used = 1;
                str_copy(env_vars[i].name, name, ENV_NAME_MAX);
                str_copy(env_vars[i].value, value, ENV_VALUE_MAX);
                return;
            }
        }
    }
    g_api->kprintf(ATTR_RED, "%s", "env: table full\n");
}

void env_unset(const char *name)
{
    int idx = env_find(name);
    if (idx >= 0) env_vars[idx].used = 0;
}

/* $VAR / ${VAR} / ~ 展開 */
/* I-2: 戻り値 0 / 1 = 展開の有無、**負 = 収まらなかった**。以前は 4095B で
 * 黙って打ち切っていたので、`${PAD}` を並べた行で末尾の宛先が落ちたまま
 * 実行されていた。呼び手は負を見たら行ごと捨てること。 */
int env_expand(const char *src, char *dst, int max)
{
    int si = 0, di = 0;
    int expanded = 0;
    int truncated = 0;

    while (src[si] && di < max - 1) {
        if (src[si] == '~' && (si == 0 || src[si - 1] == ' ') &&
            (src[si + 1] == '\0' || src[si + 1] == '/' || src[si + 1] == ' ')) {
            /* チルダ展開 → $HOME */
            const char *home = env_get("HOME");
            if (home) {
                while (*home && di < max - 1) dst[di++] = *home++;
                if (*home) truncated = 1;
                expanded = 1;
            } else {
                dst[di++] = '~';
            }
            si++;
        } else if (src[si] == '$') {
            /* $VAR or ${VAR} 展開 */
            char var_name[ENV_NAME_MAX];
            int vi = 0;
            const char *val;
            int braced = 0;

            si++; /* '$' をスキップ */

            /* 票 §2-4: `$?` は**名前の走査より手前**、`{` の判定より手前で
             * 特別扱いする。だから `set ?=5` で作った変数があっても隠れるし、
             * 環境変数表には 1 バイトも書かない (子に継承させない)。
             * `${?}` は**対応しない** — 下の braced 経路へ落ちて空になる。
             * `$?x` は `0x` のように展開される (現状のシェルでは無害)。 */
            if (src[si] == '?') {
                char num[12];
                int n = 0;
                int v = sh_status_get();
                int neg = 0;

                si++;                       /* '?' を消費 */
                if (v < 0) { neg = 1; v = -v; }
                if (v == 0) num[n++] = '0';
                while (v > 0) { num[n++] = (char)('0' + (v % 10)); v /= 10; }
                if (neg) num[n++] = '-';
                while (n > 0 && di < max - 1) dst[di++] = num[--n];
                if (n > 0) truncated = 1;   /* 最大 11 文字 — 既存の溢れ検査へ */
                expanded = 1;
                continue;
            }

            if (src[si] == '{') { braced = 1; si++; }

            /* T9: 名前の終端 (`}` / 区切り / 行末) を**幅の検査より先**に見る。
             * 以前は `vi < ENV_NAME_MAX - 1` が先に効いたので
             *   - `${` + 31 文字 + `}` は `}` を食べ残してリテラルに漏らし、
             *   - 32 文字以上の名前は 31 文字で打ち切って残りを素通しした。
             * どちらも「展開されない文字列がコマンド行に混ざる」= 切り詰め。 */
            for (;;) {
                if (!src[si]) break;              /* 行末 (`${FOO` も従来どおり) */
                if (braced) {
                    if (src[si] == '}') { si++; break; }
                } else {
                    if (src[si] == ' ' || src[si] == '/' ||
                        src[si] == '.' || src[si] == ':' ||
                        src[si] == '$') break;
                }
                if (vi >= ENV_NAME_MAX - 1) return ENV_EXPAND_ERR_NAME;
                var_name[vi++] = src[si++];
            }
            var_name[vi] = '\0';

            /* 名前が続かない裸の '$' はリテラルとして残す
             * (以前は空文字列で env_get を引き、`set =x` で出来た
             * 名前空の変数にヒットして "x" が出た) */
            if (vi == 0) {
                dst[di++] = '$';
                continue;
            }

            val = env_get(var_name);
            if (val) {
                while (*val && di < max - 1) dst[di++] = *val++;
                if (*val) truncated = 1;
                expanded = 1;
            }
            /* 変数が見つからない場合は空文字に展開 (UNIXの慣習) */
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
    if (truncated || src[si] != '\0') return -1;
    return expanded;
}

/* ======================================================================== */
/*  シェルコマンド                                                           */
/* ======================================================================== */

static int cmd_env(int argc, char **argv)
{
    int i;
    (void)argc; (void)argv;
    for (i = 0; i < MAX_ENV_VARS; i++) {
        if (env_vars[i].used) {
            g_api->kprintf(ATTR_WHITE, "%s=%s\n",
                           env_vars[i].name, env_vars[i].value);
        }
    }
    return 0;
}

static int cmd_set(int argc, char **argv)
{
    char name[ENV_NAME_MAX];
    const char *val_start;

    if (argc < 2) {
        /* 引数なし → env と同じ */
        return cmd_env(argc, argv);
    }

    /* 票 §2-5 (往復 1 所見 6): `set -e` / `set +e` は **ここで拾う**。
     * 下の「値の表示」分岐 (argc == 2 で `=` が無い) に落ちると
     * `-e: not set` を出して終わってしまう。`set` の登録は 1 本のまま
     * (二重登録しない) — 旗の実体は cmd_script.c にある。 */
    if (str_eq(argv[1], "-e") || str_eq(argv[1], "+e")) {
        script_errexit_set(argv[1][0] == '-');
        return 0;
    }

    /* "VAR=VALUE" 形式をパース */
    {
        const char *arg = argv[1];
        int ni = 0;
        /* T8: 名前を切ってから登録すると別の変数が書き換わる。
         * `=` の手前が収まらなければ登録も表示もせずに断る。 */
        while (*arg && *arg != '=') {
            if (ni >= ENV_NAME_MAX - 1) {
                sh_refuse("set: variable name", ENV_NAME_MAX - 1);
                return SH_STATUS_USAGE;
            }
            name[ni++] = *arg++;
        }
        name[ni] = '\0';

        if (name[0] == '\0') {
            g_api->kprintf(ATTR_RED, "%s: invalid variable name '%s'\n", argv[0], argv[1]);
            return SH_STATUS_USAGE;
        }

        if (*arg == '=') {
            arg++; /* '=' をスキップ */
            val_start = arg;
        } else if (argc >= 3 && str_eq(argv[2], "=") && argc >= 4) {
            /* set VAR = VALUE 形式 (スペース区切り) */
            val_start = argv[3];
        } else if (argc >= 3) {
            /* set VAR VALUE 形式 (古い形式, fallback) */
            val_start = argv[2];
        } else {
            /* 値の表示 */
            const char *v = env_get(name);
            if (!v) {
                g_api->kprintf(ATTR_RED, "%s: not set\n", name);
                return SH_STATUS_ERROR;
            }
            g_api->kprintf(ATTR_WHITE, "%s=%s\n", name, v);
            return 0;
        }

        /* T8: 値は写さずそのまま渡す。以前はここで ENV_VALUE_MAX - 1 に
         * 切ってから env_set へ渡していたので、長い値が黙って短くなった。
         * 長さの検査は登録口 (env_set) が 1 か所で行う。
         * `=` の後ろは val_start に全部入っている (追加引数は使わない)。 */
        env_set(name, val_start);
    }
    return 0;
}

static int cmd_unset(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    for (i = 1; i < argc; i++) {
        env_unset(argv[i]);
    }
    return 0;
}

/* 登録用テーブル */
static const ShellCmd env_cmds[] = {
    { "env",    cmd_env,   "",            "Show all environment variables" },
    { "set",    cmd_set,   "VAR=VALUE | -e | +e", "Set variable / errexit" },
    { "export", cmd_set,   "VAR=VALUE",   "Set environment variable" },
    { "unset",  cmd_unset, "VAR...",      "Unset environment variables" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_env_init(void)
{
    shell_register_cmds(env_cmds);
}
