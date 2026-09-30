#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif
/* ======================================================================== */
/*  INSTALL.C — OS32 HDDインストーラー v5.0                                  */
/*                                                                          */
/*  FAT12フロッピー (サブディレクトリ構造) から起動して実行し、               */
/*  IDEドライブにシステムを書き込む。                                        */
/*                                                                          */
/*  v4.0: FDDサブディレクトリ対応                                            */
/*  v4.1 (票 S3I2-I): 生カーネル (/kernel.bin → LBA 6) を廃止し、ローダ v3   */
/*    が読む ext2 の /boot/vmkernel.lz4 を置く。あわせて                     */
/*      - 承認前に /VMKRNL.LZ4 / /sys/boot_hdd.bin / /sys/loader_h.bin を検査 */
/*        (欠損なら 1 バイトも書かずに中止)                                   */
/*      - 宛先名を ASCII 小文字へ正規化 (FAT は大文字を返す、ext2 は区別する)  */
/*      - /etc/profile は写さない (FDD 用の PATH)                            */
/*      - コピー / 列挙 / sync の失敗を終了コードまで伝播                     */
/*    /sys/boot_hdd.bin, /sys/loader_h.bin → HDDブートセクタ                */
/*    /VMKRNL.LZ4 → /hd0/boot/vmkernel.lz4                                  */
/*    /sys/, /bin/, /sbin/, /etc/ → /hd0/ 配下にディレクトリごとコピー       */
/*  2026-09-24: FD の中身は CD の BOOT + MINIMAL と同じ集合。FAT の 8.3 に    */
/*    合わせて名前を変えた物 (build/packages.yaml の fd.rename) は正規名へ    */
/*    戻して写し、ブート領域へ書く物と FD だけの物 (fd.only) は写さない      */
/*    (fd_renames / fd_only。FD → install → HDD の集合 = MINIMAL)            */
/*  v5.0 (票 TASK_HDD_INSTALL 段 2): hd0 の扱いを CD の cdinst と共通の      */
/*    inst_hdd.c / inst_disk.c に移した — 区画表は PC-98 標準配置、IPL の     */
/*    [8]/[9] と区画の CHS は BIOS 幾何、区画は LBA 1632 以上の最初の BIOS    */
/*    シリンダ境界から 256MiB まで。空のディスクか OS32 の項目 1 つ (再作成) */
/*    だけを扱い、全検査 (媒体の大きさ・容量を含む) → 確認と y/N → (表が    */
/*    使えなければ ERASE の打鍵 → LBA 0/1 の消去) → ext2_format_at →        */
/*    区画表 → 読み戻し → マウント → ローダ / IPL → 展開 → sync の順。       */
/* ======================================================================== */

#include "os32api.h"
#include "inst_hdd.h"

#define IDE_DRIVE INST_DRIVE

/* IOバッファ (ヒープから確保する) */
static u8 *file_buf;
#define FILE_BUF_SIZE (128 * 1024)  /* 128KB */

#define MAX_FILES 64
typedef struct {
    char names[MAX_FILES][32];
    u8   types[MAX_FILES];  /* 1=FILE, 2=DIR */
    u32  sizes[MAX_FILES];  /* ファイルの長さ (容量の見積もり) */
    int count;
    int overflow;           /* MAX_FILES を超えて取りこぼした */
} FileList;

/* 媒体側の名前 (FDD)。FAT の 8.3 名は大文字で返るので、開くときは媒体の
 * 名前のまま、HDD へ作るときは小文字に正規化する。 */
#define SRC_KERNEL_LZ4  "/VMKRNL.LZ4"
#define SRC_BOOT_HDD    "/sys/boot_hdd.bin"
#define SRC_LOADER_H    "/sys/loader_h.bin"
#define SRC_SHELL       "/sys/shell.bin"    /* HDD 起動の常駐シェル (cdinst の MINIMAL と同じ必須) */
#define DST_SHELL       "/hd0/sys/shell.bin"
#define DST_BOOT_DIR    "/hd0/boot"
#define DST_KERNEL_LZ4  "/hd0/boot/vmkernel.lz4"

/* I2-1: drivers/ide.h の IdeInfo と**同じ並び** (i386 で 96 B) でなければ
 * ならない。ide_identify はカーネル側の定義で書くので、ここが 92 B のままだと
 * 末尾 phys_sector_size の 2 バイトが呼び手の領域を踏む。drivers/ide.h は
 * カーネル内部ヘッダで外部プログラムからは引けないため写しを置く —
 * ide.h を変えたら必ず一緒に直すこと (userland/shell/cmd_sys.c も同じ写し)。 */
typedef struct {
    u32 total_sectors;
    u16 cylinders;
    u16 heads;
    u16 sectors;
    u32 size_mb;
    char model[41];
    char serial[21];
    char firmware[9];
    int lba_supported;
    u16 phys_sector_size;   /* 物理セクタサイズ (SASI=256, IDE=512) */
} IdeInfo;

static KernelAPI *g_api;

/* ======== 回復モードの KAPI 境界 (票 S3-I §1) ======== */
/* `--recover-settings` / `--revert-settings` の本体は install_recover.inc に
 * あり、KAPI へは RecoverOps (関数ポインタ表) 経由でしか触らない。ホスト
 * TDD (tools/tests/install_recover_host.c) が同じ .inc を取り込めるように
 * するため。通常インストール経路はこの表を使わない。 */

static int rop_stat(const char *p, OS32_Stat *st)  { return g_api->sys_stat(p, st); }
static int rop_open(const char *p, int mode)       { return g_api->sys_open(p, mode); }
static int rop_read(int fd, void *b, u32 n)        { return g_api->sys_read(fd, b, n); }
static int rop_write(int fd, const void *b, u32 n) { return g_api->sys_write(fd, b, n); }
static void rop_close(int fd)                      { g_api->sys_close(fd); }
static int rop_unlink(const char *p)               { return g_api->sys_unlink(p); }
static int rop_rename(const char *o, const char *n){ return g_api->sys_rename(o, n); }
static int rop_mount(const char *pre, const char *dev, const char *fs)
{ return g_api->sys_mount(pre, dev, fs); }
static int rop_is_mounted(const char *pre)         { return g_api->sys_is_mounted(pre); }
static int rop_sync(void)                          { return g_api->vfs_sync(); }
static int rop_db_open_existing(const char *p, int w)
{ return g_api->db_open_existing(p, w); }
static int rop_db_prepare_only(int h, const char *sql)
{ return g_api->db_prepare_only(h, sql); }
static int rop_db_step(int h)                      { return g_api->db_step(h); }
static int rop_db_finalize(int h)                  { return g_api->db_finalize(h); }
static int rop_db_close(int h)                     { return g_api->db_close(h); }
static int rop_db_error_code(int h)                { return g_api->db_error_code(h); }
static unsigned char *rop_shm(void)                { return (unsigned char *)g_api->shm_base; }

static int rop_getkey(void)
{
    int key;
    for (;;) {
        key = g_api->kbd_trygetchar();
        if (key <= 0) key = g_api->serial_trygetchar();
        if (key > 0) return key;
    }
}

/* ======== 文字列・パスユーティリティ ======== */

static int str_len(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static void str_cat(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) { *dst++ = *src++; }
    *dst = '\0';
}

static void str_cpy(char *dst, const char *src) {
    while (*src) { *dst++ = *src++; }
    *dst = '\0';
}

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower_c(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

/* 宛先名の小文字化 (票 S3I2-I §1 の B1)。FAT (FF_USE_LFN 0) の列挙は
 * SHELL.BIN のように大文字を返し、ext2 は大小文字を区別するので、そのまま
 * 写すと /hd0/etc/SETTINGS.DB になり seed も /sys/shell.bin の起動も
 * 成立しない。ASCII だけを畳む (8.3 名に非 ASCII は来ない)。 */
static void str_cat_lower(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) { *dst++ = lower_c(*src); src++; }
    *dst = '\0';
}

static int str_eq_lower(const char *s, const char *lower) {
    while (*s && lower_c(*s) == *lower) { s++; lower++; }
    return lower_c(*s) == *lower;
}

static int str_endswith_ci(const char *s, const char *suffix) {
    int slen = str_len(s);
    int suflen = str_len(suffix);
    int i;
    char c1, c2;
    if (suflen > slen) return 0;
    for (i = 0; i < suflen; i++) {
        c1 = s[slen - suflen + i];
        c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}

static void ls_cb(const DirEntry_Ext *entry, void *ctx) {
    FileList *fl = (FileList *)ctx;
    int i;
    if (fl->count >= MAX_FILES) { fl->overflow = 1; return; }
    i = 0;
    while (entry->name[i] && i < 31) {
        fl->names[fl->count][i] = entry->name[i];
        i++;
    }
    fl->names[fl->count][i] = '\0';
    fl->types[fl->count] = entry->type;
    fl->sizes[fl->count] = entry->size;
    fl->count++;
}

/* ======== Stream I/O コピー ======== */

/* 成功なら写したバイト数 (>= 0)、失敗なら負。
 *   -1 = src が開けない / -2 = dst が開けない
 *   -3 = short write / -4 = read が負 (EOF ではなく I/O 失敗)
 * read の負を EOF と同じ扱いにすると、途中で落ちたコピーが「成功」に
 * 見えてしまう (票 S3I2-I §1 の B3)。 */
static int copy_file(const char *src, const char *dst) {
    int fd_src, fd_dst;
    int read_len;
    int total = 0;
    int rc = 0;

    fd_src = g_api->sys_open(src, O_RDONLY);
    if (fd_src < 0) return -1;

    fd_dst = g_api->sys_open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_dst < 0) {
        g_api->sys_close(fd_src);
        return -2;
    }

    while (1) {
        read_len = g_api->sys_read(fd_src, file_buf, FILE_BUF_SIZE);
        if (read_len < 0) { rc = -4; break; }
        if (read_len == 0) break;
        if (g_api->sys_write(fd_dst, file_buf, read_len) != read_len) {
            rc = -3;
            break;
        }
        total += read_len;
    }

    g_api->sys_close(fd_src);
    g_api->sys_close(fd_dst);
    return rc != 0 ? rc : total;
}

static int read_file_to_buf(const char *path, int max_size) {
    int fd = g_api->sys_open(path, KAPI_O_RDONLY);
    int len;
    if (fd < 0) return fd;
    len = g_api->sys_read(fd, file_buf, max_size);
    g_api->sys_close(fd);
    return len;
}

/* ======== 安全ロック ======== */

/* 鍵は inst_hdd_getkey (0 以上はすべて入力、CR の直後の LF は捨てる) */
static int confirm_install(void) {
    int key;
    g_api->kprintf(ATTR_RED, "%s", "\nWARNING: The OS32 area on hd0 will be ERASED during installation!\n");
    g_api->kprintf(ATTR_YELLOW, "%s", "Do you want to proceed? [y/N]: ");
    while (1) {
        key = inst_hdd_getkey(g_api);

        if (key == 'y' || key == 'Y') {
            g_api->kprintf(ATTR_YELLOW, "%s", "Y\n\n");
            return 1;
        } else if (key == 'n' || key == 'N' || key == 0x0D || key == 0x0A || key == 0x1B) {
            g_api->kprintf(ATTR_YELLOW, "%s", "N\n\n");
            return 0;
        }
    }
}

/* ======== ディレクトリ再帰コピー ======== */

/* ======== FD の名前 → HDD の正規名 (build/packages.yaml の fd: の逆) ========
 * FD の中身は CD の BOOT + MINIMAL と同じ集合で、FAT (LFN なし) に置けない名前と
 * 置き場所の違うものだけ fd.rename で名前を変えてある。install はそれを**逆に**
 * 当てて正規名で写す — そのまま写すと HDD に /etc/filetype が残り、HDD
 * 起動では filetypes が失われる (実装レビュー往復 2。既定フォントも同じ扱い
 * だったが、2026-09-25 に MINIMAL から NORMAL へ移して FD に載らなくなった)。
 * この表は packages.yaml の fd.rename / fd.only と tools/tests/test_packages.py
 * case 9 が突き合わせる (足したら両方直す)。
 *   hdd が /boot/ … : ファイルとしては写さない。ブート領域へ書く (boot_hdd /
 *                     loader) か、Phase 3 で別に写す (vmkernel.lz4)
 *   それ以外         : その正規名で写す */
typedef struct {
    const char *fd;     /* FD 上のパス (小文字で書く。比較は大小文字を無視) */
    const char *hdd;    /* HDD (配備マニフェスト) の正規名 */
} FdRename;

static const FdRename fd_renames[] = {
    { "/vmkrnl.lz4",           "/boot/vmkernel.lz4" },
    { "/sys/boot_hdd.bin",     "/boot/boot_hdd.bin" },
    { "/sys/loader_h.bin",     "/boot/loader_hdd.bin" },
    { "/etc/filetype",         "/etc/filetypes" }
};

/* FD だけの物 (fd.only)。HDD へ写さない。/etc/profile は FD 用の PATH
 * (/usr/bin が無い)、/LOADER.BIN は FAT の IPL が読む 2 段目 */
static const char *const fd_only[] = {
    "/loader.bin",
    "/etc/profile"
};

#define FD_DIM(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* src_path (FD 上のパス、FAT は大文字で返す) を HDD のどこへ写すか。
 * 戻り値: 1 = hdd_out に "/hd0" + 正規名、0 = 既定 (小文字にして同じ場所)、
 *         -1 = 写さない */
static int fd_map_to_hdd(const char *src_path, char *hdd_out)
{
    int i;
    for (i = 0; i < FD_DIM(fd_only); i++) {
        if (str_eq_lower(src_path, fd_only[i])) return -1;
    }
    for (i = 0; i < FD_DIM(fd_renames); i++) {
        const char *h = fd_renames[i].hdd;
        if (!str_eq_lower(src_path, fd_renames[i].fd)) continue;
        if (h[0] == '/' && h[1] == 'b' && h[2] == 'o' && h[3] == 'o' &&
            h[4] == 't' && h[5] == '/') return -1;
        str_cpy(hdd_out, "/hd0");
        str_cat(hdd_out, h);
        return 1;
    }
    return 0;
}

/* FDDのsrc_dir配下のファイル/ディレクトリを /hd0/dst_dir 配下にコピー。
 * 戻り値は **失敗の件数** (0 = 全部成功)。sys_ls の負 (列挙途中の I/O
 * 失敗) / mkdir / コピー / 再帰の失敗をすべて数える (票 S3I2-I §1 の B3、
 * 往復 2 の R2)。宛先の名前は各成分を ASCII 小文字に正規化する。
 * measure が NULL でなければ**何も書かず**、写すはずのファイルとディレクトリを
 * 数えるだけ (票 TASK_HDD_INSTALL 段 2-11 の容量の事前検査)。列挙の失敗・
 * 取りこぼし・深すぎは同じく失敗に数えるので、書く前に分かる。 */
static int copy_directory(const char *src_dir, const char *dst_dir, int depth,
                          InstNeed *measure)
{
    FileList fl;
    int i, fail_count = 0;
    int ls_rc;

    if (depth > 4) {
        /* 無限再帰防止。ここに来たら写し漏れなので失敗として数える。 */
        g_api->kprintf(ATTR_RED, "  [FAIL] %s: too deep\n", src_dir);
        return 1;
    }

    fl.count = 0;
    fl.overflow = 0;
    ls_rc = g_api->sys_ls(src_dir, ls_cb, &fl);
    if (ls_rc < 0) {
        g_api->kprintf(ATTR_RED, "  [FAIL] list %s (code: %d)\n", src_dir, ls_rc);
        return 1;
    }
    if (fl.overflow) {
        g_api->kprintf(ATTR_RED, "  [FAIL] %s: more than %d entries\n",
                       src_dir, MAX_FILES);
        fail_count++;
    }

    for (i = 0; i < fl.count; i++) {
        char src_path[128];
        char dst_path[128];

        /* "." と ".." をスキップ */
        if (fl.names[i][0] == '.') {
            if (fl.names[i][1] == '\0') continue;
            if (fl.names[i][1] == '.' && fl.names[i][2] == '\0') continue;
        }

        /* ソースパス構築 (媒体の名前のまま開く) */
        str_cpy(src_path, src_dir);
        if (src_path[str_len(src_path) - 1] != '/') str_cat(src_path, "/");
        str_cat(src_path, fl.names[i]);

        /* 宛先パス構築: /hd0 + dst_dir + / + 小文字にした名前。FD だけの物は
         * 写さず、fd.rename の物は正規名へ (上の表) */
        {
            int m = (fl.types[i] == OS32_FILE_TYPE_DIR) ? 0
                                                        : fd_map_to_hdd(src_path, dst_path);
            if (m < 0) continue;
            if (m == 0) {
                str_cpy(dst_path, "/hd0");
                str_cat(dst_path, dst_dir);
                if (dst_path[str_len(dst_path) - 1] != '/') str_cat(dst_path, "/");
                str_cat_lower(dst_path, fl.names[i]);
            }
        }

        if (measure) {
            if (fl.types[i] == OS32_FILE_TYPE_DIR) {
                char sub_dst[128];
                inst_need_dir(measure);
                str_cpy(sub_dst, dst_dir);
                if (sub_dst[str_len(sub_dst) - 1] != '/') str_cat(sub_dst, "/");
                str_cat_lower(sub_dst, fl.names[i]);
                fail_count += copy_directory(src_path, sub_dst, depth + 1, measure);
            } else {
                inst_need_file(measure, fl.sizes[i]);
            }
            continue;
        }

        if (fl.types[i] == OS32_FILE_TYPE_DIR) {
            /* サブディレクトリ: HDD側にmkdirして再帰 */
            if (g_api->sys_mkdir(dst_path) != 0) {
                g_api->kprintf(ATTR_RED, "  [FAIL] mkdir %s\n", dst_path);
                fail_count++;
                continue;
            }

            {
                char sub_dst[128];
                str_cpy(sub_dst, dst_dir);
                if (sub_dst[str_len(sub_dst) - 1] != '/') str_cat(sub_dst, "/");
                str_cat_lower(sub_dst, fl.names[i]);
                fail_count += copy_directory(src_path, sub_dst, depth + 1, 0);
            }
        } else {
            /* ファイル: コピー */
            int bytes = copy_file(src_path, dst_path);
            if (bytes >= 0) {
                g_api->kprintf(ATTR_GREEN, "  [OK] %s -> %s (%d b)\n", src_path, dst_path, bytes);
            } else {
                g_api->kprintf(ATTR_RED, "  [FAIL] %s -> %s (code: %d)\n", src_path, dst_path, bytes);
                fail_count++;
            }
        }
    }

    return fail_count;
}

#include "install_recover.inc"

static const RecoverOps recover_kapi_ops = {
    rop_stat, rop_open, rop_read, rop_write, rop_close, rop_unlink,
    rop_rename, rop_mount, rop_is_mounted, rop_sync,
    rop_db_open_existing, rop_db_prepare_only, rop_db_step, rop_db_finalize,
    rop_db_close, rop_db_error_code, rop_shm,
    0,                    /* kprintf — main() で api から埋める */
    rop_getkey
};

/* ======== メイン ======== */

/* Phase 0 (承認前): 媒体に要る 4 つが在り、通常のファイルで、空でないことを
 * 確かめる。1 つでも欠ければ **1 バイトも書かずに** 中止する (票 S3I2-I §1、
 * TASK_HDD_INSTALL 段 2 の Codex 往復 1 P1-3 — cdinst の必須と同じ規則)。
 * 戻り値 0 = 揃っている。sizes[] に大きさ (VMKRNL.LZ4 / IPL / ローダ / shell の
 * 順) を返す。大きさの上限は inst_hdd_check_media が見る (段 2-11、N8)。 */
#define MEDIA_KERNEL 0
#define MEDIA_IPL    1
#define MEDIA_LOADER 2
#define MEDIA_SHELL  3
#define MEDIA_COUNT  4
static int precheck_media(u32 *sizes)
{
    static const char *const need[MEDIA_COUNT] = {
        SRC_KERNEL_LZ4, SRC_BOOT_HDD, SRC_LOADER_H, SRC_SHELL
    };
    OS32_Stat st;
    int i, rc;

    for (i = 0; i < MEDIA_COUNT; i++) {
        sizes[i] = 0;
        rc = g_api->sys_stat(need[i], &st);
        if (rc != 0) {
            g_api->kprintf(0x4F, "Error: Missing %s (stat %d)\n", need[i], rc);
            return -1;
        }
        if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) {
            g_api->kprintf(0x4F, "Error: %s is not a regular file\n", need[i]);
            return -1;
        }
        if (st.st_size == 0) {
            g_api->kprintf(0x4F, "Error: Empty %s\n", need[i]);
            return -1;
        }
        sizes[i] = st.st_size;
    }
    return 0;
}

/* HDD に最初に作るディレクトリ (親が先)。1 つでも作れなければインストールは
 * 未完成 (往復 1 の B1)。 */
static const char *const init_dirs[] = {
    DST_BOOT_DIR,
    "/hd0/sys", "/hd0/bin", "/hd0/sbin", "/hd0/etc",
    "/hd0/usr", "/hd0/usr/bin", "/hd0/usr/man",
    "/hd0/data", "/hd0/home", "/hd0/home/user", "/hd0/tmp"
};
#define INIT_DIRS ((int)(sizeof(init_dirs) / sizeof(init_dirs[0])))

/* FD から写すディレクトリ (媒体の名前 → /hd0 の下の名前) */
static const char *const copy_dirs[] = { "/sys", "/bin", "/sbin", "/etc" };
#define COPY_DIRS ((int)(sizeof(copy_dirs) / sizeof(copy_dirs[0])))

/* 展開先に要る量を**書く前に**数える。列挙の失敗は失敗の件数で返す。 */
static int measure_need(u32 lz4_size, InstNeed *need)
{
    int i, fails = 0;

    inst_need_init(need);
    for (i = 0; i < INIT_DIRS; i++) inst_need_dir(need);
    inst_need_file(need, lz4_size);
    for (i = 0; i < COPY_DIRS; i++)
        fails += copy_directory(copy_dirs[i], copy_dirs[i], 0, need);
    return fails;
}

/* src を丸ごと file_buf に読む。長さが want と違えば負 (途中で読めない・
 * stat と中身が食い違う)。 */
static int read_exact(const char *src, u32 want)
{
    int len;
    if (want > FILE_BUF_SIZE) return -1;
    len = read_file_to_buf(src, FILE_BUF_SIZE);
    if (len < 0 || (u32)len != want) return -1;
    return len;
}

int __cdecl main(int argc, char **argv, KernelAPI *api)
{
    static IdeInfo info;
    static RecoverOps rops;
    static InstTarget tgt;
    static InstNeed need;
    static u8 ipl_buf[512];
    int ret, i;
    int rc = 1;                 /* 失敗を既定にする (票 S3I2-I §1) */
    u32 sizes[MEDIA_COUNT];
    int copied;

    g_api = api;

    /* --- 回復モード (票 S3-I §1)。通常インストール経路には入らない --- */
    if (argc >= 2 && argv && argv[1] &&
        (str_eq(argv[1], "--recover-settings") ||
         str_eq(argv[1], "--revert-settings"))) {
        const char *drive = (argc >= 3 && argv[2]) ? argv[2] : "hd0";
        rops = recover_kapi_ops;
        rops.kprintf = api->kprintf;
        return recover_settings_main(&rops, drive,
                                     str_eq(argv[1], "--revert-settings"));
    }
    (void)argc;
    (void)argv;

    api->kprintf(ATTR_CYAN, "%s", "\n========================================\n");
    api->kprintf(ATTR_CYAN, "%s", "      OS32 HDD Installer v5.0        \n");
    api->kprintf(ATTR_CYAN, "%s", "========================================\n\n");

    file_buf = (u8 *)api->mem_alloc(FILE_BUF_SIZE);
    if (!file_buf) {
        api->kprintf(0x4F, "%s", "Error: Out of memory\n");
        return 1;
    }

    /* IDEの初期化とディスク情報の取得 (表示用。判定は hdd_geom_info) */
    api->kprintf(0x07, "%s", "Initializing IDE controller...\n");
    api->ide_init();

    if (!api->ide_drive_present(IDE_DRIVE)) {
        api->kprintf(0x4F, "%s", "Error: IDE Master Drive (hd0) not found.\n");
        goto end;
    }

    if ((ret = api->ide_identify(IDE_DRIVE, &info)) != 0) {
        api->kprintf(0x4F, "%s", "Error: Could not identify drive.\n");
        goto end;
    }

    api->kprintf(ATTR_WHITE, "Target Drive Found: %s\n", info.model);
    api->kprintf(ATTR_WHITE, "Size: %u MB (%u sectors)\n", info.size_mb, info.total_sectors);

    /* === Phase 0: 全検査 (承認前 = まだ何も書いていない) ===
     * 媒体の中身 → FD の列挙 → hd0 (幾何・区画表のモード・マウント。表が
     * 使えなければ要約を出して「消した後の空のディスク」として続ける) →
     * 大きさと容量 */
    if (precheck_media(sizes) != 0) {
        api->kprintf(0x4F, "%s",
                     "Nothing was written; use a complete install floppy.\n");
        goto end;
    }
    if (measure_need(sizes[MEDIA_KERNEL], &need) != 0) {
        api->kprintf(0x4F, "%s",
                     "Error: cannot list the floppy. Nothing was written.\n");
        goto end;
    }
    if (inst_hdd_check(api, &tgt) != 0) goto end;
    if (inst_hdd_check_media(api, &tgt, sizes[MEDIA_IPL], sizes[MEDIA_LOADER],
                             sizes[MEDIA_KERNEL], &need) != 0)
        goto end;

    /* 安全ロック */
    inst_hdd_describe(api, &tgt);
    if (!confirm_install()) {
        /* 利用者が断っただけ = 失敗ではない (まだ何も書いていない) */
        api->kprintf(ATTR_WHITE, "%s", "Installation aborted. Nothing was written.\n");
        rc = 0;
        goto end;
    }
    /* 表が使えないディスクは y の後に ERASE の打鍵 (受けなければ何も書かない) */
    if (inst_hdd_ask_erase(api, &tgt) != 0) goto end;

    /* === Phase 1: (ERASE なら LBA 0/1 の消去 →) ext2 → 区画表 → 読み戻し →
     * マウント (R3-1) === */
    api->kprintf(ATTR_YELLOW, "%s", "[1/3] Preparing the OS32 area on hd0...\n");
    if (inst_hdd_release(api, &tgt) != 0) goto end;
    if (inst_hdd_prepare(api, &tgt) != 0) goto end;

    /* === Phase 2: ローダ (LBA 2〜) → IPL (LBA 0) === */
    api->kprintf(ATTR_YELLOW, "%s", "\n[2/3] Writing Boot Sectors...\n");
    if (read_exact(SRC_BOOT_HDD, sizes[MEDIA_IPL]) < 0) {
        inst_hdd_incomplete(api, "cannot read /sys/boot_hdd.bin", -1);
        goto end;
    }
    for (i = 0; i < 512; i++) ipl_buf[i] = (i < (int)sizes[MEDIA_IPL]) ? file_buf[i] : 0;
    if (read_exact(SRC_LOADER_H, sizes[MEDIA_LOADER]) < 0) {
        inst_hdd_incomplete(api, "cannot read /sys/loader_h.bin", -1);
        goto end;
    }
    if (inst_hdd_write_boot(api, &tgt, ipl_buf, sizes[MEDIA_IPL],
                            file_buf, sizes[MEDIA_LOADER]) != 0)
        goto end;

    /* === Phase 3: ファイルコピー === */
    api->kprintf(ATTR_YELLOW, "%s", "\n[3/3] Copying files to HDD...\n");

    /* HDD側ディレクトリ構造を作成。1 つでも作れなければ未完成なので中止する
     * (往復 1 の B1: 戻り値を捨てると /hd0/tmp だけ無い HDD で終了 0 になる)。
     * 親は子より先に並べること。 */
    api->kprintf(0x0A, "%s", "  Creating directories...\n");
    for (i = 0; i < INIT_DIRS; i++) {
        if (api->sys_mkdir(init_dirs[i]) != 0) {
            api->kprintf(0x4F, "Error: Failed to create %s\n", init_dirs[i]);
            inst_hdd_incomplete(api, "cannot create the directories", -1);
            goto end;
        }
    }

    /* 圧縮カーネル → /hd0/boot/vmkernel.lz4 (128KB バッファで反復コピー)。
     * ローダ v3 がここを読む。長さが合わなければ失敗。 */
    copied = copy_file(SRC_KERNEL_LZ4, DST_KERNEL_LZ4);
    if (copied < 0 || (u32)copied != sizes[MEDIA_KERNEL]) {
        api->kprintf(0x4F, "  [FAIL] %s -> %s (code: %d, want %u bytes)\n",
                     SRC_KERNEL_LZ4, DST_KERNEL_LZ4, copied, sizes[MEDIA_KERNEL]);
        inst_hdd_incomplete(api, "cannot copy vmkernel.lz4", copied);
        goto end;
    }
    api->kprintf(0x0A, "  vmkernel.lz4 -> /boot (%d bytes)\n", copied);

    /* FDDのサブディレクトリ構造をそのままHDDにコピー (宛先の名前は小文字)。
     * /etc は profile を写さない、settings.db は seed */
    ret = 0;
    for (i = 0; i < COPY_DIRS; i++) {
        api->kprintf(ATTR_CYAN, "  Copying %s/ ...\n", copy_dirs[i]);
        ret += copy_directory(copy_dirs[i], copy_dirs[i], 0, 0);
    }

    if (ret != 0) {
        api->kprintf(0x4F, "  %d file(s) failed to copy.\n", ret);
        inst_hdd_incomplete(api, "files failed to copy", ret);
        goto end;
    }

    /* 必須の shell が HDD に事前検査のとおりの大きさで在る (最終の状態、
     * Codex 往復 2 P1-2 — cdinst と同じ守り。FD の名前は FAT で一意なので
     * 後から上書きされる経路は今は無いが、写し先の表 (fd_renames) を足したとき
     * の保険) */
    {
        OS32_Stat st;
        if (api->sys_stat(DST_SHELL, &st) != 0 || (st.st_mode & OS_S_IFMT) != OS_S_IFREG ||
            st.st_size != sizes[MEDIA_SHELL]) {
            api->kprintf(0x4F, "  %s is missing or has the wrong size (want %u)\n",
                         DST_SHELL, sizes[MEDIA_SHELL]);
            inst_hdd_incomplete(api, "the shell was not installed", -1);
            goto end;
        }
    }

    /* ファイルシステム同期 */
    api->kprintf(ATTR_CYAN, "%s", "  Syncing filesystem...\n");
    ret = api->vfs_sync();
    if (ret != 0) {
        api->kprintf(0x4F, "  sync failed (code: %d)\n", ret);
        inst_hdd_incomplete(api, "sync failed", ret);
        goto end;
    }

    api->kprintf(0x7E, "%s", "\n========================================\n");
    api->kprintf(ATTR_CYAN, "%s", " OS32 Installation complete!            \n");
    api->kprintf(ATTR_CYAN, "%s", " Remove the Floppy Disk and reboot.     \n");
    api->kprintf(0x7E, "%s", "========================================\n");
    rc = 0;

end:
    api->mem_free(file_buf);
    file_buf = 0;
    if (rc != 0)
        api->kprintf(ATTR_RED, "%s", "\n[FAIL] Installation did not complete.\n");
    return rc;
}
