/* ======================================================================== */
/*  BOOT_FONT.C — 起動時の既定フォント (KCG) の読み込み                      */
/*                                                                          */
/*  正規名 SYS_FONT_DEFAULT (/sys/font/default.kcgfont) を読む。読めず、     */
/*  **かつそのパスを載せているマウントが LFN の無い FS (FAT) のときだけ**   */
/*  8.3 の短い名前 SYS_FONT_DEFAULT_83 を読む — FD は FatFs (LFN なし) なので */
/*  正規名を置けない (2026-09-25 からフォントは NORMAL で、起動 FD には載せて */
/*  いない。手で置けば読む)。どちらも無ければ呼び出し側 (kernel.c) が NG を  */
/*  出して先へ進み、字は本体のフォント ROM から読む。HDD (ext2) で正規名     */
/*  が欠けたり壊れたりしたとき、残っている短い名前を黙って掴まないように    */
/*  FS を見る (Codex 実装レビュー往復 1 / 2)。                               */
/*  マウントの判定: パスの親を 1 段ずつ遡り、vfs_fstype(prefix) が空でない  */
/*  最初のマウント点 (= 最長一致のマウント) の FS 名を見る。FD ルートの上に  */
/*  /sys へ ext2 が載っていればそちらを見る。                                */
/*  SYS_FONT_83_FSTYPE は fs/fatfs_vfs.c の VfsOps の名前と同じであること   */
/*  (tools/tests/test_packages.py case 8 が突き合わせる)。                   */
/* ======================================================================== */

#include "boot_font.h"
#include "config.h"
#include "kcg.h"
#include "vfs.h"
#include "kstring.h"

/* path を載せているマウントの FS 名。無ければ "" */
static const char *boot_font_fstype_of(const char *path)
{
    static char dir[VFS_MAX_PATH];
    const char *t;
    int n;

    kstrncpy(dir, path, VFS_MAX_PATH);
    for (;;) {
        n = (int)kstrlen(dir);
        if (n <= 1) return vfs_fstype("/");
        while (n > 0 && dir[n - 1] != '/') n--;     /* 最後の要素を落とす */
        if (n <= 1) {
            dir[0] = '/';
            dir[1] = '\0';
        } else {
            dir[n - 1] = '\0';                     /* 末尾の '/' も落とす */
        }
        t = vfs_fstype(dir);
        if (t[0]) return t;
    }
}

int boot_font_load(void)
{
    int fret;

    fret = kcg_load_font(SYS_FONT_DEFAULT);
    if (fret != 0 &&
        kstrcmp(boot_font_fstype_of(SYS_FONT_DEFAULT), SYS_FONT_83_FSTYPE) == 0) {
        fret = kcg_load_font(SYS_FONT_DEFAULT_83);
    }
    return fret;
}
