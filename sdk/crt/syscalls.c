#include <sys/stat.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <errno.h>

#undef st_atime
#undef st_mtime
#undef st_ctime

#include "os32api.h"

extern KernelAPI *kapi; /* CRT0 もしくはメインから初期化されて渡される */

#define ALIAS(f) __attribute__((weak, alias(#f)))

void __attribute__((weak)) _init(void) {}

/* OS32 の負のエラーコード (OS32_ERR_*) を newlib の -1 + errno に直す
 * (票 TASK_VFS_FD_PATH 方針 v3 の 7)。以前は負値をそのまま返していたので、
 * newlib (stdio) は -11 を「-11 バイト書けた」などと読み、errno も立たなかった。
 * 対応の無いものは EIO。非負はそのまま返す。 */
static int os32_errno(int rc)
{
    switch (rc) {
    case OS32_ERR_NOTFOUND:    return ENOENT;
    case OS32_ERR_EXIST:       return EEXIST;
    case OS32_ERR_NOSPC:       return ENOSPC;
    case OS32_ERR_NOTDIR:      return ENOTDIR;
    case OS32_ERR_NOTEMPTY:    return ENOTEMPTY;
    case OS32_ERR_ISDIR:       return EISDIR;
    case OS32_ERR_INVAL:       return EINVAL;
    case OS32_ERR_NOSYS:       return ENOSYS;
    case OS32_ERR_STALE:       return ESTALE;
    case OS32_ERR_ROFS:        return EROFS;
    case OS32_ERR_NAMETOOLONG: return ENAMETOOLONG;
    case OS32_ERR_BUSY:        return EBUSY;
    default:                   return EIO;
    }
}

static int os32_ret(int rc)
{
    if (rc >= 0) return rc;
    errno = os32_errno(rc);
    return -1;
}

int _read(int fd, char *ptr, int len) {
    return os32_ret(kapi->sys_read(fd, ptr, len));
}
int read(int fd, char *ptr, int len) ALIAS(_read);

int _write(int fd, char *ptr, int len) {
    return os32_ret(kapi->sys_write(fd, ptr, len));
}
int write(int fd, char *ptr, int len) ALIAS(_write);

int _open(const char *name, int flags, ...) {
    int os32_flags = 0;
    if (flags & O_WRONLY) os32_flags |= KAPI_O_WRONLY;
    else if (flags & O_RDWR) os32_flags |= KAPI_O_RDWR;
    else os32_flags |= KAPI_O_RDONLY;
    
    if (flags & O_CREAT) os32_flags |= KAPI_O_CREAT;
    if (flags & O_TRUNC) os32_flags |= KAPI_O_TRUNC;
    
    return os32_ret(kapi->sys_open(name, os32_flags));
}
int open(const char *name, int flags, ...) ALIAS(_open);

int _close(int fd) {
    kapi->sys_close(fd);
    return 0;
}
int close(int fd) ALIAS(_close);

int _lseek(int fd, int ptr, int dir) {
    return os32_ret(kapi->sys_lseek(fd, ptr, dir));
}
int lseek(int fd, int ptr, int dir) ALIAS(_lseek);

int _fstat(int fd, struct stat *st) {
    OS32_Stat os_st;
    int rc = kapi->sys_fstat(fd, &os_st);
    if (rc < 0) return os32_ret(rc);
    st->st_mode = os_st.st_mode;
    st->st_size = os_st.st_size;
    return 0;
}
int fstat(int fd, struct stat *st) ALIAS(_fstat);

int _stat(const char *name, struct stat *st) {
    OS32_Stat os_st;
    int rc = kapi->sys_stat(name, &os_st);
    if (rc < 0) return os32_ret(rc);
    st->st_mode = os_st.st_mode;
    st->st_size = os_st.st_size;
    return 0;
}
int stat_func(const char *name, struct stat *st) __attribute__((weak, alias("_stat")));

int _unlink(char *name) {
    return os32_ret(kapi->sys_unlink(name));
}
int unlink(char *name) ALIAS(_unlink);

int _isatty(int fd) {
    return kapi->sys_isatty(fd);
}
int isatty(int fd) ALIAS(_isatty);

void _exit(int exit_status) {
    kapi->sys_exit(exit_status);
    while (1) {}
}

int _kill(int pid, int sig) {
    errno = EINVAL;
    return -1;
}
int kill(int pid, int sig) ALIAS(_kill);

int _getpid(void) {
    return 1;
}
int getpid(void) ALIAS(_getpid);

int _gettimeofday(struct timeval *tv, void *tz) {
    if (tv) {
        tv->tv_sec = kapi->sys_time();
        tv->tv_usec = 0;
    }
    return 0;
}
int gettimeofday(struct timeval *tv, void *tz) ALIAS(_gettimeofday);

void *__env[1] = { 0 };
char **environ = (char **)__env;

int _link(char *old, char *new) {
    errno = EMLINK;
    return -1;
}
int link(char *old, char *new) ALIAS(_link);

int _execve(char *name, char **argv, char **env) {
    errno = ENOMEM;
    return -1;
}
int execve(char *name, char **argv, char **env) ALIAS(_execve);

clock_t _times(struct tms *buf) {
    return -1;
}
clock_t times(struct tms *buf) ALIAS(_times);

/* Embed the adapter's morecore only: standalone SDK clients already link
 * syscalls.o, while nano malloc/free routing belongs to f7. */
#define OS32_NANO_MORECORE_ONLY
#include "../allocator/nano_adapter.c"

#define CRT_CHUNK_ALIGN 4u
extern char _end[];
static struct os32_nano_arena primary_arena;
static int primary_initialized;

#ifndef OS32_CRT_RESIDENT
static int crt_grow_exact(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    if (!kapi->mem_map) return -1;
    return kapi->mem_map(bytes, (void *)base, OS32_MEM_MAP_EXACT) == (void *)base ? 0 : -1;
}
#endif

void *_sbrk(int incr)
{
    if (!primary_initialized) {
        uintptr_t initial = (uintptr_t)&_end;
        if (initial > UINTPTR_MAX - (CRT_CHUNK_ALIGN - 1u)) {
            errno = ENOMEM;
            return (void *)-1;
        }
        initial = (initial + CRT_CHUNK_ALIGN - 1u) & ~(CRT_CHUNK_ALIGN - 1u);
        primary_arena.initial = initial;
        primary_arena.brk = initial;
        primary_arena.mapped_end = kapi->sbrk_heap_limit;
#ifdef OS32_CRT_RESIDENT
        primary_arena.limit = kapi->sbrk_heap_limit;
        primary_arena.grow_exact = NULL;
#else
        /* The kernel enforces the available VA ranges. This KAPI field is
         * only the initial mapped end, never the growing USER heap ceiling. */
        primary_arena.limit = UINTPTR_MAX;
        primary_arena.grow_exact = crt_grow_exact;
#endif
        primary_initialized = 1;
    }
    return os32_nano_morecore(&primary_arena, _impure_ptr, incr);
}
void *sbrk(int incr) ALIAS(_sbrk);

/* memcpy/memset は newlib (libc.a) の実装をそのまま使用する。
 * 全外部プログラムは -lc でリンクされるため、カーネルへの迂回は不要。 */

/* Common USER helpers travel in the existing CRT object, including in the
 * standalone SDK (whose applications already link syscalls.o). */
#include "../../userland/lib/rt/ls.c"
