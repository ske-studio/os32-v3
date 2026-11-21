#include "shell_loop_ops.h"
#include "console.h"
#include "kernel/idle_work.h"
#include "kprintf.h"

static int gui_default(void)
{
    return 0;
}

static int prepare(int gui)
{
    return gui ? 2 : 0;
}

static void loading(const char *path, int gui)
{
    (void)gui;
    kprintf(0x07, "Loading %s\n", path);
}

static void result(const char *path, int rc, int gui, int streak)
{
    (void)gui;
    (void)streak;
    rshell_active = 0;
    kprintf(0x07, "%s returned %d\n", path, rc);
}

static void failure(int gui, int rc, int streak)
{
    (void)rc;
    (void)streak;
    if (gui) return;
    shell_print("Cannot load /sys/shell.bin\n", 1);
    for (;;) idle_wait_step();
}

const struct shell_loop_ops pm_shell_loop_ops = {
    .gui_default = gui_default,
    .prepare = prepare,
    .loading = loading,
    .result = result,
    .failure = failure,
    .delay_clear = idle_wait_step,
};
