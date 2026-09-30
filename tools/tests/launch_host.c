/* ========================================================================
 *  launch_host.c — 起動要求表を **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_T9_sh.md §1 D3 / §1a (K 側)
 *  実行:   python3 -B tools/tests/test_launch.py
 *  記録:   tools/tests/t9_tdd.md
 *
 *  exec/launch.c と exec/appslot.c を 1 行も写さずそのまま #include する
 *  (模型ではない)。カーネル帯の代わりにハーネスが持つのは 3 つだけ:
 *    - 所有者 res_owner_get/set (fs/fd_redirect.c)
 *    - GUI 中かどうか con_sink_is_enabled (kernel/con_sink.c)
 *    - kstrncpy (lib/kstring_asm.asm。n は**バッファ全体サイズ**)
 *  スロットは実物の AppSlot を直に組み立てる (exec/exec.c の起動経路は
 *  CR3 / pgalloc を引くのでホストへは持ち込めない)。
 *
 *  tools/tests/con_sink_host.c と同じ様式 — ホスト ILP32 GNU89 で走らせ、
 *  同じソースが i386-elf-gcc -Werror でも通ることを別に見る ([C1])。
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 * ======================================================================== */

#include "types.h"

/* ---- カーネル帯の代わり ------------------------------------------------ */
static int host_owner = 1;
void res_owner_set(int owner) { host_owner = owner; }
int  res_owner_get(void)      { return host_owner; }

static int host_gui = 1;                  /* con_sink 有効 = GUI 中 */
int con_sink_is_enabled(void) { return host_gui; }

char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    while (i + 1 < n && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return dst;
}

/* appslot.c が引く (票 T9 §12 T1: リダイレクト表を ID の文脈にする)。
 * この票の対象外なので空の錠にする — 表そのものは実物の fs/fd_redirect.c と
 * 組んで tools/tests/multiapp_impl_host.c のケース 24 が見ている。 */
#include "fd_redirect.h"
void fd_redirect_save(FdRedirectState *out)         { (void)out; }
void fd_redirect_restore(const FdRedirectState *in) { (void)in; }
void fd_redirect_clear_state(FdRedirectState *st)   { (void)st; }

/* 実物。appslot.c が先 (launch.c が AppSlot を引く)。 */
#include "appslot.c"
#include "launch.c"

/* ---- 最小の報告系 (libc 無し) ------------------------------------------ */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text)
{
    u32 len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;

static void check(int cond, const char *name)
{
    report(cond ? "  ok   " : "  FAIL ");
    report(name);
    report("\n");
    if (!cond) failures++;
}

/* ---- 試験の道具 -------------------------------------------------------- */

static char takebuf[LAUNCH_CMDLINE_MAX];

static int str_eq(const char *a, const char *b)
{
    u32 i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/* CPL=3 アプリの AppSlot を 1 本組み立てる (実物の欄をそのまま使う)。 */
static void make_app(int id, int gui, u32 flags)
{
    AppSlot *a = appslot_at(id);
    a->state = APP_STATE_RUNNING;
    a->cpl3 = 1;
    a->gui = gui;
    a->hdr_flags = flags;
}

static void kill_app(int id)
{
    AppSlot *a = appslot_at(id);
    a->state = APP_STATE_FREE;
    a->cpl3 = 0;
    a->gui = 0;
    a->hdr_flags = 0;
}

/* 試験ごとに「GUI 中、WM top-level、ID 2/3 は LAUNCHER 宣言のアプリ」から。 */
static void reset_all(void)
{
    int i;
    appslot_init();
    launch_init();
    host_gui = 1;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) kill_app(i);
    make_app(APP_ID_MIN,     1, (u32)OS32X_FLAG_LAUNCHER);   /* 2 = 端末 */
    make_app(APP_ID_MIN + 1, 1, (u32)OS32X_FLAG_LAUNCHER);   /* 3 = sh   */
    res_owner_set(APP_ID_SHELL);
}

/* 要求者 id から 1 本積む。 */
static i32 req_from(int id, const char *cmdline)
{
    i32 t;
    res_owner_set(id);
    t = launch_req(cmdline);
    res_owner_set(APP_ID_SHELL);
    return t;
}

static i32 poll_from(int id, i32 token, i32 *status)
{
    i32 rc;
    res_owner_set(id);
    rc = launch_poll(token, status);
    res_owner_set(APP_ID_SHELL);
    return rc;
}

static i32 cancel_from(int id, i32 token)
{
    i32 rc;
    res_owner_set(id);
    rc = launch_cancel(token);
    res_owner_set(APP_ID_SHELL);
    return rc;
}

/* ========================================================================
 *  1. launch_req の権限と引数検査 (§1a)
 * ======================================================================== */
static void case_req_admission(void)
{
    char longline[LAUNCH_CMDLINE_MAX + 8];
    u32 i;
    i32 t;

    reset_all();
    report("1 launch_req (admission)\n");

    host_gui = 0;
    check(req_from(APP_ID_MIN, "kbd_echo") == OS32_ERR_INVAL,
          "1a GUI 外 (con_sink 無効) は断る");
    host_gui = 1;

    res_owner_set(APP_ID_SHELL);
    check(launch_req("kbd_echo") == OS32_ERR_INVAL,
          "1b WM top-level (owner 1) からは断る");

    make_app(APP_ID_MIN + 2, 1, 0);             /* 宣言なし */
    check(req_from(APP_ID_MIN + 2, "kbd_echo") == OS32_ERR_INVAL,
          "1c 宣言 LAUNCHER が無ければ断る");

    make_app(APP_ID_MIN + 3, 0, (u32)OS32X_FLAG_LAUNCHER);  /* 入れ子の子 */
    check(req_from(APP_ID_MIN + 3, "kbd_echo") == OS32_ERR_INVAL,
          "1d 入れ子 exec_run の子 (gui == 0) からは断る");

    check(req_from(APP_ID_MIN, 0) == OS32_ERR_INVAL, "1e NULL は断る");
    check(req_from(APP_ID_MIN, "") == OS32_ERR_INVAL, "1f 空は断る");

    for (i = 0; i < (u32)LAUNCH_CMDLINE_MAX + 7; i++) longline[i] = 'a';
    longline[LAUNCH_CMDLINE_MAX + 7] = '\0';
    check(req_from(APP_ID_MIN, longline) == OS32_ERR_INVAL,
          "1g 255B を超える cmdline は断る");

    check(launch_pending() == 0, "1h ここまで 1 本も積まれていない");

    t = req_from(APP_ID_MIN, "kbd_echo");
    check(t > 0, "1i 宣言を持つ GUI アプリからは通る");
    check(launch_pending() == 1, "1j PENDING は 1 本");
    check(req_from(APP_ID_MIN, "cat /etc/system.cfg") == OS32_ERR_FULL,
          "1k 同じ要求者の 2 本目は FULL (表は 1 本)");
    check(req_from(APP_ID_MIN + 1, "ls") > 0,
          "1l 別の要求者は自分の表に積める");
    check(launch_pending() == 2, "1m PENDING は 2 本");
}

/* ========================================================================
 *  2. take → report(rc>0) → RUNNING → 子の回収で DONE (§1a / D3)
 * ======================================================================== */
static void case_launch_running(void)
{
    i32 token, got, st;
    i32 requester = -99, kind = -99, arg = -99;

    reset_all();
    report("2 LAUNCH: take -> report(rc>0) -> RUNNING -> 回収で DONE\n");

    token = req_from(APP_ID_MIN, "kbd_echo");

    res_owner_set(APP_ID_MIN);
    check(launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == OS32_ERR_INVAL,
          "2a take は owner 1 専用");
    res_owner_set(APP_ID_SHELL);
    check(launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX - 1, 0, 0, 0) == OS32_ERR_INVAL,
          "2b cap < LAUNCH_CMDLINE_MAX は断る (buf が非 NULL のとき)");
    check(launch_take(0, 0, 0, 0, 0) == token,
          "2b2 buf = NULL は断らない (§1a: 出力ポインタは NULL 可)");
    check(launch_take(0, 0, 0, 0, 0) == 0,
          "2b3 その take は成立している (表は TAKEN へ動いた)");
    g_req[APP_ID_MIN].phase = LAUNCH_PHASE_PENDING;   /* 2c 以降のために戻す */
    check(launch_pending() == 1, "2c 断った take は表を動かさない");

    got = launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, &requester, &kind, &arg);
    check(got == token, "2d take は token を返す");
    check(requester == (i32)APP_ID_MIN, "2e requester が入る");
    check(kind == (i32)LAUNCH_KIND_LAUNCH, "2f kind = LAUNCH");
    check(str_eq(takebuf, "kbd_echo"), "2g cmdline が入る");
    check(launch_pending() == 0, "2h TAKEN は PENDING に数えない");
    check(launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == 0,
          "2i 取るものが無ければ 0 (失敗ではない)");

    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_TAKEN,
          "2j 要求者から見ると TAKEN");

    res_owner_set(APP_ID_MIN);
    check(launch_report(token, 4) == OS32_ERR_INVAL, "2k report は owner 1 専用");
    res_owner_set(APP_ID_SHELL);

    check(launch_report(token, (i32)(APP_ID_MAX + 1)) == OS32_ERR_INVAL,
          "2l 範囲外の子 ID は断る");
    check(launch_report(token, (i32)(APP_ID_MIN + 2)) == OS32_ERR_INVAL,
          "2m 生きていない子 ID は断る");
    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_TAKEN,
          "2n 断った report は TAKEN のまま");

    make_app(APP_ID_MIN + 2, 1, 0);             /* 子が立った */
    check(launch_report(token, (i32)(APP_ID_MIN + 2)) == 0, "2o rc > 0 を受ける");
    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 2),
          "2p 表が子を所有する (launch_child)");
    check(poll_from(APP_ID_MIN, token, &st) == 0 &&
          st == (i32)LAUNCH_ST_RUNNING + (i32)(APP_ID_MIN + 2),
          "2q status = 0x100 + child");
    check(launch_report(token, 0) == OS32_ERR_STALE,
          "2r RUNNING への report は STALE");

    /* 子が畳まれた (正常終了 / kill / fault のどれでも同じ通知) */
    kill_app(APP_ID_MIN + 2);
    launch_owner_exit(APP_ID_MIN + 2);
    check(launch_child((i32)APP_ID_MIN) == 0, "2s 回収通知で child = 0");
    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_DONE,
          "2t 回収通知で DONE");
    check(poll_from(APP_ID_MIN, token, &st) == OS32_ERR_STALE,
          "2u DONE を渡した表は IDLE (再 poll は STALE)");
    check(req_from(APP_ID_MIN, "ls") > 0, "2v 同じ要求者が次の要求を積める");
}

/* ========================================================================
 *  3. rc == 0 は即 DONE / rc < 0 は FAILED (§5 blocker 1 の短命な子)
 * ======================================================================== */
static void case_launch_done_failed(void)
{
    i32 token, st;

    reset_all();
    report("3 LAUNCH: rc == 0 -> DONE / rc < 0 -> FAILED\n");

    token = req_from(APP_ID_MIN, "true");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    check(launch_report(token, 0) == 0, "3a rc == 0 を受ける");
    check(launch_child((i32)APP_ID_MIN) == 0, "3b 短命な子は所有しない");
    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_DONE,
          "3c park より前に終わった子も DONE (取り逃がさない)");

    token = req_from(APP_ID_MIN, "nosuch");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    check(launch_report(token, OS32_ERR_NOTFOUND) == 0, "3d rc < 0 を受ける");
    check(poll_from(APP_ID_MIN, token, &st) == 0 &&
          st == (i32)LAUNCH_ST_FAILED + (-(i32)OS32_ERR_NOTFOUND),
          "3e status = 0x300 + (-rc)");
    check(poll_from(APP_ID_MIN, token, &st) == OS32_ERR_STALE,
          "3f FAILED を渡した表も IDLE");
}

/* ========================================================================
 *  4. launch_cancel (D9): RUNNING → KILL の PENDING → 回収で DONE
 * ======================================================================== */
static void case_cancel(void)
{
    i32 token, st;
    i32 requester = -99, kind = -99, arg = -99;

    reset_all();
    report("4 cancel: RUNNING -> KILL(child) PENDING -> 回収で DONE\n");

    token = req_from(APP_ID_MIN, "sh");
    check(cancel_from(APP_ID_MIN, token) == OS32_ERR_AGAIN,
          "4a PENDING への cancel は AGAIN (端末は次のタイマで再試行)");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    check(cancel_from(APP_ID_MIN, token) == OS32_ERR_AGAIN,
          "4b TAKEN への cancel も AGAIN");

    make_app(APP_ID_MIN + 2, 1, 0);
    (void)launch_report(token, (i32)(APP_ID_MIN + 2));

    res_owner_set(APP_ID_MIN + 1);
    check(launch_cancel(token) == OS32_ERR_STALE,
          "4c 要求者でない ID からの cancel は STALE (§1a: 不一致 → STALE)");
    res_owner_set(APP_ID_SHELL);

    check(cancel_from(APP_ID_MIN, token) == 0, "4d RUNNING への cancel は通る");
    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 2),
          "4e 取消の途中でも child は消えない");
    check(launch_pending() == 1, "4f KILL の PENDING として並ぶ");

    {
        i32 got = launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX,
                              &requester, &kind, &arg);
        check(got == token, "4g 同じ token で取れる");
        check(kind == (i32)LAUNCH_KIND_KILL, "4h kind = KILL");
        check(arg == (i32)(APP_ID_MIN + 2), "4i arg = 畳む ID");
        check(requester == (i32)APP_ID_MIN, "4j requester は元のまま");
    }

    /* WM が exec_kill(child) を終える = 回収通知が先に来る (票 §10 2) */
    kill_app(APP_ID_MIN + 2);
    launch_owner_exit(APP_ID_MIN + 2);
    check(launch_child((i32)APP_ID_MIN) == 0, "4k 回収通知で child = 0");
    check(launch_report(token, 0) == OS32_ERR_STALE,
          "4l KILL 後の report は STALE (WM は再試行せず正常扱い)");
    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_DONE,
          "4m 端末は DONE を見てプロンプトへ戻れる");
    check(cancel_from(APP_ID_MIN, token) == OS32_ERR_STALE,
          "4n 解放済みの token への cancel は STALE");
    check(req_from(APP_ID_MIN, "ls") > 0, "4o ERR_FULL で固着しない");
}

/* ========================================================================
 *  5. 要求者の退場 = 孤児回収 (D3 / §10 non-blocker 1)
 * ======================================================================== */
static void case_orphan(void)
{
    i32 token;
    i32 requester = -99, kind = -99, arg = -99;

    reset_all();
    report("5 要求者の退場 -> 孤児回収 (KILL) -> 完了で IDLE\n");

    token = req_from(APP_ID_MIN, "sh");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    make_app(APP_ID_MIN + 2, 1, 0);
    (void)launch_report(token, (i32)(APP_ID_MIN + 2));

    /* 要求者 (端末) が CTRL+STOP などで畳まれた */
    kill_app(APP_ID_MIN);
    launch_owner_exit(APP_ID_MIN);
    check(launch_pending() == 1, "5a 孤児回収が PENDING として並ぶ");
    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 2),
          "5b 子は保持される (カーネルは回収文脈から kill しない)");

    /* ID が再利用された: 新しい住人は表が空くまで待たされる */
    make_app(APP_ID_MIN, 1, (u32)OS32X_FLAG_LAUNCHER);
    check(req_from(APP_ID_MIN, "ls") == OS32_ERR_FULL,
          "5c 再利用 ID からの launch_req は孤児回収が終わるまで FULL");

    {
        i32 got = launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX,
                              &requester, &kind, &arg);
        check(got == token, "5d WM が同じ token で受け取る");
        check(kind == (i32)LAUNCH_KIND_KILL, "5e kind = KILL");
        check(arg == (i32)(APP_ID_MIN + 2), "5f arg = 孤児の子");
        check(requester == (i32)LAUNCH_REQ_ORPHAN, "5g requester は孤児の印");
    }
    check(poll_from(APP_ID_MIN, token, 0) == OS32_ERR_INVAL,
          "5h 再利用 ID は孤児の表を poll できない (照合は token)");
    check(cancel_from(APP_ID_MIN, token) == OS32_ERR_STALE,
          "5h2 再利用 ID からの旧 token の cancel は STALE (再試行させない)");

    kill_app(APP_ID_MIN + 2);
    launch_owner_exit(APP_ID_MIN + 2);
    check(launch_child((i32)APP_ID_MIN) == 0, "5i 回収通知で child = 0");
    check(launch_pending() == 0, "5j 孤児の完了は poll を待たない");
    check(req_from(APP_ID_MIN, "ls") > 0, "5k 表が空いて再利用 ID が使える");

    /* 子を持たない要求者の退場も表を解放する */
    reset_all();
    token = req_from(APP_ID_MIN, "ls");
    check(token > 0 && launch_pending() == 1, "5l PENDING を 1 本積む");
    kill_app(APP_ID_MIN);
    launch_owner_exit(APP_ID_MIN);
    check(launch_pending() == 0, "5m 子を持たない要求者の退場で表は IDLE");
    make_app(APP_ID_MIN, 1, (u32)OS32X_FLAG_LAUNCHER);
    check(req_from(APP_ID_MIN, "ls") > 0, "5n 直後の再利用 ID が使える");
}

/* ========================================================================
 *  6. token の照合と STALE (§9 blocker 4)
 * ======================================================================== */
static void case_token(void)
{
    i32 t1, t2, st;

    reset_all();
    report("6 token: 全体単調増加・照合は token・不一致は STALE\n");

    t1 = req_from(APP_ID_MIN, "a");
    t2 = req_from(APP_ID_MIN + 1, "b");
    check(t1 > 0 && t2 > t1, "6a token は要求者をまたいで単調増加");
    check((t2 & 0xFF000000) == 0 || t2 > 0,
          "6b token の上位に要求者 ID を混ぜない (符号は常に正)");

    check(poll_from(APP_ID_MIN + 1, t1, &st) == OS32_ERR_INVAL,
          "6c 他人の token は poll できない");
    check(poll_from(APP_ID_MIN, t2, &st) == OS32_ERR_INVAL,
          "6d 逆向きも同じ");
    check(poll_from(APP_ID_MIN, 0x7FFF0000, &st) == OS32_ERR_STALE,
          "6e 知らない token は STALE");
    check(poll_from(APP_ID_MIN, 0, &st) == OS32_ERR_STALE, "6f token 0 は STALE");
    check(poll_from(APP_ID_MIN, -5, &st) == OS32_ERR_STALE, "6g 負の token も STALE");
    check(launch_report(0, 0) == OS32_ERR_STALE, "6h report も token で照合");

    /* ID を再利用しても古い token は当たらない */
    kill_app(APP_ID_MIN);
    launch_owner_exit(APP_ID_MIN);
    make_app(APP_ID_MIN, 1, (u32)OS32X_FLAG_LAUNCHER);
    check(req_from(APP_ID_MIN, "c") > t2, "6i 再利用 ID にも新しい token");
    check(poll_from(APP_ID_MIN, t1, &st) == OS32_ERR_STALE,
          "6j 古い token は新しい住人の表に当たらない");
}

/* ========================================================================
 *  7. launch_child / launch_chain (D8 の連鎖)
 * ======================================================================== */
static void case_chain(void)
{
    int chain[APP_MAX_APPS];
    int n;
    i32 t2, t3;

    reset_all();
    report("7 連鎖: 端末 -> sh -> 子 を末尾まで辿る (D8)\n");

    make_app(APP_ID_MIN + 2, 1, 0);             /* 4 = 子 */

    /* 2 (端末) が 3 (sh) を、3 が 4 を所有する */
    t2 = req_from(APP_ID_MIN, "sh");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    (void)launch_report(t2, (i32)(APP_ID_MIN + 1));
    t3 = req_from(APP_ID_MIN + 1, "kbd_echo");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    (void)launch_report(t3, (i32)(APP_ID_MIN + 2));

    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 1), "7a 2 の子は 3");
    check(launch_child((i32)(APP_ID_MIN + 1)) == (i32)(APP_ID_MIN + 2),
          "7b 3 の子は 4");
    check(launch_child((i32)(APP_ID_MIN + 2)) == 0, "7c 4 は末尾");
    check(launch_child(0) == 0 && launch_child(99) == 0, "7d 不正 ID は 0");

    n = launch_chain(APP_ID_MIN, chain, APP_MAX_APPS);
    check(n == 3, "7e 連鎖は 3 段");
    check(chain[0] == APP_ID_MIN && chain[1] == APP_ID_MIN + 1 &&
          chain[2] == APP_ID_MIN + 2, "7f 先頭から末尾の順に並ぶ");
    n = launch_chain(APP_ID_MIN + 1, chain, APP_MAX_APPS);
    check(n == 2 && chain[0] == APP_ID_MIN + 1, "7g 途中からも辿れる");
    check(launch_chain(0, chain, APP_MAX_APPS) == 0, "7h 不正 ID は 0 本");
    check(launch_chain(APP_ID_MIN, chain, 0) == 0, "7i max 0 は 0 本");

    /* CTRL+STOP: WM は末尾 (4) だけを畳む → 3 の表が DONE になる */
    kill_app(APP_ID_MIN + 2);
    launch_owner_exit(APP_ID_MIN + 2);
    check(launch_child((i32)(APP_ID_MIN + 1)) == 0, "7j 末尾を畳むと 3 の child = 0");
    check(launch_chain(APP_ID_MIN, chain, APP_MAX_APPS) == 2,
          "7k 連鎖は 2 段に縮む");

    /* 壊れた表が環を作っても止まる (保険) */
    reset_all();
    g_req[APP_ID_MIN].phase = LAUNCH_PHASE_RUNNING;
    g_req[APP_ID_MIN].child = APP_ID_MIN + 1;
    g_req[APP_ID_MIN + 1].phase = LAUNCH_PHASE_RUNNING;
    g_req[APP_ID_MIN + 1].child = APP_ID_MIN;
    check(launch_chain(APP_ID_MIN, chain, APP_MAX_APPS) == 2,
          "7l 環でも 2 本で止まる (無限ループしない)");
}

/* ========================================================================
 *  8. launch_selftest (ブート時に踏む形) が通ること
 * ======================================================================== */
static void case_selftest(void)
{
    u32 bad;
    reset_all();
    report("8 launch_selftest (kselftest と同じ 3 項)\n");
    bad = launch_selftest();
    check((bad & (1u << 0)) == 0, "8a 起動 -> 子 -> 回収で DONE + child = 0");
    check((bad & (1u << 1)) == 0, "8b 完了は 1 度だけ渡り表は IDLE へ");
    check((bad & (1u << 2)) == 0, "8c 要求者の退場は孤児回収、完了で IDLE");
    check(launch_pending() == 0, "8d 自己診断は表を空にして戻る");
}

/* ========================================================================
 *  9. KILL の report が回収通知より **先** に来たとき (順序の反例)
 *
 *  正常な順序は「take → exec_kill(child) → 回収通知で DONE → report は STALE」
 *  (ケース 4)。WM が exec_kill を呼べなかった / 呼ぶ前に report した場合は
 *  表が TAKEN のまま届く。ここで DONE + child = 0 にすると、**生きている子の
 *  所有が誰の表からも消え、その子はもう誰にも回収されない** (Codex 実装
 *  レビュー 往復 1/3 の blocker 3)。取得済みの印だけ消して RUNNING に戻す。
 * ======================================================================== */
static void case_kill_report_before_reclaim(void)
{
    i32 token, st;

    reset_all();
    report("9 KILL の report が回収通知より先に来ても child を落とさない\n");

    token = req_from(APP_ID_MIN, "sh");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    make_app(APP_ID_MIN + 2, 1, 0);
    (void)launch_report(token, (i32)(APP_ID_MIN + 2));
    check(poll_from(APP_ID_MIN, token, &st) == 0 &&
          st == (i32)LAUNCH_ST_RUNNING + (i32)(APP_ID_MIN + 2),
          "9a RUNNING(child) から始める");

    check(cancel_from(APP_ID_MIN, token) == 0, "9b 要求者が取り消す");
    check(launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == token,
          "9c WM が KILL を取る");

    /* WM が exec_kill を終える前に (あるいは畳めずに) report した */
    check(launch_report(token, 0) == 0, "9d TAKEN のままの report は通る");
    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 2),
          "9e child は落ちない (生きている子の所有を手放さない)");
    check(poll_from(APP_ID_MIN, token, &st) == 0 &&
          st == (i32)LAUNCH_ST_RUNNING + (i32)(APP_ID_MIN + 2),
          "9f 表は RUNNING(child) に戻る (DONE にしない)");
    check(launch_pending() == 0, "9g 取得済みの印は消えている");

    /* もう一度 cancel すれば KILL(child) がまた PENDING になるだけ */
    check(cancel_from(APP_ID_MIN, token) == 0, "9h 再度の cancel も通る");
    check(launch_pending() == 1, "9i KILL(child) がまた並ぶ");
    check(launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == token,
          "9j WM がもう一度取る");

    /* 今度は本当に畳めた: DONE を付けるのは必ず回収通知 */
    kill_app(APP_ID_MIN + 2);
    launch_owner_exit(APP_ID_MIN + 2);
    check(launch_child((i32)APP_ID_MIN) == 0, "9k 回収通知で child = 0");
    check(poll_from(APP_ID_MIN, token, &st) == 0 && st == (i32)LAUNCH_ST_DONE,
          "9l DONE は回収通知だけが付ける");
    check(launch_report(token, 0) == OS32_ERR_STALE,
          "9m 解放後の report は STALE");
    check(req_from(APP_ID_MIN, "ls") > 0, "9n ERR_FULL で固着しない");

    /* 要求者が先に退場しても、生きている子は孤児回収に載る (所有が残るため) */
    reset_all();
    token = req_from(APP_ID_MIN, "sh");
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    make_app(APP_ID_MIN + 2, 1, 0);
    (void)launch_report(token, (i32)(APP_ID_MIN + 2));
    (void)cancel_from(APP_ID_MIN, token);
    (void)launch_take(takebuf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0);
    (void)launch_report(token, 0);            /* 回収より先の report */
    kill_app(APP_ID_MIN);
    launch_owner_exit(APP_ID_MIN);            /* 要求者が退場 */
    check(launch_pending() == 1, "9o 要求者の退場で孤児回収が並ぶ");
    check(launch_child((i32)APP_ID_MIN) == (i32)(APP_ID_MIN + 2),
          "9p 子の所有が残っているので回収できる");
}

int main(void)
{
    failures = 0;
    report("launch request table (T9 K)\n");
    case_req_admission();
    case_launch_running();
    case_launch_done_failed();
    case_cancel();
    case_orphan();
    case_token();
    case_chain();
    case_selftest();
    case_kill_report_before_reclaim();
    if (failures) {
        report("FAILURES\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
    return 0;
}

/* -nostdlib の入口 */
void _start(void)
{
    main();
}
