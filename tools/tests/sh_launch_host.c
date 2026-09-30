/* ========================================================================
 *  sh_launch_host.c — sh.bin の起動待ち状態機械を **実物のソースで** 確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_T9_sh.md §1 D3a (S 側)
 *  実行:   python3 -B tools/tests/test_sh_launch.py
 *  記録:   tools/tests/t9_tdd.md
 *
 *  userland/shell/sh_launch.inc を 1 行も写さずそのまま #include する
 *  (模型ではない)。カーネルの代わりに置くのは KernelAPI 表だけ — launch_req /
 *  launch_poll / sys_yield / kprintf を関数ポインタで差し替え、台本どおりの
 *  status を返して DONE / FAILED / STALE / FULL の 4 経路を見る。
 *
 *  併せて「待っている間 kbd_getchar / kbd_trygetchar / ime_getkey を呼ばない」
 *  (票 D3a) も見る: その 3 本にも印を立てる handler を挿しておき、1 度でも
 *  呼ばれたら落とす。
 *
 *  tools/tests/launch_host.c と同じ様式 — ホスト ILP32 GNU89、libc 無し
 *  (-nostdlib、Linux の int 0x80 で write/exit するだけ)。kprintf の書式は
 *  %s / %d だけ使うので、捕まえる側もその 2 つを自前で組み立てる。
 * ======================================================================== */

#include "os32api.h"

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

/* ---- 小道具 ------------------------------------------------------------ */

static int str_eq(const char *a, const char *b)
{
    u32 i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void str_put(char *dst, u32 cap, u32 *pos, const char *src)
{
    while (*src && *pos + 1 < cap) dst[(*pos)++] = *src++;
    dst[*pos] = '\0';
}

static void int_put(char *dst, u32 cap, u32 *pos, int v)
{
    char tmp[12];
    int n = 0;
    unsigned int u;

    if (v < 0) {
        if (*pos + 1 < cap) dst[(*pos)++] = '-';
        u = (unsigned int)(-(long)v);
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n > 0 && *pos + 1 < cap) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

/* ---- 台本と記録 -------------------------------------------------------- */

#define MAX_POLLS   8
#define MSG_CAP     256

typedef struct {
    i32 req_rc;                 /* launch_req が返す値 */
    i32 poll_rc[MAX_POLLS];     /* launch_poll が返す値 */
    i32 poll_st[MAX_POLLS];     /* launch_poll が *status へ書く値 */
    int poll_n;                 /* 台本の長さ */
} Script;

static Script g_script;
static char   g_req_cmdline[LAUNCH_CMDLINE_MAX];
static i32    g_last_poll_token;
static int    g_reqs;               /* launch_req を呼んだ回数 (票 T11 / U12) */
static int    g_refused;            /* sh_refuse が呼ばれたら 1 (同) */
static int    g_yields;
static int    g_polls;
static int    g_yield_before_poll;  /* poll の時点で yield が足りていれば 1 */
static int    g_kbd_touched;        /* kbd_* / ime_* が呼ばれたら 1 */
static char   g_msg[MSG_CAP];
static u32    g_msg_len;

static void reset_harness(void)
{
    int i;
    g_script.req_rc = 0;
    g_script.poll_n = 0;
    for (i = 0; i < MAX_POLLS; i++) {
        g_script.poll_rc[i] = 0;
        g_script.poll_st[i] = 0;
    }
    g_req_cmdline[0] = '\0';
    g_last_poll_token = 0;
    g_reqs = 0;
    g_refused = 0;
    g_yields = 0;
    g_polls = 0;
    g_yield_before_poll = 1;
    g_kbd_touched = 0;
    g_msg[0] = '\0';
    g_msg_len = 0;
}

static void script_poll(int i, i32 rc, i32 st)
{
    g_script.poll_rc[i] = rc;
    g_script.poll_st[i] = st;
    if (i + 1 > g_script.poll_n) g_script.poll_n = i + 1;
}

/* ---- 差し替える KernelAPI の中身 --------------------------------------- */

/* sh_launch.inc が使う書式は "%s" と "%d" だけ。__builtin_va_* で受ける
 * (libc 無しなので stdarg.h は引かない)。 */
static void __cdecl h_kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *p = fmt;
    (void)attr;

    __builtin_va_start(ap, fmt);
    while (*p) {
        if (p[0] == '%' && p[1] == 's') {
            str_put(g_msg, MSG_CAP, &g_msg_len,
                    __builtin_va_arg(ap, const char *));
            p += 2;
        } else if (p[0] == '%' && p[1] == 'd') {
            int_put(g_msg, MSG_CAP, &g_msg_len, __builtin_va_arg(ap, int));
            p += 2;
        } else {
            if (g_msg_len + 1 < MSG_CAP) g_msg[g_msg_len++] = *p;
            g_msg[g_msg_len] = '\0';
            p++;
        }
    }
    __builtin_va_end(ap);
}

static i32 __cdecl h_launch_req(const char *cmdline)
{
    u32 pos = 0;
    g_reqs++;
    str_put(g_req_cmdline, (u32)LAUNCH_CMDLINE_MAX, &pos, cmdline);
    return g_script.req_rc;
}

static i32 __cdecl h_launch_poll(i32 token, i32 *status)
{
    int i;
    /* 票 D3a のループは「まず譲ってから問い合わせる」。poll の回数より
     * yield が少なければ順序が崩れている。 */
    if (g_yields < g_polls + 1) g_yield_before_poll = 0;
    g_last_poll_token = token;
    i = g_polls;
    if (i >= g_script.poll_n) i = g_script.poll_n - 1;   /* 末尾で固定 */
    g_polls++;
    if (status) *status = g_script.poll_st[i];
    return g_script.poll_rc[i];
}

static i32 __cdecl h_sys_yield(void)
{
    g_yields++;
    return 0;
}

static int __cdecl h_kbd_getchar(void)    { g_kbd_touched = 1; return 0; }
static int __cdecl h_kbd_trygetchar(void) { g_kbd_touched = 1; return -1; }
static int __cdecl h_ime_getkey(void)     { g_kbd_touched = 1; return 0; }

static KernelAPI g_fake;
KernelAPI *g_api;

static void build_api(void)
{
    u32 i;
    u8 *raw = (u8 *)&g_fake;
    for (i = 0; i < sizeof(g_fake); i++) raw[i] = 0;
    g_fake.kprintf = h_kprintf;
    g_fake.launch_req = h_launch_req;
    g_fake.launch_poll = h_launch_poll;
    g_fake.sys_yield = h_sys_yield;
    g_fake.kbd_getchar = h_kbd_getchar;
    g_fake.kbd_trygetchar = h_kbd_trygetchar;
    g_fake.ime_getkey = h_ime_getkey;
    g_api = &g_fake;
}

/* ---- 実物のソース ------------------------------------------------------ */

/* 票 TASK_SH_TRUNCATION §5 の段 3 (T11) で sh_launch が呼ぶようになった
 * 「断り」の口。実体は main.c にあるが、この試験は sh_launch.inc だけを
 * 取り込むので同じ契約の最小実装を置く — 赤字 1 行 (g_msg) + 印。 */
void sh_refuse(const char *what, int limit);

void sh_refuse(const char *what, int limit)
{
    g_refused = 1;
    h_kprintf(ATTR_RED, "%s too long (max %d)\n", what, limit);
}

#include "../../userland/shell/sh_launch.inc"

/* ========================================================================
 *  1. DONE — PENDING → TAKEN → RUNNING → DONE で 0 を返す
 * ======================================================================== */
static void case_done(void)
{
    int rc;

    reset_harness();
    report("1 DONE (PENDING -> TAKEN -> RUNNING -> DONE)\n");
    g_script.req_rc = 7;
    script_poll(0, 0, LAUNCH_ST_PENDING);
    script_poll(1, 0, LAUNCH_ST_TAKEN);
    script_poll(2, 0, LAUNCH_ST_RUNNING + 3);
    script_poll(3, 0, LAUNCH_ST_DONE);

    rc = sh_launch("/bin/kbd_echo.bin -v");

    check(rc == EXEC_SUCCESS,                       "1a DONE は 0 を返す");
    check(str_eq(g_req_cmdline, "/bin/kbd_echo.bin -v"),
                                                    "1b cmdline をそのまま渡す");
    check(g_last_poll_token == 7,                   "1c poll は req の token で");
    check(g_polls == 4,                             "1d DONE を見るまで poll");
    check(g_yields == 4,                            "1e poll ごとに 1 回譲る");
    check(g_yield_before_poll,                      "1f 譲ってから問い合わせる");
    check(g_kbd_touched == 0,                       "1g 待ちの間 kbd/ime を呼ばない");
    check(g_msg[0] == '\0',                         "1h 何も印字しない");
}

/* ========================================================================
 *  2. FAILED — 下位 8bit が -rc。PATH 走査が続く 2 つは黙って返す
 * ======================================================================== */
static void case_failed_not_found(void)
{
    int rc;

    reset_harness();
    report("2 FAILED(NOT_FOUND) — 次の PATH 候補へ回すので黙る\n");
    g_script.req_rc = 11;
    script_poll(0, 0, LAUNCH_ST_RUNNING + 4);
    script_poll(1, 0, LAUNCH_ST_FAILED + 3);        /* -rc = 3 */

    rc = sh_launch("/usr/bin/nosuch.bin");

    check(rc == EXEC_ERR_NOT_FOUND,                 "2a status の -rc をそのまま返す");
    check(g_polls == 2,                             "2b FAILED を見たらやめる");
    check(g_kbd_touched == 0,                       "2c 待ちの間 kbd/ime を呼ばない");
    check(g_msg[0] == '\0',                         "2d 印字しない (候補の数だけ出る)");
}

static void case_failed_nomem(void)
{
    int rc;

    reset_harness();
    report("3 FAILED(NOMEM) — sh: <名>: launch failed (rc)\n");
    g_script.req_rc = 12;
    script_poll(0, 0, LAUNCH_ST_FAILED + 4);        /* -rc = 4 */

    rc = sh_launch("/bin/hog.bin a b");

    check(rc == EXEC_ERR_NOMEM,                     "3a NOMEM を返す");
    check(str_eq(g_msg, "sh: /bin/hog.bin: launch failed (-4)\n"),
                                                    "3b 名前は cmdline の先頭語");
    check(g_kbd_touched == 0,                       "3c 待ちの間 kbd/ime を呼ばない");
}

/* ========================================================================
 *  4. STALE — launch_poll が負を返したらループを抜けて負を返す
 * ======================================================================== */
static void case_stale(void)
{
    int rc;

    reset_harness();
    report("4 STALE — launch_poll < 0 でループを抜ける\n");
    g_script.req_rc = 21;
    script_poll(0, 0, LAUNCH_ST_RUNNING + 5);
    script_poll(1, OS32_ERR_STALE, 0);

    rc = sh_launch("/bin/kbd_echo.bin");

    check(rc == EXEC_ERR_GENERAL,                   "4a 負を返す");
    check(g_polls == 2,                             "4b 負を見たら回り続けない");
    check(str_eq(g_msg, "sh: /bin/kbd_echo.bin: launch_poll failed (-11)\n"),
                                                    "4c rc つきで報告する");
    check(g_kbd_touched == 0,                       "4d 待ちの間 kbd/ime を呼ばない");
}

/* ========================================================================
 *  5. FULL — launch_req が塞がっていたら poll も yield もしない
 * ======================================================================== */
static void case_full(void)
{
    int rc;

    reset_harness();
    report("5 FULL — launch_req が OS32_ERR_FULL なら busy\n");
    g_script.req_rc = OS32_ERR_FULL;
    script_poll(0, 0, LAUNCH_ST_DONE);              /* 使われないはず */

    rc = sh_launch("ls -l");

    check(rc == EXEC_ERR_GENERAL,                   "5a 負を返す");
    check(g_polls == 0,                             "5b poll しない");
    check(g_yields == 0,                            "5c 譲らない");
    check(str_eq(g_msg, "sh: ls: busy\n"),          "5d busy と出す");
}

static void case_req_other(void)
{
    int rc;

    reset_harness();
    report("6 launch_req が NOSYS — launch_req failed (rc)\n");
    g_script.req_rc = OS32_ERR_NOSYS;
    script_poll(0, 0, LAUNCH_ST_DONE);

    rc = sh_launch("kbd_echo");

    check(rc == EXEC_ERR_GENERAL,                   "6a 負を返す");
    check(g_polls == 0,                             "6b poll しない");
    check(str_eq(g_msg, "sh: kbd_echo: launch_req failed (-10)\n"),
                                                    "6c rc つきで報告する");
}

/* GUI 外 (CUI から直に起動)。
 *
 *  **票 TASK_EXIT_STATUS R1b で契約が変わった** (2026-09-16)。以前は
 *  EXEC_ERR_NOT_FOUND を返して「次の PATH 候補へ」と読ませていたので、
 *  1 本しか無いコマンドでも候補の数だけ launch_req を呼び、最後に
 *  "command not found" で閉じていた。新しい契約は
 *    「起こせなかった」= EXEC_KIND_NOMEM → `$?` は 126 で **走査は止まる**。
 *  理由の 1 行は今までどおり 1 回だけ出す (印 sh_launch_nogui)。
 *  結果 API (sh_exec_result) で見るので、旧 sh_launch の戻り値の契約
 *  (NOT_FOUND = 次の候補へ) は残さない。 */
static void case_req_nogui(void)
{
    int rc, kind, code;

    reset_harness();
    sh_launch_nogui = 0;
    report("7 launch_req が INVAL — GUI 外は 126 で走査を止める (R1b)\n");
    g_script.req_rc = OS32_ERR_INVAL;
    script_poll(0, 0, LAUNCH_ST_DONE);

    kind = -1; code = -1;
    rc = sh_exec_result("/bin/kbd_echo.bin", &kind, &code);
    check(rc == EXEC_ERR_GENERAL,                   "7a 負を返す");
    check(kind == EXEC_KIND_NOMEM,
          "7a2 種別は「起こせなかった」(次の候補へ回さない)");
    check(code == 0,                                "7a3 値は 0");
    check(str_eq(g_msg, "sh: external programs need the GUI terminal\n"),
                                                    "7b 理由を 1 行だけ出す");
    check(g_polls == 0,                             "7c poll しない");

    /* 2 本目 (呼び手が止めなかった場合でも) 同じ種別で、黙る */
    g_msg[0] = '\0';
    g_msg_len = 0;
    kind = -1;
    rc = sh_exec_result("/usr/bin/kbd_echo.bin", &kind, &code);
    check(kind == EXEC_KIND_NOMEM,                  "7d 2 本目も同じ種別");
    check(g_msg[0] == '\0',                         "7e 2 本目は黙る");

    /* 起動が通れば印は寝る (次に GUI 外へ落ちたらまた報せる) */
    reset_harness();
    g_script.req_rc = 5;
    script_poll(0, 0, LAUNCH_ST_DONE);
    kind = -1; code = -1;
    rc = sh_exec_result("ls", &kind, &code);
    check(sh_launch_nogui == 0,                     "7f 通ったら印を寝かせる");
    check(rc == EXEC_SUCCESS && kind == EXEC_KIND_EXITED && code == 0,
          "7g DONE は (EXITED, 0) に写る (§2-6: 終了コードは別票)");
}

/* ========================================================================
 *  8. T11 / U12 — 256 バイト以上の行は **launch_req を呼ばずに** 断る
 *
 *  カーネル (exec/launch.c) は len >= LAUNCH_CMDLINE_MAX を OS32_ERR_INVAL
 *  で返すが、sh.bin はその INVAL を「GUI 外」と読むので理由が入れ替わる。
 *  送る前に測って断ること。上限ちょうど (255) は今までどおり通る。
 * ======================================================================== */
static void case_cmdline_too_long(void)
{
    static char line[LAUNCH_CMDLINE_MAX + 8];
    int i, rc;

    reset_harness();
    report("8 T11 — 256 バイト以上は launch_req を呼ばずに断る\n");

    for (i = 0; i < LAUNCH_CMDLINE_MAX; i++) line[i] = 'a';
    line[LAUNCH_CMDLINE_MAX] = '\0';            /* ちょうど 256 バイト */

    g_script.req_rc = 7;
    script_poll(0, 0, LAUNCH_ST_DONE);

    rc = sh_launch(line);
    check(rc == EXEC_ERR_GENERAL,  "8a 負を返す (GUI 外の NOT_FOUND ではない)");
    check(g_reqs == 0,             "8b launch_req を呼ばない");
    check(g_polls == 0,            "8c poll しない");
    check(g_refused == 1,          "8d 断りの印を立てる");
    check(str_eq(g_msg, "sh: launch command line too long (max 255)\n"),
                                   "8e 何が上限を超えたか + 上限を出す");

    /* 誤発火の裏: 255 バイトちょうどは今までどおり通る (「以上」で数える) */
    reset_harness();
    line[LAUNCH_CMDLINE_MAX - 1] = '\0';        /* 255 バイト */
    g_script.req_rc = 7;
    script_poll(0, 0, LAUNCH_ST_DONE);

    rc = sh_launch(line);
    check(rc == EXEC_SUCCESS,      "8f 255 バイトちょうどは通る");
    check(g_reqs == 1,             "8g 255 では launch_req を呼ぶ");
    check(g_refused == 0,          "8h 255 では印を立てない");
}

/* ---- entry ------------------------------------------------------------- */

void _start(void)
{
    build_api();
    case_done();
    case_failed_not_found();
    case_failed_nomem();
    case_stale();
    case_full();
    case_req_other();
    case_req_nogui();
    case_cmdline_too_long();
    report(failures ? "SOME FAIL\n" : "ALL PASS\n");
    die(failures ? 1 : 0);
}
