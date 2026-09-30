#include "shell.h"
#include "config.h"
#include <stdlib.h>
#include "save/libos32save.h"   /* save_crc32 — CRC32 は既存実装を使う */
#include "serial_watchdog.h"    /* 切替後に会話が続いているかの判定 (純粋) */

/* ======================================================================== */
/*  シリアル・リモート連携モジュール (rshell.c)                             */
/* ======================================================================== */

/* recv 用転送バッファ (BSS配置, 4KB — ストリーミングコピー用) */
static u8 xfer_buf[4096];

/* ------------------------------------------------------------------------ */
/*  ホットデプロイ用ステージングバッファ                                     */
/*                                                                          */
/*  ホストは NP21/W の aidebug API (POST /api/mem?space=linear) でこの配列へ */
/*  直接バイト列を書き込み、そのあと hotdeploy コマンドでファイル化させる。  */
/*  シリアルを通さないので hex 2 倍化もエミュレート速度も効かない。          */
/*  設計: docs/tasks/hotdeploy/DESIGN.md (案 A)                              */
/*                                                                          */
/*  シェル帯域は 0x300000 から 468KB (MEM_SHELL_MAX_SIZE)。text 約 58KB +    */
/*  既存 bss 約 59KB に 256KB を足しても収まる。                            */
/* ------------------------------------------------------------------------ */
#define HD_BUF_SIZE (256u * 1024u)
static u8 hd_buf[HD_BUF_SIZE];

/* ------------------------------------------------------------------------ */
/*  切替後の番犬 — **明示の合図 `serial ack` で確認する** (往復 4 で設計変更) */
/*                                                                           */
/*  `serial N` で速度を上げても、**ホストが N で開き直せたかはゲストには     */
/*  分からない**。013Ah が効かない機種、ケーブルが速度に耐えない、ホスト側の  */
/*  開き直しが失敗した — どれでも「こちらは N、ホストは別の速度」になり、     */
/*  戻すための `serial 9600` すら届かない。                                  */
/*                                                                           */
/*  ⚠ 往復 1〜3 では「受信した」「1 行往復した」を証拠に使おうとして、その   */
/*  たびに穴が出た (切替行自身を数える / 断片を数える / EOT の送信失敗を     */
/*  数える / ローカルキーで解除 / 本文が落ちて EOT だけ通る……)。            */
/*  **暗黙の推定は尽きない**ので推定をやめ、合図を明示にした:                */
/*                                                                           */
/*    arm   : rshell 経由の `serial N` が速度を変えた瞬間                     */
/*            (応答 EOT の前後を問わない。EOT の成否も見ない)                */
/*    解除  : **新速度で `serial ack` を受けて実行したときだけ**              */
/*    戻す  : 期限まで ack が来ない / **arm 中に rshell を抜ける**            */
/*                                                                           */
/*  ローカル CUI で打った `serial N` は arm しない — **戻す相手が居ない**。   */
/* ------------------------------------------------------------------------ */
static struct serial_watchdog ser_wd;

/* rshell の中に居るか。`cmd_serial` は「ホストが居るか」をこれで判断する。
 * `g_api->rshell_set_active()` と同じ値をこちら側にも持つ (KAPI に読み口が
 * 無いため)。cmd_serial と cmd_rshell は同じ翻訳単位なので static で足りる。 */
static int rsh_in_rshell;

/* ------------------------------------------------------------------------ */
/*  rsh_getch — rshell の入力読み出しは **必ずここを通す**                   */
/*                                                                           */
/*  **シリアルは 1 回しか読まない** (往復 3 ④)。`kbd_trygetchar()` は rshell */
/*  中シリアルも見るので、これで 2 度読みになると「シリアルを読んだのに       */
/*  ローカル扱い」が起きる。ローカル側は `kbd_trygetchar_local()` (KAPI v57、 */
/*  cooked リングだけ) を使う。                                              */
/* ------------------------------------------------------------------------ */
static int rsh_getch(int *from_serial)
{
    int ch = g_api->serial_trygetchar();

    if (ch >= 0) {                       /* シリアル由来 */
        *from_serial = 1;
        return ch;
    }
    *from_serial = 0;
    return g_api->kbd_trygetchar_local();/* ローカルだけ — シリアルは見ない */
}

/* 行頭のシリアルの ESC の後ろに続きが来るか (票 TASK_SERIAL_HOSTFS §1-v3)。
 * RSH_ESC_ALONE_TICKS 待って来なければ -1 (= 単独)。来ればそのバイト。 */
static int rsh_serial_follow(void)
{
    u32 end = g_api->get_tick() + (u32)RSH_ESC_ALONE_TICKS;
    int ch;

    for (;;) {
        ch = g_api->serial_trygetchar();
        if (ch >= 0) return ch;
        if ((int)(g_api->get_tick() - end) >= 0) return -1;
        g_api->sys_halt();
    }
}

#ifndef SHELL_AS_APP
/* `sfs run` の本体 (下)。rshell の行ループが**行全体を**渡す */
static int sfs_run_child(const char *child);
#endif

/* 直前の設定へ戻す (番犬が REVERT を出したとき)。 */
static void ser_wd_revert(void)
{
    if (ser_wd.prev_mode == KAPI_SER_MODE_COMPAT) {
        g_api->serial_init((u32)ser_wd.prev_baud);
    } else {
        (void)g_api->serial_init_vfast((u32)ser_wd.prev_baud);
    }
    /* **戻したことを画面にも残す** ([V4])。 */
    g_api->kprintf(ATTR_YELLOW,
                   "[ser] no ack after switch: reverted to %ubps\n",
                   (u32)ser_wd.prev_baud);
    /* 戻した速度でホストへ EOT を 1 つ返す。ホストは切替に失敗したあと
     * 元の速度へ開き直して待っているので、これが「戻したよ」の合図になる。 */
    (void)g_api->serial_putchar(0x04);
}

/* rshell の待ちループから毎周呼ぶ。戻り 1 = 速度を戻した。 */
static int ser_wd_poll(void)
{
    if (serial_watchdog_poll(&ser_wd, (unsigned long)g_api->get_tick())
        != SER_WD_REVERT) {
        return 0;
    }
    ser_wd_revert();
    return 1;
}

/* rshell を抜けるときに 1 回呼ぶ (ESC / `exit` / 行の途中の ESC)。
 * **arm 中で未確認なら期限を待たずに戻す** (往復 4 B4) — 抜けたあとは
 * poll する者が居ないので、待っても誰も見に来ない。 */
static void ser_wd_leave(void)
{
    if (serial_watchdog_leave(&ser_wd) == SER_WD_REVERT) {
        ser_wd_revert();
    }
}

/* 引数なしの `serial` が出す現在の設定。**初期化はしない。**
 * 速度を変えるのは `serial <baud>` だけにして、「見るだけ」と「切り替える」を
 * 分ける (V･FAST への切り替えは回線が化けるので、うっかり打てない方がよい)。 */
static void serial_show_status(void)
{
    u32 mode = 0, baud = 0, fifo = 0;

    if (!g_api->serial_is_initialized()) {
        g_api->kprintf(ATTR_YELLOW, "%s",
                       "RS-232C: not initialized ('serial 9600' to start)\n");
        return;
    }
    (void)g_api->serial_get_status(&mode, &baud, &fifo);
    g_api->kprintf(ATTR_CYAN, "RS-232C: mode=%s baud=%u FIFO=%s\n",
                   mode == KAPI_SER_MODE_VFAST ? "V-FAST" : "compat",
                   baud, fifo ? "yes" : "no");
    if (!fifo) {
        g_api->kprintf(ATTR_YELLOW, "%s",
                       "  (no FIFO: V-FAST unavailable, 8253 divisor only)\n");
    }
    /* 受信の誤り (KAPI v66、票 TASK_SERIAL_HOSTFS §1-v2「ISR の計数」)。
     * 実機の切り分け用 — 数が増えるなら速度かケーブルを疑う。 */
    if (g_api->version >= 66) {
        SerialDiag d;
        if (g_api->serial_diag(&d) == 0) {
            g_api->kprintf(d.oe || d.fe || d.pe || d.overflow ? ATTR_YELLOW
                                                              : ATTR_CYAN,
                           "  rx errors: overrun=%u framing=%u parity=%u "
                           "ring_overflow=%u\n",
                           d.oe, d.fe, d.pe, d.overflow);
        }
    }
}

static int cmd_serial(int argc, char **argv)
{
    int vfast;
    int had;
    u32 baud;
    u32 prev_mode = 0;
    u32 prev_baud = 0;

    /* 引数なし = 状態表示。**初期化もマウントもしない。** */
    if (argc < 2) {
        serial_show_status();
        return 0;
    }

    /* ==================================================================== */
    /*  `serial ack` — 速度切替の**明示の合図** (往復 4 で追加)             */
    /*                                                                      */
    /*  ホストが新速度で投げ、ゲストは `ACK <baud> <mode>` を 1 行返す。     */
    /*  **これを受けて実行したときだけ番犬が下りる。** 受信バイト数でも      */
    /*  往復した行数でもない — 暗黙の推定は穴が尽きなかった。               */
    /*                                                                      */
    /*  **arm されていなくても同じ応答** (冪等)。ホストは何回でも投げてよく、 */
    /*  どこかの回で読めればそれが確認になる。速度も初期化もいじらない。     */
    /* ==================================================================== */
    if (strcmp(argv[1], "ack") == 0) {
        u32 mode = 0, ack_baud = 0;

        (void)g_api->serial_get_status(&mode, &ack_baud, (u32 *)0);
        g_api->kprintf(ATTR_GREEN, "ACK %u %s\n", ack_baud,
                       mode == KAPI_SER_MODE_VFAST ? "V-FAST" : "compat");
        serial_watchdog_ack(&ser_wd);
        return 0;
    }

    /* 速度を指定できる (`serial 38400` / `serial 115200`)。
     * **9600 は互換モード** (8253 の整数分周)。1.9968MHz / 2.4576MHz の
     * どちらでもちょうど出る唯一の標準速度で、実機の起動経路もこれ。
     * **それ以外は V･FAST を試す** — FIFO 搭載機 (0136h で判定) で資料の
     * 表にある速度なら 013Ah で 8253 と無関係に出る。入れなければカーネルが
     * 互換モードへ落とし、割り切れなければ `[ser] WARN ...` を出す。 */
    {
        int v = atoi(argv[1]);
        if (v <= 0) {
            g_api->kprintf(ATTR_RED, "%s", "serial: bad baud\n");
            return SH_STATUS_ERROR;
        }
        baud = (u32)v;
    }

    /* 戻し先として **いまの設定** を控えてから切り替える。 */
    (void)g_api->serial_get_status(&prev_mode, &prev_baud, (u32 *)0);
    had = g_api->serial_is_initialized();

    if (baud == (u32)SYS_SERIAL_BAUD) {
        /* 既定速度は従来経路 = 互換モードへ戻す口でもある。 */
        g_api->serial_init(baud);
        vfast = KAPI_SER_INIT_COMPAT;
    } else {
        vfast = g_api->serial_init_vfast(baud);
    }

    /* **「初期化した」と言い切らない。** 分周比が割り切れないと実際の速度は
     * ずれ、その事実はカーネルが直前に `[ser] ...` として出している。
     * ここで要求値を成功として書くと、その行と矛盾する ([V4])。 */
    if (vfast == KAPI_SER_INIT_REFUSED) {
        /* カーネルが**適用しなかった**。速度は変わっていないので、
         * ホストとの足並みも崩れていない = 番犬は要らない。 */
        g_api->kprintf(ATTR_RED,
                       "serial: %ubps は出せないので変更しませんでした "
                       "(理由は上の [ser] refuse 行)\n", baud);
        serial_show_status();
        return SH_STATUS_ERROR;
    }

    g_api->kprintf(ATTR_GREEN, "RS-232C init: requested %ubps "
                               "(actual rate is in the [ser] line above)\n", baud);
    if (vfast != KAPI_SER_INIT_VFAST && baud != (u32)SYS_SERIAL_BAUD) {
        /* 頼んだのに V･FAST へ入れなかった = FIFO 非搭載か表に無い速度。 */
        g_api->kprintf(ATTR_YELLOW, "%s",
                       "  (V-FAST not available: fell back to 8253 divisor)\n");
    }
    serial_show_status();

    /* ==================================================================== */
    /*  **速度が変わった瞬間に番犬を仕掛ける** (往復 4 で設計変更)          */
    /*                                                                      */
    /*  応答 EOT の前後も、その成否も見ない — 見ようとしたのが往復 3 までの  */
    /*  設計で、そのたびに穴が出た。下りるのは `serial ack` を受けたときだけ */
    /*  なので、ここで早く仕掛けても「切替行自身で解除される」ことは無い。   */
    /*                                                                      */
    /*  **rshell 経由のときだけ。** ローカル CUI で打った `serial N` は      */
    /*  戻す相手が居ないので仕掛けない (勝手に戻ると手元の操作の方が驚く。   */
    /*  往復 4 B3 — pending を跨いで後の応答で arm されるのもこれで消える)。 */
    /*  初期化前 (had == 0) も戻し先が無いので仕掛けない。                   */
    /* ==================================================================== */
    if (rsh_in_rshell && had && prev_baud != 0) {
        u32 now_baud = 0;
        (void)g_api->serial_get_status((u32 *)0, &now_baud, (u32 *)0);
        if (now_baud != prev_baud) {
            serial_watchdog_arm(&ser_wd, (unsigned long)g_api->get_tick(),
                                (unsigned long)prev_mode,
                                (unsigned long)prev_baud);
            g_api->kprintf(ATTR_CYAN,
                           "  (watchdog: revert to %ubps unless 'serial ack' "
                           "arrives within %u ticks)\n",
                           prev_baud, (u32)SER_SWITCH_WATCHDOG_TICKS);
        }
    }

    /* 旧版はここで `/host` に SerialFS を自動でマウントしていた (2026-04 に
     * SerialFS ごと削除された名残)。**自動マウントはしない** — SerialFS は
     * `sfs run` のセッションの中だけで付く (票 TASK_SERIAL_HOSTFS §1-v2 B-4')。 */
    return 0;
}

static int cmd_terminal(int argc, char **argv)
{
    int kch, sch;
    (void)argc; (void)argv;
    if (!g_api->serial_is_initialized()) {
        g_api->kprintf(ATTR_RED, "%s", "RS-232C not initialized. Run 'serial 9600' first.\n");
        return SH_STATUS_ERROR;
    }
    g_api->kprintf(ATTR_CYAN, "%s", "Terminal mode (ESC to exit)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "--------------------------------\n");
    for (;;) {
        kch = g_api->kbd_trygetchar();
        if (kch == 0x1B) break;
        if (kch >= 0) {
            g_api->serial_putchar((u8)kch);
            g_api->shell_putchar((char)kch, ATTR_GREEN);
        }
        sch = g_api->serial_trygetchar();
        if (sch >= 0) {
            if (sch == '\r') {
                g_api->shell_putchar('\n', ATTR_YELLOW);
            } else if (sch >= 0x20 || sch == '\n') {
                g_api->shell_putchar((char)sch, ATTR_YELLOW);
            }
        }
        if (kch < 0 && sch < 0) {
            /* 次の割り込み (PIT 100Hz / KBD / SER) まで CPU を止める。
             * get_tick の連打で 1 tick を潰していたが、リング 3 では
             * KAPI 1 回 6us でアイドルが CPU を振り切る。sys_halt なら
             * 待ち時間 (~1 tick) を保ったまま get_tick 呼び出しがゼロになる。 */
            u32 w = g_api->get_tick() + 1;
            while (g_api->get_tick() < w) g_api->sys_halt();
        }
    }
    g_api->kprintf(ATTR_CYAN, "%s", "\n[Terminal closed]\n");
    return 0;
}

/* 1 コマンド分の応答を閉じる。/api/cmd は EOT (0x04) を待っているので、
 * **断ったときも必ずここを通す** — 返さないとホストは timeout まで待ち、
 * 以後のコマンドが全滅する (票 §2-2)。 */
static void rshell_end_reply(void)
{
    g_api->buz_off();
    {
        u32 wait_end = g_api->get_tick() + 1;
        while (g_api->get_tick() < wait_end) g_api->sys_halt();
    }
    /* **EOT を送れたかは診断に出すだけ** (往復 4)。番犬の解除は
     * `serial ack` だけが決めるので、ここの成否は解除に関与しない。
     * それでも黙って捨てない ([V4]) — 送れていないなら、ホストが
     * タイムアウトする理由がここに出ている。 */
    if (g_api->serial_putchar(0x04) != KAPI_SER_TX_OK) {
        g_api->kprintf(ATTR_YELLOW, "%s", "[ser] EOT dropped\n");
    }
}

/* rsh_line_rest (serial_watchdog.c) に渡す線と時計。行頭の待ちと同じ
 * rsh_getch を通す (シリアルは 1 回しか読まない)。 */
static int rsh_io_getch(void *ctx, int *from_serial)
{
    (void)ctx;
    return rsh_getch(from_serial);
}

static unsigned long rsh_io_tick(void *ctx)
{
    (void)ctx;
    return (unsigned long)g_api->get_tick();
}

static void rsh_io_idle(void *ctx)
{
    u32 w = g_api->get_tick() + 1;
    (void)ctx;
    while (g_api->get_tick() < w) g_api->sys_halt();
}

static const struct rsh_io rsh_io_real = {
    rsh_io_getch, rsh_io_tick, rsh_io_idle, (void *)0
};

static int cmd_rshell(int argc, char **argv)
{
    char rbuf[RSHELL_LINE_MAX];
    struct rsh_line ln;
    int ch, fs, r;
    (void)argc; (void)argv;

    if (!g_api->serial_is_initialized()) {
        g_api->kprintf(ATTR_RED, "%s", "Serial not initialized. Run 'serial 9600' first.\n");
        return SH_STATUS_ERROR;
    }

    g_api->rshell_set_active(1);
    rsh_in_rshell = 1;
    g_api->kprintf(ATTR_GREEN, "%s", "Remote shell active (ESC to exit)\n");
    g_api->kprintf(ATTR_CYAN, "%s", "Waiting for commands via serial...\n");
    g_api->serial_putchar(0x04);

    rsh_line_begin(&ln, rbuf, (int)sizeof(rbuf));
    for (;;) {
        /* **ESC を見るより先に** 1 行分の状態を畳む。ln.bytes は抜け口で
         * 「ホストへ EOT を返し損ねていないか」の判定に使うので、前の行の
         * 長さが残っていると待っていないホストへ 2 つ目を返してしまう。 */
        rsh_line_begin(&ln, rbuf, (int)sizeof(rbuf));

        /* ---- 行頭: 1 文字目を待つ ----
         * **受信そのものでは番犬を解除しない。** 解除するのは `serial ack`
         * の行を実行した cmd_serial の 1 か所だけ。 */
        for (;;) {
            ch = rsh_getch(&fs);
            if (ch >= 0) {
                /* 行頭の改行・制御文字は読み飛ばす (ESC は下で判定) */
                if (ch == 0x1B || (ch >= 0x20 && ch < 0x7F)) break;
                continue;
            }
            /* **切替に失敗していないか見る。** 期限まで `serial ack` が
             * 来なければ元の速度へ戻して EOT を返す (ホストはそちらで
             * 待っている)。 */
            (void)ser_wd_poll();
            {
                /* rshell コマンド待ち。sys_halt でアイドル時の get_tick
                 * 連打 (実測 311k/s) を止める。SER 受信 IRQ でも起きる。 */
                u32 w = g_api->get_tick() + 1;
                while (g_api->get_tick() < w) g_api->sys_halt();
            }
        }

        /* ---- 行頭の ESC (票 TASK_SERIAL_HOSTFS §1-v3 / 決裁 1B) ----
         * シリアルの ESC で閉じるのは**単独のとき**だけ。後ろに続きが来たら
         * フレームの断片か化けた行なので、閉じずに「実行しない行」にする。 */
        if (ch == 0x1B && fs) {
            int nxt = rsh_serial_follow();
            r = rsh_line_feed(&ln, ch, fs, 1, nxt >= 0);
            ch = nxt;
            fs = 1;
        } else {
            r = rsh_line_feed(&ln, ch, fs, 1, 0);
            ch = -1;
        }
        if (r == RSH_LINE_EXIT) goto rshell_exit;

        /* ---- 行の残り ----
         * T10: 上限を超えたら**そこで読み取りを止めない**。止めると残りが
         * 次の入力になって勝手に実行される。行末まで読み捨てて印だけ立てる。
         * ESC を含んで拒否した行は、**本当の行末まで**拒否のまま読む
         * (受信の間で解けると残りが次の行として実行される、Codex 8)。沈黙
         * では解かない (往復 2、Codex 3) — 閉じるのはホストの次の改行か
         * 本体の Enter、本体の ESC は rshell を抜ける。 */
        r = rsh_line_rest(&ln, r, ch, fs, &ser_wd, &rsh_io_real);
        if (r == RSH_LINE_REVERT) {
            /* 拒否した行を待つ間に番犬の期限が来た (Codex P2)。行はもう
             * 捨ててある。戻した速度で EOT を返して行頭へ。 */
            ser_wd_revert();
            continue;
        }
        if (r == RSH_LINE_EXIT) goto rshell_exit;

        if (ln.junk) {
            /* ESC を含む行は**実行しない**。EOT は必ず返す (§2-2)。
             * 化けた行やセッションの断片を命令として走らせない。 */
            g_api->kprintf(ATTR_RED, "%s",
                           "rshell: line with ESC not executed\n");
            sh_status_set(SH_STATUS_USAGE);
            rshell_end_reply();
            continue;
        }

        if (ln.overflow) {
            /* 赤字 1 行を出して**実行しない**。EOT は必ず返す (§2-2)。 */
            sh_refuse("rshell: command line", RSHELL_LINE_MAX - 2);
            (void)sh_refused_take();   /* 対話と同じ — 次の行へ持ち越さない */
            /* 票 §2-3 の表: 断った行の `$?` は 2。 */
            sh_status_set(SH_STATUS_USAGE);
            rshell_end_reply();
            continue;
        }

        if (ln.pos == 0) continue;

        g_api->buz_off();

        if (rbuf[0]=='e' && rbuf[1]=='x' && rbuf[2]=='i' && rbuf[3]=='t' && rbuf[4]=='\0') break;

        g_api->kprintf(ATTR_YELLOW, "%s", "> ");
        g_api->kprintf(ATTR_WHITE, "%s", rbuf);
        g_api->kprintf(ATTR_WHITE, "%s", "\n");

#ifndef SHELL_AS_APP
        /* `sfs run ...` は**行全体を**ここで引き受ける (execute_command の
         * 分割より前、Codex 4)。全バイトがシリアル由来の行だけ (決裁 3A)。 */
        if (rsh_sfs_child(rbuf) != (const char *)0) {
            if (ln.local) {
                g_api->kprintf(ATTR_RED, "%s",
                               "sfs: 'sfs run' must be sent by the host "
                               "(line contains local keystrokes)\n");
                sh_status_set(SH_STATUS_USAGE);
            } else {
                sh_status_set(sfs_run_child(rsh_sfs_child(rbuf)));
            }
            (void)sh_refused_take();
            rshell_end_reply();
            continue;
        }
#endif
        /* `$?` は execute_command が入れる。**rshell を抜けるとこの handler の
         * 0 で上書きされる** ので、ホストから見るときは「試験コマンド →
         * 完了待ち → `echo $?` を別送信」の順にすること (票 §2-5-1)。 */
        (void)execute_command(rbuf);

        /* 印を **1 行ぶんで下ろす** (対話の ui.c と同じ扱い)。
         * rshell 自身が execute_command("rshell") の中で走っているので、
         * ここの execute_command は必ず入れ子 (g_exec_depth >= 1) になり、
         * main.c の「いちばん外側だけ消す」が効かない。下ろさないと一度
         * 断りが起きたきり印が立ちっぱなしになり、**以降どのスクリプトも
         * 1 行目で打ち切られる** (2026-09-16 実機で退行として出た)。
         * source の中の打ち切りは script_exec が自分で take して済ませた
         * 後なので、ここで下ろしても壊れない。 */
        (void)sh_refused_take();

        rshell_end_reply();
    }
rshell_exit:
    /* 抜ける口はここ 1 つ — ホストの `exit`、行頭の単独の ESC、本体の ESC。
     * ln.bytes > 0 は「この行のバイトを受け取ったのに、まだ EOT を返して
     * いない」ことと同値なので、そのときだけ閉じる。
     *   - `exit`        : ホストは EOT を待っている → 返す
     *   - 行の途中の本体の ESC: 同じく待っている → 返す
     *   - 行頭の ESC    : 直前の行の EOT は返し終えている → **返さない**。
     *                     返すと 1 コマンドに EOT が 2 つ出て、/api/cmd は
     *                     次のコマンドの終端と取り違える (票 §2-2 の裏)。 */
    if (ln.bytes > 0) rshell_end_reply();

    rsh_in_rshell = 0;
    /* **arm 中で未確認なら、抜ける前にここで戻す** (往復 4 B4)。
     * 抜けたあとは ser_wd_poll を呼ぶ者が居ないので、番犬が仕掛かったまま
     * 忘れられて「確認の取れていない速度」のまま会話が死ぬ。 */
    ser_wd_leave();
    g_api->rshell_set_active(0);
    g_api->kprintf(ATTR_CYAN, "%s", "\n[Remote shell closed]\n");
    return 0;
}

#ifndef SHELL_AS_APP
/* ------------------------------------------------------------------------ */
/*  sfs run <コマンド行> — シリアル越しの /host で 1 コマンドを走らせる       */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md §1-v3 / ユーザー決裁          */
/*  (2026-09-24)。**常駐シェルだけ** (sh.bin には入れない) で、**ホストが    */
/*  rshell へ送った 1 行が丸ごと `sfs run ...` のときだけ** 動く (決裁 3A)。 */
/*  ホストは `rshell_serial.py --serve-host <dir> cmd "sfs run hsync boot"`。 */
/*                                                                          */
/*    sfs_begin (ゲート → HELLO → /host にマウント)                          */
/*    → 子を execute_command で走らせる (通常 / 非ゼロ / fault kill /        */
/*      CTRL+STOP のどれでもここへ戻る)                                      */
/*    → sfs_end (BYE → アンマウント → 隔離 → 溜めた出力と sfs: exit=N を     */
/*      フレームで送る → ゲートを下ろす)                                     */
/*    → rshell が行末の EOT を返す                                          */
/*                                                                          */
/*  vfs_mount は cwd を `/` に戻すので、始まりと終わりで元の cwd へ戻す      */
/*  (§1-v3「その他」)。                                                     */
/* ------------------------------------------------------------------------ */
static void sfs_restore_cwd(const char *saved)
{
    if (saved[0]) (void)g_api->sys_chdir(saved);
}

/* `sfs run` は**rshell の行ループが行全体を引き受けて**動かす (レビュー往復 1、
 * Codex 4 / Fable m1)。execute_command の分割 (`|` `;` `&&` `>`) を通ると、
 * `sfs run a | b` の後段がセッションの外でもう一度走る。組込みとしての
 * `sfs` はここへ来た時点で必ず断る (本体のキーボード・スクリプト・パイプの中)。 */
static int cmd_sfs(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "run") != 0) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }
    g_api->kprintf(ATTR_RED, "%s",
                   "sfs: 'sfs run' works only as a whole line sent by the "
                   "host over rshell (rshell_serial.py --serve-host)\n");
    return SH_STATUS_USAGE;
}

static int sfs_run_child(const char *child)
{
    char saved[OS32_MAX_PATH];
    const char *cwd;
    int rc, status, i;

    if (g_api->version < 66) {
        g_api->kprintf(ATTR_RED, "sfs: kernel KAPI v%d < 66\n",
                       (int)g_api->version);
        return SH_STATUS_ERROR;
    }

    saved[0] = '\0';
    cwd = g_api->sys_getcwd();
    if (cwd) {
        for (i = 0; i < OS32_MAX_PATH - 1 && cwd[i]; i++) saved[i] = cwd[i];
        saved[i] = '\0';
        if (cwd[i]) saved[0] = '\0';          /* 収まらない = 戻さない */
    }

    rc = g_api->sfs_begin();
    sfs_restore_cwd(saved);
    if (rc != 0) {
        /* ゲートは下りている。ここの文字はそのままホストへ届く */
        g_api->kprintf(ATTR_RED, "sfs: session not started (%d%s)\n", rc,
                       rc == OS32_ERR_BUSY ? ": /host in use or session active" :
                       rc == OS32_ERR_IO ? ": no host answered" : "");
        return SH_STATUS_ERROR;
    }

    /* 子の中の `sfs run` は組込みが断る (sfs_begin も BUSY)。子の行の中の
     * パイプやリダイレクトはセッションの中で普通に動く */
    status = execute_command(child);

    /* 子がどう終わってもここへ来る。**ここより後は何も出さない** —
     * ゲートを下ろした後の文字はフレームの外の生の文字としてホストへ行く */
    (void)g_api->sfs_end(status);
    sfs_restore_cwd(saved);
    return status;
}
#endif

static int cmd_send(int argc, char **argv)
{
    int i;
    if (!g_api->serial_is_initialized()) {
        g_api->kprintf(ATTR_RED, "%s", "RS-232C not initialized. Run 'serial 9600' first.\n");
        return SH_STATUS_ERROR;
    }
    for (i = 1; i < argc; i++) {
        g_api->serial_puts(argv[i]);
        if (i < argc - 1) g_api->serial_putchar(' ');
    }
    g_api->serial_putchar('\r');
    g_api->serial_putchar('\n');
    g_api->kprintf(ATTR_YELLOW, "%s", "Sent\n");
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  hotdeploy — ステージングバッファの内容をファイルへ書き出す               */
/*                                                                          */
/*    hotdeploy                        ステージング領域の番地とサイズを報告  */
/*    hotdeploy PATH LEN CRC32         hd_buf[0..LEN) を PATH へ書き込む     */
/*                                                                          */
/*  出力はホスト側ラッパが読むので機械可読に保つこと                         */
/*  (HOTDEPLOY base= / HOTDEPLOY OK / HOTDEPLOY ERR)。                       */
/* ------------------------------------------------------------------------ */
static int cmd_hotdeploy(int argc, char **argv)
{
    const char *path;
    u32 len, want, got;
    int fd;

    /* 引数なし: ホストが書き込み先を知るための問い合わせ */
    if (argc < 2) {
        g_api->kprintf(ATTR_CYAN, "HOTDEPLOY base=0x%08X size=%u\n",
                       (u32)(unsigned long)hd_buf, (u32)HD_BUF_SIZE);
        return 0;
    }
    if (argc < 4) {
        g_api->kprintf(ATTR_RED, "%s", "HOTDEPLOY ERR usage\n");
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    path = argv[1];
    len  = (u32)strtoul(argv[2], (char **)0, 0);
    want = (u32)strtoul(argv[3], (char **)0, 0);

    if (len == 0 || len > HD_BUF_SIZE) {
        g_api->kprintf(ATTR_RED, "HOTDEPLOY ERR bad-length %u\n", len);
        return SH_STATUS_ERROR;
    }

    /* ホストが書き終える前に叩かれた場合や、前回の残骸を掴んだ場合を
     * ここで落とす。CRC を必須にしているのはそのため。 */
    got = save_crc32(hd_buf, len);
    if (got != want) {
        g_api->kprintf(ATTR_RED, "HOTDEPLOY ERR crc want=0x%08X got=0x%08X\n",
                       want, got);
        return SH_STATUS_ERROR;
    }

    fd = g_api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd < 0) {
        g_api->kprintf(ATTR_RED, "HOTDEPLOY ERR open %s\n", path);
        return SH_STATUS_ERROR;
    }
    if ((u32)g_api->sys_write(fd, hd_buf, len) != len) {
        g_api->kprintf(ATTR_RED, "%s", "HOTDEPLOY ERR write\n");
        g_api->sys_close(fd);
        return SH_STATUS_ERROR;
    }
    g_api->sys_close(fd);

    g_api->kprintf(ATTR_GREEN, "HOTDEPLOY OK %s %u\n", path, len);
    return 0;
}

/* host: プレフィックスを /host/ パスに変換するヘルパ
 *
 * 戻り値: 1 = 変換した / 0 = "host:" で始まっていない /
 *         -1 = out に収まらない (票 T19)。
 * 以前は max - 1 で黙って切っていたので、`push` / `recv` が
 * **切れた別のパス**を O_TRUNC で作っていた。 */
static int resolve_host_path(const char *arg, char *out, int max)
{
    const char *p;
    int i;
    /* "host:" で始まるか判定 */
    if (arg[0] != 'h' || arg[1] != 'o' || arg[2] != 's' ||
        arg[3] != 't' || arg[4] != ':') return 0;

    p = arg + 5;  /* "host:" の後ろ */
    out[0] = '/'; out[1] = 'h'; out[2] = 'o'; out[3] = 's'; out[4] = 't';
    i = 5;
    if (*p != '/') { out[i++] = '/'; }  /* / を補完 */
    while (*p) {
        if (i >= max - 1) return -1;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

static int cmd_recv(int argc, char **argv)
{

    /* SerialFS モード: recv host:/path [localpath]
     *
     * パスは専用バッファに置く。以前は xfer_buf の先頭に置いたうえで
     * 同じ buf にデータを読み込んでおり、argc<3 のとき local_path が
     * xfer_buf の内部を指すという別名参照になっていた。 */
    static char host_path[RSHELL_HOST_PATH_MAX];
    int hp;

    hp = (argc >= 2) ? resolve_host_path(argv[1], host_path,
                                         (int)sizeof(host_path)) : 0;
    if (hp < 0) {
        sh_refuse("recv: host path", (int)sizeof(host_path) - 1);
        return SH_STATUS_USAGE;
    }
    if (hp > 0) {
        const char *local_path;
        int fd_in, fd_out, n;
        u32 total;
        u32 t0, t1, elapsed;

        /* ローカルパスの決定 */
        if (argc >= 3) {
            local_path = argv[2];
        } else {
            /* ファイル名のみ抽出 */
            const char *p = host_path;
            const char *last_slash = host_path;
            while (*p) { if (*p == '/') last_slash = p + 1; p++; }
            local_path = last_slash;
        }

        g_api->kprintf(ATTR_CYAN, "Downloading: %s -> %s\n", host_path, local_path);
        t0 = g_api->get_tick();

        fd_in = g_api->sys_open(host_path, KAPI_O_RDONLY);
        if (fd_in < 0) {
            g_api->kprintf(ATTR_RED, "recv: %s not found\n", host_path);
            return SH_STATUS_ERROR;
        }
        fd_out = g_api->sys_open(local_path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
        if (fd_out < 0) {
            g_api->kprintf(ATTR_RED, "recv: cannot create %s\n", local_path);
            g_api->sys_close(fd_in);
            return SH_STATUS_ERROR;
        }

        /* xfer_buf 単位で読み切るまで回す。以前は sys_read が 1 回だけで、
         * 4096 バイトを超えるファイルは黙って切り詰められていた。 */
        total = 0;
        for (;;) {
            n = g_api->sys_read(fd_in, xfer_buf, sizeof(xfer_buf));
            if (n < 0) {
                g_api->kprintf(ATTR_RED, "%s", "recv: read failed\n");
                g_api->sys_close(fd_in);
                g_api->sys_close(fd_out);
                return SH_STATUS_ERROR;
            }
            if (n == 0) break;
            if ((int)g_api->sys_write(fd_out, xfer_buf, (u32)n) != n) {
                g_api->kprintf(ATTR_RED, "%s", "recv: write failed\n");
                g_api->sys_close(fd_in);
                g_api->sys_close(fd_out);
                return SH_STATUS_ERROR;
            }
            total += (u32)n;
        }
        g_api->sys_close(fd_in);
        g_api->sys_close(fd_out);

        t1 = g_api->get_tick();
        elapsed = t1 - t0;
        if (elapsed == 0) elapsed = 1;
        g_api->kprintf(ATTR_GREEN, "Received %u bytes", total);
        g_api->kprintf(ATTR_GREEN, " (%u.%02us, ",
                       elapsed / 100, elapsed % 100);
        g_api->kprintf(ATTR_GREEN, "%u B/s)\n",
                       total * 100 / elapsed);
        return 0;
    }

    /* 引数なし or host: プレフィックスなし */
    shell_print_help(argv[0]);
    return SH_STATUS_USAGE;
}

static int cmd_push(int argc, char **argv)
{
    char host_path[RSHELL_HOST_PATH_MAX];
    const char *local_path;
    int fd_in, fd_out, n, hp;
    u32 total;
    u32 t0, t1, elapsed;

    if (argc < 3) {
        shell_print_help(argv[0]);
        return SH_STATUS_USAGE;
    }

    local_path = argv[1];

    /* 宛先が host: プレフィックスか確認 */
    hp = resolve_host_path(argv[2], host_path, (int)sizeof(host_path));
    if (hp < 0) {
        sh_refuse("push: host path", (int)sizeof(host_path) - 1);
        return SH_STATUS_USAGE;
    }
    if (hp == 0) {
        g_api->kprintf(ATTR_RED, "%s", "push: destination must be host:path\n");
        return SH_STATUS_ERROR;
    }

    g_api->kprintf(ATTR_CYAN, "Uploading: %s -> %s\n", local_path, host_path);
    t0 = g_api->get_tick();

    fd_in = g_api->sys_open(local_path, KAPI_O_RDONLY);
    if (fd_in < 0) {
        g_api->kprintf(ATTR_RED, "push: %s not found\n", local_path);
        return SH_STATUS_ERROR;
    }

    fd_out = g_api->sys_open(host_path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
    if (fd_out < 0) {
        g_api->kprintf(ATTR_RED, "push: cannot create %s\n", host_path);
        g_api->sys_close(fd_in);
        return SH_STATUS_ERROR;
    }

    /* T24: xfer_buf 単位で**読み切るまで回す** (cmd_recv と同じ形)。
     * 以前は sys_read が 1 回だけで、4KB を超えるファイルは先頭 4KB だけを
     * 送って `Sent` と報告していた。 */
    total = 0;
    for (;;) {
        n = g_api->sys_read(fd_in, xfer_buf, sizeof(xfer_buf));
        if (n < 0) {
            g_api->kprintf(ATTR_RED, "%s", "push: read failed\n");
            g_api->sys_close(fd_in);
            g_api->sys_close(fd_out);
            return SH_STATUS_ERROR;
        }
        if (n == 0) break;
        if ((int)g_api->sys_write(fd_out, xfer_buf, (u32)n) != n) {
            g_api->kprintf(ATTR_RED, "%s", "push: write failed\n");
            g_api->sys_close(fd_in);
            g_api->sys_close(fd_out);
            return SH_STATUS_ERROR;
        }
        total += (u32)n;
    }
    g_api->sys_close(fd_in);
    g_api->sys_close(fd_out);

    t1 = g_api->get_tick();
    elapsed = t1 - t0;
    if (elapsed == 0) elapsed = 1;
    g_api->kprintf(ATTR_GREEN, "Sent %u bytes", total);
    g_api->kprintf(ATTR_GREEN, " (%u.%02us, ",
                   elapsed / 100, elapsed % 100);
    g_api->kprintf(ATTR_GREEN, "%u B/s)\n",
                   total * 100 / elapsed);
    return 0;
}

static int cmd_tvdump(int argc, char **argv)
{
    volatile u16 *text = (volatile u16 *)0xA0000UL;
    volatile u8  *attr_base = (volatile u8 *)0xA2000UL;
    int row, col;
    (void)argc; (void)argv;

    if (!g_api->serial_is_initialized()) {
        g_api->kprintf(ATTR_RED, "%s", "Serial not initialized.\n");
        return SH_STATUS_ERROR;
    }

    g_api->serial_putchar('T'); g_api->serial_putchar('V');
    g_api->serial_putchar('D'); g_api->serial_putchar('M');
    g_api->serial_putchar(80);  g_api->serial_putchar(25);

    for (row = 0; row < 25; row++) {
        for (col = 0; col < 80; col++) {
            int idx = row * 80 + col;
            u16 ch_val = text[idx];
            u8  at = attr_base[idx * 2];
            g_api->serial_putchar((u8)(ch_val & 0xFF));
            g_api->serial_putchar(at);
        }
    }
    g_api->kprintf(ATTR_GREEN, "%s", "TVRAM dump sent\n");
    return 0;
}

/* 登録用テーブル */
static const ShellCmd rshell_cmds[] = {
    { "serial",   cmd_serial,   "[baud]",        "Show RS-232C status / init at baud (115200 = V-FAST)" },
    { "terminal", cmd_terminal, "",              "Enter serial terminal mode" },
    { "rshell",   cmd_rshell,   "",              "Start remote shell host" },
    { "send",     cmd_send,     "TEXT...",       "Send text via serial" },
    { "hotdeploy", cmd_hotdeploy, "[PATH LEN CRC32]",
      "Write staging buffer to file (no args: report buffer address)" },
    { "recv",     cmd_recv,     "[host:PATH [LOCAL]]", "Receive file (SerialFS or legacy)" },
    { "push",     cmd_push,     "LOCAL host:PATH",     "Upload file to host via SerialFS" },
    { "tvdump",   cmd_tvdump,   "",              "Dump Text VRAM over serial" },
#ifndef SHELL_AS_APP
    { "sfs",      cmd_sfs,      "run COMMAND...",
      "Run COMMAND with the host directory on /host over serial (from rshell_serial.py --serve-host only)" },
#endif
    { (const char *)0, 0, 0, 0 }
};

void shell_rshell_init(void)
{
    shell_register_cmds(rshell_cmds);
}
