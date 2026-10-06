#include "os32api.h"

static void ls_callback(const void *entry, void *ctx);
static KernelAPI *test_api;
static int count, bad, kill_mode;
static void *expected_ctx;

void main(int argc, char **argv, KernelAPI *api)
{
    test_api = api;
    count = bad = 0;
    kill_mode = argc > 1 && argv[1][0] == 'k';
    expected_ctx = &count;
    int rc = api->sys_ls("/usr/bin", (void *)ls_callback, expected_ctx);
    api->kprintf(bad || rc < 0 || !count ? 0x41 : 0xE1,
                 "kcallback: rc=%d count=%d bad=%d CPL=3 %s\n", rc, count, bad,
                 bad || rc < 0 || !count ? "FAIL" : "PASS");
}

static void ls_callback(const void *entry, void *ctx)
{
    unsigned short cs;
    __asm__ volatile("mov %%cs,%0" : "=r"(cs));
    if ((cs & 3) != 3 || ctx != expected_ctx || !entry) bad++;
    count++;
    if (count == 1)
        test_api->kprintf(0xE1, "kcallback: callback CPL=%d ctx=%d\n", cs & 3,
                         ctx == expected_ctx);
    if (kill_mode && count == 3) {
        test_api->kprintf(0xE1, "kcallback: intentional fault after 3 entries\n");
        __asm__ volatile("ud2");
    }
}
