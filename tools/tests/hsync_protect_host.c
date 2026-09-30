/* ========================================================================
 *  hsync_protect_host.c — hsync の「コピー先が /etc/settings.db* か」の
 *  字句判定を **実物のソースで** 確かめる (票 S0-D / D0)
 *
 *  対象票: docs/archive/settings/TASK_S0.md §2
 *  実行:   python3 -B tools/tests/test_hsync_protect.py
 *  記録:   tools/tests/s0_tdd.md 節 D
 *
 *  userland/system/hsync_protect.inc を 1 行も写さずそのまま #include する
 *  (模型ではない)。判定は KernelAPI にも FS にも触らない純関数なので、ホストで
 *  そのまま走らせられる。実体規則 (inode 比較) は sys_stat が要るので
 *  hsync.c 側に残っており、ここでは見ない。
 * ======================================================================== */

#include <stdio.h>
#include <string.h>

#include "hsync_protect.inc"

/* 真偽だけを見る包み。実体は .inc の 3 値 hsp_path_classify で、
 * 判定できないもの (正規化に失敗) は保護側に倒す (fail-closed)。
 * 本体 (hsync.c) は「保護」と「判定できない」を数え分けるので 3 値のまま使う。 */
static int hsp_path_protected(const char *path)
{
    return hsp_path_classify(path) != 0;
}

static int failures;

static void check(int cond, const char *name)
{
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

static void check_norm(const char *in, const char *want, const char *name)
{
    char out[HSP_MAX_PATH];
    int ok = hsp_normalize(in, out, (int)sizeof(out));
    if (!ok) {
        printf("  FAIL %s (正規化できない: %s)\n", name, in);
        failures++;
        return;
    }
    if (strcmp(out, want) != 0) {
        printf("  FAIL %s (%s -> %s, 期待 %s)\n", name, in, out, want);
        failures++;
        return;
    }
    printf("  ok   %s\n", name);
}

int main(void)
{
    char joined[HSP_MAX_PATH];

    printf("== 字句正規化 ==\n");
    check_norm("/etc/settings.db", "/etc/settings.db", "そのまま");
    check_norm("/etc/./settings.db", "/etc/settings.db", "'./' を畳む");
    check_norm("//etc///settings.db", "/etc/settings.db", "連続 '/' を畳む");
    check_norm("/etc/sub/../settings.db", "/etc/settings.db", "'..' を戻す");
    check_norm("etc/settings.db", "/etc/settings.db", "相対を絶対に");
    check_norm("/etc/settings.db/", "/etc/settings.db",
               "末尾 '/' (mkdir 経路)");
    check(hsp_normalize("/../etc/settings.db", joined,
                        (int)sizeof(joined)) == 0,
          "root を越える '..' は失敗");

    printf("== 名前規則 ==\n");
    check(hsp_path_protected("/etc/settings.db"), "/etc/settings.db");
    check(hsp_path_protected("/etc/settings.db-journal"), "journal");
    check(hsp_path_protected("/etc/settings.db-wal"), "wal");
    check(hsp_path_protected("/etc/settings.db-shm"), "shm");
    check(hsp_path_protected("/etc/settings.db.bak"), "bak");
    check(hsp_path_protected("/etc/./settings.db"), "'./' 経由でも守る");
    check(hsp_path_protected("/etc/sub/../settings.db"), "'..' 経由でも守る");
    check(hsp_path_protected("//etc//settings.db"), "連続 '/' でも守る");
    check(hsp_path_protected("/ETC/SETTINGS.DB"), "大文字でも守る");
    check(hsp_path_protected("/etc/Settings.Db-Journal"), "混在大文字");
    check(hsp_path_protected("/etc/settings.db/"),
          "保護対象名のディレクトリも作らせない");

    printf("== 通してよいもの ==\n");
    check(!hsp_path_protected("/etc/settings.tsv"), "初期値 tsv は通す");
    check(!hsp_path_protected("/etc/motd"), "/etc の他のファイル");
    check(!hsp_path_protected("/etc/sub/settings.db"),
          "親が /etc でなければ対象外");
    check(!hsp_path_protected("/settings.db"), "ルート直下は対象外");
    check(!hsp_path_protected("/bin/sh.bin"), "普通のバイナリ");
    check(!hsp_path_protected("/etc"), "/etc そのものは作ってよい");

    printf("== /etc 列挙の一致判定 (実体規則の入口、往復 1 の B5) ==\n");
    /* sys_ls が返す**実在名**を大文字小文字を無視して拾えること。
     * 小文字 5 名を決め打ちで stat するだけでは SETTINGS.DB を取りこぼし、
     * そこへの hardlink を hsync -f bin が上書きしていた。 */
    check(hsp_is_protected_basename("settings.db"), "小文字そのまま");
    check(hsp_is_protected_basename("SETTINGS.DB"), "全部大文字");
    check(hsp_is_protected_basename("Settings.Db"), "混在");
    check(hsp_is_protected_basename("SETTINGS.DB-JOURNAL"), "大文字 journal");
    check(hsp_is_protected_basename("settings.db.BAK"), "大文字 bak");
    check(hsp_is_protected_basename("settings.db-WAL"), "大文字 wal");
    check(hsp_is_protected_basename("settings.db-Shm"), "混在 shm");
    check(!hsp_is_protected_basename("settings.tsv"), "tsv は拾わない");
    check(!hsp_is_protected_basename("settings.db2"), "接頭一致では拾わない");
    check(!hsp_is_protected_basename("ettings.db"), "部分一致では拾わない");
    check(!hsp_is_protected_basename(""), "空文字");

    printf("== 祖先の保護 (往復 2 の 2) ==\n");
    /* `/etc/settings.db` がディレクトリのとき、その中への宛先は最終要素だけ
     * 見ても素通りする (親が /etc ではないので名前規則に当たらない)。 */
    check(hsp_path_protected("/etc/settings.db/sub"), "保護対象の直下");
    check(hsp_path_protected("/etc/settings.db/sub/file"), "保護対象の孫");
    check(hsp_path_protected("/etc/SETTINGS.DB/inner"), "大文字の祖先");
    check(hsp_path_protected("/etc/settings.db-journal/x"), "journal の直下");
    check(hsp_path_protected("/etc/./settings.db/../settings.db/x"),
          "正規化してから祖先を見る");
    check(!hsp_path_protected("/etc/settings.tsv/x"), "tsv は祖先でも対象外");
    check(!hsp_path_protected("/etc/sub/settings.db2/x"), "別名の祖先");

    printf("== 長いパスの連結 (往復 2 の 5) ==\n");
    {
        /* `hsync ./././...etc` 相当。連結が容量を越えたら**判定より前に**
         * 止まる必要がある。純関数側は「正規化できない = 保護側」に倒す。
         * バッファは "a/" x HSP_MAX_PATH + "x" + NUL = 2*HSP_MAX_PATH+2 必要
         * (2 倍では 2 バイト足りずに試験自身が溢れていた)。 */
        static char longp[HSP_MAX_PATH * 2 + 16];
        int n;
        longp[0] = '\0';
        for (n = 0; n < HSP_MAX_PATH; n++) strcat(longp, "a/");
        strcat(longp, "x");
        check(hsp_normalize(longp, joined, (int)sizeof(joined)) == 0,
              "容量を越える正規化は失敗する");
        check(hsp_path_protected(longp), "正規化できないものは保護側");
    }

    printf("== 名前の長さ (往復 3 の D6) ==\n");
    {
        /* ls_cb は名前を NAME_CAP(64) のバッファに写す。切り詰めて写すと
         * **別のファイル**を作って「成功」と出るので、収まらないものは
         * 取り込まずにエラーへ回す。 */
        char name[128];
        int n;

        check(hsp_name_fits("sh.bin", 64), "普通の名前");
        check(hsp_name_fits("", 64), "空文字");
        for (n = 0; n < 63; n++) name[n] = 'a';
        name[63] = '\0';
        check(hsp_name_fits(name, 64), "63 文字ちょうどは収まる");
        name[63] = 'a';
        name[64] = '\0';
        check(!hsp_name_fits(name, 64), "64 文字は収まらない");
        for (n = 0; n < 127; n++) name[n] = 'b';
        name[127] = '\0';
        check(!hsp_name_fits(name, 64), "127 文字は収まらない");
        check(!hsp_name_fits(0, 64), "NULL");
    }

    printf("== 最終行のラベル (最終往復) ==\n");
    /* 失敗があるのに "Done:" と出すと、終了コードを見ない目には成功に読める
     * (「失敗時に成功表示へ進めない」、FOUNDATION §5)。件数はそのまま。 */
    check(strcmp(hsp_final_label(0), "Done:") == 0, "0 件なら Done:");
    check(strcmp(hsp_final_label(1), "FAILED:") == 0, "1 件でも FAILED:");
    check(strcmp(hsp_final_label(42), "FAILED:") == 0, "複数でも FAILED:");

    printf("== -f の subdir 連結 (hsync.c:254 相当) ==\n");
    /* `hsync -f etc` は dst = "/" + "etc"、その下に settings.db を作る */
    strcpy(joined, "/");
    strcat(joined, "etc");
    check(!hsp_path_protected(joined), "hsync -f etc の dst 自体は /etc");
    strcat(joined, "/");
    strcat(joined, "settings.db");
    check(hsp_path_protected(joined), "hsync -f etc の下の settings.db");

    /* `hsync -f etc/` のような指定でも連結後に守れること */
    strcpy(joined, "/");
    strcat(joined, "etc/");
    strcat(joined, "/settings.db-journal");
    check(hsp_path_protected(joined), "hsync -f etc/ の journal");

    /* `hsync ..` のような指定は正規化に失敗し、保護側 (= コピーしない) */
    strcpy(joined, "/");
    strcat(joined, "../etc/settings.db");
    check(hsp_path_protected(joined), "'..' 混じりは保護側へ倒す");

    printf("== S3-D リカバリ生成物の名前 (票 TASK_S3 §0、往復 3 の B5) ==\n");
    /* `install --recover-settings` / `--revert-settings` が /hd0/etc に作る
     * 7 名 (票 §1b の 9 名から本体 `settings.db` と `settings.db-journal` を
     * 除いたもの)。`.bak` / `.bak-journal` は元の対の**唯一の写し**、
     * `.recover-state` は phase の印なので、通常同期 (hsync) が 1 つでも
     * 掴むと「元へ戻す」経路そのものが消える。`.new*` / `.failed*` も
     * 他人の生成物で、配備が触ってよいものではない。 */
    {
        static const char *rec[] = {
            "settings.db.bak",
            "settings.db.bak-journal",
            "settings.db.failed",
            "settings.db.failed-journal",
            "settings.db.new",
            "settings.db.new-journal",
            "settings.db.recover-state",
            0
        };
        char path[HSP_MAX_PATH];
        char upper[HSP_MAX_PATH];
        int i;
        int k;

        for (i = 0; rec[i]; i++) {
            /* 名前規則: /etc/<name> */
            strcpy(path, "/etc/");
            strcat(path, rec[i]);
            check(hsp_path_protected(path), rec[i]);

            /* /etc 列挙の一致判定 (実体規則の入口) */
            check(hsp_is_protected_basename(rec[i]), rec[i]);

            /* ext2 は大文字小文字を区別する。実在名が大文字でも守る */
            for (k = 0; rec[i][k]; k++) {
                int c = rec[i][k];
                upper[k] = (char)((c >= 'a' && c <= 'z')
                                  ? c - ('a' - 'A') : c);
            }
            upper[k] = '\0';
            check(hsp_is_protected_basename(upper), "大文字の実在名");
            strcpy(path, "/etc/");
            strcat(path, upper);
            check(hsp_path_protected(path), "大文字の名前規則");

            /* 残骸ディレクトリになっていても中へ書かせない (祖先の保護) */
            strcpy(path, "/etc/");
            strcat(path, rec[i]);
            strcat(path, "/inner");
            check(hsp_path_protected(path), "祖先が保護対象");

            /* 正規化を挟んでも守る (`./` / `..` / 連続 '/') */
            strcpy(path, "/etc/./sub/../");
            strcat(path, rec[i]);
            check(hsp_path_protected(path), "正規化してから名前規則");
        }
    }

    printf("== S3-D 似ているだけの名前は通す ==\n");
    /* 接頭一致で拾うと通常配備の対象まで止まる。7 名は**完全一致**だけ。 */
    check(!hsp_path_protected("/etc/settings.db.bak2"), "bak2");
    check(!hsp_path_protected("/etc/settings.db.new2"), "new2");
    check(!hsp_path_protected("/etc/settings.db.recover"), "recover");
    check(!hsp_path_protected("/etc/settings.db.recover-st"), "recover-st");
    check(!hsp_path_protected("/etc/settings.db.recover-state.old"),
          "recover-state.old");
    check(!hsp_path_protected("/etc/settings.db.failed.old"), "failed.old");
    check(!hsp_path_protected("/etc/settings.dbbak"), "settings.dbbak");
    check(!hsp_path_protected("/etc/settings.db.bak-journal2"),
          "bak-journal2");
    check(!hsp_path_protected("/etc/sub/settings.db.bak"),
          "親が /etc でなければ対象外");
    check(!hsp_path_protected("/settings.db.recover-state"),
          "ルート直下は対象外");
    check(!hsp_is_protected_basename("settings.db.bak2"), "列挙でも bak2");
    check(!hsp_is_protected_basename("ettings.db.new"), "部分一致では拾わない");

    printf("== S3-D -f の subdir 連結でもリカバリ生成物を守る ==\n");
    /* `hsync -f etc` は dst = "/" + "etc" + "/" + name (hsync.c:254 相当) */
    strcpy(joined, "/");
    strcat(joined, "etc");
    strcat(joined, "/");
    strcat(joined, "settings.db.recover-state");
    check(hsp_path_protected(joined), "hsync -f etc の下の recover-state");
    strcpy(joined, "/");
    strcat(joined, "etc/");
    strcat(joined, "/settings.db.bak-journal");
    check(hsp_path_protected(joined), "hsync -f etc/ の bak-journal");

    printf("== S3-D 表の件数と hsync.c の収集上限の結合 ==\n");
    {
        /* hsync.c は /etc の**実在名**を HS_MAX_PROT 件まで集め、越えたら
         * 「守れない」として同期を拒否する。表が伸びると余裕が減るので、
         * 名前を足すときは HS_MAX_PROT (現在 24) を必ず見直すこと。
         * リカバリ途中の /etc は票 TASK_S3 §1b の 9 名 + wal/shm = 11 名。 */
        int n = 0;
        while (hsp_protected_names[n]) n++;
        check(n == 11, "保護対象は 11 名 (本体 2 + wal/shm 2 + リカバリ 7)");
    }

    printf("== B1 '\\' を含むパスは正規化の入口で断る ==\n");
    check(hsp_has_backslash("..\\os32-other"), "'..\\os32-other' を見つける");
    check(hsp_has_backslash("bin\\x"), "要素の途中の '\\'");
    check(hsp_has_backslash("\\"), "'\\' 単独");
    check(!hsp_has_backslash("bin/x"), "'/' だけなら通す");
    check(!hsp_has_backslash(""), "空文字");
    check(hsp_normalize("..\\os32-other", joined, (int)sizeof(joined)) == 0,
          "'..\\os32-other' は正規化できない ('..' 検査を素通りさせない)");
    check(hsp_normalize("/bin/a\\b", joined, (int)sizeof(joined)) == 0,
          "要素名の '\\' も断る");
    check(hsp_path_classify("/bin/a\\b") == -1,
          "'\\' 混じりは「判定できない」(-1) であって保護 (1) ではない");
    check(hsp_path_protected("/bin/a\\b"),
          "真偽で見れば保護側に倒れる (fail-closed)");

    printf("== B2 要素数と 3 値分類 ==\n");
    check(hsp_depth("/") == 0, "'/' は 0 要素");
    check(hsp_depth("") == 0, "空文字は 0 要素");
    check(hsp_depth("/bin") == 1, "'/bin' は 1 要素");
    check(hsp_depth("/host/bin") == 2, "'/host/bin' は 2 要素");
    check(hsp_depth("/host/usr/sys") == 3, "'/host/usr/sys' は 3 要素");
    check(hsp_path_classify("/etc/settings.db") == 1, "保護は 1");
    check(hsp_path_classify("/bin/sh.bin") == 0, "対象外は 0");
    {
        /* 正規化できない長さ。保護 (1) と混ぜず -1 で返すこと */
        char longp[HSP_MAX_PATH * 2];
        int i;
        for (i = 0; i < (int)sizeof(longp) - 1; i++)
            longp[i] = (i % 8 == 0) ? '/' : 'a';
        longp[sizeof(longp) - 1] = '\0';
        check(hsp_path_classify(longp) == -1,
              "正規化できない長さは -1 (PROTECTED と数えない)");
    }
    {
        /* HSP_MAX_DEPTH ちょうど (32 要素) は正規化を通る。
         * `/host` を足した 33 要素を止めるのは呼び手 (hsync.c の
         * HS_MAX_PATH_DEPTH) の仕事で、ここではないことを固定する。 */
        char deep[HSP_MAX_PATH];
        char out[HSP_MAX_PATH];
        int i;
        deep[0] = '\0';
        for (i = 0; i < HSP_MAX_DEPTH; i++) strcat(deep, "/a");
        check(hsp_normalize(deep, out, (int)sizeof(out)) == 1,
              "32 要素は正規化を通る");
        check(hsp_depth(out) == HSP_MAX_DEPTH, "32 要素と数える");
        strcpy(joined, "/host");
        strcat(joined, out);
        check(hsp_depth(joined) == HSP_MAX_DEPTH + 1,
              "'/host' を足すと 33 要素 (呼び手が止める)");
    }

    printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
