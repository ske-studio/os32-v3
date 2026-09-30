/* ======================================================================== */
/*  CMD_SCRIPT.C — OS32 シェル スクリプトエンジン (バッチ処理)                */
/*                                                                          */
/*  source, if, goto, ask, return コマンドを実装する。                       */
/*  Phase 1: source (逐次実行版) + コメント処理                             */
/* ======================================================================== */
#include "shell.h"

/* ======================================================================== */
/*  スクリプト実行コンテキスト                                               */
/* ======================================================================== */

/* スクリプト行配列 — 動的確保 (使用時のみメモリ消費) */
static char (*script_lines)[SCRIPT_MAX_LINE] = NULL;
static int  script_line_count;
static int  script_current_line;
static int  script_abort_flag;

/* ------------------------------------------------------------------------ */
/*  `set -e` の旗 (票 TASK_EXIT_STATUS §2-5)                                 */
/*                                                                          */
/*  `set` の登録は cmd_env.c に 1 本だけ (二重登録しない)。cmd_set の先頭が   */
/*  `-e` / `+e` を拾ってここへ流す。入れ子の source を抜けるときは            */
/*  script_abort_flag と同じく save / restore する。                          */
/* ------------------------------------------------------------------------ */
static int script_errexit = 0;

void script_errexit_set(int on) { script_errexit = on ? 1 : 0; }
int  script_errexit_get(void)   { return script_errexit; }

/* ネスト深度カウンタ */
static int g_script_depth = 0;

int script_in_script(void) { return g_script_depth > 0; }

/* ======================================================================== */
/*  行ごとの譲り (GUI 端末だけ)                                              */
/*                                                                          */
/*  sh.bin (SHELL_AS_APP) は協調型 GUI の中を走る CPL=3 アプリなので、譲ら   */
/*  ないかぎり WM は 1 度も回らない — スクリプトが長いあいだ画面も打鍵も     */
/*  止まる。2026-09-16 まで、この譲りは **行ごとの ESC 監視の副作用** で     */
/*  出ていた: kbd_trygetkey が空振りすると drivers/kbd.c が exec_park_poll  */
/*  を呼び、PIT tick 1 回に 1 度だけ WM へ park していた。監視を覗くだけの   */
/*  kbd_peekkey に替えたときその副作用ごと消えたので、ここで明示的に譲り     */
/*  直す (KAPI v49 sys_yield = 票 T9 D5 の第 4 の park 点)。                 */
/*                                                                          */
/*  間引きは消えた側と同じ **PIT tick 1 回に 1 度**。sys_yield はカーネルで  */
/*  間引かない (明示的な譲りなので) から、間引くならここで間引く — 譲りは    */
/*  park / resume の往復なので、`goto` で回る軽い行のループでは往復のほうが  */
/*  行そのものより重くなる。exec_park_poll の g_poll_last_tick と同じ考え方。*/
/*                                                                          */
/*  常駐シェル (CUI) には譲る相手が居ない。exec_sys_yield は CUI では        */
/*  `hlt` 1 回で戻る = PIT 1 tick (10ms) 待つので、行ごとに呼ぶと 128 行の    */
/*  スクリプトに 1 秒以上足すことになる。だから **CUI では呼び出しごと       */
/*  消す** — sh_gfx_restore / sh_erase_cells と同じ形で、常駐のコード生成は  */
/*  1 バイトも変わらない。                                                   */
/* ======================================================================== */
#ifdef SHELL_AS_APP
static u32 g_script_yield_tick = 0;

static void script_yield_gui(void)
{
    u32 now = g_api->get_tick();

    if (now == g_script_yield_tick) return;   /* 同じ tick の中では譲らない */
    g_script_yield_tick = now;
    g_api->sys_yield();
}
#else
#define script_yield_gui() ((void)0)
#endif

/* ======================================================================== */
/*  行ごとの譲り (GUI 端末だけ)                                              */
/*                                                                          */
/*  sh.bin (SHELL_AS_APP) は協調型 GUI の中を走る CPL=3 アプリなので、譲ら   */
/*  ないかぎり WM は 1 度も回らない — スクリプトが長いあいだ画面も打鍵も     */
/*  止まる。2026-09-16 まで、この譲りは **行ごとの ESC 監視の副作用** で     */
/*  出ていた: kbd_trygetkey が空振りすると drivers/kbd.c が exec_park_poll  */
/*  を呼び、PIT tick 1 回に 1 度だけ WM へ park していた。監視を覗くだけの   */
/*  kbd_peekkey に替えたときその副作用ごと消えたので、ここで明示的に譲り     */
/*  直す (KAPI v49 sys_yield = 票 T9 D5 の第 4 の park 点)。                 */
/*                                                                          */
/*  間引きは消えた側と同じ **PIT tick 1 回に 1 度**。sys_yield はカーネルで  */
/*  間引かない (明示的な譲りなので) から、間引くならここで間引く — 譲りは    */
/*  park / resume の往復なので、`goto` で回る軽い行のループでは往復のほうが  */
/*  行そのものより重くなる。exec_park_poll の g_poll_last_tick と同じ考え方。*/
/*                                                                          */
/*  常駐シェル (CUI) には譲る相手が居ない。exec_sys_yield は CUI では        */
/*  `hlt` 1 回で戻る = PIT 1 tick (10ms) 待つので、行ごとに呼ぶと 128 行の    */
/*  スクリプトに 1 秒以上足すことになる。だから **CUI では呼び出しごと       */
/*  消す** — sh_gfx_restore / sh_erase_cells と同じ形で、常駐のコード生成は  */
/*  1 バイトも変わらない。                                                   */

/* ======================================================================== */
/*  内部ヘルパー: 行の先頭空白をスキップ                                     */
/* ======================================================================== */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* ======================================================================== */
/*  内部ヘルパー: コメント・空行判定                                         */
/*                                                                          */
/*  戻り値:                                                                 */
/*    0 = 通常行 (実行対象)                                                 */
/*    1 = コメントまたは空行 (スキップ対象)                                  */
/*    2 = ''' ブロックコメントのトグル行                                     */
/* ======================================================================== */
static int classify_line(const char *line)
{
    const char *p = skip_spaces(line);

    /* 空行 */
    if (*p == '\0') return 1;

    /* # コメント */
    if (*p == '#') return 1;

    /* // コメント */
    if (p[0] == '/' && p[1] == '/') return 1;

    /* ''' ブロックコメントトグル */
    if (p[0] == '\'' && p[1] == '\'' && p[2] == '\'') return 2;

    return 0;
}

/* ======================================================================== */
/*  ロードフェーズ: ファイルを読み込み、script_lines[] に格納                */
/*                                                                          */
/*  コメント行・空行・'''ブロックはこの段階で除外する。                       */
/*  ラベル行 (:LABEL) はそのまま保持する。                                   */
/* ======================================================================== */
static int script_load(const char *path)
{
    int raw_buf_size = SCRIPT_MAX_LINES * SCRIPT_MAX_LINE;
    char *raw_buf;
    int fd, sz;
    int bi, li;
    int in_block_comment = 0;
    int cls;
    int more;
    int refused = 0;
    char line_tmp[SCRIPT_MAX_LINE];

    /* raw_buf を動的確保 */
    raw_buf = (char *)g_api->mem_alloc(raw_buf_size);
    if (!raw_buf) {
        g_api->kprintf(ATTR_RED, "%s", "source: out of memory (raw)\n");
        return -1;
    }

    /* script_lines を動的確保 */
    script_lines = (void *)g_api->mem_alloc(SCRIPT_MAX_LINES * SCRIPT_MAX_LINE);
    if (!script_lines) {
        g_api->mem_free(raw_buf);
        g_api->kprintf(ATTR_RED, "%s", "source: out of memory (lines)\n");
        return -1;
    }

    fd = g_api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        g_api->kprintf(ATTR_RED, "source: cannot open %s\n", path);
        g_api->mem_free(raw_buf);
        g_api->mem_free(script_lines);
        script_lines = NULL;
        return -1;
    }
    sz = g_api->sys_read(fd, raw_buf, raw_buf_size - 1);
    /* T2': 読み切れたかを**閉じる前に**確かめる。raw_buf を埋め切ったときは
     * 続きが残っているかもしれず、残っていればスクリプトの途中から先が
     * 無かったことになる (32KB を超えるスクリプトが黙って切れていた)。 */
    more = 0;
    if (sz == raw_buf_size - 1) {
        char probe;
        if (g_api->sys_read(fd, &probe, 1) > 0) more = 1;
    }
    g_api->sys_close(fd);
    if (sz < 0) {
        g_api->kprintf(ATTR_RED, "source: cannot read %s\n", path);
        g_api->mem_free(raw_buf);
        g_api->mem_free(script_lines);
        script_lines = NULL;
        return -1;
    }
    if (sz == 0) {
        /* 票 §2-5-1 / 受入 S13: 空のファイルは「読めない」ではない。
         * 実行する行が 1 つも無いだけなので 0 行で成功させる ($? は 0)。 */
        script_line_count = 0;
        g_api->mem_free(raw_buf);
        return 0;
    }
    if (more) {
        /* 上限の書式には乗らない (読み切れなかった) ので印だけ立てる。 */
        g_api->kprintf(ATTR_RED, "source: %s too large to read in full\n", path);
        sh_refuse_mark();
        g_api->mem_free(raw_buf);
        g_api->mem_free(script_lines);
        script_lines = NULL;
        return -1;
    }
    raw_buf[sz] = '\0';

    script_line_count = 0;
    li = 0;

    for (bi = 0; bi <= sz; bi++) {
        if (raw_buf[bi] == '\n' || raw_buf[bi] == '\r' || raw_buf[bi] == '\0') {
            int i;

            /* \r\n 対応: \r の直後の \n はスキップ */
            if (raw_buf[bi] == '\r' && bi + 1 <= sz && raw_buf[bi + 1] == '\n') {
                bi++;
            }

            /* T2: 行が収まらなかったら**切り詰めた行を実行しない**。
             * 以前はここで 255 バイトへ切って、切れた行がそのまま走った。 */
            if (li > SCRIPT_MAX_LINE - 1) {
                sh_refuse("source: script line", SCRIPT_MAX_LINE - 1);
                refused = 1;
                break;
            }
            line_tmp[li] = '\0';

            /* 行を分類 */
            cls = classify_line(line_tmp);

            if (cls == 2) {
                /* ''' トグル */
                in_block_comment = !in_block_comment;
            } else if (in_block_comment) {
                /* ブロックコメント内 — スキップ */
            } else if (cls == 0) {
                /* 通常行 — 配列に格納 */
                if (script_line_count >= SCRIPT_MAX_LINES) {
                    /* T2: 129 行目以降を捨てて先頭 128 行を実行すると、
                     * 捨てた行 (後始末など) が無かったことになる。
                     * 上限の書式には乗らないので印だけ立てる。 */
                    g_api->kprintf(ATTR_RED, "source: too many lines (max %d)\n",
                                   SCRIPT_MAX_LINES);
                    sh_refuse_mark();
                    refused = 1;
                    break;
                }
                for (i = 0; i < li && i < SCRIPT_MAX_LINE - 1; i++) {
                    script_lines[script_line_count][i] = line_tmp[i];
                }
                script_lines[script_line_count][i] = '\0';
                script_line_count++;
            }
            /* cls == 1 (コメント/空行) → スキップ */

            li = 0;
        } else {
            /* 文字を一時行バッファに蓄積 */
            if (li < SCRIPT_MAX_LINE - 1) {
                line_tmp[li] = raw_buf[bi];
            }
            li++;
        }
    }

    g_api->mem_free(raw_buf);
    if (refused) {
        /* 1 行でも切り詰めたら**スクリプトを実行しない** (票 U3)。
         * script_source_file はここで止まり、source は失敗する。 */
        g_api->mem_free(script_lines);
        script_lines = NULL;
        script_line_count = 0;
        return -1;
    }
    return 0;
}

/* ======================================================================== */
/*  実行フェーズ: script_lines[] を順次実行                                  */
/*                                                                          */
/*  戻り値: 0 = 最後まで / ESC / return / exit で終わった                    */
/*          1 = 行を断ったので打ち切った (票 TASK_SH_TRUNCATION §2-1)        */
/*  *out_status: このスクリプトの `$?` (票 TASK_EXIT_STATUS §2-3)            */
/*    - 最後に実行した行の値                                                 */
/*    - `exit N` で打ち切ったら N                                            */
/*    - ESC で打ち切ったら SH_STATUS_ABORTED                                 */
/*    - 実行する行が 1 つも無ければ 0 (受入 S13)                             */
/* ======================================================================== */
static int script_exec(int *out_status)
{
    int refused = 0;
    int status = SH_STATUS_OK;

    script_current_line = 0;
    script_abort_flag = 0;

    /* 入口では印を触らない。印を消すのは「いちばん外側の execute_command の
     * 入口」1 か所だけで、script_exec へ来る経路は必ずそこを通っている
     * (source / .sh / if / time のどれでも)。印を立てる側は立てたらすぐ
     * 戻るので、ここに古い印が残っていることはない。 */

    while (script_current_line < script_line_count && !script_abort_flag) {
        const char *line = script_lines[script_current_line];
        const char *p = skip_spaces(line);

        /* D2(d) / 票 §2-5: source 中の `exit` はその場で打ち切る。**各行の
         * 前**に見るので goto がここへ巻き戻しても回り続けず、ラベル行でも
         * 抜ける。ネストした source は内側がこれで戻り、script_source_file が
         * script_lines を解放してコンテキストを戻した先で外側もまた同じ判定で
         * 抜ける。**常駐でも見る** — 常駐は cmd_script.c 側の `exit` を
         * 登録しており、シェルは終わらないが打ち切りは同じに効かせる。 */
        if (sh_exit_flag) { status = sh_exit_code; break; }

        /* ラベル行 (:LABEL) はスキップ */
        if (*p == ':') {
            script_current_line++;
            continue;
        }

        /* GUI 端末では**行を実行する前に** WM へ譲る (間引きつき)。ESC の
         * 監視より前に置くのは、GUI 中の打鍵が「WM が動いて注入リングへ
         * 入れる」ことでしか届かないため — 先に譲れば、この行の手前に
         * 打たれた ESC がその場で見える。常駐 (CUI) では消える。 */
        script_yield_gui();

        /* ESC キーブレーク: **覗くだけ** でキューを確認する (KAPI v54)。
         *
         * ここは 1 行ごとに回るので、kbd_trygetkey で「取り出して捨てる」と
         * スクリプト中に打った ESC 以外のキーが全部消える (継承バグ台帳の
         * 「source が ESC 以外も食う」)。kbd_peekkey はキューを 1 バイトも
         * 動かさないので、ESC でなければその打鍵は次の読み手 — この行が
         * 起こすコマンド、あるいはスクリプトが終わった後の行編集 — に
         * そのまま届く。取り除くのは **ESC だと分かってから**、1 回だけ。 */
        {
            int k = g_api->kbd_peekkey();
            if (k >= 0 && (k & 0xFF) == 0x1B) {
                (void)g_api->kbd_trygetkey();   /* ESC 自身は取り除く */
                g_api->kprintf(ATTR_RED, "%s", "^C Script aborted.\n");
                script_abort_flag = 1;
                /* 票 §2-3: ESC 中断は非 0。CTRL+STOP と同じ 130。 */
                status = SH_STATUS_ABORTED;
                break;
            }
        }

        /* コマンド実行 */
        status = execute_command(line);

        /* §2-1: 切り詰めで行を断ったら、そこでスクリプトを打ち切る。
         * 断った行を捨てて次へ進むと、本来 goto で飛び越されるはずだった
         * 後続行 (`rm -rf /data` など) へ落ちてしまう。goto のラベルが
         * 見つからないときと同じ扱いにする。
         * 印は 1 行ぶんの寿命なので、ここで読んで消す。 */
        if (sh_refused_take()) {
            g_api->kprintf(ATTR_RED, "%s", "script: aborted (line refused)\n");
            script_abort_flag = 1;
            refused = 1;
            status = SH_STATUS_USAGE;
            break;
        }

        /* 往復 5 の注意 1: `exit` は値を書いてから要求を立てる。行の値は
         * execute_command が既に sh_exit_code で上書きしてある。 */
        if (sh_exit_flag) { status = sh_exit_code; break; }

        /* 票 §2-5: `set -e` — 失敗した行で打ち切る。行番号 N は
         * **「保持された行の位置」** (1 起点)。コメントと空行は script_load が
         * 詰め、ラベル行 (`:label`) は残るので、ファイルの行番号とも実行した
         * コマンドの本数とも一致しない (往復 2 所見 7)。 */
        if (script_errexit && status != SH_STATUS_OK) {
            g_api->kprintf(ATTR_RED, "script: line %d: status %d\n",
                           script_current_line + 1, status);
            script_abort_flag = 1;
            break;
        }

        script_current_line++;
    }

    if (out_status) *out_status = status;
    return refused;
}

/* ======================================================================== */
/*  公開API: script_source_file — ファイルを読み込んで実行                   */
/*                                                                          */
/*  戻り値 (票 TASK_EXIT_STATUS §2-5-1 で確定):                             */
/*    >= 0               最後に実行した行の状態 (= source の `$?`)          */
/*    -1                 開けない / 読めない / 確保できない / 深すぎる       */
/*    SCRIPT_ERR_REFUSED 行を断って打ち切った (票 §2-1)                     */
/* ======================================================================== */
int script_source_file(const char *path)
{
    char (*saved_lines)[SCRIPT_MAX_LINE];
    int saved_line_count;
    int saved_current_line;
    int saved_abort_flag;
    int saved_errexit;
    int result;

    /* ネスト深度チェック */
    if (g_script_depth >= SCRIPT_MAX_DEPTH) {
        g_api->kprintf(ATTR_RED, "source: nesting too deep (max %d)\n",
                       SCRIPT_MAX_DEPTH);
        return -1;
    }

    /* 現在のコンテキストを退避 (ネスト対応) */
    saved_lines = script_lines;
    saved_line_count = script_line_count;
    saved_current_line = script_current_line;
    saved_abort_flag = script_abort_flag;
    /* `set -e` の旗も入れ子で save / restore する (票 §2-5、受入 S5b)。 */
    saved_errexit = script_errexit;
    script_lines = NULL;

    g_script_depth++;

    /* ロード→実行 */
    result = script_load(path);
    if (result == 0) {
        int status = SH_STATUS_OK;
        /* 断って打ち切ったことは戻り値で親へ伝える。印そのものは
         * script_exec が消しているので、ここで勝手に立て直さない —
         * 立て直すかどうかは呼び手が決める (source は立て直し、
         * 起動時の profile は立て直さずに続行する)。 */
        if (script_exec(&status)) result = SCRIPT_ERR_REFUSED;
        else result = status;
    }

    /* 現在のスクリプト行を解放 */
    if (script_lines) {
        g_api->mem_free(script_lines);
    }

    /* コンテキスト復元 */
    script_lines = saved_lines;
    script_line_count = saved_line_count;
    script_current_line = saved_current_line;
    script_abort_flag = saved_abort_flag;
    script_errexit = saved_errexit;
    g_script_depth--;

    return result;
}

/* ======================================================================== */
/*  source コマンドハンドラ                                                  */
/* ======================================================================== */
static int cmd_source(int argc, char **argv)
{
    int r;

    if (argc < 2) {
        g_api->kprintf(ATTR_RED, "%s", "Usage: source <file>\n");
        return SH_STATUS_USAGE;
    }
    /* 入れ子の source: 内側が断って打ち切ったら、外側のスクリプトも
     * 打ち切る (§2-1)。印を立て直して execute_command 経由で親の
     * script_exec に見せる。 */
    r = script_source_file(argv[1]);
    if (r == SCRIPT_ERR_REFUSED) { sh_refuse_mark(); return SH_STATUS_USAGE; }
    /* 開けない / 読めない / 深すぎる もまとめて 2 (票 §2-5-1)。 */
    if (r < 0) return SH_STATUS_USAGE;
    /* 票 §2-3: `source` は最後に実行した行の値。 */
    return r;
}

/* ======================================================================== */
/*  起動スクリプト (/etc/profile, $HOME/.profile) の入口                     */
/*                                                                          */
/*  断られても**起動は止めない** (票 §2-1 末尾 / 受入 R2)。メッセージを     */
/*  出して既定値のまま続ける。印は立て直さないので、この後の 1 行目が        */
/*  巻き添えで捨てられることもない。                                         */
/* ======================================================================== */
void script_source_profile(const char *path)
{
    int r = script_source_file(path);

    /* 起動は止めないので印は**先に**下ろす。script_load が断った (T2) 場合は
     * script_exec を通らないため印が立ったままで、そのままだと起動後の
     * 1 行目が巻き添えで捨てられる。 */
    if (r < 0) (void)sh_refused_take();

    if (r == SCRIPT_ERR_REFUSED) {
        g_api->kprintf(ATTR_RED,
                       "sh: %s aborted; continuing with defaults\n", path);
    }

    /* 票 §2-5-1: profile の結果を `$?` に残さない。最初のプロンプトで
     * `echo $?` を打ったら 0 であってほしい (初期値は 0)。
     * profile の中の `exit` も起動時には効かせない — 常駐は execute_command が
     * 1 行ごとに下ろしているが、sh.bin は shell_run の入口で見るので、
     * ここでは下ろさない (D2(d) の挙動を変えない)。 */
    sh_status_set(SH_STATUS_OK);
}

/* ======================================================================== */
/*  ask コマンド — ユーザー入力を環境変数に格納                              */
/*                                                                          */
/*  書式: ask "プロンプト文字列" VAR_NAME                                    */
/*  最後の引数が変数名、それ以前の全引数を結合してプロンプトとする。         */
/*  引数中の二重引用符 " は除去する。                                        */
/* ======================================================================== */
static int cmd_ask(int argc, char **argv)
{
    char prompt[ASK_PROMPT_MAX];
    char input[ASK_INPUT_MAX];
    int pi = 0;
    int i, j, len, key;
    int dropped = 0;

    if (argc < 3) {
        g_api->kprintf(ATTR_RED, "%s", "Usage: ask \"prompt\" VAR_NAME\n");
        return SH_STATUS_USAGE;
    }

    /* argv[1]..argv[argc-2] をスペース区切りで結合 (引用符除去)。
     * T18: 収まらないプロンプトは切って出さない — 何を訊かれているか
     * 分からないまま答えを変数に入れることになる。 */
    for (i = 1; i < argc - 1; i++) {
        for (j = 0; argv[i][j]; j++) {
            if (argv[i][j] == '"') continue;
            if (pi >= ASK_PROMPT_MAX - 2) {
                sh_refuse("ask: prompt", ASK_PROMPT_MAX - 2);
                return SH_STATUS_USAGE;
            }
            prompt[pi++] = argv[i][j];
        }
        if (i < argc - 2) {
            if (pi >= ASK_PROMPT_MAX - 2) {
                sh_refuse("ask: prompt", ASK_PROMPT_MAX - 2);
                return SH_STATUS_USAGE;
            }
            prompt[pi++] = ' ';
        }
    }
    prompt[pi] = '\0';

    /* プロンプト表示 */
    g_api->kprintf(ATTR_WHITE, "%s", prompt);

    /* キー入力ループ (Enter まで) */
    len = 0;
    for (;;) {
        key = g_api->kbd_getchar();
        if ((key & 0xFF) == 0x0D || (key & 0xFF) == '\n') {
            break;
        }
        if ((key & 0xFF) == 0x08) {
            /* バックスペース */
            if (len > 0) {
                len--;
#ifdef SHELL_AS_APP
                /* B5: 端末の BS は 1 セル左へ動くだけでセルを消さないので、
                 * BS + 空白 + BS で上書きする (ui.c の行編集と同じ扱い)。
                 * 常駐は console が BS で消すので従来どおり。 */
                sh_erase_cells(input[len]);
#else
                g_api->shell_putchar(0x08, ATTR_WHITE);
#endif
            }
            continue;
        }
        if ((key & 0xFF) >= 0x20 && (key & 0xFF) < 0x7F) {
            /* T18: 255 文字目以降を黙って捨てて変数へ入れると、
             * 打ったものと違う値が登録される。捨てたら印を立てておき、
             * ENTER のところで断る (ui.c の行編集と同じ形)。 */
            if (len >= ASK_INPUT_MAX - 2) {
                dropped = 1;
                continue;
            }
            input[len++] = (char)(key & 0xFF);
            g_api->shell_putchar((char)(key & 0xFF), ATTR_WHITE);
        }
    }
    input[len] = '\0';
    g_api->shell_putchar('\n', ATTR_WHITE);

    if (dropped) {
        sh_refuse("ask: input", ASK_INPUT_MAX - 2);
        return SH_STATUS_USAGE; /* 切れた値は登録しない */
    }

    /* 環境変数にセット (名前 / 値の長さは env_set が見る — 票 T8) */
    env_set(argv[argc - 1], input);
    return 0;
}

/* ======================================================================== */
/*  if コマンド — 1行条件分岐                                                */
/*                                                                          */
/*  書式:                                                                    */
/*    if VAL1 == VAL2 COMMAND...     文字列一致                              */
/*    if VAL1 != VAL2 COMMAND...     文字列不一致                            */
/*    if exist PATH COMMAND...       ファイル存在                            */
/*    if not exist PATH COMMAND...   ファイル非存在                          */
/*                                                                          */
/*  注意: $VAR 展開は execute_command() 到達前に env_expand() で処理済み。   */
/*        引用符 " は parse_args_and_glob() が既に落としている (sh_args.inc  */
/*        の「インプレースでクォート除去」)。argv に残るのはエスケープ等で    */
/*        生き残った " だけなので、strip_quotes はその取りこぼしを掃除する    */
/*        役目になっている。長さの上限は**クォート除去後**で数える。          */
/* ======================================================================== */

/* 内部ヘルパー: 引用符を除去して比較用文字列を取得
 *
 * 戻り値: 除去後の長さ / dst に収まらなければ -1 (票 T1)。
 * 以前はここで黙って max-1 文字に切っていたため、**先頭 255 文字が同じで
 * 256 文字目以降が違う 2 つの値が「等しい」と判定され**、`==` では本来
 * 実行されない枝が走り `!=` では逆に走らなかった。切り詰めた値では比べない。 */
static int strip_quotes(const char *src, char *dst, int max)
{
    int di = 0;
    while (*src) {
        if (*src != '"') {
            if (di >= max - 1) return -1;
            dst[di++] = *src;
        }
        src++;
    }
    dst[di] = '\0';
    return di;
}

/* 内部ヘルパー: argv[start]..argv[argc-1] をスペース区切りで結合
 *
 * 戻り値: 結合後の長さ / buf に収まらなければ -1 (票 T12)。
 * 以前は max - 1 で黙って切っていたので、**切れたコマンド行がそのまま
 * 実行された** (glob 展開で argv が伸びた行で届く)。 */
static int join_args(int argc, char **argv, int start, char *buf, int max)
{
    int bi = 0;
    int i, j;
    for (i = start; i < argc; i++) {
        if (i > start) {
            if (bi >= max - 1) return -1;
            buf[bi++] = ' ';
        }
        for (j = 0; argv[i][j]; j++) {
            if (bi >= max - 1) return -1;
            buf[bi++] = argv[i][j];
        }
    }
    buf[bi] = '\0';
    return bi;
}

static int cmd_if(int argc, char **argv)
{
    int condition = 0;
    int cmd_start = 0;   /* COMMAND... の開始インデックス */

    if (argc < 4) {
        g_api->kprintf(ATTR_RED, "%s", "Usage: if VAL1 == VAL2 COMMAND...\n");
        return SH_STATUS_USAGE;
    }

    /* "if not exist PATH COMMAND..." */
    if (str_eq(argv[1], "not") && argc >= 5 && str_eq(argv[2], "exist")) {
        int fd = g_api->sys_open(argv[3], KAPI_O_RDONLY);
        if (fd >= 0) {
            g_api->sys_close(fd);
            condition = 0; /* 存在する → not exist は偽 */
        } else {
            condition = 1; /* 存在しない → not exist は真 */
        }
        cmd_start = 4;
    }
    /* "if exist PATH COMMAND..." */
    else if (str_eq(argv[1], "exist") && argc >= 4) {
        int fd = g_api->sys_open(argv[2], KAPI_O_RDONLY);
        if (fd >= 0) {
            g_api->sys_close(fd);
            condition = 1;
        } else {
            condition = 0;
        }
        cmd_start = 3;
    }
    /* "if VAL1 == VAL2 COMMAND..." / "if VAL1 != VAL2 COMMAND..." */
    else if (argc >= 5) {
        char v1[IF_VALUE_MAX], v2[IF_VALUE_MAX];
        int n1 = strip_quotes(argv[1], v1, IF_VALUE_MAX);
        int n2 = strip_quotes(argv[3], v2, IF_VALUE_MAX);

        /* 収まらない値は**比べない**。切り詰めて比べると条件が逆になり、
         * 本来実行されない枝が走る (票 T1 / U1)。断った行はここで終わり、
         * スクリプト中なら script_exec が後続行も実行しない (§2-1)。 */
        if (n1 < 0 || n2 < 0) {
            sh_refuse(n1 < 0 ? "if: left value" : "if: right value",
                      IF_VALUE_MAX - 1);
            return SH_STATUS_USAGE;
        }

        if (str_eq(argv[2], "==")) {
            condition = str_eq(v1, v2);
        } else if (str_eq(argv[2], "!=")) {
            condition = !str_eq(v1, v2);
        } else {
            g_api->kprintf(ATTR_RED, "if: unknown operator '%s'\n", argv[2]);
            return SH_STATUS_USAGE;
        }
        cmd_start = 4;
    } else {
        g_api->kprintf(ATTR_RED, "%s", "if: syntax error\n");
        return SH_STATUS_USAGE;
    }

    /* 条件が真のときのみコマンドを実行。票 §2-3: `if` の値は**内側の値**で、
     * 条件が偽なら 0。 */
    if (condition && cmd_start < argc) {
        static char cmd_buf[CMD_BUF_SIZE];
        if (join_args(argc, argv, cmd_start, cmd_buf, CMD_BUF_SIZE) < 0) {
            sh_refuse("if: command line", CMD_BUF_SIZE - 1);
            return SH_STATUS_USAGE;
        }
        return execute_command(cmd_buf);
    }
    return SH_STATUS_OK;
}

/* ======================================================================== */
/*  goto コマンド — スクリプト内のラベルにジャンプ                            */
/*                                                                          */
/*  スクリプト実行コンテキスト外で呼ばれた場合は無害に無視する。             */
/* ======================================================================== */
static int cmd_goto(int argc, char **argv)
{
    int i;
    char label[SCRIPT_MAX_LINE];
    int li = 0;

    if (argc < 2) {
        g_api->kprintf(ATTR_RED, "%s", "Usage: goto LABEL\n");
        return SH_STATUS_USAGE;
    }

    /* スクリプト実行中でなければ無視 */
    if (g_script_depth == 0) {
        g_api->kprintf(ATTR_RED, "%s", "goto: not in a script\n");
        return SH_STATUS_USAGE;
    }

    /* ":LABEL" 形式でラベルを構築 */
    label[li++] = ':';
    {
        const char *s = argv[1];
        while (*s && li < SCRIPT_MAX_LINE - 1) label[li++] = *s++;
    }
    label[li] = '\0';

    /* script_lines[] からラベルを検索 */
    for (i = 0; i < script_line_count; i++) {
        const char *p = skip_spaces(script_lines[i]);
        if (str_eq(p, label)) {
            /* ラベルの次の行から実行を再開 */
            /* script_exec() が current_line++ するので、ラベル行そのものにセット */
            script_current_line = i;
            return SH_STATUS_OK;
        }
    }

    /* ラベルが見つからない (票 §2-3: 非 0) */
    g_api->kprintf(ATTR_RED, "goto: label '%s' not found\n", argv[1]);
    script_abort_flag = 1;
    return SH_STATUS_ERROR;
}

/* ======================================================================== */
/*  return コマンド — スクリプト実行を終了                                    */
/* ======================================================================== */
static int cmd_return(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (g_script_depth == 0) {
        /* 対話シェルから呼ばれた場合は無害に無視 */
        return 0;
    }

    /* current_line を末尾にセットして実行ループを終了させる */
    script_current_line = script_line_count;
    return 0;
}

#ifndef SHELL_AS_APP
/* ======================================================================== */
/*  exit [N] — 常駐シェル版 (票 TASK_EXIT_STATUS §2-5)                       */
/*                                                                          */
/*  **表は先勝ち**なので、sh.bin 側の `exit` (cmd_base.c) とは #ifdef で     */
/*  登録を分けてある。常駐で登録するのはこちら。                             */
/*    スクリプトの中 : 打ち切って `source` の値を N にする                   */
/*    対話           : **シェルを終わらせず** `$?` を N にするだけ           */
/*  どちらも「値を先に、要求をあとで」— 段ループを抜けた後も値が残る         */
/*  (往復 5 の注意 1)。要求はいちばん外側の execute_command が下ろす。       */
/* ======================================================================== */
static int cmd_exit(int argc, char **argv)
{
    int code = 0;

    if (sh_exit_arg(argc, argv, &code) < 0) {
        /* 不正な引数では打ち切らない (票 §2-5-1)。 */
        return SH_STATUS_USAGE;
    }
    sh_status_set(code);        /* 先に値 */
    sh_exit_code = code;
    sh_exit_flag = 1;           /* そのあと要求 (script_exec / 段ループが見る) */
    return code;
}
#endif /* !SHELL_AS_APP */

/* ======================================================================== */
/*  コマンド登録テーブル                                                     */
/* ======================================================================== */
static const ShellCmd script_cmds[] = {
#ifndef SHELL_AS_APP
    { "exit",   cmd_exit,   "[N]",                   "Set $? / stop a script" },
#endif
    { "source", cmd_source, "FILE",                  "Execute script file" },
    { ".",      cmd_source, "FILE",                  "Alias for source" },
    { "ask",    cmd_ask,    "\"prompt\" VAR",         "Read user input into variable" },
    { "if",     cmd_if,     "VAL1 == VAL2 CMD...",   "Conditional execution" },
    { "goto",   cmd_goto,   "LABEL",                 "Jump to label in script" },
    { "return", cmd_return, "",                      "Exit current script" },
    { (const char *)0, 0, 0, 0 }
};

void shell_cmd_script_init(void)
{
    shell_register_cmds(script_cmds);
}

