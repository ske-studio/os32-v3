/* Expectation origin: PPC_FP_NEWLIB_DESIGN §4 N3・§8 / astra レビュー 2026-11-18.
 * heap の回収は DEFERRED PPC-NEWLIB-HEAP-RECLAIM.
 * Acceptance-only RAM-root file; run twice to observe owner/FD reuse. */
#include "os32api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

extern char __bss_start[], __bss_end[];
static volatile unsigned initialized;
static volatile unsigned zero __attribute__((section(".sbss")));
static volatile unsigned zero2 __attribute__((section(".sbss2")));
static volatile unsigned data __attribute__((section(".sdata"))) = 0x12345678;
void _init(void) { initialized = 73; }

static int heap_probe(void)
{
    unsigned char *p = malloc(65535), *q;
    if (!p || ((uintptr_t)p & 7)) return 21;
    for (unsigned i = 0; i < 65535; ++i) p[i] = (unsigned char)i;
    q = realloc(p, 65537);
    if (!q) { free(p); return 22; }
    for (unsigned i = 0; i < 65535; ++i)
        if (q[i] != (unsigned char)i) { free(q); return 23; }
    q[65536] = 91;
    p = realloc(q, 31);
    if (!p) { free(q); return 24; }
    for (unsigned i = 0; i < 31; ++i)
        if (p[i] != (unsigned char)i) { free(p); return 25; }
    free(p);
    p = calloc(17, 13);
    if (!p) return 26;
    for (unsigned i = 0; i < 221; ++i)
        if (p[i]) { free(p); return 27; }
    free(p);
    volatile size_t impossible = (size_t)-1 - 4095;
    errno = 0;
    p = malloc(impossible);
    if (p) { free(p); return 28; }
    return errno == ENOMEM ? 0 : 29;
}

static int file_probe(void)
{
    const char path[] = "/tmp/newlib-probe.tmp";
    const char payload[] = "newlib RAM file\n";
    char readback[sizeof payload];
    FILE *f = fopen(path, "wb");
    if (!f) return 31;
    if (fwrite(payload, 1, sizeof payload, f) != sizeof payload) {
        fclose(f); return 32;
    }
    if (fclose(f)) return 33;
    f = fopen(path, "rb");
    if (!f) return 34;
    if (fread(readback, 1, sizeof readback, f) != sizeof readback ||
        memcmp(payload, readback, sizeof payload) || fgetc(f) != EOF || !feof(f)) {
        fclose(f); return 35;
    }
    if (fclose(f)) return 36;
    errno = 0;
    f = fopen("/tmp/newlib-probe-missing/no-file", "rb");
    if (f) { fclose(f); return 37; }
    return errno == ENOENT ? 0 : 38;
}

static char exit_buffer[BUFSIZ];
static void exit_b(void) { putchar('B'); }
static void exit_c(void) { putchar('C'); }

static int fds[256];
static int resources(KernelAPI *api, int retain, int fault)
{
    unsigned nf = 0;
    unsigned char *held = malloc(65536);
    if (!held) return 44;
    for (unsigned i = 0; i < 65536; i += 4096) held[i] = 1;
    while (nf < sizeof fds / sizeof fds[0]) {
        int fd = api->sys_open("/tmp/newlib-probe.tmp", 0);
        if (fd < 0) break;
        fds[nf++] = fd;
    }
    if (!nf || nf == sizeof fds / sizeof fds[0]) return 44;
    printf("N3-resources fd=%u\n", nf);
    fflush(NULL);
    if (fault) __asm__ volatile("trap" : : : "memory");
    if (!retain) {
        free(held);
        while (nf) api->sys_close(fds[--nf]);
    }
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int rc;
    if (argc > 1 && !strcmp(argv[1], "crt-return")) {
        setvbuf(stdout, exit_buffer, _IOFBF, sizeof(exit_buffer));
        putchar('A');
        atexit(exit_b);
        atexit(exit_c);
        return 37;
    }
    /* Multiple FP arguments exercise CR1/varargs stfd before any syscall. */
    printf("N3-float=%.2f,%.2f,%.2f\n", 1.25, -2.5, 3.75);
    if (initialized != 73) rc = 11;
    else if (zero || zero2 || data != 0x12345678 ||
        (uintptr_t)&zero < (uintptr_t)__bss_start ||
        (uintptr_t)&zero + sizeof zero > (uintptr_t)__bss_end ||
        (uintptr_t)&zero2 < (uintptr_t)__bss_start ||
        (uintptr_t)&zero2 + sizeof zero2 > (uintptr_t)__bss_end) rc = 12;
    else if ((rc = heap_probe()) == 0) rc = file_probe();
    if (!rc && argc > 1 && !strcmp(argv[1], "resources"))
        rc = resources(api, 0, 0);
    if (!rc && argc > 1 && !strcmp(argv[1], "retain"))
        rc = resources(api, 1, 0);
    if (!rc && argc > 1 && !strcmp(argv[1], "retain-fault"))
        rc = resources(api, 1, 1);
    if (!rc && argc > 1 && !strcmp(argv[1], "help-found")) {
        int kind, code;
        fflush(NULL);
        api->exec_run("/bin/hello.bin --help");
        api->exec_last_result(&kind, &code);
        if (kind != EXEC_KIND_EXITED || code) {
            printf("N3-help-child kind=%d code=%d\n", kind, code);
            rc = 41;
        }
    }
    if (!rc && argc > 1 && !strcmp(argv[1], "help-missing")) {
        int kind, code;
        fflush(NULL);
        api->exec_run("/bin/nlprobe.bin --help");
        api->exec_last_result(&kind, &code);
        if (kind != EXEC_KIND_EXITED || code) rc = 42;
    }
    if (!rc && argc > 1 && !strcmp(argv[1], "fault")) {
        void *held = malloc(65537);
        FILE *open_file = fopen("/tmp/newlib-probe.tmp", "rb");
        if (!held || !open_file) rc = 43;
        else { fflush(NULL); __asm__ volatile("trap" : : "r"(held), "r"(open_file) : "memory"); }
    }
    if (!rc && argc > 1 && !strcmp(argv[1], "exit")) rc = 7;
    printf("N3-exit=%d\n", rc);
    fflush(NULL);
    return rc;
}
