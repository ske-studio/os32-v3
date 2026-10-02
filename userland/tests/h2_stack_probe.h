#ifndef H2_STACK_PROBE_H
#define H2_STACK_PROBE_H
#include "memmap.h"
static void h2_plan(unsigned int stack, unsigned int *bytes, unsigned int *depth, unsigned long *guard);
static int h2_entry_ok(int argc);
/* Test-only contract shared with h3: invoke wait at the deepest live frame. */
#define H2_PAGE_BYTES 4096U
#define H2_DEEP_BYTES (4U * H2_PAGE_BYTES)
#define H2_LARGE_BYTES (72U * H2_PAGE_BYTES)
#define H2_SMALL_BYTES (24U * H2_PAGE_BYTES)
#define H2_PATTERN 0xA5U
#define H2_ATTR 0x07
struct h2_probe {
    int argc;
    char **argv;
    unsigned long low;
    unsigned long high;
    unsigned int pages;
    int (*wait)(struct h2_probe *, volatile unsigned char *, unsigned int);
    void *context;
};
static int h2_probe_run(struct h2_probe *p, unsigned int bytes, unsigned int depth);
#endif
