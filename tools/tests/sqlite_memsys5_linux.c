/* Only host stdout/stderr and process exit have real backing.
 * Newlib sbrk fails: it cannot supply SQLite allocations.
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int write(int fd, const void *bytes, size_t count)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(4), "b"(fd), "c"(bytes), "d"(count) : "memory");
    return rc;
}

void *sbrk(ptrdiff_t increment)
{
    (void)increment;
    errno = ENOMEM;
    return (void *)-1;
}
int close(int fd) { (void)fd; return -1; }
int read(int fd, void *p, size_t n) { (void)fd; (void)p; (void)n; return -1; }
off_t lseek(int fd, off_t off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int fstat(int fd, struct stat *s)
{
    (void)fd;
    s->st_mode = S_IFCHR;
    return 0;
}
int isatty(int fd) { return fd == 1 || fd == 2; }
int getpid(void) { return 1; }
int kill(int pid, int sig) { (void)pid; (void)sig; _exit(2); }

void _exit(int code) __attribute__((noreturn));
void _exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
