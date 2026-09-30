#include "shell.h"
#include "config.h"

/* ======================================================================== */
/*  シェルメインループ・UI（履歴・補完）                                     */
/* ======================================================================== */

#define HIST_SIZE    16

/* ------------------------------------------------------------------------ */
/*  行入力のキー源 (票 §10 non-blocker 3)                                    */
/*                                                                          */
/*  常駐は FEP を通す ime_getkey。sh.bin は端末の子なので FEP の確定は        */
/*  gshell 側で済んでおり、二重に通すと確定済み UTF-8 をもう一度食う。        */
/*  kbd_getkey は同じ u16 形式 (上位=スキャンコード / 下位=ASCII) を返し、    */
/*  GUI 中は注入リングの 1 バイト (スキャンコード 0) を WAIT_KEY で park      */
/*  しながら渡す — UTF-8 の後続バイトも同じ経路なので下の行編集はそのまま     */
/*  動く (矢印などスキャンコードを要する枝は端末経由では最初から来ない)。     */
/*  常駐側の展開は以前と同一トークン。                                        */
/* ------------------------------------------------------------------------ */
#ifdef SHELL_AS_APP
#define sh_getkey() (g_api->kbd_getkey())
#else
#define sh_getkey() (g_api->ime_getkey())
#endif

/* 履歴ファイル名。常駐の ~/.history を sh.bin が壊さないよう別名 (D2(c))。
 * HIST_FILE_ROOM は hist_build_path が HOME をコピーするときに残す余白
 * ("/" + 名前 + NUL) で、名前を長くしたら必ず一緒に広げること。 */
#ifdef SHELL_AS_APP
#define HIST_FILE_NAME  ".sh_history"
#define HIST_FILE_ROOM  16
#else
#define HIST_FILE_NAME  ".history"
#define HIST_FILE_ROOM  12
#endif

/* $HOME/.profile を組み立てるときに残す余白 ("/" + ".profile" + NUL = 10)。
 * HIST_FILE_ROOM と同じ作法で、名前を変えたら一緒に広げること ([C4])。 */
#define PROFILE_PATH_ROOM  12

/* 画面に出ている入力行の写しを更新する印 (実装レビュー blocker 1)。
 * shell_run は 1 文字追加や BS を redraw_line を通さずに直接印字するので、
 * その場でも写しを合わせないと「純粋な延長」の判定が狂う。
 * sh_drop_drawn() は行の作り直しを強制する (候補一覧を挟んだ後など)。
 * 常駐では両方とも消える — redraw_line も写しも常駐には無い。 */
#ifdef SHELL_AS_APP
/* 直接印字の後は画面カーソルも行末にある (追加・行末 BS しか通らない) */
#define sh_mark_drawn(buf, len)  sh_set_drawn((buf), (len), (len))
#define sh_drop_drawn()          sh_set_drawn((const char *)0, -1, -1)
#else
#define sh_mark_drawn(buf, len)  ((void)0)
#define sh_drop_drawn()          ((void)0)
#endif

static char hist_buf[HIST_SIZE][HIST_LINE_MAX];
static int  hist_count = 0;
static int  hist_idx   = 0;
static int  hist_dirty = 0;
static int  prev_draw_len = 0;

static void hist_add(const char *s) {
    int i;
    if (str_len(s) == 0) return;
    /* T14: 収まらない行は**履歴に入れない**。切って入れると、↑ キーで
     * 呼び出した「切れた行」がそのまま実行される (行そのものは
     * CMD_BUF_SIZE まで正当なので、実行は断らず履歴だけ見送る)。 */
    if ((int)str_len(s) > HIST_LINE_MAX - 1) return;
    if (hist_count > 0 && str_eq(hist_buf[(hist_count - 1) % HIST_SIZE], s)) return;
    i = hist_count % HIST_SIZE;
    {
        int j;
        for (j = 0; s[j] && j < HIST_LINE_MAX - 1; j++) hist_buf[i][j] = s[j];
        hist_buf[i][j] = 0;
    }
    hist_count++;
    hist_dirty = 1;
}

static void show_prompt(void) {
#ifdef SHELL_AS_APP
    /* D2(e): 常駐 (OS32:/path$) と見分けが付くように短い固定プロンプト */
    g_api->kprintf(ATTR_WHITE, "%s", "sh> ");
#else
    g_api->kprintf(ATTR_GREEN, "OS32");
    g_api->kprintf(ATTR_GREEN, ":");
    g_api->kprintf(ATTR_CYAN, "%s", g_api->sys_getcwd());
    g_api->kprintf(ATTR_WHITE, "$ ");
#endif
}

#ifdef SHELL_AS_APP
/* GUI 中はコンソール座標が使えない (理由と方針は sh_redraw.inc の頭)。
 * ホスト TDD が同じソースを #include できるように別ファイルにしてある。 */
#include "sh_redraw.inc"
#else
static void redraw_line(const char *buf, int len, int cursor) {
    int i, cl;
    char tmp_buf[CMD_BUF_SIZE];
    g_api->console_set_cursor(0, g_api->console_get_cursor_y());
    show_prompt();
    /* バッファ内容をNUL終端コピーしてUTF-8対応印字 */
    for (i = 0; i < len && i < CMD_BUF_SIZE - 1; i++) tmp_buf[i] = buf[i];
    tmp_buf[i] = '\0';
    g_api->shell_print_utf8(tmp_buf, ATTR_WHITE);
    cl = prev_draw_len - len;
    for (i = 0; i < cl; i++) g_api->shell_putchar(' ', ATTR_WHITE);
    prev_draw_len = len;

    g_api->console_set_cursor(0, g_api->console_get_cursor_y());
    show_prompt();
    for (i = 0; i < cursor; i++) g_api->console_set_cursor(g_api->console_get_cursor_x() + 1, g_api->console_get_cursor_y());
}
#endif

/* ======================================================================== */
/*  タブ補完: コマンド名 (内部+PATH) + ファイル名                           */
/* ======================================================================== */

#define TAB_MAX_MATCHES 40

/* PATH内.binファイル列挙用コールバック構造体 */
struct PathCompCtx {
    const char *matches[TAB_MAX_MATCHES];
    char name_store[TAB_MAX_MATCHES][64]; /* .bin除去後の名前を格納 */
    int count;
    const char *prefix;
    int prefix_len;
    const char **cmd_names_ref; /* 内部コマンド名との重複チェック用 */
};

static void path_comp_cb(const DirEntry_Ext *entry, void *c)
{
    struct PathCompCtx *ctx = (struct PathCompCtx *)c;
    int nlen, i, ok, dup;
    const char *name = entry->name;

    if (ctx->count >= TAB_MAX_MATCHES) return;
    if (entry->type == OS32_FILE_TYPE_DIR) return;

    /* .bin拡張子チェック */
    nlen = 0;
    while (name[nlen]) nlen++;
    if (nlen < 5) return; /* 少なくとも "x.bin" */
    if (name[nlen-4] != '.' || name[nlen-3] != 'b' ||
        name[nlen-2] != 'i' || name[nlen-1] != 'n') return;

    /* .bin を除去した名前を構築 */
    {
        int base_len = nlen - 4;
        /* T21: 収まらない候補は**補完しない**。切った名前を挿すと
         * ENTER で別のファイルに作用する。 */
        if (base_len > (int)sizeof(ctx->name_store[0]) - 1) return;

        /* プレフィックスマッチ */
        ok = 1;
        for (i = 0; i < ctx->prefix_len && i < base_len; i++) {
            if (name[i] != ctx->prefix[i]) { ok = 0; break; }
        }
        if (!ok || ctx->prefix_len > base_len) return;

        /* 内部コマンドとの重複チェック */
        for (i = 0; i < base_len; i++)
            ctx->name_store[ctx->count][i] = name[i];
        ctx->name_store[ctx->count][base_len] = '\0';

        dup = 0;
        if (ctx->cmd_names_ref) {
            for (i = 0; ctx->cmd_names_ref[i]; i++) {
                if (str_eq(ctx->name_store[ctx->count], ctx->cmd_names_ref[i])) {
                    dup = 1; break;
                }
            }
        }
        /* 既に追加済みの名前との重複チェック */
        if (!dup) {
            for (i = 0; i < ctx->count; i++) {
                if (str_eq(ctx->name_store[ctx->count], ctx->name_store[i])) {
                    dup = 1; break;
                }
            }
        }
        if (dup) return;

        ctx->matches[ctx->count] = ctx->name_store[ctx->count];
        ctx->count++;
    }
}

/* ファイル名補完用コールバック構造体 */
struct FileCompCtx {
    const char *matches[TAB_MAX_MATCHES];
    char name_store[TAB_MAX_MATCHES][128];
    int count;
    const char *prefix;
    int prefix_len;
};

static void file_comp_cb(const DirEntry_Ext *entry, void *c)
{
    struct FileCompCtx *ctx = (struct FileCompCtx *)c;
    int nlen, i, ok;
    const char *name = entry->name;

    if (ctx->count >= TAB_MAX_MATCHES) return;
    /* . と .. をスキップ */
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) return;

    nlen = 0;
    while (name[nlen]) nlen++;

    /* プレフィックスマッチ */
    ok = 1;
    for (i = 0; i < ctx->prefix_len && i < nlen; i++) {
        if (name[i] != ctx->prefix[i]) { ok = 0; break; }
    }
    if (!ok || ctx->prefix_len > nlen) return;

    /* 名前を格納 (ディレクトリなら末尾に '/' 付加)。
     * T21: 収まらない候補は**補完しない** (切ると別の名前になる)。
     * ディレクトリは付け足す '/' まで数える。 */
    {
        int need = nlen + ((entry->type == OS32_FILE_TYPE_DIR) ? 1 : 0);
        if (need > (int)sizeof(ctx->name_store[0]) - 1) return;
    }
    for (i = 0; i < nlen; i++)
        ctx->name_store[ctx->count][i] = name[i];
    if (entry->type == OS32_FILE_TYPE_DIR) {
        ctx->name_store[ctx->count][i++] = '/';
    }
    ctx->name_store[ctx->count][i] = '\0';
    ctx->matches[ctx->count] = ctx->name_store[ctx->count];
    ctx->count++;
}

static int tab_complete(char *buf, int pos, int show_candidates) {
    const char *matches[TAB_MAX_MATCHES];
    int match_count = 0, i, j, common_len;
    int has_space = 0;

    if (pos == 0) return pos;

    /* バッファ内にスペースがあるかチェック (コマンド名 vs 引数) */
    for (i = 0; i < pos; i++) {
        if (buf[i] == ' ') { has_space = 1; break; }
    }

    if (!has_space) {
        /* ====== コマンド名補完 (最初のワード) ====== */

        /* 内部コマンド名の検索 */
        for (i = 0; cmd_names[i]; i++) {
            int ok = 1;
            for (j = 0; j < pos; j++) {
                if (cmd_names[i][j] == 0 || cmd_names[i][j] != buf[j]) { ok = 0; break; }
            }
            if (ok && match_count < TAB_MAX_MATCHES)
                matches[match_count++] = cmd_names[i];
        }

        /* PATH内の.binファイルを検索 */
        {
            struct PathCompCtx pctx;
            const char *path_str = shell_get_path();
            const char *pp = path_str;
            char prefix_tmp[PATH_MAX_LEN];

            /* バッファからプレフィックスを取得 */
            for (i = 0; i < pos && i < PATH_MAX_LEN - 1; i++) prefix_tmp[i] = buf[i];
            prefix_tmp[i] = '\0';

            pctx.count = 0;
            pctx.prefix = prefix_tmp;
            pctx.prefix_len = pos;
            pctx.cmd_names_ref = cmd_names;

            while (*pp) {
                char dir[PATH_MAX_LEN];
                int di = 0;
                int lost = 0;
                while (*pp && *pp != ':') {
                    if (di >= PATH_MAX_LEN - 1) { lost = 1; break; }
                    dir[di++] = *pp++;
                }
                dir[di] = '\0';
                /* T17: 項目が収まり切らなかったら `:` の区切りを見失っている。
                 * 残りを別のディレクトリとして走査すると、実在しない場所の
                 * 候補が出る。ここで走査ごと止める。 */
                if (lost) break;
                if (*pp == ':') pp++;
                if (di > 0) {
                    g_api->sys_ls(dir, (void *)path_comp_cb, &pctx);
                }
            }

            /* 結果をmatches配列に追加 */
            for (i = 0; i < pctx.count && match_count < TAB_MAX_MATCHES; i++) {
                matches[match_count++] = pctx.matches[i];
            }
        }
    } else {
        /* ====== ファイル名補完 (2番目以降のワード) ====== */
        struct FileCompCtx fctx;
        char dir_path[PATH_MAX_LEN];
        const char *file_prefix;
        int word_start, last_slash;

        /* 現在のワードの開始位置を特定 */
        word_start = pos - 1;
        while (word_start > 0 && buf[word_start - 1] != ' ') word_start--;

        /* ワード内の最後の '/' を見つける */
        last_slash = -1;
        for (i = word_start; i < pos; i++) {
            if (buf[i] == '/') last_slash = i;
        }

        if (last_slash >= word_start) {
            /* ディレクトリ部分 + ファイルプレフィックス */
            int dlen = last_slash - word_start + 1;
            for (i = 0; i < dlen && i < PATH_MAX_LEN - 1; i++)
                dir_path[i] = buf[word_start + i];
            dir_path[i] = '\0';
            file_prefix = &buf[last_slash + 1];
            fctx.prefix_len = pos - last_slash - 1;
        } else {
            dir_path[0] = '.'; dir_path[1] = '\0';
            file_prefix = &buf[word_start];
            fctx.prefix_len = pos - word_start;
        }

        fctx.count = 0;
        fctx.prefix = file_prefix;

        g_api->sys_ls(dir_path, (void *)file_comp_cb, &fctx);

        for (i = 0; i < fctx.count && match_count < TAB_MAX_MATCHES; i++) {
            matches[match_count++] = fctx.matches[i];
        }

        /* ファイル名補完: ワードのプレフィックス部分を保持して補完 */
        if (match_count == 1) {
            /* 完全一致: ワード部分を書き換え */
            int new_pos = word_start;
            if (last_slash >= word_start) {
                /* ディレクトリ部分を保持 */
                new_pos = last_slash + 1;
            }
            i = 0;
            while (matches[0][i] && new_pos < CMD_BUF_SIZE - 2) {
                buf[new_pos++] = matches[0][i++];
            }
            /* ディレクトリでない場合はスペースを追加 */
            if (i > 0 && matches[0][i - 1] != '/') {
                buf[new_pos++] = ' ';
            }
            buf[new_pos] = '\0';
            return new_pos;
        }
        if (match_count > 1) {
            /* 共通プレフィックスを計算 */
            common_len = 0;
            for (;;) {
                char c = matches[0][common_len];
                int ok = 1;
                if (c == 0) break;
                for (i = 1; i < match_count; i++) {
                    if (matches[i][common_len] != c) { ok = 0; break; }
                }
                if (!ok) break;
                common_len++;
            }
            if (common_len > fctx.prefix_len) {
                int new_pos = (last_slash >= word_start) ? last_slash + 1 : word_start;
                for (i = 0; i < common_len && new_pos < CMD_BUF_SIZE - 1; i++) {
                    buf[new_pos++] = matches[0][i];
                }
                buf[new_pos] = '\0';
                return new_pos;
            }
            if (show_candidates) {
                g_api->kprintf(ATTR_WHITE, "%s", "\n");
                for (i = 0; i < match_count; i++)
                    g_api->kprintf(ATTR_CYAN, "  %s", matches[i]);
                g_api->kprintf(ATTR_WHITE, "%s", "\n");
                sh_drop_drawn();   /* 行が流れた — 次は作り直す */
                return -1;
            }
        }
        return pos;
    }

    /* コマンド名補完の共通処理 */
    if (match_count == 0) return pos;
    if (match_count == 1) {
        i = 0;
        while (matches[0][i] && i < CMD_BUF_SIZE - 2) { buf[i] = matches[0][i]; i++; }
        buf[i++] = ' '; buf[i] = 0;
        return i;
    }
    common_len = pos;
    for (;;) {
        char c = matches[0][common_len]; int ok = 1;
        if (c == 0) break;
        for (i = 1; i < match_count; i++) if (matches[i][common_len] != c) { ok = 0; break; }
        if (!ok) break;
        common_len++;
    }
    if (common_len > pos) {
        for (i = pos; i < common_len && i < CMD_BUF_SIZE - 1; i++) buf[i] = matches[0][i];
        buf[common_len] = 0;
        return common_len;
    }
    if (show_candidates) {
        g_api->kprintf(ATTR_WHITE, "%s", "\n");
        for (i = 0; i < match_count; i++) g_api->kprintf(ATTR_CYAN, "  %s", matches[i]);
        g_api->kprintf(ATTR_WHITE, "%s", "\n");
        sh_drop_drawn();   /* 行が流れた — 次は作り直す */
        return -1;
    }
    return pos;
}

/* ======================================================================== */
/*  履歴ファイルパス構築ヘルパ                                               */
/* ======================================================================== */
static int hist_build_path(char *path, int max)
{
    const char *home = env_get("HOME");
    int pi = 0;
    const char *h;
    const char *fn;

    if (!home) return -1;
    h = home;
    /* T20: HOME を切って組み立てると**別のディレクトリの履歴**を読み書き
     * する。収まらなければ履歴を使わない (呼び手は < 0 で黙って戻る)。 */
    while (*h) {
        if (pi >= max - HIST_FILE_ROOM) return -1;
        path[pi++] = *h++;
    }
    if (pi > 0 && path[pi - 1] != '/') path[pi++] = '/';
    fn = HIST_FILE_NAME;
    while (*fn) path[pi++] = *fn++;
    path[pi] = '\0';
    return 0;
}

/* ======================================================================== */
/*  履歴ファイル保存                                                         */
/* ======================================================================== */
void hist_save(void)
{
    char path[PATH_MAX_LEN];
    int fd, i, start, end;

    if (!hist_dirty) return;
    if (hist_build_path(path, PATH_MAX_LEN) < 0) return;

    fd = g_api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) return;

    start = (hist_count > HIST_SIZE) ? hist_count - HIST_SIZE : 0;
    end = hist_count;
    for (i = start; i < end; i++) {
        int idx = i % HIST_SIZE;
        int len = 0;
        while (hist_buf[idx][len]) len++;
        if (len > 0) {
            g_api->sys_write(fd, hist_buf[idx], len);
            g_api->sys_write(fd, "\n", 1);
        }
    }
    g_api->sys_close(fd);
    hist_dirty = 0;
}

/* ======================================================================== */
/*  履歴ファイル読み込み                                                     */
/* ======================================================================== */
void hist_load(void)
{
    char path[PATH_MAX_LEN];
    char *buf;
    int fd, sz, bi, li;
    int more;

    if (hist_build_path(path, PATH_MAX_LEN) < 0) {
        /* 起動時に 1 度だけ理由を出す (hist_save は毎行通るので黙る)。
         * ここは execute_command の外なので印は残さない。 */
        sh_refuse("sh: $HOME for history path", PATH_MAX_LEN - HIST_FILE_ROOM);
        (void)sh_refused_take();
        return;
    }

    fd = g_api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) return;

    buf = (char *)g_api->mem_alloc(HIST_SIZE * HIST_LINE_MAX);
    if (!buf) { g_api->sys_close(fd); return; }

    sz = g_api->sys_read(fd, buf, HIST_SIZE * HIST_LINE_MAX - 1);
    /* T14: 読み切れたか (末尾の行が途中で切れていないか) を閉じる前に見る。 */
    more = 0;
    if (sz == HIST_SIZE * HIST_LINE_MAX - 1) {
        char probe;
        if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;
    }
    g_api->sys_close(fd);
    if (sz <= 0) { g_api->mem_free(buf); return; }
    buf[sz] = '\0';

    hist_count = 0;
    li = 0;
    for (bi = 0; bi <= sz; bi++) {
        if (bi == sz || buf[bi] == '\n' || buf[bi] == '\r') {
            /* T14: 切れた行は**読み込まない** — 入れると ↑ キーで
             * 「切れた行」が実行できてしまう。
             *   - HIST_LINE_MAX に収まらない行
             *   - 読み切れなかったファイルの、改行で終わっていない末尾行 */
            int cut = (li > HIST_LINE_MAX - 1) || (bi == sz && more);
            if (li > 0 && !cut) {
                int idx = hist_count % HIST_SIZE;
                int j;
                int src_start = bi - li;
                for (j = 0; j < li; j++)
                    hist_buf[idx][j] = buf[src_start + j];
                hist_buf[idx][li] = '\0';
                hist_count++;
            }
            li = 0;
            if (bi < sz && buf[bi] == '\r' &&
                bi + 1 < sz && buf[bi + 1] == '\n') bi++;
        } else {
            li++;
        }
    }
    g_api->mem_free(buf);
    hist_dirty = 0;
}

void shell_run(void) {
    char cmd_buf[CMD_BUF_SIZE];
    int cmd_pos, cmd_len, key, last_tab = 0;
    /* T15: 行編集が打鍵を捨てたことを覚えておく印。以前は 4092 バイトで
     * 黙って捨て、ENTER でその**接頭辞**を実行していた (機械注入
     * `/api/key text=` では気付けない = 本票の動機であるランナーが
     * 偽の結果を得る)。ESC で行を捨てたときだけ下ろす。 */
    int cmd_dropped = 0;

    g_api->kprintf(ATTR_CYAN, "%s", "================================\n");
    g_api->kprintf(ATTR_CYAN, "%s", " OS32 External Shell Started\n");
    g_api->kprintf(ATTR_CYAN, "%s", "================================\n");

    /* === スタートアップスクリプト === */
    /* 1. /etc/profile — システム全体の初期化 (旧autoexec.binの後継) */
    {
        int sfd = g_api->sys_open("/etc/profile", KAPI_O_RDONLY);
        if (sfd >= 0) {
            g_api->sys_close(sfd);
            /* 断られても起動は止めない (票 TASK_SH_TRUNCATION §2-1 / R2) */
            script_source_profile("/etc/profile");
        }
    }

    /* 2. $HOME/.profile — ユーザー個人の設定 */
    {
        const char *home = env_get("HOME");
        if (home) {
            char profile_path[PATH_MAX_LEN];
            int pi = 0;
            int too_long = 0;
            const char *h = home;
            /* T20: HOME を切ると**別のディレクトリの .profile** を読む。
             * 収まらなければ読まない (起動は続ける。票 R2)。 */
            while (*h) {
                if (pi >= PATH_MAX_LEN - PROFILE_PATH_ROOM) { too_long = 1; break; }
                profile_path[pi++] = *h++;
            }
            if (too_long) {
                sh_refuse("sh: $HOME for .profile path",
                          PATH_MAX_LEN - PROFILE_PATH_ROOM);
                (void)sh_refused_take();      /* 起動は止めない */
                pi = 0;
            }
            if (pi > 0 && profile_path[pi - 1] != '/') profile_path[pi++] = '/';
            { const char *pn = ".profile"; while (*pn) profile_path[pi++] = *pn++; }
            profile_path[pi] = '\0';

            if (!too_long) {
                int ufd = g_api->sys_open(profile_path, KAPI_O_RDONLY);
                if (ufd >= 0) {
                    g_api->sys_close(ufd);
                    script_source_profile(profile_path);
                }
            }
        }
    }

    /* 履歴ファイル読み込み */
    hist_load();

#ifndef SHELL_AS_APP
    /* 初期化時に自動シリアル＆rshell開始
     * D2(a): sh.bin では呼ばない。シリアルを初期化すると rshell_active の
     * タイムアウト (kbd_getchar が一定時間でスペースを返す) まで巻き込む。 */
    g_api->serial_init(SYS_SERIAL_BAUD);
    (void)execute_command("rshell");
#endif

    for (;;) {
#ifdef SHELL_AS_APP
        /* D2(d): `exit` の印はプロンプトを出す**前**に見る。/etc/profile や
         * $HOME/.profile の中の `exit` でも、入力待ちに入らずここで終わる
         * (前の行の実行が立てた印もここで拾うので、ループ末尾では見ない)。 */
        if (sh_exit_flag) break;
#endif
        show_prompt();
        cmd_pos = cmd_len = cmd_buf[0] = prev_draw_len = 0;
        cmd_dropped = 0;
        sh_mark_drawn(cmd_buf, 0);
        hist_idx = hist_count;

        for (;;) {
            key = sh_getkey();

            if ((key & 0xFF) == 0x0D) { g_api->shell_putchar('\n', ATTR_WHITE); break; }
            if ((key & 0xFF) == 0x08) {
                if (cmd_pos > 0) {
                    if (cmd_pos == cmd_len) {
#ifdef SHELL_AS_APP
                        /* 端末の BS はセルを消さないので BS + 空白 + BS。
                         * 写しの確定も sh_backspace_tail の中で行う。 */
                        cmd_pos--; cmd_len--;
                        sh_backspace_tail(cmd_buf, cmd_len);
                        prev_draw_len = cmd_len;
#else
                        cmd_pos--; cmd_len--; cmd_buf[cmd_len] = 0;
                        g_api->shell_putchar(0x08, ATTR_WHITE); prev_draw_len = cmd_len;
                        sh_mark_drawn(cmd_buf, cmd_len);
#endif
                    } else {
                        int i; for (i = cmd_pos - 1; i < cmd_len - 1; i++) cmd_buf[i] = cmd_buf[i+1];
                        cmd_len--; cmd_pos--; cmd_buf[cmd_len] = 0; redraw_line(cmd_buf, cmd_len, cmd_pos);
                    }
                }
                continue;
            }
            if ((key >> 8) == 0x3A) { /* UP */
                if (hist_idx > 0 && hist_idx > hist_count - HIST_SIZE) {
                    int i = (--hist_idx) % HIST_SIZE; cmd_len = 0;
                    while (hist_buf[i][cmd_len]) { cmd_buf[cmd_len] = hist_buf[i][cmd_len]; cmd_len++; }
                    cmd_buf[cmd_len] = 0; cmd_pos = cmd_len; redraw_line(cmd_buf, cmd_len, cmd_pos);
                }
                continue;
            }
            if ((key >> 8) == 0x3D) { /* DOWN */
                if (hist_idx < hist_count - 1) {
                    int i = (++hist_idx) % HIST_SIZE; cmd_len = 0;
                    while (hist_buf[i][cmd_len]) { cmd_buf[cmd_len] = hist_buf[i][cmd_len]; cmd_len++; }
                    cmd_buf[cmd_len] = 0; cmd_pos = cmd_len; redraw_line(cmd_buf, cmd_len, cmd_pos);
                } else {
                    hist_idx = hist_count; cmd_len = cmd_pos = cmd_buf[0] = 0; redraw_line(cmd_buf, cmd_len, cmd_pos);
                }
                continue;
            }
            /* LEFT / RIGHT / HOME は内容を変えずに**画面カーソルだけ**動かす。
             * 写しは内容しか見ていないので、そのままだと次の TAB 補完が
             * 「行末への延長」と誤判定する (`hel` → LEFT → TAB で `hep `)。
             * 追従させるより捨てて次回を行の作り直しに倒す方が安全 — 経路を
             * 1 つ見落としても表示が壊れない。GUI 中はスキャンコードが来ない
             * のでここは通らず、CUI 直起動のときだけ効く。 */
            if ((key >> 8) == 0x3B) { /* LEFT */
                if (cmd_pos > 0) { cmd_pos--; g_api->shell_putchar(0x08, ATTR_WHITE); sh_drop_drawn(); } continue;
            }
            if ((key >> 8) == 0x3C) { /* RIGHT */
                if (cmd_pos < cmd_len) { g_api->shell_putchar(cmd_buf[cmd_pos], ATTR_WHITE); cmd_pos++; sh_drop_drawn(); } continue;
            }
            if ((key >> 8) == 0x3E) { /* HOME */
                while (cmd_pos > 0) { cmd_pos--; g_api->shell_putchar(0x08, ATTR_WHITE); } sh_drop_drawn(); continue;
            }
            if ((key >> 8) == 0x39) { /* DEL */
                if (cmd_pos < cmd_len) {
                    int i; for(i=cmd_pos; i<cmd_len-1; i++) cmd_buf[i]=cmd_buf[i+1];
                    cmd_len--; cmd_buf[cmd_len]=0; redraw_line(cmd_buf, cmd_len, cmd_pos);
                }
                continue;
            }
            if ((key >> 8) == 0x0F || (key & 0xFF) == 0x09) { /* TAB */
                int npos = tab_complete(cmd_buf, cmd_len, last_tab);
                if (npos == -1) {
#ifndef SHELL_AS_APP
                    show_prompt();
#endif
                    redraw_line(cmd_buf, cmd_len, cmd_pos);
                }
                else if (npos != cmd_len) { cmd_len = cmd_pos = npos; redraw_line(cmd_buf, cmd_len, cmd_pos); last_tab = 0; }
                else last_tab = 1;
                continue;
            }
            last_tab = 0;
            if ((key & 0xFF) == 0x1B) { cmd_len=cmd_pos=cmd_buf[0]=0; cmd_dropped=0; redraw_line(cmd_buf, cmd_len, cmd_pos); continue; }
            /* 印字可能文字: ASCII (0x20-0x7E) および IME確定UTF-8バイト (0x80+, scancode=0) */
            {
                u8 ascii_byte = (u8)(key & 0xFF);
                int scancode = (key >> 8) & 0x7F;
                int is_printable = (ascii_byte >= 0x20 && ascii_byte < 0x7F);
                int is_ime_byte  = (scancode == 0x00 && ascii_byte >= 0x80);
                if ((is_printable || is_ime_byte) && cmd_len >= CMD_BUF_SIZE - 4) {
                    /* T15: ここで黙って捨てると接頭辞が実行される。
                     * 印を立てて ENTER のところで行ごと断る。 */
                    cmd_dropped = 1;
                } else if (is_printable || is_ime_byte) {
                    if (is_ime_byte) {
                        /* UTF-8マルチバイト: 蓄積して一括表示 */
                        char utf8_tmp[5];
                        int utf8_len = 0;
                        int expect;
                        utf8_tmp[utf8_len++] = (char)ascii_byte;
                        /* UTF-8先頭バイトから期待バイト数を判定 */
                        if ((ascii_byte & 0xE0) == 0xC0) expect = 2;
                        else if ((ascii_byte & 0xF0) == 0xE0) expect = 3;
                        else if ((ascii_byte & 0xF8) == 0xF0) expect = 4;
                        else expect = 1;
                        /* 後続バイトを読み取る */
                        while (utf8_len < expect) {
                            int nk = sh_getkey();
                            u8 nb = (u8)(nk & 0xFF);
                            if ((nb & 0xC0) != 0x80) break;
                            utf8_tmp[utf8_len++] = (char)nb;
                        }
                        utf8_tmp[utf8_len] = '\0';
                        /* コマンドバッファに追加 */
                        if (cmd_len + utf8_len >= CMD_BUF_SIZE - 1) {
                            cmd_dropped = 1;   /* T15: 同上 */
                        } else {
                            int bi;
                            if (cmd_pos == cmd_len) {
                                for (bi = 0; bi < utf8_len; bi++) {
                                    cmd_buf[cmd_pos++] = utf8_tmp[bi];
                                    cmd_len++;
                                }
                                cmd_buf[cmd_len] = 0;
                                g_api->shell_print_utf8(utf8_tmp, ATTR_WHITE);
                                prev_draw_len = cmd_len;
                                sh_mark_drawn(cmd_buf, cmd_len);
                            } else {
                                int i;
                                for (i = cmd_len - 1 + utf8_len; i >= cmd_pos + utf8_len; i--)
                                    cmd_buf[i] = cmd_buf[i - utf8_len];
                                for (bi = 0; bi < utf8_len; bi++)
                                    cmd_buf[cmd_pos++] = utf8_tmp[bi];
                                cmd_len += utf8_len;
                                cmd_buf[cmd_len] = 0;
                                redraw_line(cmd_buf, cmd_len, cmd_pos);
                            }
                        }
                    } else {
                        /* ASCII文字 */
                        char ch = (char)ascii_byte;
                        if (cmd_pos == cmd_len) {
                            cmd_buf[cmd_pos++] = ch; cmd_len++; cmd_buf[cmd_len] = 0;
                            g_api->shell_putchar(ch, ATTR_WHITE); prev_draw_len = cmd_len;
                            sh_mark_drawn(cmd_buf, cmd_len);
                        } else {
                            int i; for (i = cmd_len; i > cmd_pos; i--) cmd_buf[i] = cmd_buf[i-1];
                            cmd_buf[cmd_pos++] = ch; cmd_len++; cmd_buf[cmd_len] = 0;
                            redraw_line(cmd_buf, cmd_len, cmd_pos);
                        }
                    }
                }
            }
        }
        cmd_buf[cmd_len] = 0;
        if (cmd_dropped) {
            /* T15: 打鍵を捨てた行は**実行しない / 履歴にも入れない**。
             * ここは execute_command の外なので印は残さない (対話)。 */
            sh_refuse("sh: line", CMD_BUF_SIZE - 4);
            (void)sh_refused_take();
            /* 票 §2-3 の表: 断った行の `$?` は 2。 */
            sh_status_set(SH_STATUS_USAGE);
            continue;
        }
        if (cmd_len > 0) hist_add(cmd_buf);
        (void)execute_command(cmd_buf);   /* `$?` は execute_command が入れる */
        if (hist_dirty) hist_save();
        /* `exit` の印はループの入口で見る (起動時の profile も拾うため)。
         * 抜けると main が 0 を返して sh.bin が終わり、端末は launch_poll の
         * DONE でプロンプトへ戻る。常駐シェルには exit を登録していない。 */
    }
}
