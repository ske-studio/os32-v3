/* =========================================================================
 *  FS_KIND_HOST.C — 種別判定を「列挙の成否」で代用していた退行 (B5) を
 *  **実物のソースで** 確かめる
 *
 *  票: H1 (docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md) / Codex 実装レビュー
 *      往復 3 の B5。`d574704` で入った退行。
 *  実行: python3 -B tools/tests/test_fs_kind.py [--target]
 *  記録: tools/tests/h1_tdd.md
 *
 *  userland/shell/cmd_fs_shared.c と userland/shell/cmd_file.c を 1 行も
 *  写さずそのまま #include する (模型ではない)。差し替えるのは KernelAPI と
 *  `shell_print_help` だけ。贋ファイルシステムは
 *    - `sys_stat` は正しく答える
 *    - `sys_ls` は指定したパスで OS32_ERR_FULL / OS32_ERR_IO を返す
 *  という状態を作れる (1000 件超のディレクトリ / 途中で切れた列挙の再現)。
 *
 *  いちばん大事なのは `cp -r` の**宛先の階層**:
 *    cp -r /src /big   ->   /big/src/a.txt   (× /big/a.txt)
 *  列挙がエラーを返しても、宛先がディレクトリである限りここは動かない。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include "fs_kind_fake.h"

/* ------------------------------------------------------------------------ */
/*  実物のソース                                                              */
/* ------------------------------------------------------------------------ */

#include "../../userland/shell/cmd_fs_shared.c"
#include "../../userland/shell/cmd_file.c"

/* shell.c 側の実体。ここでは使わない (コマンド表の登録もしない)。
 * ShellCmd は shell.h (上の #include 経由) で定義される。 */
void shell_print_help(const char *cmd) { (void)cmd; }
void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }

/* ------------------------------------------------------------------------ */

static void run_cp(const char *a, const char *b, const char *c)
{
    char *av[5];
    int n = 0;
    fsk_log_len = 0;
    fsk_log[0] = '\0';
    av[n++] = (char *)"cp";
    if (a) av[n++] = (char *)a;
    if (b) av[n++] = (char *)b;
    if (c) av[n++] = (char *)c;
    av[n] = 0;
    cmd_cp(n, av);
}

/* /src (ディレクトリ, a.txt を持つ) と /big (ディレクトリ) を作る。
 * /big の列挙は ls_err を返す (1000 件超 / 途中で切れた列挙の再現)。
 * /big/a.txt には既存の内容を置いておく — 取り違えたら壊れる。 */
static void setup_cp_tree(int ls_err)
{
    int n;
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "NEW");
    n = fsk_add("/big", 1, 0);
    fsk[n].ls_err = ls_err;
    fsk_add("/big/a.txt", 0, "KEEP");
}

int main(void)
{
    int n;

    fake_api_init();
    printf("=== fs_is_dir / cp -r の宛先階層 (票 H1 / 往復 3 の B5) ===\n");

    printf("== fs_path_kind / fs_is_dir は型で答える ==\n");
    fsk_reset();
    fsk_add("/d", 1, 0);
    fsk_add("/f", 0, "x");
    n = fsk_find("/d");
    fsk[n].ls_err = OS32_ERR_FULL;
    check(fs_is_dir("/d") == 1,
          "1000 件超 (列挙 FULL) でもディレクトリと答える");
    check(fs_path_kind("/d") == FS_KIND_DIR, "fs_path_kind も DIR");
    fsk[n].ls_err = OS32_ERR_IO;
    check(fs_is_dir("/d") == 1,
          "途中で切れた列挙 (IO) でもディレクトリと答える");
    check(fs_is_dir("/f") == 0, "通常ファイルは 0");
    check(fs_path_kind("/f") == FS_KIND_FILE, "fs_path_kind は FILE");
    check(fs_path_kind("/nope") == OS32_ERR_NOTFOUND, "不存在は NOTFOUND");
    check(fs_is_dir("/nope") == 0, "不存在は 0");

    printf("== stat が使えない FS のときだけ列挙を代替に使う ==\n");
    fsk_reset();
    n = fsk_add("/d", 1, 0);
    fsk[n].stat_err = OS32_ERR_NOSYS;         /* stat 非対応の FS を模す */
    check(fs_is_dir("/d") == 1, "stat 非対応でも列挙が通ればディレクトリ");
    fsk[n].ls_err = OS32_ERR_FULL;
    check(fs_path_kind("/d") == OS32_ERR_NOSYS,
          "列挙も読めなければ stat のエラーを返す "
          "(「ディレクトリでない」と言い切らない)");
    check(fs_is_dir("/d") == 0, "判定できないときの真偽は 0 (従来の形)");

    printf("== cp -r の宛先階層 ==\n");
    /* (a) 宛先の列挙が FULL — B5 の反例そのもの */
    setup_cp_tree(OS32_ERR_FULL);
    run_cp("-r", "/src", "/big");
    check(fsk_find("/big/src/a.txt") >= 0,
          "列挙 FULL でも /big/src/a.txt へ入る");
    n = fsk_find("/big/a.txt");
    check(n >= 0 && strcmp(fsk[n].data, "KEEP") == 0,
          "**/big/a.txt を上書きしない** (階層の取り違えが無い)");

    /* (b) 宛先の列挙が途中で切れた */
    setup_cp_tree(OS32_ERR_IO);
    run_cp("-r", "/src", "/big");
    check(fsk_find("/big/src/a.txt") >= 0, "列挙 IO でも /big/src/a.txt へ入る");
    n = fsk_find("/big/a.txt");
    check(n >= 0 && strcmp(fsk[n].data, "KEEP") == 0,
          "/big/a.txt を上書きしない");

    /* (c) 列挙が普通に通る場合 — 退行していないこと */
    setup_cp_tree(0);
    run_cp("-r", "/src", "/big");
    check(fsk_find("/big/src/a.txt") >= 0, "通常時も /big/src/a.txt");
    n = fsk_find("/big/a.txt");
    check(n >= 0 && strcmp(fsk[n].data, "KEEP") == 0, "通常時も上書きしない");

    /* (d) 宛先が存在しない = そのものを作る (従来の意味) */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "NEW");
    run_cp("-r", "/src", "/newdir");
    check(fsk_find("/newdir/a.txt") >= 0,
          "宛先が無いときは /newdir 直下へ (basename を足さない)");

    printf("== 単一ファイルの cp ==\n");
    fsk_reset();
    fsk_add("/f.txt", 0, "NEW");
    n = fsk_add("/big", 1, 0);
    fsk[n].ls_err = OS32_ERR_FULL;
    fsk_add("/big/f.txt", 0, "KEEP");
    run_cp("/f.txt", "/big", 0);
    n = fsk_find("/big/f.txt");
    check(n >= 0 && strcmp(fsk[n].data, "NEW") == 0,
          "列挙 FULL のディレクトリ宛でも /big/f.txt へ写す");
    check(fsk_find("/big") >= 0 && fsk[fsk_find("/big")].is_dir,
          "/big をファイルで潰さない");

    printf("== 複数入力の cp ==\n");
    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/b.txt", 0, "B");
    n = fsk_add("/big", 1, 0);
    fsk[n].ls_err = OS32_ERR_FULL;
    run_cp("/a.txt", "/b.txt", "/big");
    check(fsk_find("/big/a.txt") >= 0 && fsk_find("/big/b.txt") >= 0,
          "列挙 FULL でも複数入力を受け付ける");
    check(strstr(fsk_log, "multiple files must be copied into a directory")
              == 0,
          "「ディレクトリでない」と誤って断らない");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
