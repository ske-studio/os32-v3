/* ========================================================================
 *  newlib_errno_host.c — sdk/crt/syscalls.c が OS32 の負値を -1 + errno に
 *  直すこと (票 TASK_VFS_FD_PATH 方針 v3 の 7)
 *
 *  実行: python3 -B tools/tests/test_vfs_fd_path.py errno
 *  記録: tools/tests/vfs_fd_path_tdd.md
 *
 *  実物の syscalls.c を**newlib のヘッダ** (クロス環境の i386-elf/include) で
 *  ホストの gcc -m32 に組ませ、この足場と一緒に -nostdlib でつなぐ。errno は
 *  newlib の形 (*__errno()) なので __errno をここで持つ。KAPI は贋物で、
 *  sys_* が返す値を表から選ぶ。
 * ======================================================================== */

#include <errno.h>
#include <sys/stat.h>
/* syscalls.c と同じ: newlib の st_atime マクロが OS32_Stat の欄名とぶつかる */
#undef st_atime
#undef st_mtime
#undef st_ctime
#include "os32api.h"

static int h_sys3(int nr, long a, long b, long c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return r;
}
static unsigned h_strlen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void report(const char *t) { (void)h_sys3(4, 1, (long)t, (long)h_strlen(t)); }
static void report_i(int v)
{
    char buf[16];
    int i = 15;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) buf[--i] = '-';
    report(&buf[i]);
}

static int g_errno;
int *__errno(void) { return &g_errno; }
void *memset(void *d, int c, unsigned n)
{ unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }

KernelAPI *kapi;
static KernelAPI g_fake;
static int g_ret;          /* 贋の sys_* が返す値 */

static int __cdecl f_read(int fd, void *b, u32 n) { (void)fd; (void)b; (void)n; return g_ret; }
static int __cdecl f_write(int fd, const void *b, u32 n) { (void)fd; (void)b; (void)n; return g_ret; }
static int __cdecl f_open(const char *p, int m) { (void)p; (void)m; return g_ret; }
static int __cdecl f_lseek(int fd, int o, int w) { (void)fd; (void)o; (void)w; return g_ret; }
static int __cdecl f_fstat(int fd, OS32_Stat *st)
{ (void)fd; if (g_ret >= 0) { memset(st, 0, sizeof(*st)); st->st_size = 7; } return g_ret; }
static int __cdecl f_stat(const char *p, OS32_Stat *st)
{ (void)p; if (g_ret >= 0) { memset(st, 0, sizeof(*st)); st->st_size = 9; } return g_ret; }
static int __cdecl f_unlink(const char *p) { (void)p; return g_ret; }
static void __cdecl f_close(int fd) { (void)fd; }

int _read(int fd, char *ptr, int len);
int _write(int fd, char *ptr, int len);
int _open(const char *name, int flags, ...);
int _close(int fd);
int _lseek(int fd, int ptr, int dir);
int _fstat(int fd, struct stat *st);
int _stat(const char *name, struct stat *st);
int _unlink(char *name);

static int g_checks, g_failures;
static void check_at(int cond, const char *what, int line)
{
    g_checks++;
    if (cond) return;
    g_failures++;
    report("  FAIL line "); report_i(line); report(": "); report(what); report("\n");
}
#define CHECK(x) check_at((x) ? 1 : 0, #x, __LINE__)

/* 負値 rc が全関数で -1 + want になる */
static void one(int rc, int want)
{
    struct stat st;
    char b[4];
    int f0 = g_failures;
    g_ret = rc;
    g_errno = 0; CHECK(_read(3, b, 4) == -1 && g_errno == want);
    g_errno = 0; CHECK(_write(3, b, 4) == -1 && g_errno == want);
    g_errno = 0; CHECK(_open("/x", 0) == -1 && g_errno == want);
    g_errno = 0; CHECK(_lseek(3, 0, 0) == -1 && g_errno == want);
    g_errno = 0; CHECK(_fstat(3, &st) == -1 && g_errno == want);
    g_errno = 0; CHECK(_stat("/x", &st) == -1 && g_errno == want);
    g_errno = 0; CHECK(_unlink((char *)"/x") == -1 && g_errno == want);
    if (g_failures != f0) { report("    ^ rc="); report_i(rc); report("\n"); }
}

static void run(void)
{
    struct stat st;
    char b[4];

    g_fake.sys_read = f_read;
    g_fake.sys_write = f_write;
    g_fake.sys_open = f_open;
    g_fake.sys_lseek = f_lseek;
    g_fake.sys_fstat = f_fstat;
    g_fake.sys_stat = f_stat;
    g_fake.sys_unlink = f_unlink;
    g_fake.sys_close = f_close;
    kapi = &g_fake;

    one(OS32_ERR_STALE, ESTALE);
    one(OS32_ERR_NAMETOOLONG, ENAMETOOLONG);
    one(OS32_ERR_ISDIR, EISDIR);
    one(OS32_ERR_BUSY, EBUSY);
    one(OS32_ERR_NOTFOUND, ENOENT);
    one(OS32_ERR_EXIST, EEXIST);
    one(OS32_ERR_NOSPC, ENOSPC);
    one(OS32_ERR_IO, EIO);
    one(OS32_ERR_ROFS, EROFS);
    one(-99, EIO);                                  /* 対応の無いものは EIO */

    /* 非負はそのまま、errno は触らない */
    g_ret = 3; g_errno = 1234;
    CHECK(_read(3, b, 4) == 3 && g_errno == 1234);
    CHECK(_write(3, b, 4) == 3 && g_errno == 1234);
    CHECK(_open("/x", 0) == 3 && g_errno == 1234);
    CHECK(_lseek(3, 0, 0) == 3 && g_errno == 1234);
    g_ret = 0;
    CHECK(_fstat(3, &st) == 0 && st.st_size == 7);
    CHECK(_stat("/x", &st) == 0 && st.st_size == 9);
    CHECK(_unlink((char *)"/x") == 0 && g_errno == 1234);
    CHECK(_close(3) == 0);                          /* _close は int のまま */

    report("checks "); report_i(g_checks);
    report(" failures "); report_i(g_failures); report("\n");
}

void errno_start_c(void);
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  andl $-16, %esp\n"
        "  call errno_start_c\n"
        "  hlt\n");

void errno_start_c(void)
{
    run();
    (void)h_sys3(1, g_failures ? 1 : 0, 0, 0);
    for (;;) { }
}
