/* PRIO-2: Execute the guest main with controlled sys_ls results. */
#define main kcallback_guest_main
#include "guest.inc"
#undef main

static int ls_mode;
static void quiet_print(int attr, const char *fmt, ...)
{
    (void)attr;
    (void)fmt;
}

static int fake_ls(const char *path, void *callback, void *ctx)
{
    (void)path;
    if (ls_mode == 1) return -1;
    if (ls_mode == 2) return 0;
    ((void (*)(const void *, void *))callback)(
        ls_mode == 4 ? (void *)0 : &ls_mode,
        ls_mode == 3 ? (void *)0 : ctx);
    return 0;
}

void _start(void)
{
    KernelAPI api = { fake_ls, quiet_print };
    char *argv[] = { "kcallback_test", (void *)0 };
    int result = 0;
    for (ls_mode = 0; ls_mode < 5; ls_mode++) {
        int rc = kcallback_guest_main(1, argv, &api);
        if (rc != (ls_mode ? 1 : 0)) result = 1;
    }
    __asm__ volatile("int $0x80" :: "a"(1), "b"(result));
    __builtin_unreachable();
}
