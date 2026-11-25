/* ======================================================================== */
/*  RESTEST.C — リソース自動クリーンアップ テストプログラム                  */
/*                                                                          */
/*  exec_exit() のリソース回収機能を検証する。                               */
/*  各テスト項目を順番に実行し、意図的にリソースをリークさせる。             */
/*  プログラム終了後、シェルから手動で状態を確認する。                       */
/*                                                                          */
/*  テスト項目:                                                             */
/*    1. FDリーク — vfs_open() 後に vfs_close() を呼ばずに終了              */
/*    2. stdoutリダイレクト残り — stdout をファイルにリダイレクトして放置    */
/*    3. パイプリーク — sys_pipe_alloc() 後に sys_pipe_free() を呼ばない    */
/*    ※ 各テストは個別に実行 (引数で選択)                                   */
/* ======================================================================== */

#include "os32api.h"
#include "rt/testresult.h"
#include <stdio.h>
#include <string.h>

/* FD を 3 本開くための的。**起動していれば必ず在るもの**を指す
 * ([C4] ここが管理元)。2026-09-17 まで `/shell` を開いていたが、これは
 * 古い配置の名残でルートには無く、3 本とも -2 (OS32_ERR_NOTFOUND) で
 * 失敗していた。終了コードが常に 0 だったので誰も気づかなかった。 */
#define RESTEST_OPENABLE  "/bin/sh.bin"

static KernelAPI *api;

/* 合否の出し方は票 docs/archive/test/TASK_TEST_RESULT.md §2。以前は結果を
 * 画面に書くだけで、何が起きても終了コードは 0 だった。 */
static int g_total;
static int g_passed;

static void check(int cond, const char *label)
{
    g_total++;
    if (cond) {
        g_passed++;
        printf("  PASS: %s\n", label);
    } else {
        printf("  FAIL: %s\n", label);
    }
}

/* 文字列比較 (libc不要) */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* ======================================================================== */
/*  テスト1: FDリーク                                                       */
/*  ファイルを開いたまま終了する。exec_exit() がクローズするはず。           */
/* ======================================================================== */
static void test_fd_leak(void)
{
    int fd1, fd2, fd3;

    printf("=== Test 1: FD Leak ===\n");
    printf("Opening 3 files without closing...\n");

    fd1 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);  /* O_RDONLY */
    fd2 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);
    fd3 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);

    printf("  fd1=%d, fd2=%d, fd3=%d\n", fd1, fd2, fd3);

    check(fd1 >= 0 && fd2 >= 0 && fd3 >= 0, "3 FDs opened");

    printf("  Exiting WITHOUT closing FDs...\n");
    printf("  After return, run 'restest verify_fd' to check.\n");
    /* ここで意図的にクローズしない。exec_exit() が回収してくれるか? */
}

/* ======================================================================== */
/*  テスト2: stdoutリダイレクト残り                                          */
/*  stdout をファイルにリダイレクトして戻さずに終了する。                    */
/*  exec_exit() がリセットするはず。リセットされないと画面に何も出ない。    */
/* ======================================================================== */
static void test_redirect_leak(void)
{
    int rc;

    printf("=== Test 2: Redirect Leak ===\n");
    printf("Redirecting stdout to /tmp_redir.txt...\n");

    rc = api->sys_redirect_fd(1, "/tmp_redir.txt", 1); /* FD_REDIR_WRITE */

    check(rc == 0, "stdout redirected to /tmp_redir.txt");
    if (rc == 0) {
        /* この出力はファイルに行く */
        printf("This text should go to file, not screen.\n");
    }

    /* ここで意図的にリセットしない。exec_exit() がリセットしてくれるか? */
    /* リセットされなければ、シェルに戻った後も画面に何も表示されない */
}

/* ======================================================================== */
/*  テスト3: パイプリーク                                                    */
/*  パイプを確保して解放せずに終了する。                                     */
/* ======================================================================== */
static void test_pipe_leak(void)
{
    int id1, id2, id3;

    printf("=== Test 3: Pipe Buffer Leak ===\n");
    printf("Allocating pipe buffers without freeing...\n");

    id1 = api->sys_pipe_alloc();
    id2 = api->sys_pipe_alloc();

    printf("  pipe1=%d, pipe2=%d\n", id1, id2);

    check(id1 >= 0 && id2 >= 0, "2 pipe buffers allocated");

    /* 3つ目 — 上限2個なので失敗するはず */
    id3 = api->sys_pipe_alloc();
    printf("  pipe3=%d (should be -1: limit is 2)\n", id3);
    check(id3 < 0, "the 3rd pipe is refused (limit is 2)");

    printf("  Exiting WITHOUT freeing pipes...\n");
    printf("  After return, run 'restest verify_pipe' to check.\n");
}

/* ======================================================================== */
/*  検証: FDリーク後の確認                                                  */
/*  exec_exit() がクリーンアップした後に新しいFDが開けるか?                 */
/* ======================================================================== */
static void verify_fd(void)
{
    int fd1, fd2, fd3;

    printf("=== Verify: FD Cleanup ===\n");
    printf("Trying to open 3 files (should succeed if cleanup worked)...\n");

    fd1 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);
    fd2 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);
    fd3 = api->sys_open(RESTEST_OPENABLE, KAPI_O_RDONLY);

    printf("  fd1=%d, fd2=%d, fd3=%d\n", fd1, fd2, fd3);

    check(fd1 >= 0 && fd2 >= 0 && fd3 >= 0,
          "FD cleanup worked (all 3 FDs opened again)");

    /* 後始末 */
    if (fd1 >= 0) api->sys_close(fd1);
    if (fd2 >= 0) api->sys_close(fd2);
    if (fd3 >= 0) api->sys_close(fd3);
}

/* ======================================================================== */
/*  検証: パイプリーク後の確認                                              */
/* ======================================================================== */
static void verify_pipe(void)
{
    int id1, id2;

    printf("=== Verify: Pipe Cleanup ===\n");
    printf("Trying to allocate 2 pipes (should succeed if cleanup worked)...\n");

    id1 = api->sys_pipe_alloc();
    id2 = api->sys_pipe_alloc();

    printf("  pipe1=%d, pipe2=%d\n", id1, id2);

    check(id1 >= 0 && id2 >= 0,
          "pipe cleanup worked (both pipes allocated again)");

    /* 後始末 */
    if (id1 >= 0) api->sys_pipe_free(id1);
    if (id2 >= 0) api->sys_pipe_free(id2);
}

/* ======================================================================== */
/*  検証: リダイレクト後の確認                                              */
/*  画面に表示されていれば成功                                              */
/* ======================================================================== */
static void verify_redirect(void)
{
    printf("=== Verify: Redirect Cleanup ===\n");
    printf("If you can see this message, redirect cleanup PASSED!\n");
    printf("(stdout is correctly pointing to console)\n");
    /* 「画面に出たか」は人にしか見えない。機械が読めるのは FD 1 の行き先で、
     * リダイレクトが残っていればファイルなので端末ではない。 */
    check(api->sys_isatty(1) == 1, "stdout points at the console again");
}

/* ======================================================================== */
/*  全テスト一括実行 (リーク → 終了 → 次回 verify)                         */
/* ======================================================================== */
static void test_all(void)
{
    printf("=== Resource Cleanup Test Suite ===\n\n");

    /* FDリークテスト */
    test_fd_leak();
    printf("\n");

    /* パイプリークテスト */
    test_pipe_leak();
    printf("\n");

    /* リダイレクトテストは最後 (stdout が消えるため) */
    printf("Skipping redirect test (run 'restest redirect' separately)\n");
    printf("\nAll leak tests done. Resources intentionally leaked.\n");
    printf("Run 'restest verify' after this to check cleanup.\n");
}

/* ======================================================================== */
/*  全検証一括実行                                                          */
/* ======================================================================== */
static void verify_all(void)
{
    printf("=== Resource Cleanup Verification ===\n\n");

    verify_fd();
    printf("\n");

    verify_pipe();
    printf("\n");

    verify_redirect();
}

/* ======================================================================== */
/*  usage                                                                    */
/* ======================================================================== */
static void usage(void)
{
    printf("Usage: restest <command>\n");
    printf("\n");
    printf("Leak tests (intentionally leak resources):\n");
    printf("  all        Run all leak tests\n");
    printf("  fd         FD leak test\n");
    printf("  redirect   Stdout redirect leak test\n");
    printf("  pipe       Pipe buffer leak test\n");
    printf("\n");
    printf("Verification (check if cleanup worked):\n");
    printf("  verify        Run all verifications\n");
    printf("  verify_fd     Check FD cleanup\n");
    printf("  verify_pipe   Check pipe cleanup\n");
    printf("  verify_redir  Check redirect cleanup\n");
}

/* ======================================================================== */
/*  main                                                                     */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *kapi_arg)
{
    api = kapi_arg;
    g_total = 0;
    g_passed = 0;

    if (argc < 2) {
        usage();
        return os32_test_summary_skip(api, "restest", "no subcommand given");
    }

    if (str_eq(argv[1], "all"))           test_all();
    else if (str_eq(argv[1], "fd"))       test_fd_leak();
    else if (str_eq(argv[1], "redirect")) test_redirect_leak();
    else if (str_eq(argv[1], "pipe"))     test_pipe_leak();
    else if (str_eq(argv[1], "verify"))   verify_all();
    else if (str_eq(argv[1], "verify_fd"))    verify_fd();
    else if (str_eq(argv[1], "verify_pipe"))  verify_pipe();
    else if (str_eq(argv[1], "verify_redir")) verify_redirect();
    else {
        printf("Unknown command: %s\n", argv[1]);
        usage();
        return os32_test_summary_skip(api, "restest", "unknown subcommand");
    }

    /* 集計行は os32_test_summary が **fd 1** へ出す (票 §11)。
     * `restest redirect` は FD 1 をファイルへ向けたまま終わるのが試験の
     * 中身なので、その場合だけ集計行もそのファイルへ落ちる — これは
     * 正しい。集計行はリダイレクトに従う行であって、画面に出す行では
     * ない。自動回帰に載せているのは `restest all` (リダイレクト試験を
     * 含まない) なので、ランナーはいつもどおり拾える。 */
    return os32_test_summary(api, "restest", g_passed, g_total);
}
