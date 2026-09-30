/* =========================================================================
 *  FS_KIND_FAKE.H — シェルの種別判定の試験が共有する贋ファイルシステム
 *
 *  使うのは fs_kind_host.c (B5: 列挙の失敗を「ディレクトリでない」にしない) と
 *  fs_kind_callers_host.c (TASK_FS_TYPE §3: 呼び出し元が「不明」を断る)。
 *  どちらも実物の userland/shell/cmd_fs_shared.c と cmd_file.c をこの後で
 *  #include する。ここに置くのは KernelAPI の贋物だけで、シェルのコードは
 *  1 行も写さない。
 *
 *  注入できるもの (ノードごと):
 *    stat_err … sys_stat がこの値を返す
 *    ls_err   … sys_ls がこの値を返す (ディレクトリのときだけ)
 *  全体:
 *    fsk_rename_err … sys_rename がこの値を返す (FS をまたぐ = OS32_ERR_INVAL)
 *    fsk_mkdir_err  … sys_mkdir がこの値を返す (作れない宛先の再現)
 *  呼び出し回数 (受け手が副作用を起こしたかを見る):
 *    fsk_mkdir_calls / fsk_open_calls / fsk_unlink_calls / fsk_rename_calls /
 *    fsk_write_calls
 * ========================================================================= */
#ifndef FS_KIND_FAKE_H
#define FS_KIND_FAKE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ------------------------------------------------------------------------ */
/*  贋ファイルシステム                                                        */
/* ------------------------------------------------------------------------ */

/* MAX_COPY_ENTRIES (64) を超えるディレクトリを作れる大きさにしておく */
#define FSK_MAX_NODES 96
#define FSK_MAX_FDS   8
#define FSK_PATH_CAP  256

typedef struct {
    int  used;
    char path[FSK_PATH_CAP];
    int  is_dir;
    char data[64];
    u32  size;
    int  stat_err;    /* != 0 … sys_stat がこの値を返す */
    int  ls_err;      /* != 0 … sys_ls がこの値を返す (列挙の失敗を注入) */
} FskNode;

typedef struct { int used; int node; u32 pos; } FskFd;

static FskNode fsk[FSK_MAX_NODES];
static FskFd   fsk_fds[FSK_MAX_FDS];
static int     fsk_mkdir_calls;
static int     fsk_open_calls;
static int     fsk_unlink_calls;
static int     fsk_rename_calls;
static int     fsk_write_calls;
static int     fsk_rename_err;   /* != 0 … sys_rename がこの値を返す */
static int     fsk_mkdir_err;    /* != 0 … sys_mkdir がこの値を返す */
static char    fsk_log[16384];
static u32     fsk_log_len;

static void fsk_reset(void)
{
    memset(fsk, 0, sizeof(fsk));
    memset(fsk_fds, 0, sizeof(fsk_fds));
    fsk_mkdir_calls = 0;
    fsk_open_calls = 0;
    fsk_unlink_calls = 0;
    fsk_rename_calls = 0;
    fsk_write_calls = 0;
    fsk_rename_err = 0;
    fsk_mkdir_err = 0;
    fsk_log_len = 0;
    fsk_log[0] = '\0';
}

static int fsk_find(const char *path)
{
    int i;
    for (i = 0; i < FSK_MAX_NODES; i++)
        if (fsk[i].used && strcmp(fsk[i].path, path) == 0) return i;
    return -1;
}

static int fsk_add(const char *path, int is_dir, const char *data)
{
    int i;
    for (i = 0; i < FSK_MAX_NODES; i++) {
        if (fsk[i].used) continue;
        memset(&fsk[i], 0, sizeof(FskNode));
        fsk[i].used = 1;
        fsk[i].is_dir = is_dir;
        strncpy(fsk[i].path, path, FSK_PATH_CAP - 1);
        if (data) {
            strncpy(fsk[i].data, data, sizeof(fsk[i].data) - 1);
            fsk[i].size = (u32)strlen(fsk[i].data);
        }
        return i;
    }
    printf("  (harness) node table full\n");
    exit(2);
}

static void fsk_parent(const char *path, char *out)
{
    int last = -1;
    int i;
    for (i = 0; path[i]; i++) if (path[i] == '/') last = i;
    if (last <= 0) { strcpy(out, "/"); return; }
    memcpy(out, path, (size_t)last);
    out[last] = '\0';
}

static const char *fsk_base(const char *path)
{
    const char *b = path;
    const char *p;
    for (p = path; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

/* ------------------------------------------------------------------------ */
/*  贋 KernelAPI                                                              */
/* ------------------------------------------------------------------------ */

static void *fk_mem_alloc(u32 n) { return malloc(n); }
static void  fk_mem_free(void *p) { free(p); }

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    char line[1024];
    int n;
    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((u32)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    if (fsk_log_len + (u32)n + 1 < sizeof(fsk_log)) {
        memcpy(fsk_log + fsk_log_len, line, (size_t)n);
        fsk_log_len += (u32)n;
        fsk_log[fsk_log_len] = '\0';
    }
}

static int fk_sys_stat(const char *path, OS32_Stat *buf)
{
    int n = fsk_find(path);
    memset(buf, 0, sizeof(OS32_Stat));
    if (n < 0) return OS32_ERR_NOTFOUND;
    if (fsk[n].stat_err) return fsk[n].stat_err;
    buf->st_dev = 1;
    buf->st_ino = (u32)(n + 100);
    buf->st_nlink = 1;
    buf->st_mode = (u16)(fsk[n].is_dir ? (OS_S_IFDIR | 0755)
                                       : (OS_S_IFREG | 0644));
    buf->st_size = fsk[n].size;
    return 0;
}

static int fk_sys_ls(const char *path, void *cb, void *ctx)
{
    DirCallback fn = (DirCallback)cb;
    char dir[FSK_PATH_CAP];
    char parent[FSK_PATH_CAP];
    DirEntry_Ext e;
    int i, n;

    strncpy(dir, path && path[0] ? path : "/", FSK_PATH_CAP - 1);
    dir[FSK_PATH_CAP - 1] = '\0';
    n = (int)strlen(dir);
    while (n > 1 && dir[n - 1] == '/') dir[--n] = '\0';

    i = fsk_find(dir);
    if (strcmp(dir, "/") != 0) {
        if (i < 0) return OS32_ERR_NOTFOUND;
        if (!fsk[i].is_dir) return OS32_ERR_NOTDIR;
        /* 注入: 1000 件超 (FULL) / 途中で切れた列挙 (IO) */
        if (fsk[i].ls_err) return fsk[i].ls_err;
    }

    for (i = 0; i < FSK_MAX_NODES; i++) {
        if (!fsk[i].used) continue;
        fsk_parent(fsk[i].path, parent);
        if (strcmp(parent, dir) != 0) continue;
        memset(&e, 0, sizeof(e));
        strncpy(e.name, fsk_base(fsk[i].path), OS32_MAX_PATH - 1);
        e.size = fsk[i].size;
        e.type = fsk[i].is_dir ? OS32_FILE_TYPE_DIR : OS32_FILE_TYPE_FILE;
        fn(&e, ctx);
    }
    return 0;
}

static int fk_sys_mkdir(const char *path)
{
    fsk_mkdir_calls++;
    if (fsk_mkdir_err) return fsk_mkdir_err;
    if (fsk_find(path) >= 0) return OS32_ERR_EXIST;
    fsk_add(path, 1, 0);
    return 0;
}

static int fk_sys_open(const char *path, int mode)
{
    int n = fsk_find(path);
    int f;
    fsk_open_calls++;
    if (n < 0) {
        if (!(mode & KAPI_O_CREAT)) return OS32_ERR_NOTFOUND;
        {   /* 親がディレクトリでなければ作れない (実 FS と同じ) */
            char parent[FSK_PATH_CAP];
            int pi;
            fsk_parent(path, parent);
            pi = fsk_find(parent);
            if (strcmp(parent, "/") != 0 && (pi < 0 || !fsk[pi].is_dir))
                return OS32_ERR_NOTDIR;
        }
        n = fsk_add(path, 0, 0);
    } else if (fsk[n].is_dir) {
        return OS32_ERR_ISDIR;
    }
    if (mode & KAPI_O_TRUNC) { fsk[n].size = 0; fsk[n].data[0] = '\0'; }
    for (f = 0; f < FSK_MAX_FDS; f++) {
        if (fsk_fds[f].used) continue;
        fsk_fds[f].used = 1;
        fsk_fds[f].node = n;
        fsk_fds[f].pos = 0;
        return f + 3;
    }
    return OS32_ERR_NOSPC;
}

static void fk_sys_close(int fd)
{
    if (fd >= 3 && fd - 3 < FSK_MAX_FDS) fsk_fds[fd - 3].used = 0;
}

static int fk_sys_read(int fd, void *buf, u32 size)
{
    FskFd *h;
    FskNode *nd;
    u32 avail;
    if (fd < 3 || fd - 3 >= FSK_MAX_FDS || !fsk_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fsk_fds[fd - 3];
    nd = &fsk[h->node];
    if (h->pos >= nd->size) return 0;
    avail = nd->size - h->pos;
    if (avail > size) avail = size;
    memcpy(buf, nd->data + h->pos, avail);
    h->pos += avail;
    return (int)avail;
}

static int fk_sys_write(int fd, const void *buf, u32 size)
{
    FskFd *h;
    FskNode *nd;
    fsk_write_calls++;
    if (fd < 3 || fd - 3 >= FSK_MAX_FDS || !fsk_fds[fd - 3].used)
        return OS32_ERR_IO;
    h = &fsk_fds[fd - 3];
    nd = &fsk[h->node];
    if (h->pos + size >= sizeof(nd->data)) return OS32_ERR_NOSPC;
    memcpy(nd->data + h->pos, buf, size);
    h->pos += size;
    if (h->pos > nd->size) nd->size = h->pos;
    nd->data[nd->size] = '\0';
    return (int)size;
}

static int fk_sys_unlink(const char *path)
{
    int n = fsk_find(path);
    fsk_unlink_calls++;
    if (n < 0) return OS32_ERR_NOTFOUND;
    fsk[n].used = 0;
    return 0;
}

static int fk_sys_rmdir(const char *path) { return fk_sys_unlink(path); }
static int fk_sys_rename(const char *a, const char *b)
{
    int n = fsk_find(a);
    fsk_rename_calls++;
    if (fsk_rename_err) return fsk_rename_err;
    if (n < 0) return OS32_ERR_NOTFOUND;
    strncpy(fsk[n].path, b, FSK_PATH_CAP - 1);
    return 0;
}
static const char *fk_sys_getcwd(void) { return "/"; }
static int fk_sys_isatty(int fd) { (void)fd; return 0; }

static KernelAPI g_fake;
KernelAPI *g_api = &g_fake;

static void fake_api_init(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.mem_alloc = fk_mem_alloc;
    g_fake.mem_free = fk_mem_free;
    g_fake.kprintf = fk_kprintf;
    g_fake.sys_stat = fk_sys_stat;
    g_fake.sys_ls = fk_sys_ls;
    g_fake.sys_mkdir = fk_sys_mkdir;
    g_fake.sys_open = fk_sys_open;
    g_fake.sys_close = fk_sys_close;
    g_fake.sys_read = fk_sys_read;
    g_fake.sys_write = fk_sys_write;
    g_fake.sys_unlink = fk_sys_unlink;
    g_fake.sys_rmdir = fk_sys_rmdir;
    g_fake.sys_rename = fk_sys_rename;
    g_fake.sys_getcwd = fk_sys_getcwd;
    g_fake.sys_isatty = fk_sys_isatty;
}

#endif /* FS_KIND_FAKE_H */
