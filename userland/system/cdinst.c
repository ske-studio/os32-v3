#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* ======================================================================== */
/*  CDINST.C — OS32 CDパッケージインストーラー v2.0                          */
/*                                                                          */
/*  CD-ROM (ISO 9660) 上の .PKG ファイルからHDDにインストールする。           */
/*  - インストールタイプ選択 (Minimal / Normal / Full)                       */
/*    1 = BOOT + MINIMAL (CUI のみ、起動 FD と同じ集合)、2 = + GUI + NORMAL、 */
/*    3 = + DEBUG。展開は依存の順 MINIMAL → GUI → NORMAL → DEBUG。中身は配備マニフェストの */
/*    タグから tools/mkpkg.py --plan が作る (構成は build/packages.yaml)     */
/*  - 128 項目を超えるパッケージは NAME.PKG, NAME2.PKG, … に分かれている。   */
/*    連番を欠けるまで順に展開する                                          */
/*  - BOOT.PKG はブートセクタ直接書込み (IPL / ローダ)                       */
/*  - 他のPKGは /hd0 マウントポイントにファイルシステム展開                  */
/*  v3.0 (票 TASK_HDD_INSTALL 段 2): hd0 の扱いを FD の install と共通の     */
/*    inst_hdd.c / inst_disk.c に移した。区画表は PC-98 標準配置、IPL の      */
/*    [8]/[9] と区画の CHS は BIOS 幾何、区画は LBA 1632 以上の最初の BIOS    */
/*    シリンダ境界から 256MiB まで。空のディスクか OS32 の項目 1 つ (再作成) */
/*    だけを扱う。**全検査** (パッケージの必須の中身・ローダ ≤ 8192 B・      */
/*    vmkernel.lz4 ≤ 508KiB・展開先の容量・hd0 の幾何とモードとマウント) → */
/*    確認画面と y/N → (表が使えなければ ERASE の打鍵 → LBA 0/1 の消去) →   */
/*    ext2_format_at → 区画表 → 読み戻し → マウント → ローダ / IPL → 展開 →  */
/*    sync の順。どこかで失敗すれば「完了」とは言わない。                    */
/* ======================================================================== */

#define OS32_DBG_SERIAL
#include "os32api.h"
#include "rt/dbgserial.h"
#include "rt/pkg.h"
#include "inst_hdd.h"

#define CD_MOUNT "/cd0"
#define HDD_MOUNT "/hd0"

/* パッケージファイルパス。BOOT は分割しない (セクタへ直接書く) */
#define PKG_BOOT    "/cd0/BOOT.PKG"
/* 分割されうるパッケージのベース名 (build/packages.yaml と一致させる。
 * make check-packages-host が突き合わせる)。連番は 2..PKG_SERIES_MAX */
#define PKG_BASE_MINIMAL "MINIMAL"
#define PKG_BASE_GUI     "GUI"
#define PKG_BASE_NORMAL  "NORMAL"
#define PKG_BASE_DEBUG   "DEBUG"
#define PKG_SERIES_MAX   9
/* "/cd0/" + 8 文字 + ".PKG" + NUL */
#define PKG_PATH_BUF     32

/* MINIMAL に無ければ HDD から起動できない物 (段 2-11 の必須の中身) */
#define PKG_NEED_KERNEL  "/boot/vmkernel.lz4"
#define PKG_NEED_SHELL   "/sys/shell.bin"
/* BOOT.PKG の中の名前 (build/packages.yaml の boot:) */
#define BOOT_IPL_NAME    "boot_hdd.bin"
#define BOOT_LOADER_NAME "loader_hdd.bin"

/* 色定数 */
#define COL_TITLE  (0xE1 | 0x40)
#define COL_NORMAL 0xE1
#define COL_GREEN  0x81
#define COL_CYAN   0xA1
#define COL_RED    (0x41)
#define COL_YELLOW 0xC1

static KernelAPI *api;

/* ---- ユーティリティ ---- */

static void print(u8 attr, const char *s)
{
    api->kprintf(attr, "%s", s);
}

static void println(u8 attr, const char *s)
{
    api->kprintf(attr, "%s\n", s);
}

/* 鍵は inst_hdd_getkey (0 以上はすべて入力、CR の直後の LF は捨てる) */
static int getkey(void)
{
    return inst_hdd_getkey(api);
}

static int check_cd(void)
{
    OS32_Stat st;
    return (api->sys_stat(PKG_BOOT, &st) == 0 && st.st_size > 0) ? 1 : 0;
}

static int pkg_exists(const char *path)
{
    OS32_Stat st;
    return (api->sys_stat(path, &st) == 0) ? 1 : 0;
}

/* 分割の n 本目 (1 始まり) のパス: n = 1 は "/cd0/NORMAL.PKG"、
 * n >= 2 は "/cd0/NORMAL<n>.PKG" */
static void pkg_series_path(char *buf, const char *base, int n)
{
    const char *pre = CD_MOUNT "/";
    const char *ext = ".PKG";
    int i = 0;

    while (*pre && i < PKG_PATH_BUF - 1) buf[i++] = *pre++;
    while (*base && i < PKG_PATH_BUF - 1) buf[i++] = *base++;
    if (n >= 2 && i < PKG_PATH_BUF - 1) buf[i++] = (char)('0' + n);
    while (*ext && i < PKG_PATH_BUF - 1) buf[i++] = *ext++;
    buf[i] = '\0';
}

/* 媒体にある分割の本数 (0 = 1 本も無い) */
static int pkg_series_count(const char *base)
{
    char path[PKG_PATH_BUF];
    int n;

    for (n = 1; n <= PKG_SERIES_MAX; n++) {
        pkg_series_path(path, base, n);
        if (!pkg_exists(path)) break;
    }
    return n - 1;
}

/* 型 choice ('1'〜'3') に要るパッケージのうち媒体に 1 本も無いものの名前を
 * 出し、その数を返す (0 = 揃っている)。要る順は展開の順と同じ */
static int report_missing_packages(int choice)
{
    static const char *const need[4] = {
        PKG_BASE_MINIMAL, PKG_BASE_GUI, PKG_BASE_NORMAL, PKG_BASE_DEBUG
    };
    /* need[i] が要る最小の型 */
    static const char need_from[4] = { '1', '2', '2', '3' };
    int i, missing = 0;

    for (i = 0; i < 4; i++) {
        if (choice < need_from[i]) continue;
        if (pkg_series_count(need[i]) == 0) {
            api->kprintf(COL_RED, "  missing on CD: %s.PKG\n", need[i]);
            missing++;
        }
    }
    return missing;
}

/* 大文字小文字を無視してサフィックス一致判定 */
static int str_endswith(const char *s, const char *suffix)
{
    int slen = 0, suflen = 0;
    const char *p;
    char c1, c2;

    for (p = s; *p; p++) slen++;
    for (p = suffix; *p; p++) suflen++;
    if (suflen > slen) return 0;

    p = s + slen - suflen;
    while (*suffix) {
        c1 = *p; c2 = *suffix;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
        p++;
        suffix++;
    }
    return 1;
}

/* ======================================================================== */
/*  BOOT.PKG の読み込み (承認前。書くのはマウントの確認の後)                 */
/*  boot_hdd.bin → LBA 0 (IPL)、loader_hdd.bin → LBA 2〜                    */
/*  注: kernel.bin の生書き込みは廃止した (loader v3 は ext2 の             */
/*  /boot/vmkernel.lz4 を読む)。                                            */
/* ======================================================================== */

typedef struct {
    u8        *data;            /* BOOT.PKG のデータ部 (mem_alloc) */
    const u8  *ipl;
    u32        ipl_len;
    const u8  *loader;
    u32        loader_len;
} BootImg;

static void boot_img_free(BootImg *b)
{
    if (b->data) api->mem_free(b->data);
    b->data = 0;
}

static int pkg_data_ok(const char *path, const PkgInfo *info);

/* 戻り値 0 = IPL とローダが揃った。失敗は表示して負 (何も書いていない) */
static int load_boot_pkg(BootImg *b)
{
    static PkgInfo info;
    u32 comp_size, offset;
    int ret, i, rd, fd;

    b->data = 0;
    b->ipl = 0;
    b->loader = 0;
    b->ipl_len = 0;
    b->loader_len = 0;

    ret = pkg_parse(api, PKG_BOOT, &info);
    if (ret != PKG_OK) {
        api->kprintf(COL_RED, "  BOOT.PKG parse failed (rc=%d)\n", ret);
        return ret;
    }
    DBGF("[cdinst] BOOT.PKG: %d files, flags=0x%02x",
         info.entry_count, info.header.flags);

    /* BOOT.PKGは非圧縮でなければならない */
    if (info.header.flags & PKG_FLAG_LZSS) {
        println(COL_RED, "  BOOT.PKG must not be LZSS compressed");
        return PKG_ERR_CORRUPT;
    }

    comp_size = info.header.comp_size;
    if (comp_size == 0) {
        println(COL_RED, "  BOOT.PKG has no data");
        return PKG_ERR_CORRUPT;
    }
    if (!pkg_data_ok(PKG_BOOT, &info)) return PKG_ERR_CORRUPT;
    b->data = (u8 *)api->mem_alloc(comp_size);
    if (!b->data) {
        println(COL_RED, "  out of memory (BOOT.PKG)");
        return PKG_ERR_NOMEM;
    }
    fd = api->sys_open(PKG_BOOT, KAPI_O_RDONLY);
    if (fd < 0) { boot_img_free(b); return PKG_ERR_IO; }
    api->sys_lseek(fd, (int)info.data_offset, SEEK_SET);
    rd = api->sys_read(fd, b->data, (int)comp_size);
    api->sys_close(fd);
    if (rd != (int)comp_size) {
        println(COL_RED, "  BOOT.PKG is truncated");
        boot_img_free(b);
        return PKG_ERR_IO;
    }

    offset = 0;
    for (i = 0; i < info.entry_count; i++) {
        const PkgEntry *ent = &info.entries[i];
        if (ent->type != PKG_TYPE_FILE) continue;
        if (ent->size > comp_size - offset) {
            println(COL_RED, "  BOOT.PKG table does not match its data");
            boot_img_free(b);
            return PKG_ERR_CORRUPT;
        }
        if (str_endswith(ent->path, BOOT_IPL_NAME)) {
            b->ipl = b->data + offset;
            b->ipl_len = ent->size;
        } else if (str_endswith(ent->path, BOOT_LOADER_NAME)) {
            b->loader = b->data + offset;
            b->loader_len = ent->size;
        }
        offset += ent->size;
    }
    if (!b->ipl || !b->loader) {
        api->kprintf(COL_RED, "  BOOT.PKG lacks %s\n",
                     !b->ipl ? BOOT_IPL_NAME : BOOT_LOADER_NAME);
        boot_img_free(b);
        return PKG_ERR_CORRUPT;
    }
    return 0;
}

/* ======================================================================== */
/*  展開の事前検査 (承認前): 選んだ型のパッケージを全部 pkg_parse し、       */
/*  前置 (/hd0) で溢れる項目・必須の中身・要る容量を見る。                   */
/*  戻り値 0 = 通る。kernel_len に MINIMAL の /boot/vmkernel.lz4 の長さ。    */
/* ======================================================================== */

static int path_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* PKG のデータ部が表と一致するか (Codex 往復 1 の P1-1)。事前検査はヘッダと
 * 表しか読まないので、データ部が切れた媒体や orig_size = 0 のヘッダは展開の
 * 途中 (か黙って空のファイル) で初めて分かっていた。書く前に:
 *   ファイルの項目の大きさの和 = orig_size、無圧縮なら comp_size = orig_size、
 *   PKG の長さ = データ部の先頭 + comp_size */
static int pkg_data_ok(const char *path, const PkgInfo *info)
{
    OS32_Stat st;
    u32 sum = 0;
    int i;

    for (i = 0; i < info->entry_count; i++) {
        if (info->entries[i].type != PKG_TYPE_FILE) continue;
        if (info->entries[i].size > 0xFFFFFFFFUL - sum) return 0;   /* 桁あふれ */
        sum += info->entries[i].size;
    }
    if (sum != info->header.orig_size) {
        api->kprintf(COL_RED, "  %s: files add up to %u bytes, header says %u\n",
                     path, sum, info->header.orig_size);
        return 0;
    }
    if (!(info->header.flags & PKG_FLAG_LZSS) && info->header.comp_size != sum) {
        api->kprintf(COL_RED, "  %s: stored size %u != %u\n", path,
                     info->header.comp_size, sum);
        return 0;
    }
    if (api->sys_stat(path, &st) != 0 ||
        info->header.comp_size > 0xFFFFFFFFUL - info->data_offset ||
        st.st_size != info->data_offset + info->header.comp_size) {
        api->kprintf(COL_RED, "  %s: file is %u bytes, table says %u (truncated media?)\n",
                     path, st.st_size, info->data_offset + info->header.comp_size);
        return 0;
    }
    return 1;
}

/* 項目のパスが /hd0 の外へ出ないか (Codex 往復 1 の P1-2、P2-5)。
 * 0 = 通る。断るときは表示して負 */
static int pkg_paths_ok(const char *path, const PkgInfo *info)
{
    int i, rc;

    for (i = 0; i < info->entry_count; i++) {
        rc = inst_check_path(info->entries[i].path);
        if (rc != 0) {
            api->kprintf(COL_RED, "  %s: %s: %s\n", path, inst_reason(rc),
                         info->entries[i].path);
            return rc;
        }
    }
    return 0;
}

/* 必須のファイルの**最終の**大きさ (Codex 往復 2 P1-2)。展開は MINIMAL → GUI →
 * NORMAL → DEBUG、各 PKG の中は項目の順に O_TRUNC で書くので、後から来た同じ
 * パスの項目 (大きさ 0 を含む) が前のものを置き換える。事前検査も同じ順に
 * たどって、最後に書かれる大きさで判定する。 */
static u32 g_final_kernel, g_final_shell;

static int measure_packages(int choice, InstNeed *need, u32 *kernel_len)
{
    static const char *const bases[4] = {
        PKG_BASE_MINIMAL, PKG_BASE_GUI, PKG_BASE_NORMAL, PKG_BASE_DEBUG
    };
    static const char need_from[4] = { '1', '2', '2', '3' };
    static PkgInfo info;
    char path[PKG_PATH_BUF];
    int b, n, count, i, ret;

    *kernel_len = 0;
    g_final_kernel = 0;
    g_final_shell = 0;
    for (b = 0; b < 4; b++) {
        if (choice < need_from[b]) continue;
        count = pkg_series_count(bases[b]);
        for (n = 1; n <= count; n++) {
            pkg_series_path(path, bases[b], n);
            ret = pkg_parse(api, path, &info);
            if (ret != PKG_OK) {
                api->kprintf(COL_RED, "  %s: parse failed (rc=%d)\n", path, ret);
                return ret;
            }
            i = pkg_first_overflow(&info, 4);   /* strlen("/hd0") */
            if (i >= 0) {
                api->kprintf(COL_RED, "  %s: PATH TOO LONG: %s\n", path,
                             info.entries[i].path);
                return PKG_ERR_TOOLONG;
            }
            if (pkg_paths_ok(path, &info) != 0) return PKG_ERR_CORRUPT;
            if (!pkg_data_ok(path, &info)) return PKG_ERR_CORRUPT;
            for (i = 0; i < info.entry_count; i++) {
                const PkgEntry *ent = &info.entries[i];
                if (ent->type == PKG_TYPE_DIR) {
                    inst_need_dir(need);
                    continue;
                }
                /* pkg_extract はファイルとディレクトリしか書かない。それ以外の型は
                 * 黙って飛ばされる (必須が検査を通って展開されない) ので断る */
                if (ent->type != PKG_TYPE_FILE) {
                    api->kprintf(COL_RED, "  %s: unknown entry type %u: %s\n",
                                 path, (u32)ent->type, ent->path);
                    return PKG_ERR_CORRUPT;
                }
                inst_need_file(need, ent->size);
                if (path_eq(ent->path, PKG_NEED_KERNEL)) g_final_kernel = ent->size;
                if (path_eq(ent->path, PKG_NEED_SHELL)) g_final_shell = ent->size;
            }
        }
    }
    *kernel_len = g_final_kernel;
    if (g_final_kernel == 0 || g_final_shell == 0) {
        api->kprintf(COL_RED, "  the packages leave %s missing or empty\n",
                     g_final_kernel == 0 ? PKG_NEED_KERNEL : PKG_NEED_SHELL);
        return PKG_ERR_CORRUPT;
    }
    return PKG_OK;
}

/* ======================================================================== */
/*  パッケージ展開 (パスを /hd0 配下にリマップ)                              */
/* ======================================================================== */

static int install_package_hd(const char *path, const char *label)
{
    PkgInfo info;
    int ret, i;

    api->kprintf(COL_CYAN, "\n  Installing %s ...", label);

    ret = pkg_parse(api, path, &info);
    if (ret != PKG_OK) {
        println(COL_RED, " PARSE ERROR");
        return ret;
    }

    api->kprintf(COL_NORMAL, " (%d files, v%d)", info.entry_count,
                 info.header.version);

    /* 前置 (/hd0) で溢れる項目があれば**展開を始める前に**断る (票
     * TASK_VFS_FD_PATH v3 の追記)。以前は溢れた分を切り詰め、後のファイルが
     * 前のファイルを上書きし得た。外部で作られた PKG への消費側の保護。 */
    i = pkg_first_overflow(&info, 4);   /* strlen("/hd0") */
    if (i >= 0) {
        api->kprintf(COL_RED, " PATH TOO LONG: %s\n", info.entries[i].path);
        return PKG_ERR_TOOLONG;
    }

    /* /hd0 の外へ出るパスと未知の型は展開の前に断る (事前検査と同じ規則をもう一度) */
    if (pkg_paths_ok(path, &info) != 0) return PKG_ERR_CORRUPT;
    for (i = 0; i < info.entry_count; i++) {
        if (info.entries[i].type != PKG_TYPE_FILE && info.entries[i].type != PKG_TYPE_DIR) {
            api->kprintf(COL_RED, " unknown entry type: %s\n", info.entries[i].path);
            return PKG_ERR_CORRUPT;
        }
    }

    /* パスに /hd0 プレフィックスを追加 */
    for (i = 0; i < info.entry_count; i++) {
        char orig[PKG_MAX_PATH];
        int j;

        for (j = 0; j < PKG_MAX_PATH - 1 && info.entries[i].path[j]; j++)
            orig[j] = info.entries[i].path[j];
        orig[j] = '\0';

        info.entries[i].path[0] = '/';
        info.entries[i].path[1] = 'h';
        info.entries[i].path[2] = 'd';
        info.entries[i].path[3] = '0';
        for (j = 0; orig[j] && j + 4 < PKG_MAX_PATH - 1; j++)
            info.entries[i].path[4 + j] = orig[j];
        info.entries[i].path[4 + j] = '\0';
    }

    ret = pkg_extract(api, path, &info);
    if (ret != PKG_OK) {
        api->kprintf(COL_RED, " EXTRACT ERROR (rc=%d)", ret);
        print(COL_NORMAL, "\n");
        return ret;
    }

    println(COL_GREEN, " OK");
    return PKG_OK;
}

/* ======================================================================== */
/*  パッケージの展開 (MINIMAL → GUI → NORMAL → DEBUG) と同期                */
/*                                                                          */
/*  **どのパッケージの失敗でも止める** (Codex / Opus 実装レビュー ラリー 1)。 */
/*  以前は MINIMAL だけ戻り値を見ていたので、NORMAL 以降が PATH TOO LONG や  */
/*  容量不足で失敗しても後続へ進み、最後に "Installation Complete" を出して   */
/*  いた。失敗したら失敗したパッケージ名を出し、完了表示は出さない。         */
/*  同期 (vfs_sync) の失敗も完了扱いにしない。                               */
/*  選んだ型のパッケージが媒体に 1 本も無いのも失敗 (黙って飛ばすと「Full を */
/*  選んだのに試験が入っていない」HDD が完了扱いになる)。                    */
/*  戻り値: PKG_OK = 完了 / それ以外 = 失敗した段の値                        */
/* ======================================================================== */

static int install_step(const char *path, const char *label)
{
    int ret = install_package_hd(path, label);
    if (ret != PKG_OK) {
        api->kprintf(COL_RED, "\n%s installation failed (rc=%d)!\n", label, ret);
        println(COL_RED, "Installation aborted. The HDD is incomplete.");
    }
    return ret;
}

/* base の分割をすべて (NAME.PKG, NAME2.PKG, …) 順に展開する */
static int install_series(const char *base)
{
    char path[PKG_PATH_BUF];
    char label[PKG_PATH_BUF];
    int n, count, ret;

    count = pkg_series_count(base);
    if (count == 0) {
        api->kprintf(COL_RED, "\n%s.PKG not found on CD!\n", base);
        println(COL_RED, "Installation aborted. The HDD is incomplete.");
        return PKG_ERR_IO;
    }
    for (n = 1; n <= count; n++) {
        pkg_series_path(path, base, n);
        /* 表示名 = パスから "/cd0/" と ".PKG" を除いたもの */
        {
            int i = 0;
            const char *p = path + sizeof(CD_MOUNT);
            while (*p && *p != '.' && i < PKG_PATH_BUF - 1) label[i++] = *p++;
            label[i] = '\0';
        }
        ret = install_step(path, label);
        if (ret != PKG_OK) return ret;
    }
    return PKG_OK;
}

static int required_on_hd0(void)
{
    static const char *const path[2] = { "/hd0" PKG_NEED_KERNEL, "/hd0" PKG_NEED_SHELL };
    u32 want[2];
    OS32_Stat st;
    int i;

    want[0] = g_final_kernel;
    want[1] = g_final_shell;
    for (i = 0; i < 2; i++) {
        if (api->sys_stat(path[i], &st) != 0 || (st.st_mode & OS_S_IFMT) != OS_S_IFREG ||
            st.st_size == 0 || st.st_size != want[i]) {
            api->kprintf(COL_RED, "\n  %s is missing or has the wrong size (want %u)\n",
                         path[i], want[i]);
            return 0;
        }
    }
    return 1;
}

static int install_packages(int choice)
{
    int ret;

    /* 2. MINIMAL → /hd0 配下に展開 (全タイプ) */
    ret = install_series(PKG_BASE_MINIMAL);
    if (ret != PKG_OK) return ret;

    /* 3. GUI → NORMAL (タイプ2以上)。GUI (gshell / libos32gui.shlib) を先に
     * 置く — NORMAL は GUI に依存しないが、GUI の後に展開すれば途中で止まっても
     * GUI 一式は揃っている。Minimal だけなら GUI は無く、system.cfg が GUI=1
     * でもカーネルが "gshell load failed -> CUI shell" で CUI に落ちる */
    if (choice >= '2') {
        ret = install_series(PKG_BASE_GUI);
        if (ret != PKG_OK) return ret;
        ret = install_series(PKG_BASE_NORMAL);
        if (ret != PKG_OK) return ret;
    }

    /* 4. DEBUG (タイプ3 = Full) */
    if (choice >= '3') {
        ret = install_series(PKG_BASE_DEBUG);
        if (ret != PKG_OK) return ret;
    }

    /* 必須のファイルが /hd0 に事前検査のとおりの大きさで在る (最終の状態、
     * Codex 往復 2 P1-2 の二重の守り) */
    /* g_final_kernel = 0 は事前検査 (measure_packages) を通っていない呼び出し
     * (tools/tests/vfs_fd_path_host.c が install_packages だけを回す) */
    if (g_final_kernel != 0 && !required_on_hd0()) {
        println(COL_RED, "Installation aborted. The HDD is incomplete.");
        return PKG_ERR_CORRUPT;
    }

    /* ファイルシステムをディスクに同期 */
    print(COL_CYAN, "\n  Syncing filesystem...");
    {
        int sret = api->vfs_sync();
        DBGF("[cdinst] vfs_sync=%d", sret);
        if (sret != 0) {
            api->kprintf(COL_RED, " FAILED (rc=%d)\n", sret);
            println(COL_RED, "Installation aborted. The HDD is incomplete.");
            return sret;
        }
        println(COL_GREEN, " OK");
    }

    /* 完了 */
    print(COL_NORMAL, "\n");
    println(COL_GREEN, "=== Installation Complete ===");
    println(COL_NORMAL, "Remove the CD and the boot floppy, then reboot from the HDD.");
    return PKG_OK;
}

/* ======================================================================== */
/*  メイン                                                                   */
/* ======================================================================== */

/* 展開の前に作るディレクトリ (親が先)。新しく作った ext2 なのでどれも作れる
 * はず — 1 つでも作れなければ未完成として止める */
static const char *const init_dirs[] = {
    "/hd0/sys", "/hd0/boot", "/hd0/bin", "/hd0/sbin", "/hd0/usr",
    "/hd0/usr/bin", "/hd0/usr/man", "/hd0/etc", "/hd0/data", "/hd0/home",
    "/hd0/home/user", "/hd0/tmp"
};
#define INIT_DIRS ((int)(sizeof(init_dirs) / sizeof(init_dirs[0])))

/* 書く前の全検査。0 = 通る (b にブート像、t に hd0 の計画) */
static int preflight(int choice, BootImg *b, InstTarget *t)
{
    static InstNeed need;
    u32 kernel_len = 0;
    int i;

    print(COL_NORMAL, "\n");
    println(COL_NORMAL, "Checking the packages and hd0 (nothing is written yet)...");
    if (load_boot_pkg(b) != 0) {
        println(COL_RED, "ERROR: BOOT.PKG is not usable. Nothing was written.");
        return -1;
    }
    inst_need_init(&need);
    for (i = 0; i < INIT_DIRS; i++) inst_need_dir(&need);
    if (measure_packages(choice, &need, &kernel_len) != PKG_OK) {
        println(COL_RED, "ERROR: the packages are not usable. Nothing was written.");
        boot_img_free(b);
        return -1;
    }
    if (inst_hdd_check(api, t) != 0 ||
        inst_hdd_check_media(api, t, b->ipl_len, b->loader_len, kernel_len, &need) != 0) {
        boot_img_free(b);
        return -1;
    }
    return 0;
}

void __cdecl main(int argc, char **argv, KernelAPI *_api)
{
    static BootImg boot;
    static InstTarget tgt;
    int choice, i, ret;

    (void)argc;
    (void)argv;
    api = _api;
    dbg_init(api);
    DBG("[cdinst] started");

    println(COL_TITLE, "========================================");
    println(COL_TITLE, "      OS32 CD Installer v3.0");
    println(COL_TITLE, "========================================");
    print(COL_NORMAL, "\n");

    /* CD-ROMマウント */
    print(COL_NORMAL, "Mounting CD-ROM... ");
    {
        int mr = api->sys_mount("/cd0", "cd0", "iso9660");
        if (mr == 0)
            println(COL_GREEN, "OK");
        else
            println(COL_YELLOW, "already mounted or skipped");
    }

    /* CD-ROM確認 */
    print(COL_NORMAL, "Checking CD-ROM... ");
    if (!check_cd()) {
        println(COL_RED, "NOT FOUND");
        println(COL_RED, "  BOOT.PKG not found on /cd0/");
        return;
    }
    println(COL_GREEN, "OK");

    /* パッケージ一覧 */
    print(COL_NORMAL, "\n");
    println(COL_NORMAL, "Available packages on CD:");
    if (pkg_exists(PKG_BOOT))    println(COL_CYAN, "  [*] BOOT.PKG");
    {
        static const char *const bases[4] = {
            PKG_BASE_MINIMAL, PKG_BASE_GUI, PKG_BASE_NORMAL, PKG_BASE_DEBUG
        };
        int b, n, count;
        for (b = 0; b < 4; b++) {
            count = pkg_series_count(bases[b]);
            for (n = 1; n <= count; n++) {
                char path[PKG_PATH_BUF];
                pkg_series_path(path, bases[b], n);
                api->kprintf(COL_CYAN, "  [*] %s\n", path + sizeof(CD_MOUNT));
            }
        }
    }

    /* インストールタイプ選択 (中身は build/packages.yaml の振り分け) */
    print(COL_NORMAL, "\n");
    println(COL_NORMAL, "Install type:");
    /* 1 は「起動して、HDD に入れて、壊れたときに直して、残りを取ってこられる」
     * レスキュー兼インストーラ (2026-09-25)。既定フォントと一般コマンドは 2 */
    println(COL_NORMAL, "  1. Minimal  (CUI rescue + installer, same as the boot FD)");
    println(COL_NORMAL, "  2. Normal   (+ GUI shell, commands, apps, manpages, font, IME, data)");
    println(COL_NORMAL, "  3. Full     (+ test programs and test data)");
    println(COL_NORMAL, "  0. Cancel");
    print(COL_NORMAL, "\n");

    print(COL_YELLOW, "Select [0-3]: ");
    do {
        choice = getkey();
    } while (choice < '0' || choice > '3');
    api->kprintf(COL_NORMAL, "%c\n", choice);

    if (choice == '0') {
        println(COL_NORMAL, "Installation cancelled.");
        return;
    }

    /* 選んだ型に要るパッケージが媒体に揃っているか、HDD を消す前に見る。
     * 欠けたものは名前を全部出す (古い CD / 焼き損じの切り分け用) */
    if (report_missing_packages(choice) != 0) {
        println(COL_RED, "ERROR: the CD lacks a package for this install type.");
        return;
    }

    /* 全検査 (段 2-11 / N4 / N6 / N8)。1 つでも欠ければ 1 セクタも書かない */
    if (preflight(choice, &boot, &tgt) != 0) return;

    print(COL_NORMAL, "\n");
    inst_hdd_describe(api, &tgt);
    println(COL_RED, "WARNING: This will format the OS32 area of hd0 and install OS32.");
    print(COL_YELLOW, "Continue? [y/N]: ");
    {
        /* [0-3] は 1 字で決まるので、選択の後の Enter (端末が「1」と一緒に送る
         * CR / LF / CRLF、後から押した Enter) が残る。その最初の 1 つは選択の
         * 行末として捨てる (取り消しにしない)。次の Enter だけなら取り消し */
        int k = inst_hdd_getkey_after_key(api);
        /* 表示できる字だけ映す (NUL や制御文字は答え = N として扱うが映さない) */
        api->kprintf(COL_NORMAL, "%c\n", (k >= 0x20 && k <= 0x7E) ? k : ' ');
        if (k != 'y' && k != 'Y') {
            println(COL_NORMAL, "Installation cancelled. Nothing was written.");
            boot_img_free(&boot);
            return;
        }
    }
    /* 表が使えないディスクは y の後に ERASE の打鍵 (受けなければ何も書かない) */
    if (inst_hdd_ask_erase(api, &tgt) != 0) {
        boot_img_free(&boot);
        return;
    }

    /* === インストール実行 (R3-1) === */
    print(COL_NORMAL, "\n");
    println(COL_GREEN, "=== Installing OS32 ===");

    /* hd0 のマウントを外す (起動時の自動マウントの ctx は旧 FS の
     * スーパーブロック / GDT を持ったまま)。外れなければ消去も format もしない
     * (umount 自身の sync は書き出し得る)。ERASE を受けていればここで LBA 0/1 を
     * 消す (インストーラ自身の最初の書き込み) */
    if (inst_hdd_release(api, &tgt) != 0) {
        boot_img_free(&boot);
        return;
    }
    /* ext2 → 区画表 → 読み戻し → /hd0 にマウント */
    if (inst_hdd_prepare(api, &tgt) != 0) {
        boot_img_free(&boot);
        return;
    }
    /* ローダ → IPL (BIOS 幾何) */
    ret = inst_hdd_write_boot(api, &tgt, boot.ipl, boot.ipl_len,
                              boot.loader, boot.loader_len);
    boot_img_free(&boot);
    if (ret != 0) return;

    /* ディレクトリ構造を作成 */
    print(COL_CYAN, "  Creating directories...");
    for (i = 0; i < INIT_DIRS; i++) {
        int mr = api->sys_mkdir(init_dirs[i]);
        if (mr != 0) {
            api->kprintf(COL_RED, "\n    mkdir %s failed (rc=%d)\n", init_dirs[i], mr);
            inst_hdd_incomplete(api, "cannot create the directories", mr);
            return;
        }
    }
    println(COL_GREEN, " OK");

    /* パッケージの展開・同期・完了表示。展開の後の必須の実物は install_packages が
     * 完了を出す前に見る (required_on_hd0) */
    ret = install_packages(choice);
    if (ret != PKG_OK) inst_hdd_incomplete(api, "package installation failed", ret);
}
