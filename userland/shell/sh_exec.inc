/* ========================================================================
 *  sh_exec.inc — コマンドの起動経路 (実体)
 *
 *  main.c が #include する。拡張子が .c でないのは build/programs.mk の
 *  SHELL_SRC が userland/shell 下の .c を wildcard で拾い、独立した .c に
 *  すると常駐 shell.bin のリンクに空の .o が混ざるため
 *  (sh_args.inc / sh_launch.inc / sh_pipe.inc / sh_redraw.inc / sh_ls.inc と
 *  同じ理由)。**常駐 / sh.bin の両方**が取り込む。
 *
 *  中身は main.c から 1 行も変えずに移しただけ (2026-09-16、票
 *  docs/archive/shell/TASK_SH_TRUNCATION.md §5 の段 1「足場」)。切り出しの
 *  目的はホスト試験 (tools/tests/sh_truncation_host.c) が
 *  try_exec / try_exec_from_path / run_cmd_internal を**実物のまま**
 *  #include できるようにすること — これらは main.c の static なので、
 *  main.c を丸ごと取り込まないと触れなかった。
 *
 *  sh_launch.inc / sh_pipe.inc の #include もここへ一緒に移してある。
 *  try_exec の**後ろ**に置くという並びがコード生成の前提 (先に置くと
 *  sh_launch が try_exec へ展開され得る) なので、並びを崩さない。
 *
 *  含むもの:
 *    has_ext / has_slash        — 拡張子とスラッシュの判定 (この経路だけ)
 *    try_exec                   — cmdline を組み立てて sh_launch へ渡す
 *    try_exec_from_path         — PATH を走査して try_exec を呼ぶ
 *    sh_is_cui_only             — sh.bin で断つ内蔵コマンド (SHELL_AS_APP)
 *    run_cmd_internal           — 内蔵 → カレント → PATH の順に解決する
 * ======================================================================== */

/* 文字列の末尾が指定の拡張子と一致するかチェック */
static int has_ext(const char *s, const char *ext)
{
    int slen = strlen(s);
    int elen = strlen(ext);
    if (slen < elen) return 0;
    return strcmp(s + slen - elen, ext) == 0;
}

/* パスにスラッシュが含まれるかチェック */
static int has_slash(const char *s)
{
    while (*s) { if (*s == '/') return 1; s++; }
    return 0;
}

/* cmdline (コマンド名+引数) を構築して exec_run を試行 */
#define TRY_EXEC_BUF_SIZE  512
#define TRY_EXEC_MARGIN    12  /* パス末尾 + スペース + NUL の余裕 */

/* ------------------------------------------------------------------------ */
/*  T3: try_exec が組み立てる行の長さを **切り詰めずに** 数える               */
/*                                                                          */
/*  下の組み立てループとまったく同じ規則で数えること。とくにクォートの        */
/*  再付与ぶんを落とさない:                                                  */
/*    - 引数と引数のあいだの区切りの空白             … 1 バイト              */
/*    - 空白 / " / ' / \ を含む引数と空の引数         … 前後の " で +2        */
/*    - その本体に含まれる " と \                     … \ が付いて 2 倍       */
/*  クォートが要らない引数には " も \ も含まれないので、エスケープの数え方を  */
/*  分岐させる必要はない (need_quote の判定条件がその 4 文字そのもの)。       */
/*                                                                          */
/*  戻り値: 必要なバイト数 (終端の NUL は含まない)。                          */
/* ------------------------------------------------------------------------ */
static int try_exec_len(const char *bin_path, int argc, char **argv)
{
    int total = 0;
    int i;
    const char *s;

    for (s = bin_path; *s; s++) total++;

    for (i = 1; i < argc; i++) {
        int need_quote = 0;
        int body = 0;

        s = argv[i];
        if (*s == '\0') need_quote = 1;      /* 空引数もクォートで保存する */
        for (; *s; s++) {
            if (*s == ' ' || *s == '"' || *s == '\'' || *s == '\\')
                need_quote = 1;
            body++;
            if (*s == '"' || *s == '\\') body++;   /* エスケープの \ */
        }

        total += 1 + body;                   /* 区切りの空白 + 本体 */
        if (need_quote) total += 2;          /* 前後の " */
    }
    return total;
}

#ifndef SHELL_AS_APP
/* ------------------------------------------------------------------------ */
/*  sh_exec_result (常駐) — 票 TASK_EXIT_STATUS §2-2                         */
/*                                                                          */
/*  exec_run の **直後** に exec_last_result を読む。間に KAPI を挟まない —  */
/*  挟むとその呼び出しが起こした子の結果を拾いかねない。                     */
/*  記録が無い (OS32_ERR_INVAL) のは exec_run が全 return 点で書く以上        */
/*  起こらないはずだが、起きたら「起こせなかった」側に倒す (次の候補へ        */
/*  進まない = 同じものを 2 回実行しない)。                                   */
/* ------------------------------------------------------------------------ */
int sh_exec_result(const char *cmdline, int *kind, int *code)
{
    int rc;
    int k = EXEC_KIND_NONE;
    int c = 0;

    rc = g_api->exec_run(cmdline);
    if (g_api->exec_last_result(&k, &c) != 0) k = EXEC_KIND_NONE;
    if (k == EXEC_KIND_NONE) { k = EXEC_KIND_GENERAL; c = 0; }

    if (kind) *kind = k;
    if (code) *code = c;
    return rc;
}
#endif /* !SHELL_AS_APP */

/* try_exec の戻り値は従来どおり EXEC_* (印字と既存の分岐のため)。
 * **起動の成否の判断は *kind だけ** で行う (票 §2-2)。断って起動しなかった
 * ときは *kind = EXEC_KIND_NONE のまま — 呼び手は印 (sh_refused_peek) を
 * 先に見るので、種別を見に来ることはない。 */
static int try_exec(const char *bin_path, int argc, char **argv,
                    int *kind, int *code)
{
    char cmd_buf[TRY_EXEC_BUF_SIZE];
    char *p = cmd_buf;
    /* 断ったときの既定 — 「起動していない」。 */
    int k = EXEC_KIND_NONE;
    int c = 0;
    char *limit_path = cmd_buf + TRY_EXEC_BUF_SIZE - TRY_EXEC_MARGIN;
    char *limit_args = cmd_buf + TRY_EXEC_BUF_SIZE - 2;
    int i;
    const char *s;

    /* T3: 溢れる行は **組み立てる前に** 断る。切り詰めたまま起動すると
     * 意図と違う引数でプログラムが走る (票 §1 T3)。上限は下のループが実際に
     * 見る値そのもの — パスは limit_path (TRY_EXEC_BUF_SIZE - TRY_EXEC_MARGIN)、
     * 行全体は limit_args (TRY_EXEC_BUF_SIZE - 2)。
     *
     * 断ったあとは run_cmd_internal / try_exec_from_path が
     * sh_refused_peek() を見て **PATH 走査ごと止める** — 止めないと候補の数
     * だけ同じ赤字が出て、最後に "command not found" で閉じてしまう。 */
    {
        int path_len = 0;
        for (s = bin_path; *s; s++) path_len++;
        if (path_len > TRY_EXEC_BUF_SIZE - TRY_EXEC_MARGIN) {
            sh_refuse("sh: program path", TRY_EXEC_BUF_SIZE - TRY_EXEC_MARGIN);
            if (kind) *kind = k;
            if (code) *code = c;
            return EXEC_ERR_GENERAL;
        }
        if (try_exec_len(bin_path, argc, argv) > TRY_EXEC_BUF_SIZE - 2) {
            sh_refuse("sh: argument list", TRY_EXEC_BUF_SIZE - 2);
            if (kind) *kind = k;
            if (code) *code = c;
            return EXEC_ERR_GENERAL;
        }
    }

    /* バイナリパスをコピー */
    s = bin_path;
    while (*s && p < limit_path) *p++ = *s++;

    /* 引数を追加。
     * parse_args_and_glob が剥がしたクォートをここで復元する。
     * 復元せずに空白区切りで再結合すると、exec_run 側の再トークナイズで
     * 空白入り引数 (例: sndctl play "T120 O4 ...") がばらばらに割れる。 */
    for (i = 1; i < argc; i++) {
        int need_quote = 0;
        const char *q;

        if (p >= limit_args) break;
        *p++ = ' ';

        s = argv[i];
        for (q = s; *q; q++) {
            if (*q == ' ' || *q == '"' || *q == '\'' || *q == '\\') {
                need_quote = 1;
                break;
            }
        }
        if (*s == '\0') need_quote = 1;   /* 空引数もクォートで保存 */

        if (need_quote) {
            if (p < limit_args) *p++ = '"';
            /* 閉じクォート分の余裕を残してコピー。" と \ はエスケープ */
            while (*s && p + 2 < limit_args) {
                if (*s == '"' || *s == '\\') *p++ = '\\';
                *p++ = *s++;
            }
            if (p < limit_args) *p++ = '"';
        } else {
            while (*s && p < limit_args) *p++ = *s++;
        }
    }
    *p = '\0';

    {
        int rc = sh_exec_result(cmd_buf, &k, &c);
        if (kind) *kind = k;
        if (code) *code = c;
        return rc;
    }
}

#ifdef SHELL_AS_APP
/* sh_launch / パイプバッファの実体。ホスト TDD が同じソースを #include
 * できるように別ファイルにしてある (tools/tests/sh_launch_host.c,
 * tools/tests/sh_shell_host.c)。 */
#include "sh_launch.inc"
#include "sh_pipe.inc"
#endif /* SHELL_AS_APP */

/* ======================================================================== */
/*  try_exec_from_path — PATH環境変数を走査してコマンドを検索・実行           */
/*                                                                          */
/*  shell_get_path() から取得したコロン区切りPATHの各ディレクトリについて     */
/*  dir + "/" + name_buf のフルパスを構築し try_exec を試行する。             */
/* ======================================================================== */
/* 票 §2-3: **PATH 走査は kind で止める**。
 *   NOT_FOUND / INVALID … 次の候補へ
 *   それ以外 (EXITED / FAULT / ABORTED / NOMEM / GENERAL) … どんな値でも止まる
 * 途中で INVALID を 1 度でも見たかは *saw_invalid に積む (呼び手が cwd と
 * PATH をまたいで覚える)。走査を尽くしたら *kind = EXEC_KIND_NOT_FOUND。 */
static int try_exec_from_path(const char *name_buf, int argc, char **argv,
                              int *kind, int *code, int *saw_invalid)
{
    const char *path_str = shell_get_path();
    const char *p = path_str;
    /* 「この走査が断ったか」は印の **差分** で見る。入ってきた時点で既に印が
     * 立っていることがある (入れ子の execute_command は印を消さない) ので、
     * peek の値そのものを見ると自分が断っていない行まで止めてしまう。 */
    int was_refused = sh_refused_peek();

    while (*p) {
        char dir_buf[PATH_MAX_LEN];
        char full_path[PATH_MAX_LEN];
        int di = 0;
        int rc;

        /* ':' で区切られたディレクトリを取得 */
        while (*p && *p != ':' && di < PATH_MAX_LEN - 2)
            dir_buf[di++] = *p++;
        dir_buf[di] = '\0';

        /* T17: 項目を入れ切れずに止まった = ':' の区切りを **見失っている**。
         * このまま進むと残りが次の項目になり、**別のディレクトリ**の同名
         * バイナリを試してしまう。切らずに行ごと断る。 */
        if (*p && *p != ':') {
            sh_refuse("sh: PATH entry", PATH_MAX_LEN - 2);
            *kind = EXEC_KIND_NONE;
            return EXEC_ERR_GENERAL;
        }

        if (*p == ':') p++;
        if (di == 0) continue;

        /* T17: dir + '/' + name_buf が入り切らないなら、strncat が切った
         * **別のパス**を起動しにいく。連結する前に測って断る。 */
        {
            int need = di + (int)strlen(name_buf);
            if (di > 0 && dir_buf[di - 1] != '/') need++;   /* 補う '/' */
            if (need > PATH_MAX_LEN - 1) {
                sh_refuse("sh: command path", PATH_MAX_LEN - 1);
                *kind = EXEC_KIND_NONE;
                return EXEC_ERR_GENERAL;
            }
        }

        /* フルパス構築: dir + '/' + name_buf */
        strncpy(full_path, dir_buf, PATH_MAX_LEN - 1);
        full_path[PATH_MAX_LEN - 1] = '\0';
        if (di > 0 && dir_buf[di - 1] != '/') {
            strncat(full_path, "/", PATH_MAX_LEN - strlen(full_path) - 1);
        }
        strncat(full_path, name_buf, PATH_MAX_LEN - strlen(full_path) - 1);

        rc = try_exec(full_path, argc, argv, kind, code);
        /* try_exec / sh_exec_result が断ったら **次の候補へ回さない**
         * (T3 / T11)。回すと同じ赤字が候補の数だけ出たうえ
         * "command not found" で閉じる。 */
        if (!was_refused && sh_refused_peek()) return EXEC_ERR_GENERAL;
        if (*kind == EXEC_KIND_INVALID) {
            /* 往復 2 所見 1: 見たことを覚えて次の候補へ。尽きたときに
             * 127 (見つからない) ではなく 126 を返すため。 */
            if (saw_invalid) *saw_invalid = 1;
            continue;
        }
        if (*kind != EXEC_KIND_NOT_FOUND) {
            return rc;
        }
    }
    *kind = EXEC_KIND_NOT_FOUND;
    return EXEC_ERR_NOT_FOUND;
}

#ifdef SHELL_AS_APP
/* D2(b): カーソル位置に依存する TUI / シリアル前提のコマンドは sh.bin では
 * 動かせない。内蔵コマンドの表を引く手前で弾く。`filer` の起動経路
 * (fl_exec_program) もここで到達しなくなる。 */
static int sh_is_cui_only(int argc, char **argv)
{
    const char *name = argv[0];

    if (str_eq(name, "os32gui") || str_eq(name, "rshell") ||
        str_eq(name, "filer")) return 1;

    /* R5: ループデバイスの枠 (drivers/loop_dev.c の loop_slots) は sh が
     * 退場しても残り、backing FD だけが owner 回収で閉じる。その後 FD 番号が
     * 再利用されると枠が別ファイルを向く。カーネル側の本修正は別票なので、
     * sh.bin では枠を作る / 使う経路をまとめて断る。 */
    if (str_eq(name, "losetup")) return 1;

    /* T2: `play` は drivers/fm.c の io_wait で**同期に**鳴らし終わるまで
     * CPL=0 で待つ。GUI 中は協調型全体が止まり、CTRL+STOP でも回収できない
     * (`C` を 100 個で 22 秒)。`beep` は一瞬なので残す。 */
    if (str_eq(name, "play")) return 1;
    if (str_eq(name, "dd") && argc > 1) {
        const char *d = argv[1];
        if (d[0] == 'l' && d[1] == 'o' && d[2] >= '0' && d[2] <= '9') return 1;
    }
    return 0;
}
#endif

/* 子が戻ったあとの 1 行 (従来の文言をそのまま保つ)。
 *   with_notfound = 1 … 2a (パス直指定)。見つからなければ "not found" を出す。
 * `[Process crashed]` は **fault のときだけ** — CTRL+STOP で畳んだ場合は
 * 種別が ABORTED なので、落ちたと言わない。 */
static void run_cmd_report(int kind, int code, const char *name, int with_notfound)
{
    if (kind == EXEC_KIND_EXITED && code == EXEC_SUCCESS) {
        g_api->kprintf(ATTR_GREEN, "%s", "\n");
    } else if (kind == EXEC_KIND_FAULT || kind == EXEC_KIND_ABORTED) {
        /* 受入 S1b: 表示は今までどおり。分けるのは `$?` の値だけ
         * (FAULT = 139 / ABORTED = 130)。 */
        g_api->kprintf(ATTR_RED, "%s", "\n[Process crashed]\n");
    } else if (with_notfound && kind == EXEC_KIND_NOT_FOUND) {
        g_api->kprintf(ATTR_RED, "%s: not found\n", name);
    }
}

/* 内蔵 / カレント / PATH の順に解決する。戻り値は行の `$?` (票 §2-3)。 */
static int run_cmd_internal(int argc, char **argv) {
    int j, rc;
    /* try_exec_from_path と同じ理由で、断りは印の **差分** で見る */
    int was_refused;
    char name_buf[PATH_MAX_LEN];
    /* 起動の結果。**この 2 つだけ**で分岐する (値から種別を作らない)。 */
    int kind = EXEC_KIND_NONE;
    int code = 0;
    /* 往復 2 所見 1: cwd と PATH をまたいで「INVALID を見たか」を覚える。
     * 見たなら候補が尽きても 127 (見つからない) とは言わない。 */
    int saw_invalid = 0;

#ifdef SHELL_AS_APP
    if (sh_is_cui_only(argc, argv)) {
        g_api->kprintf(ATTR_RED, "%s", "sh: cui only\n");
        return SH_STATUS_NOEXEC;
    }
#endif

    if (argc > 1 && (str_eq(argv[1], "-h") || str_eq(argv[1], "--help") || str_eq(argv[1], "/?"))) {
        /* help の短絡は「頼まれたことをやった」— 0 (受入 S13)。 */
        shell_print_help(argv[0]);
        return SH_STATUS_OK;
    }

    /* 0. .bat/.sh 拡張子 → 暗黙的に source として実行 */
    if (has_ext(argv[0], ".bat") || has_ext(argv[0], ".sh")) {
        /* 内側が断って打ち切ったら、呼んだ側のスクリプトも打ち切る
         * (cmd_source と同じ扱い。票 TASK_SH_TRUNCATION §2-1) */
        int sr = script_source_file(argv[0]);
        if (sr == SCRIPT_ERR_REFUSED) { sh_refuse_mark(); return SH_STATUS_USAGE; }
        if (sr < 0) return SH_STATUS_USAGE;
        return sr;
    }

    /* 1. 内部コマンドの検索 */
    for (j = 0; j < g_cmd_count; j++) {
        if (str_eq(argv[0], g_cmds[j].name)) {
            return g_cmds[j].handler(argc, argv);
        }
    }

    was_refused = sh_refused_peek();

    /* 2. 外部コマンドの検索・実行 */
    {
        /* T4: 名前を PATH_MAX_LEN - 5 で切って ".bin" を付けると、251 バイトの
         * 接頭辞が実在するときに **別のファイル** が起動する (`Pextra…` で
         * `P.bin` が走る)。切らずに断る。 */
        if ((int)strlen(argv[0]) > PATH_MAX_LEN - 5) {
            sh_refuse("sh: command name", PATH_MAX_LEN - 5);
            return SH_STATUS_USAGE;
        }

        /* コマンド名に.bin拡張子を付加 (".bin\0" = 5文字分を予約) */
        strncpy(name_buf, argv[0], PATH_MAX_LEN - 5);
        name_buf[PATH_MAX_LEN - 5] = '\0';
        if (!has_ext(name_buf, ".bin")) {
            strcat(name_buf, ".bin");
        }
    }

    /* 2a. パスにスラッシュが含まれる場合 → 直接実行 */
    if (has_slash(argv[0])) {
        rc = try_exec(name_buf, argc, argv, &kind, &code);
        (void)rc;
        /* 断った行は "not found" でも "crashed" でもない — 何も言い足さない */
        if (!was_refused && sh_refused_peek()) return SH_STATUS_USAGE;
        sh_gfx_restore();
        run_cmd_report(kind, code, argv[0], 1);
        return sh_status_from_kind(kind, code);
    }

    /* 2b. カレントディレクトリで試行 */
    rc = try_exec(name_buf, argc, argv, &kind, &code);
    (void)rc;
    if (!was_refused && sh_refused_peek()) return SH_STATUS_USAGE;
    if (kind == EXEC_KIND_INVALID) {
        /* 受入 S2c: cwd の壊れた foo.bin で止めず、PATH の正しい方を試す
         * (意図した変更 — 以前はここで止まっていた)。 */
        saw_invalid = 1;
    } else if (kind != EXEC_KIND_NOT_FOUND) {
        /* EXITED / FAULT / ABORTED / NOMEM / GENERAL は **どんな値でも止まる**
         * (事実 2 の修正 — 子が exit(-1) を返しても「無かった」と読まない)。 */
        sh_gfx_restore();
        run_cmd_report(kind, code, argv[0], 0);
        return sh_status_from_kind(kind, code);
    }

    /* 2c. PATH内の各ディレクトリで試行 */
    rc = try_exec_from_path(name_buf, argc, argv, &kind, &code, &saw_invalid);
    (void)rc;
    if (!was_refused && sh_refused_peek()) return SH_STATUS_USAGE;
    if (kind != EXEC_KIND_NOT_FOUND && kind != EXEC_KIND_NONE) {
        sh_gfx_restore();
        run_cmd_report(kind, code, argv[0], 0);
        return sh_status_from_kind(kind, code);
    }

    /* 走査を尽くした (往復 2 所見 1)。INVALID を 1 度でも見たなら
     * 「見つからない」ではなく「起こせない」— 126 で、文言も分ける。 */
    if (saw_invalid) {
        g_api->kprintf(ATTR_RED, "%s: not a valid executable\n", argv[0]);
        return SH_STATUS_NOEXEC;
    }
    g_api->kprintf(ATTR_RED, "%s: command not found\n", argv[0]);
    return SH_STATUS_NOTFOUND;
}
