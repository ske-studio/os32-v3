#include "shell_loop_ops.h"
#include "console.h"
#include "exec.h"
#include "sys/kern/idle_work.h"
#include "kprintf.h"

#ifdef OS32_C_GUI
#include "sys/dev/video/of_fb/backend_of_fb.h"
#include "sys/kern/console/font_source_kcg.h"
#include "rh_observe.h"
#endif

static int gui_default(void)
{
#ifdef OS32_C_GUI
    return 1;
#else
    return 0;
#endif
}

static int prepare(int gui)
{
#ifdef OS32_C_GUI
    if (gui && pm_rh_full_colors()->clut && kcg_file_ready()) {
        gfx_prepare_backend();
        if (of_fb_ready() && g_backend == &gfx_backend_of_fb) {
            gfx_client_to_gshell();
            return 0;
        }
    }
#endif
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
    .run = exec_run_shell,
    .prepare = prepare,
    .loading = loading,
    .result = result,
    .failure = failure,
    .delay_clear = idle_wait_step,
};
