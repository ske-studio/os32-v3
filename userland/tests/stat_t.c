/* ======================================================================== */
/*  STAT_T.C — sys_stat / sys_fstat / sys_isatty テスト                     */
/*                                                                          */
/*  合否の出し方は票 docs/archive/test/TASK_TEST_RESULT.md §2 に従う。         */
/*  以前は結果を画面に出すだけで、成否によらず sys_exit(0) していた。         */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"

/* 起動していれば必ず在るもの。常駐シェルの実体 ([C4] ここが管理元)。 */
#define STAT_T_PRESENT  "/bin/sh.bin"

static KernelAPI *g_api;
static int g_total;
static int g_passed;

static void check(int cond, const char *label)
{
    g_total++;
    if (cond) {
        g_passed++;
        g_api->kprintf(ATTR_GREEN, "[OK] %s\r\n", label);
    } else {
        g_api->kprintf(ATTR_RED, "[FAIL] %s\r\n", label);
    }
}

/* fstat と isatty が**同じことを言っている**か (票 TASK_FSTAT_REDIR §3-2)。
 *
 *   fstat(fd) が S_IFCHR  <->  isatty(fd) == 1
 *
 * 「isatty(1) == 1」や「fd 1 はキャラクタデバイス」を単独で主張すると、
 * **対話で叩いたときしか真にならない**。ランナーは `stat_t > file` で回すので
 * fd 1 はファイルになり、必ず偽になる。一致だけを見れば対話でもリダイレクト
 * でも成り立ち、2 つの API の**食い違いそのもの**を捕まえる。
 * 2026-09-17 の不具合 (fstat がリダイレクトを見ず無条件で S_IFCHR) は、
 * この形の主張なら最初から落ちていた。 */
static int stat_t_agrees(KernelAPI *api, int fd)
{
    OS32_Stat st;
    int chr;
    int tty;

    if (api->sys_fstat(fd, &st) != 0) return 0;
    chr = ((st.st_mode & OS_S_IFMT) == OS_S_IFCHR) ? 1 : 0;
    tty = (api->sys_isatty(fd) == 1) ? 1 : 0;
    return chr == tty;
}

int main(int argc, char **argv, KernelAPI *api)
{
    OS32_Stat st;
    int       rc;

    (void)argc;
    (void)argv;

    g_api = api;
    g_total = 0;
    g_passed = 0;

    api->kprintf(0x07, "%s", "=== stat API test ===\r\n");

    /* テスト1: 存在するファイルの sys_stat。
     * 2026-09-17 まで `HELLO.BIN` を見ていたが、これは FAT 時代の名残で
     * 今のルートには無い。終了コードが常に 0 だったので不合格が見えず、
     * 約束事 (票 TASK_TEST_RESULT) を入れた途端に FAIL 4/5 で露見した。
     * 起動しているシステムに必ず在るものを見る。これが stat できないなら
     * **環境が壊れている**ので、SKIP ではなく不合格のままにする —
     * 飛ばすと「ここでは関係ない」に見えて破損が隠れる。 */
    check(api->sys_stat(STAT_T_PRESENT, &st) == 0,
          "stat " STAT_T_PRESENT " success");

    /* テスト2: 標準出力の sys_fstat */
    rc = api->sys_fstat(1, &st);
    check(rc == 0, "fstat fd=1 (stdout) success");

    /* テスト3: fd 1 について fstat と isatty が一致する。
     * 2026-09-17 まではここが「fd=1 is a character device」と
     * 「isatty(1) == 1」の 2 本の**別々の**主張だった。どちらも対話でしか
     * 真にならず、しかも fstat 側はリダイレクト中でも S_IFCHR と答えて
     * いたので、ランナーが `stat_t > file` で回して初めて割れた。 */
    check(stat_t_agrees(api, 1), "fstat(1) S_IFCHR <-> isatty(1)==1 (agree)");

    /* テスト4: 無効なファイルの sys_stat */
    check(api->sys_stat("NONEXIST.TXT", &st) != 0,
          "stat NONEXIST.TXT correctly failed");

    /* テスト5: fd 0 でも同じ。パイプ (`cmd | stat_t`) では fd 0 がバッファに
     * なるので、fd 1 だけ見ていると同じ穴をもう一度踏む。 */
    check(stat_t_agrees(api, 0), "fstat(0) S_IFCHR <-> isatty(0)==1 (agree)");

    return os32_test_summary(api, "stat_t", g_passed, g_total);
}
