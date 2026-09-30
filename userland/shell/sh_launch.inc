/* ========================================================================
 *  sh_launch.inc — SHELL_AS_APP の外部プログラム起動 (票 T9 D3a、KAPI v49)
 *
 *  main.c が #ifdef SHELL_AS_APP の中で #include する。拡張子が .c でない
 *  のは build/programs.mk の SHELL_SRC が userland/shell 下の .c を wildcard で拾い、
 *  独立した .c にすると常駐 shell.bin のリンクにも空の .o が混ざるため
 *  (受入 S7 は常駐の SHA-256 一致)。
 *
 *  ホスト TDD (tools/tests/sh_launch_host.c) はこのファイルをそのまま
 *  #include して DONE / FAILED / STALE / FULL の 4 経路を見る — 模型ではなく
 *  出荷するソースそのもの。
 * ======================================================================== */

/* ======================================================================== */
/*  sh_launch — 要求表経由の起動 (票 T9 D3a、KAPI v49)                       */
/*                                                                          */
/*  launch_req(cmdline) で要求を出し、sys_yield() で CPU を WM に渡しながら   */
/*  launch_poll(token) を読む。待っている間は kbd_getchar / kbd_trygetchar / */
/*  ime_getkey を呼ばない — WAIT_KEY へ park すると注入リングの 1 バイトを    */
/*  子と奪い合う (票 §9 blocker 1 と同じ穴)。                                 */
/*                                                                          */
/*  戻り値は exec_run と同じ EXEC_* 系にそろえる。PATH 走査                   */
/*  (try_exec_from_path) は NOT_FOUND / GENERAL を「次の候補へ」と読むので、  */
/*  その 2 つでは印字しない (印字すると候補の数だけ同じ行が出る)。            */
/* ======================================================================== */
#define SH_LAUNCH_NAME_MAX 64

/* GUI 外 (CUI から sh.bin を直に起動した等) を 1 度だけ報せるための印。
 * **票 TASK_EXIT_STATUS R1b で走査が止まる形になった** ので、同じ行が候補の
 * 数だけ出ることはもう無い。それでも `exec` / `if` / スクリプトから何度も
 * 来たときに同じ理由を並べないよう、印は残してある。 */
static int sh_launch_nogui = 0;

/* B4: パイプの段を回している間だけ立つ入れ子カウンタ。execute_command の
 * パイプライン経路が段ループの前後で増減させる (source / exec 経由の入れ子
 * パイプもあるので真偽値ではなく数える)。 */
static int sh_pipeline_depth = 0;

void sh_pipeline_enter(void) { sh_pipeline_depth++; }
void sh_pipeline_leave(void)
{
    if (sh_pipeline_depth > 0) sh_pipeline_depth--;
}

/* R2: いま sh が標準 FD にリダイレクトを張っているか。リダイレクト表は全
 * アプリ共有で read/write/reset が owner を見ないので、張ったまま外部を
 * 起こすと子の出力が親のファイルへ入り、子の reset が親の FD を閉じる。
 * `sys_reset_redirect` は表を丸ごと戻すので真偽値で足りる。 */
static int sh_redirect_active = 0;

void sh_redirect_mark(void)  { sh_redirect_active = 1; }
void sh_redirect_clear(void) { sh_redirect_active = 0; }

static void sh_launch_name(const char *cmdline, char *out, int max)
{
    int i = 0;
    while (cmdline[i] && cmdline[i] != ' ' && i < max - 1) {
        out[i] = cmdline[i];
        i++;
    }
    out[i] = '\0';
}

/* ------------------------------------------------------------------------ */
/*  sh_exec_result (sh.bin) — 票 TASK_EXIT_STATUS §2-2                       */
/*                                                                          */
/*  **カーネルの記録 (exec_last_result) は読まない**。要求表の子は WM が      */
/*  起こす別アプリで、静的な記録を読むと自分の子とは限らないものを拾う       */
/*  (往復 1 所見 4)。写像は要求表の状態そのもの:                             */
/*    LAUNCH_ST_DONE   → (EXITED, 0)   … 終了コードは別票 (§2-6)             */
/*    LAUNCH_ST_FAILED → 起動失敗の種別                                      */
/*  起こす前に断った (パイプ / リダイレクト / 長さ / GUI 外) 場合は           */
/*  「起こせなかった」= NOMEM 相当 で、**走査を止める** (R1b)。               */
/* ------------------------------------------------------------------------ */
static int sh_launch_kind = EXEC_KIND_NONE;

int sh_launch(const char *cmdline)
{
    char name[SH_LAUNCH_NAME_MAX];
    /* token / status は KAPI の i32 (= long)。int で受けると -Wincompatible-
     * pointer-types になるので型をそろえ、印字のときだけ int へ落とす。 */
    i32 token, st, rc;

    sh_launch_name(cmdline, name, SH_LAUNCH_NAME_MAX);

    /* T11: カーネルの要求表が受け取るのは NUL 終端 1〜LAUNCH_CMDLINE_MAX-1
     * バイトだけ (exec/launch.c の len 検査)。超えたまま launch_req を呼ぶと
     * OS32_ERR_INVAL が返り、下の分岐がそれを「GUI 外」と読み違えて
     * "external programs need the GUI terminal" を出す — 理由が入れ替わる。
     * **送る前に**測って断る。カーネルの上限は変えない (票 §6)。 */
    {
        int len = 0;
        while (cmdline[len]) len++;
        if (len >= LAUNCH_CMDLINE_MAX) {
            sh_refuse("sh: launch command line", LAUNCH_CMDLINE_MAX - 1);
            /* 印が立つので呼び手はそちらを先に見る。種別は NONE のまま。 */
            return EXEC_ERR_GENERAL;
        }
    }

    /* B4: パイプの中から外部プログラムへは行けない。要求表経由で WM が
     * 起こすのは**別アプリ**で、sh の FD に掛けたパイプは届かないため、
     * 起動すると黙って出力が落ちる。先頭語の事前判定 (sh_stage_is_builtin)
     * をすり抜けた経路もここで捕まる。 */
    if (sh_pipeline_depth > 0) {
        g_api->kprintf(ATTR_RED, "%s",
                       "sh: pipe to external command is not supported\n");
        sh_launch_kind = EXEC_KIND_NOMEM;   /* 起こせなかった = 走査を止める */
        return EXEC_ERR_GENERAL;
    }

    /* R2: 内蔵 `exec` / `if` / `time` / 外部行を含む `source` は、
     * execute_single の事前判定を通った**後**にリダイレクトを張ってから
     * ここへ来る。最終起動口でもう一度見る。 */
    if (sh_redirect_active) {
        g_api->kprintf(ATTR_RED, "%s",
                       "sh: redirect to external command is not supported\n");
        sh_launch_kind = EXEC_KIND_NOMEM;
        return EXEC_ERR_GENERAL;
    }

    token = g_api->launch_req(cmdline);
    if (token <= 0) {
        if (token == OS32_ERR_INVAL) {
            /* GUI 外。1 度だけ理由を出し、あとは黙って次の PATH 候補へ回す
             * (最後は run_cmd_internal の "command not found" で閉じる)。 */
            if (!sh_launch_nogui) {
                sh_launch_nogui = 1;
                g_api->kprintf(ATTR_RED, "%s",
                               "sh: external programs need the GUI terminal\n");
            }
            /* R1b: 「見つからない」ではなく「起こせなかった」。以前は
             * NOT_FOUND を返して PATH の候補ぶん繰り返していた。 */
            sh_launch_kind = EXEC_KIND_NOMEM;
            return EXEC_ERR_GENERAL;
        }
        if (token == OS32_ERR_FULL)
            g_api->kprintf(ATTR_RED, "sh: %s: busy\n", name);
        else
            g_api->kprintf(ATTR_RED, "sh: %s: launch_req failed (%d)\n",
                           name, (int)token);
        sh_launch_kind = EXEC_KIND_NOMEM;
        return EXEC_ERR_GENERAL;
    }
    sh_launch_nogui = 0;   /* 通った — 次に GUI 外へ落ちたらまた報せる */

    for (;;) {
        /* 明示的な譲り。GUI 中は必ず WAIT_POLL へ park し、その間に WM が
         * 要求を取って起動し、子の回収を表へ返す (票 D5)。 */
        g_api->sys_yield();

        st = 0;
        rc = g_api->launch_poll(token, &st);
        if (rc < 0) {
            /* OS32_ERR_STALE 等。表が消えた以上これ以上の状態は来ない */
            g_api->kprintf(ATTR_RED, "sh: %s: launch_poll failed (%d)\n",
                           name, (int)rc);
            sh_launch_kind = EXEC_KIND_NOMEM;
            return EXEC_ERR_GENERAL;
        }

        if ((st & 0xF00) == LAUNCH_ST_DONE) {
            /* §2-6: この票では終了コードが表に載らないので、DONE は
             * 「起動できて終わった」までしか分からない (EXITED 0)。 */
            sh_launch_kind = EXEC_KIND_EXITED;
            return EXEC_SUCCESS;
        }

        if ((st & 0xF00) == LAUNCH_ST_FAILED) {
            int frc = (int)-(st & 0xFF);
            if (frc == 0) frc = EXEC_ERR_GENERAL;
            if (frc != EXEC_ERR_NOT_FOUND && frc != EXEC_ERR_GENERAL)
                g_api->kprintf(ATTR_RED, "sh: %s: launch failed (%d)\n",
                               name, frc);
            switch (frc) {
            case EXEC_ERR_NOT_FOUND: sh_launch_kind = EXEC_KIND_NOT_FOUND; break;
            case EXEC_ERR_INVALID:   sh_launch_kind = EXEC_KIND_INVALID;   break;
            case EXEC_ERR_NOMEM:     sh_launch_kind = EXEC_KIND_NOMEM;     break;
            case EXEC_ERR_FAULT:     sh_launch_kind = EXEC_KIND_FAULT;     break;
            default:                 sh_launch_kind = EXEC_KIND_NOMEM;     break;
            }
            return frc;
        }
        /* PENDING / TAKEN / RUNNING: まだ終わっていない */
    }
}

/* sh_launch の**後ろ**に置く: tools/tests/sh_launch_host.c はこの .inc だけを
 * #include する (shell.h を通さない) ので、前に置くと宣言が無い。 */
int sh_exec_result(const char *cmdline, int *kind, int *code)
{
    int rc;

    sh_launch_kind = EXEC_KIND_NONE;
    rc = sh_launch(cmdline);
    if (kind) *kind = sh_launch_kind;
    if (code) *code = 0;
    return rc;
}
