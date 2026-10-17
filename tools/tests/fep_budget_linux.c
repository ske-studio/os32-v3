/* ILP32 Linux boundary for the freestanding/newlib FEP memory fixture.
 * Not a USER allocator: only stdio/runtime support uses this host heap.
 * SQLite allocations exclusively use the separate 512KiB MEMSYS5 pool.
 */
#include <sys/stat.h>
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>

static union { double align; char bytes[1024 * 1024]; } host_heap;
static size_t host_break;
static int call3(int op, int a, int b, int c)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc) : "0"(op), "b"(a), "c"(b), "d"(c) : "memory");
    if (rc < 0) { errno = -rc; return -1; }
    return rc;
}
void *sbrk(ptrdiff_t n)
{
    void *old = host_heap.bytes + host_break;
    if (n < 0 || (size_t)n > sizeof(host_heap.bytes) - host_break) {
        errno = ENOMEM; return (void *)-1;
    }
    host_break += (size_t)n;
    return old;
}
int open(const char *path, int flags, int mode)
{
    /* Input files are read-only; guest writes go only to the RAM VFS. */
    (void)flags; (void)mode;
    return call3(5, (int)path, 0, 0);
}
int close(int fd) { return call3(6, fd, 0, 0); }
int read(int fd, void *p, unsigned n) { return call3(3, fd, (int)p, n); }
int write(int fd, const void *p, unsigned n) { return call3(4, fd, (int)p, n); }
long lseek(int fd, long offset, int whence) { return call3(19, fd, offset, whence); }
int isatty(int fd) { return fd <= 2; }
int fstat(int fd, struct stat *out)
{
    out->st_mode = fd <= 2 ? S_IFCHR : S_IFREG;
    out->st_blksize = 1024;
    return 0;
}
int getpid(void) { return call3(20, 0, 0, 0); }
int kill(int pid, int sig) { return call3(37, pid, sig, 0); }
void _exit(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
extern int main(int argc, char **argv);
void fixture_start(int *stack)
{ exit(main(stack[0], (char **)(stack + 1))); }
__asm__(".global _start\n_start:\nmov %esp,%eax\npush %eax\ncall fixture_start\n");
