/* =========================================================================
 *  TEST_RESULT_CONV_HOST.C — 試験プログラムの「合否の出し方」を固定する
 *
 *  実行: python3 -B tools/tests/test_result_conv.py [--target] [--mutate]
 *  記録: tools/tests/result_conv_tdd.md
 *  票  : docs/archive/test/TASK_TEST_RESULT.md §2 / §6 の 1 / §11 (追補)
 *
 *  ゲストの試験プログラムの合否を人が画面を読まずに判定できるようにするには、
 *  **終了コードと最終行の集計行が必ず一致している**ことと、その集計行が
 *  **リダイレクトで拾える口 (fd 1) に出ている**ことの両方が要る。
 *  片方だけ直すとランナー (3 段目) が 2 つの答えを持ってしまう。
 *
 *  ■ 贋物は「画面に出た」と「fd 1 に出た」を区別する
 *
 *  2026-09-17 のランナー初回実行で 16 本中 14 本が「集計行が無い」になった
 *  (票 §11)。原因は `api->kprintf` がカーネルの画面出力でリダイレクトを
 *  素通りすること。**画面と fd 1 を同じ 1 本のバッファへ流す贋物では、この穴は
 *  また見逃される。** そこでこの試験の贋物は行き先ごとに別のバッファを持つ。
 *
 *      scr    api->kprintf         カーネルの画面出力。`>` で拾えない
 *      fd1    sys_write(1,…) と printf   リダイレクトが差し替える口
 *      other  sys_write(その他の fd)     ファイルやパイプ
 *
 *  さらに fd1 は **sys_write で書かれた区間の先頭 (fd1_wmark)** を覚えていて、
 *  集計行がそこから始まっていることまで見る。printf 任せに戻す変更も拾える。
 *
 *  ■ 段
 *
 *   §1〜§4  約束事そのもの — 実物の userland/lib/rt/testresult.h を #include し、
 *           集計行の書式・終了コードの値域・両者の一致・予約値 (126/127/130/139)
 *           を返さないこと・切り詰めても溢れず改行で終わることを直に叩く。
 *
 *   §5      **出し口** (票 §11)。os32_test_summary が集計行を fd 1 へ出すこと、
 *           画面には 1 バイトも書かないこと、**短い書き込みを「書けた」ことに
 *           しない**こと (残りを書き続ける / 進まなければ終了コードで言う)。
 *
 *   §6      実物のプログラム — userland/tests/ の 5 本を**贋物の KernelAPI で
 *           実際に走らせ**、fd 1 に出た文字列と `main` の返り値の**両方**を
 *           観測する。grep では「一致」は確かめられない (集計行が PASS と
 *           言いながら 1 を返す版も、grep はどちらも通してしまう)。
 *           合格側・不合格側・SKIP 側の 3 通りを同じ 1 本で踏み、さらに
 *           **argv[0] を変えても集計行の名前が変わらない**ことを見る。
 *
 *  プログラム本体は 1 行も写さない。取り込みは tools/tests/result_conv/run_*.c が
 *  `#define main <名前>_main` + `#include` で行う (理由は shim.h)。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"
#include "rt/testresult.h"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

static void checkf(int cond, const char *fmt, ...)
{
    char line[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    check(cond, line);
}

/* ------------------------------------------------------------------------ */
/*  §1  集計行の書式と終了コード                                              */
/*                                                                            */
/*  ここで叩くのは**行を組み立てる内部**。出し口は §5 が見る。                 */
/* ------------------------------------------------------------------------ */

static void t_format(void)
{
    char buf[OS32_TEST_LINE_MAX];
    int rc;

    printf("\n[1] 集計行の書式 (票 §2-2)\n");

    rc = os32_test__build(buf, sizeof(buf), "math_test", 42, 42);
    check(strcmp(buf, "math_test: PASS 42/42\n") == 0, "全部合格なら PASS n/n");
    check(rc == OS32_TEST_EXIT_PASS, "  その終了コードは 0");

    rc = os32_test__build(buf, sizeof(buf), "math_test", 41, 42);
    check(strcmp(buf, "math_test: FAIL 41/42\n") == 0, "1 件落ちたら FAIL n/m");
    check(rc == OS32_TEST_EXIT_FAIL, "  その終了コードは 1");

    rc = os32_test__build(buf, sizeof(buf), "save_test", 0, 9);
    check(strcmp(buf, "save_test: FAIL 0/9\n") == 0, "全滅も FAIL 0/m");
    check(rc == OS32_TEST_EXIT_FAIL, "  その終了コードは 1");

    rc = os32_test__build_skip(buf, sizeof(buf), "save_test",
                               "/host is not mounted");
    check(strcmp(buf, "save_test: SKIP /host is not mounted\n") == 0,
          "前提が無ければ SKIP <理由>");
    check(rc == OS32_TEST_EXIT_SKIP, "  その終了コードは 2");

    /* 理由を書き忘れても行が壊れない (ランナーが読み違えない)。 */
    rc = os32_test__build_skip(buf, sizeof(buf), "x", "");
    check(strcmp(buf, "x: SKIP no reason given\n") == 0,
          "SKIP の理由が空でも語が 1 つは載る");
    check(rc == OS32_TEST_EXIT_SKIP, "  その終了コードは 2");

    /* 1 項目も走らなかったものを合格にしない。集計変数が初期値のまま
     * 早期 return した試験が「PASS 0/0」で緑になるのが一番危ない。 */
    rc = os32_test__build(buf, sizeof(buf), "x", 0, 0);
    check(strcmp(buf, "x: FAIL 0/0\n") == 0, "総数 0 は PASS にしない");
    check(rc == OS32_TEST_EXIT_FAIL, "  その終了コードは 1");

    /* 集計が壊れている (合格数が総数を超える / 負) のも合格にしない。 */
    rc = os32_test__build(buf, sizeof(buf), "x", 4, 3);
    check(rc == OS32_TEST_EXIT_FAIL, "合格数 > 総数 は FAIL");
    check(strcmp(buf, "x: FAIL 4/3\n") == 0, "  行も FAIL");
    rc = os32_test__build(buf, sizeof(buf), "x", -1, 3);
    check(rc == OS32_TEST_EXIT_FAIL, "合格数が負も FAIL");
    check(strcmp(buf, "x: FAIL -1/3\n") == 0, "  負値も 10 進で出る");

    /* 名前がそのまま載る (ランナーはこれで束ねる)。 */
    rc = os32_test__build(buf, sizeof(buf), "db_v50_test", 1, 1);
    check(strncmp(buf, "db_v50_test: ", 13) == 0, "名前が行の先頭に載る");
    (void)rc;
}

/* ------------------------------------------------------------------------ */
/*  §2  終了コードと集計行が食い違わないこと                                  */
/* ------------------------------------------------------------------------ */

/* 行から動詞 (PASS / FAIL / SKIP) を取り出す。見つからなければ 0。 */
static char verb_of(const char *line)
{
    const char *p = strstr(line, ": ");

    if (!p) return 0;
    p += 2;
    if (strncmp(p, "PASS ", 5) == 0) return 'P';
    if (strncmp(p, "FAIL ", 5) == 0) return 'F';
    if (strncmp(p, "SKIP ", 5) == 0) return 'S';
    return 0;
}

static void t_agreement(void)
{
    char buf[OS32_TEST_LINE_MAX];
    int total, pass, rc;
    int bad_pair = 0;
    int bad_range = 0;
    int bad_resv = 0;
    int seen_pass = 0, seen_fail = 0;

    printf("\n[2] 終了コードと集計行の一致 (票 §2-2 の最後の 1 行)\n");

    for (total = 0; total <= 40; total++) {
        for (pass = -2; pass <= total + 2; pass++) {
            char v;

            rc = os32_test__build(buf, sizeof(buf), "t", pass, total);
            v = verb_of(buf);
            if (v == 'P') seen_pass++;
            if (v == 'F') seen_fail++;
            if ((v == 'P') != (rc == OS32_TEST_EXIT_PASS)) bad_pair++;
            if ((v == 'F') != (rc == OS32_TEST_EXIT_FAIL)) bad_pair++;
            if (rc != OS32_TEST_EXIT_PASS && rc != OS32_TEST_EXIT_FAIL)
                bad_range++;
            if (os32_test_exit_reserved(rc)) bad_resv++;
        }
    }
    rc = os32_test__build_skip(buf, sizeof(buf), "t", "why");
    if ((verb_of(buf) == 'S') != (rc == OS32_TEST_EXIT_SKIP)) bad_pair++;
    if (os32_test_exit_reserved(rc)) bad_resv++;

    checkf(bad_pair == 0,
           "PASS<->0 / FAIL<->1 / SKIP<->2 が全組み合わせで一致 (食い違い %d)",
           bad_pair);
    checkf(bad_range == 0, "終了コードは 0 か 1 だけ (外れ %d)", bad_range);
    checkf(bad_resv == 0, "予約値 126/127/130/139 を返さない (違反 %d)",
           bad_resv);
    checkf(seen_pass > 0 && seen_fail > 0,
           "PASS と FAIL の両方を実際に出した (PASS %d / FAIL %d)",
           seen_pass, seen_fail);
}

/* ------------------------------------------------------------------------ */
/*  §3  予約値の表                                                            */
/* ------------------------------------------------------------------------ */

static void t_reserved(void)
{
    int i;
    int wrong = 0;

    printf("\n[3] 予約値 (票 §1 / userland/shell/shell.h)\n");

    check(os32_test_exit_reserved(126), "126 (起こせなかった) は予約");
    check(os32_test_exit_reserved(127), "127 (実行ファイルが無い) は予約");
    check(os32_test_exit_reserved(130), "130 (中断) は予約");
    check(os32_test_exit_reserved(139), "139 (例外) は予約");

    for (i = OS32_TEST_EXIT_PASS; i <= OS32_TEST_EXIT_LOCAL_MAX; i++) {
        if (os32_test_exit_reserved(i)) wrong++;
    }
    checkf(wrong == 0, "0〜125 は 1 つも予約でない (誤判定 %d)", wrong);
    check(OS32_TEST_EXIT_LOCAL_MIN == 3 && OS32_TEST_EXIT_LOCAL_MAX == 125,
          "個別の票が使ってよい帯は 3〜125");
}

/* ------------------------------------------------------------------------ */
/*  §4  切り詰め — 短いバッファでも溢れず、NUL と改行で終わる                 */
/* ------------------------------------------------------------------------ */

static void t_truncate(void)
{
    char pad[64];
    unsigned int cap;
    int overflow = 0;
    int unterminated = 0;
    int no_newline = 0;
    int wrong_rc = 0;

    printf("\n[4] 切り詰め\n");

    for (cap = 1; cap <= 40; cap++) {
        unsigned int i;
        unsigned int l;
        int rc;

        memset(pad, '@', sizeof(pad));
        rc = os32_test__build(pad, cap, "a_rather_long_test_name", 3, 3);
        if (rc != OS32_TEST_EXIT_PASS) wrong_rc++;
        if (pad[cap - 1] != '\0' && strlen(pad) >= cap) unterminated++;
        /* 改行は切り詰めても落とさない。落ちるとランナーが次の出力と
         * 1 行に繋げて読むので、行が丸ごと消えるのと同じになる。 */
        l = (unsigned int)strlen(pad);
        if (cap >= 2 && (l == 0 || pad[l - 1] != '\n')) no_newline++;
        for (i = cap; i < sizeof(pad); i++) {
            if (pad[i] != '@') overflow++;
        }
    }
    checkf(overflow == 0, "cap を 1 バイトも超えて書かない (踏み越え %d)",
           overflow);
    checkf(unterminated == 0, "必ず NUL で終わる (終端なし %d)", unterminated);
    checkf(no_newline == 0, "切り詰めても必ず改行で終わる (改行なし %d)",
           no_newline);
    checkf(wrong_rc == 0,
           "行が切れても終了コードは正しいまま (ずれ %d)", wrong_rc);

    /* buf を渡さなくても終了コードだけは返る (組み立てだけ試す呼び手用)。 */
    check(os32_test__build((char *)0, 0, "x", 1, 1) == OS32_TEST_EXIT_PASS,
          "buf が無くても終了コードは返る");
}

/* ========================================================================= */
/*  贋物の KernelAPI — **行き先ごとに別のバッファへ流す**                      */
/* ========================================================================= */

#define SINK_MAX 262144

typedef struct {
    char         b[SINK_MAX];
    unsigned int n;
} Sink;

static Sink sk_scr;     /* api->kprintf   — カーネルの画面出力          */
static Sink sk_fd1;     /* fd 1           — リダイレクトが差し替える口  */
static Sink sk_other;   /* fd 1 以外の fd — ファイル / パイプ           */

/* sk_fd1 のうち、**最後に sys_write で書き始めた位置**。printf が挟まると
 * 更新されるので、「集計行は sys_write(1,…) で出た」を offset で確かめられる。 */
static unsigned int fd1_wmark;
static int fd1_last_was_write;

/* fd 1 への sys_write 呼び出し回数 (つまみ fk_fd1_stop_after が見る)。 */
static int fk_fd1_writes;

static void sink_reset(Sink *s) { s->n = 0; s->b[0] = '\0'; }

static void sink_addn(Sink *s, const char *p, unsigned int n)
{
    if (s->n + n >= SINK_MAX) n = SINK_MAX - 1 - s->n;
    memcpy(s->b + s->n, p, n);
    s->n += n;
    s->b[s->n] = '\0';
}

static void sink_add(Sink *s, const char *p)
{
    sink_addn(s, p, (unsigned int)strlen(p));
}

/* fd 1 への追記。via_write=1 は sys_write、0 は newlib の printf。 */
static void fd1_addn(const char *p, unsigned int n, int via_write)
{
    if (via_write) {
        if (!fd1_last_was_write) {
            fd1_wmark = sk_fd1.n;
            fd1_last_was_write = 1;
        }
    } else {
        fd1_last_was_write = 0;
    }
    sink_addn(&sk_fd1, p, n);
}

static void cap_reset(void)
{
    sink_reset(&sk_scr);
    sink_reset(&sk_fd1);
    sink_reset(&sk_other);
    fd1_wmark = 0;
    fd1_last_was_write = 0;
    fk_fd1_writes = 0;
}

/* newlib の printf の差し替え (run_*.c が #define printf rconv_printf)。
 * 実機でも printf は fd 1 へ行くので、fd 1 のバッファへ流す。 */
int rconv_printf(const char *fmt, ...)
{
    char line[4096];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fd1_addn(line, (unsigned int)strlen(line), 0);
    return n;
}

/* --- 場面ごとの振る舞いを決めるつまみ ------------------------------------ */

static int fk_stat_ok;        /* sys_stat が 0 を返すか                    */
static int fk_isatty_ok;      /* sys_isatty(1) が 1 を返すか               */
static u16 fk_fstat_ifmt;     /* sys_fstat が返す種別 (S_IFCHR / S_IFREG)  */
static int fk_font_rc;        /* kcg_load_font の返り値                    */
static int fk_write_short;    /* fd 1 **以外**が要求より 1 バイト少なく返す */
static int fk_res_ok;         /* FD / パイプ / リダイレクトが回収されたか  */

/* fd 1 側 (票 §11 の主題) */
static u32 fk_fd1_chunk;      /* 1 回に書けるバイト数 (0 = 全部)           */
static int fk_fd1_rc;         /* 0 なら普通。非 0 ならその値をそのまま返す */
static int fk_fd1_over;       /* 要求より多く書いたと嘘をつく              */
static int fk_fd1_absent;     /* sys_write を持たない KernelAPI            */
static int fk_fd1_stall;      /* いつも 0 を返す (1 バイトも進まない)      */
static int fk_fd1_stop_after; /* この回数だけ書けて、以降は 0 を返す       */

static char fk_file[256];
static int  fk_file_len;
static int  fk_file_open;

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    char line[4096];
    va_list ap;

    (void)attr;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sink_add(&sk_scr, line);
}

static int fk_sys_stat(const char *path, OS32_Stat *st)
{
    /* 「無いはずのもの」は常に無い — stat_t の 3 つ目はこれを見ている。 */
    if (path && strstr(path, "NONEXIST")) return -1;
    if (!fk_stat_ok) return -1;
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_size = 1;
    }
    return 0;
}

static int fk_sys_fstat(int fd, OS32_Stat *st)
{
    (void)fd;
    if (!fk_stat_ok) return -1;
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_mode = (u16)(fk_fstat_ifmt ? fk_fstat_ifmt : OS_S_IFCHR);
    }
    return 0;
}

static int fk_sys_isatty(int fd)
{
    (void)fd;
    return fk_isatty_ok ? 1 : 0;
}

static int fk_kcg_load_font(const char *path)
{
    (void)path;
    return fk_font_rc;
}

/* restest 用。回収されていれば取れる、いなければ取れない。 */
static int fk_pipes;

static int fk_sys_pipe_alloc(void)
{
    if (!fk_res_ok) return -1;
    if (fk_pipes >= 2) return -1;
    return fk_pipes++;
}

static void fk_sys_pipe_free(int id)
{
    (void)id;
    if (fk_pipes > 0) fk_pipes--;
}

static int fk_sys_redirect_fd(int fd, const char *path, int mode)
{
    (void)fd; (void)path; (void)mode;
    return fk_res_ok ? 0 : -1;
}

static u32 fk_tick;
static u32 fk_get_tick(void) { return fk_tick++; }

static void *fk_mem_alloc(u32 n) { return malloc(n ? n : 1); }
static void  fk_mem_free(void *p) { free(p); }

static int fk_sys_open(const char *path, int mode)
{
    (void)path;
    if (mode & KAPI_O_TRUNC) fk_file_len = 0;
    fk_file_open = 1;
    return 7;
}

static void fk_sys_close(int fd) { (void)fd; fk_file_open = 0; }

/* **fd で行き先が変わる。** ここが「画面に出た」と「fd 1 に出た」を
 * 区別できる贋物の要。 */
static int fk_sys_write(int fd, const void *buf, u32 size)
{
    u32 n;

    if (fd == OS32_TEST_FD_STDOUT) {
        if (fk_fd1_rc != 0) return fk_fd1_rc;
        if (fk_fd1_stall) return 0;
        if (fk_fd1_over) return (int)(size + 1);
        if (fk_fd1_stop_after > 0 && fk_fd1_writes >= fk_fd1_stop_after)
            return 0;
        fk_fd1_writes++;
        n = size;
        if (fk_fd1_chunk > 0 && n > fk_fd1_chunk) n = fk_fd1_chunk;
        fd1_addn((const char *)buf, n, 1);
        return (int)n;
    }

    n = size;
    if (fk_write_short && n > 0) n--;
    sink_addn(&sk_other, (const char *)buf, n);
    if (fk_file_len + (int)n > (int)sizeof(fk_file))
        n = (u32)((int)sizeof(fk_file) - fk_file_len);
    memcpy(fk_file + fk_file_len, buf, n);
    fk_file_len += (int)n;
    return (int)n;
}

static int fk_sys_read(int fd, void *buf, u32 size)
{
    u32 n = (u32)fk_file_len;

    (void)fd;
    if (n > size) n = size;
    memcpy(buf, fk_file, n);
    return (int)n;
}

static KernelAPI g_fake;

static void fake_api_init(u32 version)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.version = version;
    g_fake.kprintf = fk_kprintf;
    g_fake.mem_alloc = fk_mem_alloc;
    g_fake.mem_free = fk_mem_free;
    g_fake.get_tick = fk_get_tick;
    g_fake.sys_open = fk_sys_open;
    g_fake.sys_close = fk_sys_close;
    g_fake.sys_read = fk_sys_read;
    g_fake.sys_write = fk_fd1_absent ? (int (*)(int, const void *, u32))0
                                     : fk_sys_write;
    g_fake.sys_stat = fk_sys_stat;
    g_fake.sys_fstat = fk_sys_fstat;
    g_fake.sys_isatty = fk_sys_isatty;
    g_fake.kcg_load_font = fk_kcg_load_font;
    g_fake.sys_pipe_alloc = fk_sys_pipe_alloc;
    g_fake.sys_pipe_free = fk_sys_pipe_free;
    g_fake.sys_redirect_fd = fk_sys_redirect_fd;
    fk_pipes = 0;
    fk_fstat_ifmt = OS_S_IFCHR;
    fk_fd1_chunk = 0;
    fk_fd1_rc = 0;
    fk_fd1_over = 0;
    fk_fd1_stall = 0;
    fk_fd1_stop_after = 0;
    fk_fd1_writes = 0;
}

/* ========================================================================= */
/*  §5  出し口 — 集計行は fd 1 へ出る (票 §11)                                */
/* ========================================================================= */

static void t_channel(void)
{
    char want[OS32_TEST_LINE_MAX];
    int  rc;

    printf("\n[5] 集計行の出し口 (票 §11 — ランナーが暴いた穴)\n");

    /* --- (a) そのまま出す ------------------------------------------------ */
    fk_fd1_absent = 0;
    fake_api_init(60);
    cap_reset();
    os32_test__build(want, sizeof(want), "math_test", 110, 110);
    rc = os32_test_summary(&g_fake, "math_test", 110, 110);
    checkf(strcmp(sk_fd1.b, want) == 0,
           "集計行が **fd 1** に出る [%s]", sk_fd1.b);
    checkf(sk_scr.n == 0,
           "画面 (kprintf) には 1 バイトも出さない (出たのは %u バイト)",
           sk_scr.n);
    check(sk_other.n == 0, "fd 1 以外へは出さない");
    check(rc == OS32_TEST_EXIT_PASS, "終了コードは 0 のまま");
    check(fd1_wmark == 0 && fd1_last_was_write,
          "sys_write(1, …) で出ている (printf 経由ではない)");

    /* SKIP も同じ口。 */
    cap_reset();
    os32_test__build_skip(want, sizeof(want), "e2test", "no buffers");
    rc = os32_test_summary_skip(&g_fake, "e2test", "no buffers");
    checkf(strcmp(sk_fd1.b, want) == 0, "SKIP の行も fd 1 [%s]", sk_fd1.b);
    check(sk_scr.n == 0, "  画面には出さない");
    check(rc == OS32_TEST_EXIT_SKIP, "  終了コードは 2");

    /* --- (b) 短い書き込み — 残りを書き続ける ----------------------------- */
    /* sys_write が返すのは**書けたバイト数**で、1 回で全部書けるとは限らない。
     * 1 回の返り値を「書けた」と読むと集計行が途中で切れる。 */
    cap_reset();
    fk_fd1_chunk = 3;
    os32_test__build(want, sizeof(want), "klibc_test", 49, 49);
    rc = os32_test_summary(&g_fake, "klibc_test", 49, 49);
    checkf(strcmp(sk_fd1.b, want) == 0,
           "3 バイトずつしか書けなくても**行が全部**出る [%s]", sk_fd1.b);
    check(rc == OS32_TEST_EXIT_PASS, "  終了コードは 0 のまま");

    cap_reset();
    fk_fd1_chunk = 1;
    rc = os32_test_summary(&g_fake, "klibc_test", 49, 49);
    checkf(strcmp(sk_fd1.b, want) == 0,
           "1 バイトずつでも行が全部出る [%s]", sk_fd1.b);
    check(rc == OS32_TEST_EXIT_PASS, "  終了コードは 0 のまま");
    fk_fd1_chunk = 0;

    /* --- (c) 書けない — 合格を名乗らせない ------------------------------- */
    /* 書けなかった集計行は**ランナーには存在しない**。0 を返すのは票 §11 で
     * 踏んだ「$?=0 なのに集計行が無い」そのものなので、FAIL にして
     * 「$?=1 なのに集計行が無い」= 食い違いとして必ず名指しさせる。 */
    cap_reset();
    fk_fd1_rc = -5;
    rc = os32_test_summary(&g_fake, "stat_t", 5, 5);
    checkf(sk_fd1.n == 0, "書けなければ fd 1 には何も残らない (%u バイト)",
           sk_fd1.n);
    check(rc == OS32_TEST_EXIT_FAIL,
          "sys_write が負値 → 合格ではなく 1 を返す");
    fk_fd1_rc = 0;

    /* 1 バイトも進まない (返り値 0) — 回し続けると固まるので諦めて言う。 */
    cap_reset();
    fk_fd1_stall = 1;
    rc = os32_test_summary(&g_fake, "stat_t", 5, 5);
    check(sk_fd1.n == 0, "1 バイトも進まなければ何も残らない");
    check(rc == OS32_TEST_EXIT_FAIL, "sys_write が 0 を返し続けても 1 を返す");
    fk_fd1_stall = 0;

    /* 途中まで書けてから止まる — 「半分出た」を合格にしない。 */
    cap_reset();
    fk_fd1_chunk = 4;
    fk_fd1_stop_after = 1;
    os32_test__build(want, sizeof(want), "stat_t", 5, 5);
    rc = os32_test_summary(&g_fake, "stat_t", 5, 5);
    checkf(sk_fd1.n > 0 && strcmp(sk_fd1.b, want) != 0,
           "途中で止まると行は欠ける [%s]", sk_fd1.b);
    check(rc == OS32_TEST_EXIT_FAIL, "途中までしか書けなければ 1 を返す");
    fk_fd1_chunk = 0;
    fk_fd1_stop_after = 0;

    cap_reset();
    fk_fd1_over = 1;
    rc = os32_test_summary(&g_fake, "stat_t", 5, 5);
    check(rc == OS32_TEST_EXIT_FAIL,
          "要求より多く書いたと言われたら 1 を返す");
    fk_fd1_over = 0;

    /* SKIP でも同じ — 前提の欠如を伝えられなかったなら SKIP を名乗らない。 */
    cap_reset();
    fk_fd1_rc = -5;
    rc = os32_test_summary_skip(&g_fake, "host_test", "no host agent");
    check(rc == OS32_TEST_EXIT_FAIL, "SKIP も出せなければ 1 を返す");
    fk_fd1_rc = 0;

    /* --- (d) sys_write が無い / api が無い ------------------------------- */
    cap_reset();
    fk_fd1_absent = 1;
    fake_api_init(60);
    rc = os32_test_summary(&g_fake, "stat_t", 5, 5);
    check(rc == OS32_TEST_EXIT_FAIL, "sys_write を持たない KAPI なら 1");
    fk_fd1_absent = 0;
    fake_api_init(60);

    cap_reset();
    rc = os32_test_summary((KernelAPI *)0, "stat_t", 5, 5);
    check(rc == OS32_TEST_EXIT_FAIL, "api が無くても落ちずに 1 を返す");

    /* --- (e) 行き先はリダイレクトに従う ---------------------------------- */
    /* fd 1 が何を指していようと、集計行は fd 1 へ行く。kprintf のように
     * 画面へ直に書かないので `>` `>>` で拾える — これが票 §11 の要求。 */
    cap_reset();
    rc = os32_test_summary(&g_fake, "restest", 3, 3);
    check(sk_fd1.n > 0 && sk_scr.n == 0,
          "リダイレクトを通る口だけを使っている");
    (void)rc;
}

/* ========================================================================= */
/*  §6  実物のプログラムを贋物の KernelAPI で走らせる                          */
/* ========================================================================= */

/* --- 実物の main (取り込みは tools/tests/result_conv/run_*.c) ------------- */
/* **わざと別の翻訳単位から宣言している** — `void main` に戻す変異を       */
/* コンパイルエラーではなく実行時の食い違いとして踏むため (shim.h)。        */
extern int stat_t_main(int argc, char **argv, KernelAPI *api);
extern int restest_main(int argc, char **argv, KernelAPI *api);
extern int test2_main(int argc, char **argv, KernelAPI *api);
extern int klibc_test_main(int argc, char **argv, KernelAPI *api);
extern int font_load_test_main(int argc, char **argv, KernelAPI *api);

typedef int (*prog_fn)(int argc, char **argv, KernelAPI *api);

/* --- 集計行の取り出しと判定 ---------------------------------------------- */

typedef struct {
    int  found;
    char name[64];
    char verb;          /* 'P' / 'F' / 'S' */
    int  pass;
    int  total;
    char line[256];
    unsigned int start; /* 行の先頭の offset (どの書き込みで出たかを見る) */
} Summary;

/* 捕捉した出力の**最終行**を読む。末尾の改行は読み飛ばす。 */
static void summary_of(const char *text, Summary *out)
{
    const char *end;
    const char *start;
    unsigned int n;
    char *colon;
    char *p;

    memset(out, 0, sizeof(*out));
    end = text + strlen(text);
    while (end > text && (end[-1] == '\n' || end[-1] == '\r')) end--;
    start = end;
    while (start > text && start[-1] != '\n' && start[-1] != '\r') start--;
    out->start = (unsigned int)(start - text);
    n = (unsigned int)(end - start);
    if (n == 0 || n >= sizeof(out->line)) return;
    memcpy(out->line, start, n);
    out->line[n] = '\0';

    colon = strstr(out->line, ": ");
    if (!colon) return;
    n = (unsigned int)(colon - out->line);
    if (n == 0 || n >= sizeof(out->name)) return;
    memcpy(out->name, out->line, n);
    out->name[n] = '\0';

    p = colon + 2;
    if (strncmp(p, "SKIP ", 5) == 0) {
        if (p[5] == '\0') return;
        out->verb = 'S';
        out->found = 1;
        return;
    }
    if (strncmp(p, "PASS ", 5) == 0) out->verb = 'P';
    else if (strncmp(p, "FAIL ", 5) == 0) out->verb = 'F';
    else return;
    if (sscanf(p + 5, "%d/%d", &out->pass, &out->total) != 2) return;
    out->found = 1;
}

/* 1 回走らせて、**fd 1 に出た文字列**と終了コードの両方を約束事に照らす。
 * want は期待する動詞 ('P'/'F'/'S')、sub は副引数 (無ければ 0)。 */
static void run_prog_arg(prog_fn fn, const char *name, const char *sub,
                         char want, const char *scenario)
{
    char  argv0_a[64];
    char  argv0_b[64];
    char  sub_buf[64];
    char *argv_a[3];
    char *argv_b[3];
    Summary sa, sb;
    unsigned int mark_a;
    int   rc_a, rc_b;
    int   want_rc;
    int   argc = sub ? 2 : 1;

    snprintf(argv0_a, sizeof(argv0_a), "%s", name);
    snprintf(argv0_b, sizeof(argv0_b), "/usr/bin/%s.bin", name);
    snprintf(sub_buf, sizeof(sub_buf), "%s", sub ? sub : "");
    argv_a[0] = argv0_a; argv_a[1] = sub ? sub_buf : (char *)0; argv_a[2] = (char *)0;
    argv_b[0] = argv0_b; argv_b[1] = sub ? sub_buf : (char *)0; argv_b[2] = (char *)0;

    printf("  -- %s (%s)\n", name, scenario);

    cap_reset();
    rc_a = fn(argc, argv_a, &g_fake);
    summary_of(sk_fd1.b, &sa);
    mark_a = fd1_wmark;

    /* ここが票 §11 の主眼 — 画面ではなく **fd 1** の最終行を見る。 */
    checkf(sa.found, "%s/%s: **fd 1 の**最終行が集計行になっている [%s]",
           name, scenario, sa.line);
    if (!sa.found) {
        checkf(sk_scr.n == 0,
               "%s/%s: (参考) 画面には %u バイト出ている — kprintf 頼みでは"
               "リダイレクトで拾えない", name, scenario, sk_scr.n);
        return;
    }

    checkf(sa.start >= mark_a,
           "%s/%s: 集計行は sys_write(1, …) で出ている (行頭 %u / 書き込み %u)",
           name, scenario, sa.start, mark_a);

    checkf(strcmp(sa.name, name) == 0,
           "%s/%s: 行の名前が \"%s\" (出たのは \"%s\")",
           name, scenario, name, sa.name);
    checkf(sa.verb == want, "%s/%s: 動詞が %c (出たのは %c)",
           name, scenario, want, sa.verb ? sa.verb : '?');

    want_rc = (want == 'P') ? OS32_TEST_EXIT_PASS
            : (want == 'F') ? OS32_TEST_EXIT_FAIL
                            : OS32_TEST_EXIT_SKIP;
    checkf(rc_a == want_rc, "%s/%s: 終了コードが %d (返ったのは %d)",
           name, scenario, want_rc, rc_a);
    checkf(!os32_test_exit_reserved(rc_a),
           "%s/%s: 予約値 (126/127/130/139) を返していない", name, scenario);
    checkf(rc_a >= 0 && rc_a <= OS32_TEST_EXIT_LOCAL_MAX,
           "%s/%s: 終了コードが 0〜125 の中", name, scenario);

    /* 行と終了コードが食い違っていないこと — これがこの試験の主眼。 */
    checkf((sa.verb == 'P') == (rc_a == OS32_TEST_EXIT_PASS) &&
           (sa.verb == 'F') == (rc_a == OS32_TEST_EXIT_FAIL) &&
           (sa.verb == 'S') == (rc_a == OS32_TEST_EXIT_SKIP),
           "%s/%s: 集計行と終了コードが一致 (%c / %d)",
           name, scenario, sa.verb, rc_a);

    if (sa.verb == 'P') {
        checkf(sa.total > 0 && sa.pass == sa.total,
               "%s/%s: PASS の行は n==m かつ m>0 (%d/%d)",
               name, scenario, sa.pass, sa.total);
    } else if (sa.verb == 'F') {
        checkf(!(sa.total > 0 && sa.pass == sa.total),
               "%s/%s: FAIL の行は n==m>0 にならない (%d/%d)",
               name, scenario, sa.pass, sa.total);
    }

    /* argv[0] を変えても行が変わらない (リダイレクトや呼び方で名前が動かない)。 */
    cap_reset();
    rc_b = fn(argc, argv_b, &g_fake);
    summary_of(sk_fd1.b, &sb);
    checkf(sb.found && strcmp(sa.line, sb.line) == 0,
           "%s/%s: argv[0] を \"%s\" にしても集計行が同じ [%s]",
           name, scenario, argv0_b, sb.line);
    checkf(rc_a == rc_b, "%s/%s: argv[0] を変えても終了コードが同じ",
           name, scenario);
}

static void run_prog(prog_fn fn, const char *name, char want,
                     const char *scenario)
{
    run_prog_arg(fn, name, (const char *)0, want, scenario);
}

static void t_programs(void)
{
    printf("\n[6] 実物のプログラムを贋物の KAPI で走らせる\n");

    /* --- stat_t: 4 項目。FS が応えるかどうかで合否が動く ----------------- */
    fake_api_init(60);
    fk_stat_ok = 1; fk_isatty_ok = 1; fk_fstat_ifmt = OS_S_IFCHR;
    run_prog(stat_t_main, "stat_t", 'P', "対話: stat/fstat/isatty が全部応える");
    /* 票 TASK_FSTAT_REDIR の受入 F2 — `stat_t > file`。fd 1 はファイルなので
     * **2 つの API が揃って「端末ではない」と言うなら合格**。2026-09-17 まで
     * stat_t は「S_IFCHR である」「isatty が 1」を別々に主張していたので、
     * ここは必ず FAIL 4/5 だった (ランナーが暴いた場面そのもの)。 */
    fk_stat_ok = 1; fk_isatty_ok = 0; fk_fstat_ifmt = OS_S_IFREG;
    run_prog(stat_t_main, "stat_t", 'P',
             "> file: fstat も isatty も端末ではないと言う");
    /* 食い違い — fstat は S_IFCHR と言うのに isatty は 0 (§1 の不具合)。
     * **一致の主張にした後だけ**これを捕まえられる。 */
    fk_stat_ok = 1; fk_isatty_ok = 0; fk_fstat_ifmt = OS_S_IFCHR;
    run_prog(stat_t_main, "stat_t", 'F', "fstat と isatty が食い違う");
    fk_stat_ok = 0; fk_isatty_ok = 0; fk_fstat_ifmt = OS_S_IFCHR;
    run_prog(stat_t_main, "stat_t", 'F', "FS が応えない");

    /* --- restest: 引数で場面が変わる。引数不正は SKIP (票 §2-1) ---------- */
    fake_api_init(60);
    fk_res_ok = 1; fk_isatty_ok = 1;
    run_prog_arg(restest_main, "restest", "verify", 'P', "回収できている");
    fake_api_init(60);
    fk_res_ok = 0; fk_isatty_ok = 0;
    run_prog_arg(restest_main, "restest", "verify", 'F', "回収されていない");
    fake_api_init(60);
    run_prog(restest_main, "restest", 'S', "引数なし");
    fake_api_init(60);
    run_prog_arg(restest_main, "restest", "nosuch", 'S', "知らない引数");

    /* --- test2: 版が古ければ SKIP、短い書き込みは合格にしない ------------ */
    /* fk_write_short が効くのは **fd 1 以外** (test2 が開くファイル) だけ。
     * fd 1 の短い書き込みは §5 が別に踏む — 混ぜると「集計行が出ない」のか
     * 「ファイル I/O が落ちた」のか区別できなくなる。 */
    fake_api_init(60);
    fk_write_short = 0;
    run_prog(test2_main, "test2", 'P', "KAPI v60");
    fake_api_init(1);
    run_prog(test2_main, "test2", 'S', "KAPI v1 (前提が無い)");
    fake_api_init(60);
    fk_write_short = 1;
    run_prog(test2_main, "test2", 'F', "ファイルへの sys_write が 1 バイト短い");
    fk_write_short = 0;

    /* --- klibc_test: KAPI を使わない newlib 側 --------------------------- */
    /* printf の山の**後ろ**に sys_write で集計行が出る。printf をそのまま
     * 集計行に使う版に戻すと fd1_wmark の検査で落ちる。 */
    fake_api_init(60);
    run_prog(klibc_test_main, "klibc_test", 'P', "ホストの libc");

    /* --- font_load_test: PASS / FAIL / SKIP の 3 通り -------------------- */
    fake_api_init(60);
    fk_stat_ok = 1; fk_font_rc = 0;
    run_prog(font_load_test_main, "font_load_test", 'P', "読み込み成功");
    fk_stat_ok = 1; fk_font_rc = -3;
    run_prog(font_load_test_main, "font_load_test", 'F',
             "読み込み失敗 (負値を終了コードにしない)");
    fk_stat_ok = 0;
    run_prog(font_load_test_main, "font_load_test", 'S', "フォントが無い");
}

/* ------------------------------------------------------------------------ */

int main(void)
{
    printf("=== test_result_conv_host — 合否の出し方 (票 §2 / §11) ===\n");

    t_format();
    t_agreement();
    t_reserved();
    t_truncate();
    t_channel();
    t_programs();

    printf("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
