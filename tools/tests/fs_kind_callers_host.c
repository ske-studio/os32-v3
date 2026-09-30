/* =========================================================================
 *  FS_KIND_CALLERS_HOST.C — 種別が「分からない」ときに cp / mv / rm が断るか
 *  を、**受け手 (呼び出し元の分岐) で** 確かめる
 *
 *  票: docs/archive/shell/TASK_FS_TYPE.md §3 (fs_is_dir は「不明」を運べない)
 *  実行: python3 -B tools/tests/test_fs_kind_callers.py [--target]
 *  記録: tools/tests/fs_kind_callers_tdd.md
 *
 *  fs_is_dir() は stat と列挙の**両方**が失敗すると 0 を返す。cmd_file.c の
 *  呼び出し元 5 箇所 (cp の宛先 / cp の入力 / mv の FS またぎ / mv の宛先 /
 *  rm) はその 0 を「ファイル」と読み、ファイルの枝へ進んでいた。
 *  B8 の原則 (POLICY_DEBUG §4-35): 「読めなかった」を「無い / その型ではない」
 *  と読み替えない。判定関数の戻り値ではなく、**受け手が断るか**を見る。
 *
 *  userland/shell/cmd_fs_shared.c と cmd_file.c を 1 行も写さずそのまま
 *  #include する。贋 FS は fs_kind_host.c と共有 (fs_kind_fake.h)。
 *  「分からない」は sys_stat と sys_ls の両方に OS32_ERR_IO を注入して作る。
 *
 *  断ったことの判定は 2 つ:
 *    - 断りの文言 "cannot determine the type of" が出る
 *    - 副作用の呼び出し (open / mkdir / rename / unlink) が 0 回で、
 *      媒体 (贋 FS の中身) が変わっていない
 *
 *  6 章は別の欠陥 (継承バグ台帳): `cp -r` が **失敗する経路で宛先に空の
 *  ディレクトリを残す**。do_copy_recursive_impl は sys_mkdir(dst) を件数の
 *  確認より前に呼び、しかも戻り値を見ていなかった。直しは
 *    - 収集して件数を確かめてから mkdir する
 *    - mkdir の戻り値を見て、作れなければ中へ進まない
 *    - ただし既に在る**ディレクトリ**への上書きコピーは今までどおり通す
 *      (OS32_ERR_EXIST + 型が DIR のときだけ。ファイルなら断る)
 *  判定は sys_mkdir / sys_open / sys_write の**呼び出し回数**と、贋 FS に
 *  宛先ノードが生えていないことで見る。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include "fs_kind_fake.h"

#include "../../userland/shell/cmd_fs_shared.c"
#include "../../userland/shell/cmd_file.c"

void shell_print_help(const char *cmd) { (void)cmd; }
void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }

#define REFUSAL "cannot determine the type of"

/* R1c: 組み込み handler は int になった (票 TASK_EXIT_STATUS 決裁 E2)。
 * この試験が見るのは呼び出し回数と贋 FS の状態なので、戻り値は捨てる。 */
static void run(int (*fn)(int, char **), const char *name,
                const char *a, const char *b, const char *c, const char *d)
{
    char *av[6];
    int n = 0;
    fsk_log_len = 0;
    fsk_log[0] = '\0';
    fsk_open_calls = 0;
    fsk_mkdir_calls = 0;
    fsk_rename_calls = 0;
    fsk_unlink_calls = 0;
    fsk_write_calls = 0;
    av[n++] = (char *)name;
    if (a) av[n++] = (char *)a;
    if (b) av[n++] = (char *)b;
    if (c) av[n++] = (char *)c;
    if (d) av[n++] = (char *)d;
    av[n] = 0;
    (void)fn(n, av);
}

/* stat も列挙も読めない = 種別が分からない */
static void mark_unknown(const char *path)
{
    int n = fsk_find(path);
    if (n < 0) { printf("  (harness) no node %s\n", path); exit(2); }
    fsk[n].stat_err = OS32_ERR_IO;
    fsk[n].ls_err = OS32_ERR_IO;
}

static int data_is(const char *path, const char *want)
{
    int n = fsk_find(path);
    return n >= 0 && strcmp(fsk[n].data, want) == 0;
}

static int refused(void)
{
    return strstr(fsk_log, REFUSAL) != 0;
}

static int no_side_effects(void)
{
    return fsk_open_calls == 0 && fsk_mkdir_calls == 0 &&
           fsk_rename_calls == 0 && fsk_unlink_calls == 0;
}

int main(void)
{
    fake_api_init();
    printf("=== cp / mv / rm は種別が分からないと断る (TASK_FS_TYPE §3) ===\n");

    printf("== 前提: 注入で fs_path_kind が負 (判定できない) になる ==\n");
    fsk_reset();
    fsk_add("/u", 1, 0);
    fsk_add("/uf", 0, "X");
    mark_unknown("/u");
    mark_unknown("/uf");
    check(fs_path_kind("/u") == OS32_ERR_IO, "ディレクトリの stat+列挙 IO は IO");
    check(fs_path_kind("/uf") == OS32_ERR_IO, "ファイルの stat IO は IO");

    /* ---- 1. cp の宛先 -------------------------------------------------- */
    printf("== 1. cp の宛先が分からない ==\n");
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "NEW");
    fsk_add("/u", 1, 0);
    fsk_add("/u/a.txt", 0, "KEEP");
    mark_unknown("/u");
    run(cmd_cp, "cp", "-r", "/src", "/u", 0);
    check(refused(), "cp -r /src /u: 断りを出す");
    check(data_is("/u/a.txt", "KEEP"),
          "cp -r /src /u: **/u/a.txt を上書きしない** (宛先をディレクトリで"
          "ないと読んで /u 直下へ展開しない)");
    check(no_side_effects(), "cp -r /src /u: open/mkdir を呼ばない");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/u", 1, 0);
    mark_unknown("/u");
    run(cmd_cp, "cp", "/a.txt", "/u", 0, 0);
    check(refused(), "cp /a.txt /u: 断りを出す");
    check(no_side_effects(), "cp /a.txt /u: 宛先を O_CREAT で開かない");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/b.txt", 0, "B");
    fsk_add("/u", 1, 0);
    mark_unknown("/u");
    run(cmd_cp, "cp", "/a.txt", "/b.txt", "/u", 0);
    check(refused(), "cp 複数 → /u: 断りを出す");
    check(strstr(fsk_log, "must be copied into a directory") == 0,
          "cp 複数 → /u: 「ディレクトリでない」と言い切らない");
    check(no_side_effects(), "cp 複数 → /u: 何も開かない");

    /* ---- 2. cp の入力 -------------------------------------------------- */
    printf("== 2. cp の入力が分からない ==\n");
    fsk_reset();
    fsk_add("/su", 1, 0);
    fsk_add("/su/a.txt", 0, "A");
    fsk_add("/dst", 1, 0);
    mark_unknown("/su");
    run(cmd_cp, "cp", "-r", "/su", "/dst", 0);
    check(refused(), "cp -r /su /dst: 断りを出す");
    check(no_side_effects(), "cp -r /su /dst: ファイルとして開かない");
    check(fsk_find("/dst/su") < 0, "cp -r /su /dst: /dst/su を作らない");

    fsk_reset();
    fsk_add("/uf", 0, "X");
    fsk_add("/ok.txt", 0, "OK");
    fsk_add("/dst", 1, 0);
    mark_unknown("/uf");
    run(cmd_cp, "cp", "/uf", "/ok.txt", "/dst", 0);
    check(refused(), "cp /uf /ok.txt /dst: /uf は断る");
    check(fsk_find("/dst/uf") < 0, "cp /uf ...: /dst/uf を作らない");
    check(data_is("/dst/ok.txt", "OK"),
          "cp /uf /ok.txt /dst: 分かる入力 /ok.txt は写す (1 件の断りで全体を止めない)");

    /* ---- 3. mv の FS またぎ (rename が INVAL) --------------------------- */
    printf("== 3. mv が FS をまたぐとき、入力が分からない ==\n");
    fsk_reset();
    fsk_add("/su", 1, 0);
    fsk_add("/su/a.txt", 0, "A");
    fsk_add("/dst", 1, 0);
    mark_unknown("/su");
    fsk_rename_err = OS32_ERR_INVAL;
    run(cmd_mv, "mv", "/su", "/dst/x", 0, 0);   /* 宛先は無い = 分かる */
    check(fsk_rename_calls == 1, "mv /su /dst/x: まず rename を試す");
    check(refused(), "mv /su /dst/x: rename が INVAL なら断りを出す");
    check(fsk_open_calls == 0 && fsk_unlink_calls == 0,
          "mv /su /dst/x: コピーも unlink もしない");
    check(fsk_find("/su") >= 0 && fsk_find("/dst/x") < 0,
          "mv /su /dst/x: 原本が残り、宛先を作らない");

    /* ---- 4. mv の宛先 -------------------------------------------------- */
    printf("== 4. mv の宛先が分からない ==\n");
    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/u", 1, 0);
    mark_unknown("/u");
    run(cmd_mv, "mv", "/a.txt", "/u", 0, 0);
    check(refused(), "mv /a.txt /u: 断りを出す");
    check(fsk_rename_calls == 0, "mv /a.txt /u: /u へ rename しない");
    check(data_is("/a.txt", "A"), "mv /a.txt /u: 原本が残る");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/b.txt", 0, "B");
    fsk_add("/u", 1, 0);
    mark_unknown("/u");
    run(cmd_mv, "mv", "/a.txt", "/b.txt", "/u", 0);
    check(refused(), "mv 複数 → /u: 断りを出す");
    check(strstr(fsk_log, "must be moved into a directory") == 0,
          "mv 複数 → /u: 「ディレクトリでない」と言い切らない");
    check(no_side_effects(), "mv 複数 → /u: 何もしない");

    /* ---- 5. rm --------------------------------------------------------- */
    printf("== 5. rm の対象が分からない ==\n");
    fsk_reset();
    fsk_add("/ud", 1, 0);
    fsk_add("/uf", 0, "X");
    mark_unknown("/ud");
    mark_unknown("/uf");
    run(cmd_rm, "rm", "/ud", 0, 0, 0);
    check(refused(), "rm /ud: 断りを出す");
    check(fsk_unlink_calls == 0 && fsk_find("/ud") >= 0,
          "rm /ud: **ディレクトリを unlink しない**");
    run(cmd_rm, "rm", "/uf", 0, 0, 0);
    check(refused(), "rm /uf: 断りを出す");
    check(fsk_unlink_calls == 0 && fsk_find("/uf") >= 0, "rm /uf: unlink しない");

    /* ---- 分かるときは従来どおり (退行しない) ---------------------------- */
    printf("== 分かるときは従来どおり ==\n");
    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/d", 1, 0);
    run(cmd_cp, "cp", "/a.txt", "/d", 0, 0);
    check(data_is("/d/a.txt", "A") && !refused(), "cp ファイル → ディレクトリ");
    run(cmd_cp, "cp", "/a.txt", "/new.txt", 0, 0);
    check(data_is("/new.txt", "A") && !refused(),
          "cp ファイル → 無い名前 (NOTFOUND は「無い」と分かっている)");
    run(cmd_cp, "cp", "/nope", "/d", 0, 0);
    check(!refused() && strstr(fsk_log, "cannot open '/nope'") != 0,
          "cp 無い入力: 断りではなく従来の cannot open");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/d", 1, 0);
    run(cmd_mv, "mv", "/a.txt", "/d", 0, 0);
    check(fsk_find("/d/a.txt") >= 0 && fsk_find("/a.txt") < 0 && !refused(),
          "mv ファイル → ディレクトリ (rename)");
    run(cmd_mv, "mv", "/d/a.txt", "/b.txt", 0, 0);
    check(fsk_find("/b.txt") >= 0 && !refused(), "mv ファイル → 無い名前");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/sd", 1, 0);
    fsk_add("/d", 1, 0);
    fsk_rename_err = OS32_ERR_INVAL;
    run(cmd_mv, "mv", "/a.txt", "/d", 0, 0);
    check(data_is("/d/a.txt", "A") && fsk_find("/a.txt") < 0 && !refused(),
          "mv FS またぎのファイル: コピー + unlink");
    run(cmd_mv, "mv", "/sd", "/d", 0, 0);
    check(strstr(fsk_log, "cannot move directory") != 0 &&
              fsk_find("/sd") >= 0 && !refused(),
          "mv FS またぎのディレクトリ: 従来の断り (種別は分かっている)");

    fsk_reset();
    fsk_add("/a.txt", 0, "A");
    fsk_add("/d", 1, 0);
    run(cmd_rm, "rm", "/a.txt", 0, 0, 0);
    check(fsk_find("/a.txt") < 0 && !refused(), "rm ファイル");
    run(cmd_rm, "rm", "/d", 0, 0, 0);
    check(fsk_find("/d") >= 0 && strstr(fsk_log, "Is a directory") != 0,
          "rm ディレクトリ: 従来の断り");
    run(cmd_rm, "rm", "/nope", 0, 0, 0);
    check(!refused() && strstr(fsk_log, "cannot remove '/nope'") != 0,
          "rm 無いもの: 従来の cannot remove");

    /* ---- 6. cp -r は失敗する経路で宛先に空のディレクトリを残さない ----- */
    printf("== 6. cp -r: 作れなかった / 中止した宛先を残さない ==\n");

    /* (6-1) 収集表の上限を超える -> **sys_mkdir を呼ばない** */
    {
        char name[64];
        int k;
        fsk_reset();
        fsk_add("/many", 1, 0);
        for (k = 0; k < MAX_COPY_ENTRIES + 6; k++) {
            sprintf(name, "/many/f%02d", k);
            fsk_add(name, 0, "x");
        }
        fsk_add("/dst", 1, 0);
        run(cmd_cp, "cp", "-r", "/many", "/dst", 0);
        check(strstr(fsk_log, "too many entries") != 0,
              "上限超過: 断りを出す (従来どおり)");
        check(fsk_mkdir_calls == 0,
              "上限超過: **sys_mkdir を 1 回も呼ばない** (収集が先)");
        check(fsk_find("/dst/many") < 0,
              "上限超過: 宛先に空のディレクトリを残さない");
        check(fsk_open_calls == 0 && fsk_write_calls == 0,
              "上限超過: 中へコピーしない");
    }

    /* (6-2) 列挙そのものが失敗する -> 宛先を作らない */
    fsk_reset();
    {
        int n2 = fsk_add("/src", 1, 0);
        fsk[n2].ls_err = OS32_ERR_IO;       /* stat は通るので種別は DIR */
    }
    run(cmd_cp, "cp", "-r", "/src", "/new", 0);
    check(strstr(fsk_log, "cannot read directory") != 0,
          "列挙 IO: 断りを出す");
    check(fsk_mkdir_calls == 0 && fsk_find("/new") < 0,
          "列挙 IO: 宛先を作らない");

    /* (6-3) sys_mkdir が失敗する -> 中へコピーしない */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "A");
    fsk_mkdir_err = OS32_ERR_IO;
    run(cmd_cp, "cp", "-r", "/src", "/new", 0);
    fsk_mkdir_err = 0;
    check(strstr(fsk_log, "cannot create directory") != 0,
          "mkdir 失敗: 断りを出す");
    check(fsk_mkdir_calls == 1, "mkdir 失敗: mkdir は 1 回だけ");
    check(fsk_open_calls == 0 && fsk_write_calls == 0,
          "mkdir 失敗: **中へコピーしない** (open / write を呼ばない)");
    check(fsk_find("/new/a.txt") < 0, "mkdir 失敗: /new/a.txt を作らない");

    /* (6-4) 既に在る**ディレクトリ** (OS32_ERR_EXIST) -> 今までどおり通す。
     * `cp -r /src /dst` を 2 回打つ使い方 (2 回目は /dst/src が既に在る)。 */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "NEW");
    fsk_add("/src/sub", 1, 0);
    fsk_add("/src/sub/b.txt", 0, "NEWB");
    fsk_add("/dst", 1, 0);
    fsk_add("/dst/src", 1, 0);              /* 既に在る (1 回目の結果) */
    fsk_add("/dst/src/a.txt", 0, "OLD");
    fsk_add("/dst/src/sub", 1, 0);          /* 入れ子も既に在る */
    fsk_add("/dst/src/sub/b.txt", 0, "OLDB");
    run(cmd_cp, "cp", "-r", "/src", "/dst", 0);
    check(strstr(fsk_log, "cannot create directory") == 0,
          "既存ディレクトリ: 断らない");
    check(data_is("/dst/src/a.txt", "NEW"),
          "既存ディレクトリ: 中身を上書きコピーする (2 回目の cp -r が通る)");
    check(data_is("/dst/src/sub/b.txt", "NEWB"),
          "既存ディレクトリ: 入れ子の既存ディレクトリへも入る");

    /* (6-5) 同名の**ファイル**が在る (EXIST だが型が違う) -> 中へ進まない */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "A");
    fsk_add("/f", 0, "KEEP");
    run(cmd_cp, "cp", "-r", "/src", "/f", 0);
    check(strstr(fsk_log, "cannot create directory") != 0,
          "宛先が同名のファイル: 断りを出す");
    check(data_is("/f", "KEEP"), "宛先が同名のファイル: 中身を壊さない");
    check(fsk_open_calls == 0 && fsk_write_calls == 0 &&
              fsk_find("/f/a.txt") < 0,
          "宛先が同名のファイル: 中へコピーしない");

    /* (6-6) 型が分からない宛先が既に在る -> 断る (B8 の原則) */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "A");
    fsk_add("/dst", 1, 0);
    fsk_add("/dst/src", 1, 0);
    mark_unknown("/dst/src");
    run(cmd_cp, "cp", "-r", "/src", "/dst", 0);
    check(strstr(fsk_log, "cannot create directory") != 0,
          "宛先の型が分からない: 断る (EXIST を無条件に通さない)");
    check(fsk_open_calls == 0 && fsk_write_calls == 0,
          "宛先の型が分からない: 中へコピーしない");

    /* (6-7) 正常系 — 誤発火していない裏 */
    fsk_reset();
    fsk_add("/src", 1, 0);
    fsk_add("/src/a.txt", 0, "A");
    fsk_add("/src/sub", 1, 0);
    fsk_add("/src/sub/b.txt", 0, "B");
    run(cmd_cp, "cp", "-r", "/src", "/new", 0);
    check(!refused() && strstr(fsk_log, "cannot create directory") == 0,
          "正常系: 何も断らない");
    check(data_is("/new/a.txt", "A") && data_is("/new/sub/b.txt", "B"),
          "正常系: 入れ子ごと写る");
    check(fsk_mkdir_calls == 2, "正常系: mkdir は /new と /new/sub の 2 回");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
