/* ========================================================================
 *  sh_args.inc — 引数の分解と glob 展開 (実体)
 *
 *  main.c が #include する。拡張子が .c でないのは build/programs.mk の
 *  SHELL_SRC が userland/shell 下の .c を wildcard で拾うためで、
 *  sh_launch.inc / sh_pipe.inc / sh_redraw.inc / sh_ls.inc と同じ理由。
 *  **常駐 / sh.bin の両方**が取り込む (中の #ifdef で切り替わる) — 展開後の
 *  トークンは切り出す前と同一なので常駐の .o は変わらない。
 *
 *  ホスト TDD (tools/tests/sh_shell_host.c) がこのファイルをそのまま
 *  #include して、R4 (glob は一致だけを写す / 多すぎる行は捨てる) と
 *  R7 (argv[] の 1 つ手前で止める) を見る。
 * ======================================================================== */

/* I4: 反復型。以前は '*' ごとに残りを再帰で総当たりしていたので、
 * `*a*a*a…` のようなパターンが `aaaa…` に当たると指数的にバックトラックして
 * 事実上停止した。R4 以降これは sys_ls の (CPL=0) コールバックの中で回るので、
 * 止まると GUI ごと固まる。最後の '*' の位置と対応する名前位置だけを覚えて
 * 1 段戻る標準形にする — O(n×m)。'?' と末尾 '*' の意味は変えていない。 */
int wildcard_match(const char *pattern, const char *str) {
    const char *star = (const char *)0;   /* 直近の '*' そのものの位置 */
    const char *mark = (const char *)0;   /* その '*' が飲んだ次の名前位置 */

    while (*str) {
        /* C-1: パターン側の '*' を**通常文字の比較より先に**見ること。
         * 後に置くと `*` と `*dest` で '*' 同士が通常文字として消費され、
         * `cp SRC DIR/` + アスタリスクが `*dest` を展開から落とした。 */
        if (*pattern == '*') {
            star = pattern++;
            mark = str;
            continue;
        }
        if (*pattern == '?' || (*pattern != '\0' && *pattern == *str)) {
            pattern++;
            str++;
            continue;
        }
        if (star) {
            pattern = star + 1;            /* 1 段だけ戻して '*' に 1 文字飲ませる */
            str = ++mark;
            continue;
        }
        return 0;
    }
    while (*pattern == '*') pattern++;
    return (*pattern == '\0');
}

/* B2: CPL=3 では sys_ls のコールバックから KAPI を呼べない。glob_cb は
 * mem_alloc を呼ぶので、SHELL_AS_APP では写し取り (sh_ls_collect_cb) を
 * 挟んで sys_ls が戻ってから glob_cb へ流す。常駐はマクロがそのまま
 * g_api->sys_ls(dir_path, glob_cb, ctx) に展開される (.o は不変)。 */
struct GlobCtx {
    char **argv;
    int *argc;
    int max_args;
    const char *pattern;
    const char *dir_prefix;
    int matched_any;
    int overflow;              /* I1: 詰めきれなかった (行ごと捨てる) */
    int alloc_failed;          /* T6: mem_alloc が失敗した (行ごと断る) */
    char **allocated_strings;
    int *alloc_count;
};

static void glob_cb(const DirEntry_Ext *entry, void *c) {
    struct GlobCtx *ctx = (struct GlobCtx *)c;
    int dirlen, namelen, i;
    char *full;

    if (entry->name[0] == '.' && (entry->name[1]=='\0' || (entry->name[1]=='.' && entry->name[2]=='\0'))) return;

    /* R7: argv[] は max_args 個。apply_redirects が最後に argv[argc] へ NUL を
     * 置くので、**格納は max_args - 1 個まで**にしないと 1 つ溢れて隣の
     * static (tmp_buf) を壊す (`echo` に 255 個の引数で到達した)。
     * I1: 入りきらないものは**黙って捨てない** — 印を立てて行ごと捨てる。 */
    if (!wildcard_match(ctx->pattern, entry->name)) return;
    if (*(ctx->argc) >= ctx->max_args - 1) {
        ctx->overflow = 1;
        return;
    }
    {
        dirlen = strlen(ctx->dir_prefix);
        namelen = strlen(entry->name);
        full = (char *)g_api->mem_alloc(dirlen + namelen + 1);
        /* T6: 確保に失敗したら **印を立てて** 戻る。以前は黙って戻っていたので
         * 一致の一部だけが argv に入り、`rm /tmp/item*` が 1 件だけ消えて
         * 成功に見えた。ここは CPL=0 のコールバックの中なので印だけ立て、
         * 赤字と後始末は parse_args_and_glob が戻ってから行う。 */
        if (!full) { ctx->alloc_failed = 1; return; }

        for (i = 0; i < dirlen; i++) full[i] = ctx->dir_prefix[i];
        for (i = 0; i < namelen; i++) full[dirlen + i] = entry->name[i];
        full[dirlen + namelen] = '\0';
        
        ctx->argv[*(ctx->argc)] = full;
        *(ctx->argc) += 1;
        ctx->matched_any = 1;
        
        if (*(ctx->alloc_count) < ctx->max_args) {
            ctx->allocated_strings[*(ctx->alloc_count)] = full;
            *(ctx->alloc_count) += 1;
        }
    }
}

#ifdef SHELL_AS_APP
/* R4: 写し取りの上限は「列挙順の先頭 N 件」ではなく「**一致した** N 件」に
 * 掛ける。不一致が先に 128 件以上並ぶディレクトリだと、先頭で切ると一致を
 * 取り逃がして `cat /tmp/d/target*` が未展開のまま渡ってしまう。照合は
 * wildcard_match (純関数) なのでコールバックの中で呼んでよい。
 * 一致が上限を超えたら**一部だけ展開して実行しない** — 行ごと捨てる。 */
static const char *sh_glob_pattern;

static int sh_glob_filter(const char *name)
{
    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) return 0;
    return wildcard_match(sh_glob_pattern, name);
}

/* 戻り値: 0 = 展開した / -1 = 多すぎるので行を捨てる */
static int sh_glob_run(const char *dir_path, struct GlobCtx *ctx)
{
    int i, n;
    DirEntry_Ext e;

    sh_glob_pattern = ctx->pattern;
    sh_ls_reset();
    sh_ls_set_filter(sh_glob_filter);
    g_api->sys_ls(dir_path, sh_ls_collect_cb, (void *)0);
    sh_ls_set_filter((int (*)(const char *))0);

    if (sh_ls_dropped() > 0) {
        g_api->kprintf(ATTR_RED, "%s", "sh: glob: too many matches\n");
        return -1;
    }

    n = sh_ls_count_get();
    for (i = 0; i < n; i++) {
        sh_ls_fill(i, &e);
        glob_cb(&e, ctx);
    }
    return 0;
}
/* R4: glob が諦めた行の印。parse_args_and_glob は void なのでここで渡す。 */
int sh_glob_failed = 0;
#endif

/* T7: glob のパターン部を **切らずに** 写す。入り切らないなら照合を試みずに
 * 断る — 切ったパターンは別のファイルに当たる (`prefix…suffix1*` が
 * `suffix2` まで巻き込む)。戻り値 0 = OK / -1 = 断った。 */
static int glob_copy_pattern(char *dst, const char *src)
{
    int n = 0;

    while (src[n]) {
        if (n >= GLOB_PATTERN_MAX - 1) {
            sh_refuse("sh: glob pattern", GLOB_PATTERN_MAX - 1);
            return -1;
        }
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    return 0;
}

/* I1: 戻り値 0 = OK / -1 = 引数が多すぎるので**行ごと捨てる**。以前は黙って
 * 切り捨てていて、`cp` + 253 個 + `/wrong` + `/intended` で最後の宛先が消え、
 * `/wrong/s` を上書きしていた (複数 glob の合計超過も同じ)。
 * 捨てるときは **sh_refuse_mark() で印を立てる** — 立てないとスクリプトが
 * 次の行へ落ちて、捨てたはずの行の後始末が走る (段 3 で T6 と一緒に直した)。 */
int parse_args_and_glob(char *cmd_line, char **argv, int *argc_out, int max_args, char **allocated_strings, int *alloc_count) {
    int argc = 0;
    char *p = cmd_line;

    *alloc_count = 0;

    /* R7: argv[argc] への NUL 終端ぶんを 1 つ残す (上の glob_cb と同じ理由) */
    while (*p) {
        char *start, *out;
        int has_star = 0;
        char quote = 0;

        while (*p == ' ') p++;
        if (!*p) break;
        start = p;
        out = p;   /* インプレースでクォート除去 (out <= p 常に成立) */

        while (*p) {
            if (quote) {
                /* クォート内 */
                if (*p == quote) {
                    /* クォート終了 — クォート文字自体は出力しない */
                    quote = 0;
                    p++;
                } else if (*p == '\\' && quote == '"' && *(p + 1)) {
                    /* ダブルクォート内のバックスラッシュエスケープ */
                    p++;
                    *out++ = *p++;
                } else {
                    /* シングルクォート内は全てリテラル */
                    *out++ = *p++;
                }
            } else {
                /* クォート外 */
                if (*p == ' ') break;
                if (*p == '"' || *p == '\'') {
                    /* クォート開始 — クォート文字自体は出力しない */
                    quote = *p++;
                } else if (*p == '\\' && *(p + 1)) {
                    /* バックスラッシュエスケープ */
                    p++;
                    *out++ = *p++;
                } else {
                    /* クォート外の * のみ glob 対象 */
                    if (*p == '*') has_star = 1;
                    *out++ = *p++;
                }
            }
        }
        /* p は空白か文字列末尾を指している */
        if (*p == ' ') p++;
        *out = '\0';

        if (has_star) {
            char *last_slash = (char *)0;
            char *q = start;
            char dir_path[PATH_MAX_LEN];
            char pattern[GLOB_PATTERN_MAX];
            struct GlobCtx ctx;

            while (*q) {
                if (*q == '/' || *q == '\\') last_slash = q;
                q++;
            }

            if (last_slash) {
                int dirlen = last_slash - start + 1;
                int i;
                /* T7: ディレクトリ部を切ると **別の場所** を照合しにいく */
                if (dirlen > PATH_MAX_LEN - 1) {
                    sh_refuse("sh: glob directory", PATH_MAX_LEN - 1);
                    *argc_out = argc;
                    return -1;
                }
                for (i = 0; i < dirlen; i++) dir_path[i] = start[i];
                dir_path[i] = '\0';

                if (glob_copy_pattern(pattern, last_slash + 1) < 0) {
                    *argc_out = argc;
                    return -1;
                }
            } else {
                dir_path[0] = '.'; dir_path[1] = '\0';
                if (glob_copy_pattern(pattern, start) < 0) {
                    *argc_out = argc;
                    return -1;
                }
            }

            ctx.argv = argv;
            ctx.argc = &argc;
            ctx.max_args = max_args;
            ctx.pattern = pattern;
            ctx.dir_prefix = last_slash ? dir_path : "";
            ctx.matched_any = 0;
            ctx.overflow = 0;
            ctx.alloc_failed = 0;
            ctx.allocated_strings = allocated_strings;
            ctx.alloc_count = alloc_count;

#ifdef SHELL_AS_APP
            if (sh_glob_run(dir_path, &ctx) < 0) {
                sh_glob_failed = 1;
                *argc_out = 0;
                return -1;
            }
#else
            g_api->sys_ls(dir_path, glob_cb, &ctx);
#endif

            /* T6: 一致の **一部だけ** を渡さない。行ごと断って、確保済みの
             * 文字列は呼び手 (execute_single) が allocated_strings から
             * まとめて解放する。 */
            if (ctx.alloc_failed) {
                g_api->kprintf(ATTR_RED, "%s", "sh: glob: out of memory\n");
                sh_refuse_mark();
                *argc_out = argc;
                return -1;
            }
            /* I1 も「入力を捨てたのに次の行が走る」同じ型 (PM 決裁
             * 2026-09-16、段 3 で一緒に直す)。上限ではなく「入り切らない」
             * ので sh_refuse の書式には乗らない — 文言は据え置きで印だけ
             * 立てる (空の段 / glob の確保失敗と同じ扱い)。 */
            if (ctx.overflow) {
                g_api->kprintf(ATTR_RED, "%s", "sh: too many arguments\n");
                sh_refuse_mark();
                *argc_out = argc;
                return -1;
            }
            if (!ctx.matched_any) {
                if (argc >= max_args - 1) {
                    g_api->kprintf(ATTR_RED, "%s", "sh: too many arguments\n");
                    sh_refuse_mark();
                    *argc_out = argc;
                    return -1;
                }
                argv[argc++] = start;
            }
        } else {
            if (argc >= max_args - 1) {
                g_api->kprintf(ATTR_RED, "%s", "sh: too many arguments\n");
                sh_refuse_mark();
                *argc_out = argc;
                return -1;
            }
            argv[argc++] = start;
        }
    }
    *argc_out = argc;
    return 0;
}
