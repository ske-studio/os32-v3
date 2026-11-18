/* Linux user-mode transport for the real OS32 C startup and newlib. */
#include "os32api.h"
#ifndef CRT_ONLY
#include <stdio.h>
#include <stdlib.h>
#endif

#if defined(__powerpc__) || defined(__PPC__)
#define start_c os32_start_c
#else
#define start_c _start_c
#endif

extern void start_c(int, char **, KernelAPI *, void *);

static int linux_call(int number, int fd, const void *buf, unsigned size)
{
#if defined(__powerpc__) || defined(__PPC__)
    register int r0 __asm__("r0") = number;
    register int r3 __asm__("r3") = fd;
    register const void *r4 __asm__("r4") = buf;
    register unsigned r5 __asm__("r5") = size;
    __asm__ volatile("sc" : "+r"(r3) : "r"(r0), "r"(r4), "r"(r5) : "memory", "cr0");
    return r3;
#else
    int result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(number), "b"(fd), "c"(buf), "d"(size) : "memory");
    return result;
#endif
}

static void finish(int code)
{
    linux_call(1, code, 0, 0);
    for (;;) {}
}

#ifndef CRT_ONLY
static char buffer[BUFSIZ];
static void emit_b(void) { putchar('B'); }
static void emit_c(void) { putchar('C'); }

static void prepare_output(void)
{
    if (setvbuf(stdout, buffer, _IOFBF, sizeof buffer) ||
        putchar('A') != 'A' || atexit(emit_b) || atexit(emit_c))
        finish(90);
}

static int write_output(int fd, const void *buf, u32 size)
{
    return linux_call(4, fd, buf, size);
}

static void close_output(int fd)
{
    (void)fd;
}

static int is_terminal(int fd)
{
    (void)fd;
    return 0;
}

static int file_stat(int fd, OS32_Stat *st)
{
    (void)fd;
    st->st_mode = 0100000;
    st->st_size = 0;
    return 0;
}
#endif

void _init(void) {}

int os32_help_show(const char *name)
{
    if (name[0] != 'h') return -1;
#ifndef CRT_ONLY
    prepare_output();
#endif
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)api;
    if (argv[0][0] == 'h') return 91;
#ifndef CRT_ONLY
    prepare_output();
#endif
    return 37;
}

void _start(void)
{
    static KernelAPI api = {
        .sys_exit = finish,
#ifndef CRT_ONLY
        .sys_write = write_output,
        .sys_close = close_output,
        .sys_isatty = is_terminal,
        .sys_fstat = file_stat,
#endif
    };
#if defined(HELP_SUCCESS)
    char *args[] = {"help", "--help"};
#elif defined(HELP_FAILURE)
    char *args[] = {"missing", "--help"};
#else
    char *args[] = {"return", "normal"};
#endif
    start_c(2, args, &api, 0);
}
