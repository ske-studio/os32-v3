/* Expectation origin: PPC_FP_NEWLIB_DESIGN §8 N2・N3.
 * Linux/PPC syscall transport only; allocator and formatter are real newlib. */
#include "os32api.h"
#include "nano_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static KernelAPI mock;
KernelAPI *kapi = &mock;
static unsigned char heap[262144] __attribute__((aligned(4096)));
static struct os32_nano_arena arena;

static int linux_call(int number, int a, void *b, int c)
{
    register int r0 __asm__("r0") = number;
    register int r3 __asm__("r3") = a;
    register void *r4 __asm__("r4") = b;
    register int r5 __asm__("r5") = c;
    __asm__ volatile("sc" : "+r"(r3) : "r"(r0), "r"(r4), "r"(r5) : "memory", "cr0");
    return r3;
}
void _exit(int code) { linux_call(1, code, NULL, 0); for (;;) {} }
int _write(int fd, const void *p, int size) { return linux_call(4, fd, (void *)p, size); }
int _read(int fd, void *p, int size) { (void)fd; (void)p; (void)size; return 0; }
int _close(int fd) { (void)fd; return 0; }
int _fstat(int fd, struct stat *s) { (void)fd; s->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void *_sbrk(ptrdiff_t size) { return os32_nano_sbrk(_impure_ptr, size); }
void os32_nano_prefix_check(const void *p) { (void)p; }

void _start(void)
{
    unsigned char *p, *q;
    arena.initial = arena.brk = (uintptr_t)heap;
    arena.mapped_end = arena.limit = (uintptr_t)(heap + sizeof heap);
    if (!os32_nano_select(&arena)) _exit(20);
    p = malloc(65535);
    if (!p || ((uintptr_t)p & 7)) _exit(21);
    for (unsigned i = 0; i < 65535; ++i) p[i] = (unsigned char)i;
    q = realloc(p, 65534);
    if (!q) _exit(22);
    for (unsigned i = 0; i < 65534; ++i) if (q[i] != (unsigned char)i) _exit(23);
    free(q);
    p = calloc(17, 13);
    if (!p) _exit(24);
    for (unsigned i = 0; i < 221; ++i) if (p[i]) _exit(25);
    free(p);
    errno = 0;
    volatile size_t impossible = (size_t)-4096;
    if (malloc(impossible) || errno != ENOMEM) _exit(26);
    printf("N3-float=%.2f,%.2f,%.2f\n", 1.25, -2.5, 3.75);
    fflush(NULL);
    _exit(0);
}
#define ALIAS(f) __attribute__((alias(#f)))
int write(int, const void *, int) ALIAS(_write);
int read(int, void *, int) ALIAS(_read);
int close(int) ALIAS(_close);
int fstat(int, struct stat *) ALIAS(_fstat);
int isatty(int) ALIAS(_isatty);
int lseek(int, int, int) ALIAS(_lseek);
int getpid(void) ALIAS(_getpid);
int kill(int, int) ALIAS(_kill);
void *sbrk(ptrdiff_t) ALIAS(_sbrk);
