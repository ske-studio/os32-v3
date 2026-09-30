/* ======================================================================== */
/*  TAR.C -- ustar アーカイブの作成 / 展開 / 一覧 (票 S6)                    */
/*                                                                          */
/*  Usage: tar c ARCHIVE PATH...      PATH... を ARCHIVE に束ねる            */
/*         tar x ARCHIVE [-C DIR]     ARCHIVE を DIR (既定 .) に展開         */
/*         tar t ARCHIVE              ARCHIVE の中身を一覧                   */
/*                                                                          */
/*  扱うのは通常ファイルとディレクトリだけ (ディレクトリは再帰)。             */
/*  圧縮は持たない。`lz4` を外で掛ける (例: etc.tar → etc.tar.lz4)。         */
/*                                                                          */
/*  ustar の読み書きは lib/microtar (rxi、MIT) をそのまま使い、I/O だけ      */
/*  KAPI (sys_open / sys_read / sys_write / sys_lseek / sys_close) に        */
/*  差し替える。ホスト試験用に -DHOST_TEST で POSIX に切り替わる             */
/*  (tools/tests/test_tar_cmd.py)。                                          */
/*                                                                          */
/*  名前は先頭の '/' を落として格納する (GNU tar と同じ)。ustar の name 欄は */
/*  100 バイトで、NUL 終端のために 99 バイトを超える名前は拒否する。         */
/* ======================================================================== */

#ifdef HOST_TEST
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#define TAR_FILE_TYPE_FILE 1
#define TAR_FILE_TYPE_DIR  2
#define TAR_PATH_MAX       256
#else
#include "os32api.h"
#include <stdio.h>
#include <string.h>
#define TAR_FILE_TYPE_FILE OS32_FILE_TYPE_FILE
#define TAR_FILE_TYPE_DIR  OS32_FILE_TYPE_DIR
#define TAR_PATH_MAX       OS32_MAX_PATH
#endif

#include "microtar.h"

/* --- 定数 (C4: 直値を散らさない) ------------------------------------- */
#define TAR_NAME_MAX     99      /* ustar name 欄 100B − NUL 1B          */
#define TAR_CHUNK        8192    /* ファイル本体を写す単位                */
#define TAR_MAX_DEPTH    8       /* ディレクトリ再帰の上限 (du と同じ)    */
#define TAR_ENT_MAX      512     /* 1 回の作成で覚える名前の総数          */
#define TAR_POOL_BYTES   16384   /* その名前を詰める領域                  */

#define TAR_MODE_NONE    0
#define TAR_MODE_CREATE  1
#define TAR_MODE_EXTRACT 2
#define TAR_MODE_LIST    3

#define TAR_EXIT_OK      0
#define TAR_EXIT_FAIL    1

/* --- 前方宣言 (main を先頭関数に保つため。変数の定義は関数ではない) ---- */
#ifndef HOST_TEST
static KernelAPI *g_api;
#endif
static int  cmd_create(const char *archive, int argc, char **argv, int first);
static int  cmd_extract(const char *archive, const char *destdir);
static int  cmd_list(const char *archive);
static void usage(void);
static void err_path(const char *path, const char *why);

int main(int argc, char **argv
#ifndef HOST_TEST
         , KernelAPI *kapi
#endif
         )
{
    int mode = TAR_MODE_NONE;
    const char *archive = 0;
    const char *destdir = ".";
    int i;
    int first_member = 0;

#ifndef HOST_TEST
    g_api = kapi;
#endif

    if (argc < 3) {
        usage();
        return TAR_EXIT_FAIL;
    }

    if (strcmp(argv[1], "c") == 0)      mode = TAR_MODE_CREATE;
    else if (strcmp(argv[1], "x") == 0) mode = TAR_MODE_EXTRACT;
    else if (strcmp(argv[1], "t") == 0) mode = TAR_MODE_LIST;
    else {
        usage();
        return TAR_EXIT_FAIL;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-C") == 0) {
            if (i + 1 >= argc) {
                usage();
                return TAR_EXIT_FAIL;
            }
            destdir = argv[++i];
            continue;
        }
        if (archive == 0) {
            archive = argv[i];
            first_member = i + 1;
            continue;
        }
        if (mode != TAR_MODE_CREATE) {
            usage();
            return TAR_EXIT_FAIL;
        }
        break;
    }

    if (archive == 0) {
        usage();
        return TAR_EXIT_FAIL;
    }

    if (mode == TAR_MODE_CREATE) {
        if (first_member >= argc) {
            usage();
            return TAR_EXIT_FAIL;
        }
        return cmd_create(archive, argc, argv, first_member);
    }
    if (mode == TAR_MODE_EXTRACT) {
        return cmd_extract(archive, destdir);
    }
    return cmd_list(archive);
}

/* ======================================================================== */
/*  低レベル I/O — ゲストは KAPI、ホスト試験は POSIX                        */
/* ======================================================================== */

/* アーカイブの fd (同時に 1 本しか開かない) */
static int g_fd = -1;

static int io_open_read(const char *path)
{
#ifdef HOST_TEST
    return open(path, O_RDONLY);
#else
    return g_api->sys_open(path, KAPI_O_RDONLY);
#endif
}

static int io_open_write(const char *path)
{
#ifdef HOST_TEST
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#else
    return g_api->sys_open(path, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
#endif
}

static int io_read(int fd, void *buf, unsigned size)
{
#ifdef HOST_TEST
    return (int)read(fd, buf, (size_t)size);
#else
    return g_api->sys_read(fd, buf, (u32)size);
#endif
}

static int io_write(int fd, const void *buf, unsigned size)
{
#ifdef HOST_TEST
    return (int)write(fd, buf, (size_t)size);
#else
    return g_api->sys_write(fd, buf, (u32)size);
#endif
}

static int io_seek(int fd, unsigned pos)
{
#ifdef HOST_TEST
    return (lseek(fd, (off_t)pos, SEEK_SET) == (off_t)pos) ? 0 : -1;
#else
    return (g_api->sys_lseek(fd, (int)pos, SEEK_SET) < 0) ? -1 : 0;
#endif
}

static void io_close(int fd)
{
#ifdef HOST_TEST
    close(fd);
#else
    g_api->sys_close(fd);
#endif
}

static int io_mkdir(const char *path)
{
#ifdef HOST_TEST
    return mkdir(path, 0755);
#else
    return g_api->sys_mkdir(path);
#endif
}

/* 0 = 見つかった。type に TAR_FILE_TYPE_*、size にバイト数 */
static int io_stat(const char *path, unsigned long *size, int *type)
{
#ifdef HOST_TEST
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    *size = (unsigned long)st.st_size;
    *type = S_ISDIR(st.st_mode) ? TAR_FILE_TYPE_DIR : TAR_FILE_TYPE_FILE;
    return 0;
#else
    OS32_Stat st;
    if (g_api->sys_stat(path, &st) != 0) return -1;
    *size = (unsigned long)st.st_size;
    /* st_mode の種別ビットは FS ごとに違うので、ディレクトリかどうかは
     * sys_open で確かめる (vfs_open はディレクトリを断る、06_filesystem §6-1)。 */
    {
        int fd = g_api->sys_open(path, KAPI_O_RDONLY);
        if (fd < 0) {
            *type = TAR_FILE_TYPE_DIR;
        } else {
            *type = TAR_FILE_TYPE_FILE;
            g_api->sys_close(fd);
        }
    }
    return 0;
#endif
}

/* ======================================================================== */
/*  microtar の I/O コールバック                                            */
/* ======================================================================== */

static int mt_read(mtar_t *tar, void *data, unsigned size)
{
    int n;
    (void)tar;
    n = io_read(g_fd, data, size);
    return (n >= 0 && (unsigned)n == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int mt_write(mtar_t *tar, const void *data, unsigned size)
{
    int n;
    (void)tar;
    n = io_write(g_fd, data, size);
    return (n >= 0 && (unsigned)n == size) ? MTAR_ESUCCESS : MTAR_EWRITEFAIL;
}

static int mt_seek(mtar_t *tar, unsigned pos)
{
    (void)tar;
    return (io_seek(g_fd, pos) == 0) ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int mt_close(mtar_t *tar)
{
    (void)tar;
    if (g_fd >= 0) {
        io_close(g_fd);
        g_fd = -1;
    }
    return MTAR_ESUCCESS;
}

/* mtar_open() は MTAR_NO_STDIO で外してあるので、自前で組み立てる。 */
static void mt_bind(mtar_t *tar)
{
    memset(tar, 0, sizeof(*tar));
    tar->read  = mt_read;
    tar->write = mt_write;
    tar->seek  = mt_seek;
    tar->close = mt_close;
    tar->stream = 0;
}

/* ======================================================================== */
/*  文字列・パスの小道具                                                    */
/* ======================================================================== */

static void err_path(const char *path, const char *why)
{
    printf("tar: %s: %s\n", path, why);
}

static void usage(void)
{
    printf("Usage: tar c ARCHIVE PATH...\n");
    printf("       tar x ARCHIVE [-C DIR]\n");
    printf("       tar t ARCHIVE\n");
}

static int str_copy(char *dst, int dstsize, const char *src)
{
    int i = 0;
    while (src[i] && i < dstsize - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return src[i] ? -1 : i;
}

/* dir + '/' + name。溢れたら -1 */
static int path_join(char *dst, int dstsize, const char *dir, const char *name)
{
    int i = 0, j = 0;

    if (dir && dir[0] && !(dir[0] == '.' && dir[1] == '\0')) {
        while (dir[i] && i < dstsize - 1) {
            dst[i] = dir[i];
            i++;
        }
        if (dir[i]) return -1;
        if (i > 0 && dst[i - 1] != '/') {
            if (i >= dstsize - 1) return -1;
            dst[i++] = '/';
        }
    }
    while (name[j]) {
        if (i >= dstsize - 1) return -1;
        dst[i++] = name[j++];
    }
    dst[i] = '\0';
    return i;
}

/* アーカイブに入れる名前: 先頭の '/' を落とす (GNU tar と同じ) */
static const char *archive_name(const char *path)
{
    while (*path == '/') path++;
    return path;
}

/* 末尾の '/' を落とす (Python の tarfile はディレクトリに付ける) */
static void strip_trailing_slash(char *s)
{
    int n = (int)strlen(s);
    while (n > 1 && s[n - 1] == '/') {
        s[n - 1] = '\0';
        n--;
    }
}

/* 途中のディレクトリをまとめて作る。既にあってもエラーにしない。 */
static void mkdir_parents(const char *path, int include_self)
{
    char work[TAR_PATH_MAX];
    int i;

    if (str_copy(work, (int)sizeof(work), path) < 0) return;
    for (i = 1; work[i]; i++) {
        if (work[i] == '/') {
            work[i] = '\0';
            io_mkdir(work);
            work[i] = '/';
        }
    }
    if (include_self) io_mkdir(work);
}

/* ======================================================================== */
/*  作成 (c)                                                                */
/*                                                                          */
/*  §4-26: sys_ls のコールバックの中で FS を触らない。コールバックは名前を   */
/*  自分の領域に集めるだけにして、一覧が戻ってから開く / 再帰する。          */
/* ======================================================================== */

static char  g_pool[TAR_POOL_BYTES];
static int   g_pool_used;
static int   g_ent_off[TAR_ENT_MAX];
static unsigned char g_ent_type[TAR_ENT_MAX];
static int   g_ent_count;
static int   g_collect_overflow;

static mtar_t g_tar;
static char  g_chunk[TAR_CHUNK];

static void collect_cb(
#ifdef HOST_TEST
    const char *name, int type
#else
    const DirEntry_Ext *entry, void *ctx
#endif
    )
{
#ifndef HOST_TEST
    const char *name = entry->name;
    int type = entry->type;
#endif
    int len;                 /* [C1] 宣言はブロック先頭にまとめる */

#ifndef HOST_TEST
    (void)ctx;
#endif

    if (name[0] == '.') {
        if (name[1] == '\0') return;
        if (name[1] == '.' && name[2] == '\0') return;
    }
    len = (int)strlen(name);
    if (g_ent_count >= TAR_ENT_MAX || g_pool_used + len + 1 > TAR_POOL_BYTES) {
        g_collect_overflow = 1;
        return;
    }
    memcpy(&g_pool[g_pool_used], name, (size_t)len + 1);
    g_ent_off[g_ent_count] = g_pool_used;
    g_ent_type[g_ent_count] = (unsigned char)type;
    g_ent_count++;
    g_pool_used += len + 1;
}

#ifdef HOST_TEST
static void host_list(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *de;
    if (!d) return;
    while ((de = readdir(d)) != 0) {
        char full[TAR_PATH_MAX];
        struct stat st;
        int type = TAR_FILE_TYPE_FILE;
        if (path_join(full, (int)sizeof(full), path, de->d_name) < 0) continue;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) type = TAR_FILE_TYPE_DIR;
        collect_cb(de->d_name, type);
    }
    closedir(d);
}
#endif

static int add_file(const char *path, const char *name, unsigned long size)
{
    int fd;
    unsigned long left = size;
    int err;

    err = mtar_write_file_header(&g_tar, name, (unsigned)size);
    if (err != MTAR_ESUCCESS) {
        err_path(path, mtar_strerror(err));
        return -1;
    }
    fd = io_open_read(path);
    if (fd < 0) {
        err_path(path, "cannot open");
        return -1;
    }
    while (left > 0) {
        unsigned want = (left > (unsigned long)TAR_CHUNK)
                        ? (unsigned)TAR_CHUNK : (unsigned)left;
        int n = io_read(fd, g_chunk, want);
        if (n <= 0 || (unsigned)n != want) {
            io_close(fd);
            err_path(path, "read error");
            return -1;
        }
        err = mtar_write_data(&g_tar, g_chunk, want);
        if (err != MTAR_ESUCCESS) {
            io_close(fd);
            err_path(path, mtar_strerror(err));
            return -1;
        }
        left -= want;
    }
    io_close(fd);
    return 0;
}

static int add_path(const char *path, int depth)
{
    unsigned long size = 0;
    int type = TAR_FILE_TYPE_FILE;
    const char *name;
    int err;
    int mark_pool, mark_ent, first, i, count;
    int rc = 0;

    if (io_stat(path, &size, &type) != 0) {
        err_path(path, "No such file or directory");
        return -1;
    }

    name = archive_name(path);
    if (name[0] == '\0') {
        err_path(path, "empty name in archive");
        return -1;
    }
    if ((int)strlen(name) > TAR_NAME_MAX) {
        err_path(path, "name too long for ustar (max 99 bytes)");
        return -1;
    }

    if (type == TAR_FILE_TYPE_FILE) {
        return add_file(path, name, size);
    }

    /* ディレクトリ */
    err = mtar_write_dir_header(&g_tar, name);
    if (err != MTAR_ESUCCESS) {
        err_path(path, mtar_strerror(err));
        return -1;
    }
    if (depth >= TAR_MAX_DEPTH) {
        err_path(path, "directory nesting too deep");
        return -1;
    }

    /* 一覧はここで全部集めてしまう (§4-26)。深さごとにスタックで使う。 */
    mark_pool = g_pool_used;
    mark_ent  = g_ent_count;
    first     = g_ent_count;
    g_collect_overflow = 0;
#ifdef HOST_TEST
    host_list(path);
#else
    g_api->sys_ls(path, (void *)collect_cb, 0);
#endif
    if (g_collect_overflow) {
        err_path(path, "too many entries");
        g_pool_used = mark_pool;
        g_ent_count = mark_ent;
        return -1;
    }
    count = g_ent_count;

    for (i = first; i < count; i++) {
        char child[TAR_PATH_MAX];
        if (path_join(child, (int)sizeof(child), path, &g_pool[g_ent_off[i]]) < 0) {
            err_path(path, "path too long");
            rc = -1;
            continue;
        }
        if (add_path(child, depth + 1) != 0) rc = -1;
    }

    g_pool_used = mark_pool;
    g_ent_count = mark_ent;
    return rc;
}

static int cmd_create(const char *archive, int argc, char **argv, int first)
{
    int i;
    int rc = TAR_EXIT_OK;
    int err;

    g_fd = io_open_write(archive);
    if (g_fd < 0) {
        err_path(archive, "cannot create");
        return TAR_EXIT_FAIL;
    }
    mt_bind(&g_tar);
    g_pool_used = 0;
    g_ent_count = 0;

    for (i = first; i < argc; i++) {
        if (add_path(argv[i], 0) != 0) rc = TAR_EXIT_FAIL;
    }

    err = mtar_finalize(&g_tar);
    if (err != MTAR_ESUCCESS) {
        err_path(archive, mtar_strerror(err));
        rc = TAR_EXIT_FAIL;
    }
    mtar_close(&g_tar);
    return rc;
}

/* ======================================================================== */
/*  展開 (x) と一覧 (t)                                                     */
/* ======================================================================== */

static int open_archive_for_read(const char *archive, mtar_t *tar)
{
    g_fd = io_open_read(archive);
    if (g_fd < 0) {
        err_path(archive, "No such file or directory");
        return -1;
    }
    mt_bind(tar);
    return 0;
}

static int extract_one(const mtar_header_t *h, const char *destdir)
{
    char name[sizeof(h->name) + 1];
    char dest[TAR_PATH_MAX];
    int is_dir;
    int fd;
    unsigned long left;

    if (str_copy(name, (int)sizeof(name), h->name) < 0) {
        err_path(h->name, "name too long");
        return -1;
    }
    is_dir = (h->type == MTAR_TDIR);
    if (name[0] && name[strlen(name) - 1] == '/') is_dir = 1;
    strip_trailing_slash(name);
    if (name[0] == '\0') return 0;

    /* ".." を含む名前は展開先の外に出るので断る */
    {
        const char *p = name;
        while (*p) {
            if (p[0] == '.' && p[1] == '.' &&
                (p[2] == '/' || p[2] == '\0') &&
                (p == name || p[-1] == '/')) {
                err_path(name, "unsafe path");
                return -1;
            }
            p++;
        }
    }
    if (name[0] == '/') {
        err_path(name, "unsafe path");
        return -1;
    }

    if (path_join(dest, (int)sizeof(dest), destdir, name) < 0) {
        err_path(name, "path too long");
        return -1;
    }

    if (is_dir) {
        mkdir_parents(dest, 1);
        return 0;
    }
    if (h->type != MTAR_TREG && h->type != '\0') {
        err_path(name, "unsupported entry type");
        return -1;
    }

    mkdir_parents(dest, 0);
    fd = io_open_write(dest);
    if (fd < 0) {
        err_path(dest, "cannot create");
        return -1;
    }
    left = h->size;
    while (left > 0) {
        unsigned want = (left > (unsigned long)TAR_CHUNK)
                        ? (unsigned)TAR_CHUNK : (unsigned)left;
        int err = mtar_read_data(&g_tar, g_chunk, want);
        int n;
        if (err != MTAR_ESUCCESS) {
            io_close(fd);
            err_path(name, mtar_strerror(err));
            return -1;
        }
        n = io_write(fd, g_chunk, want);
        if (n < 0 || (unsigned)n != want) {
            io_close(fd);
            err_path(dest, "write error");
            return -1;
        }
        left -= want;
    }
    io_close(fd);
    return 0;
}

static int walk_archive(const char *archive, const char *destdir, int list_only)
{
    mtar_header_t h;
    int err;
    int rc = TAR_EXIT_OK;

    if (open_archive_for_read(archive, &g_tar) != 0) return TAR_EXIT_FAIL;

    for (;;) {
        err = mtar_read_header(&g_tar, &h);
        if (err == MTAR_ENULLRECORD) break;
        if (err != MTAR_ESUCCESS) {
            err_path(archive, mtar_strerror(err));
            rc = TAR_EXIT_FAIL;
            break;
        }
        if (list_only) {
            printf("%s\n", h.name);
        } else if (extract_one(&h, destdir) != 0) {
            rc = TAR_EXIT_FAIL;
        }
        err = mtar_next(&g_tar);
        if (err != MTAR_ESUCCESS) {
            if (err != MTAR_ENULLRECORD) {
                err_path(archive, mtar_strerror(err));
                rc = TAR_EXIT_FAIL;
            }
            break;
        }
    }

    mtar_close(&g_tar);
    return rc;
}

static int cmd_extract(const char *archive, const char *destdir)
{
    return walk_archive(archive, destdir, 0);
}

static int cmd_list(const char *archive)
{
    return walk_archive(archive, ".", 1);
}
