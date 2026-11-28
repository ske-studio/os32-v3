#include "config.h"
#include "gui.h"
#include "kstring.h"
#include "shell_loop.h"
#include <setjmp.h>

static int owner = GUI_SHELL_OWNER;
static int fallback, runs, gui_prepares, verdict;
static jmp_buf done;
static const char second[] = "/bin/second.bin";

int res_owner_get(void) { return owner; }

static int default_cui(void) { return 0; }

static int prepare(int gui)
{
    if (gui) {
        if (++gui_prepares > 1) {
            verdict = 2;
            longjmp(done, 1);
        }
        return 2;
    }
    return 0;
}

static int run(const char *path, int line_edit)
{
    if (!line_edit || kstrcmp(path, fallback ? SYS_SHELL_BIN : second)) {
        verdict = 3;
        longjmp(done, 1);
    }
    ++runs;
    return 0;
}

static void loading(const char *path, int gui) {}
static void result(const char *path, int rc, int gui, int streak) {}
static void failure(int gui, int rc, int streak) { verdict = 3; longjmp(done, 1); }
static void delay(void) { if (runs == 2) longjmp(done, 1); }

static void two_runs(void)
{
    const struct shell_loop_ops ops = {
        .gui_default = default_cui, .run = run, .prepare = prepare, .loading = loading,
        .result = result, .failure = failure, .delay_clear = delay,
    };
    runs = 0;
    if (!setjmp(done)) shell_loop_run(&ops);
}

int probe(void)
{
    char overlong[OS32_MAX_PATH + 1];
    kmemset(overlong, 'a', OS32_MAX_PATH);
    overlong[OS32_MAX_PATH] = 0;
    if (sys_switch_shell(second)) return 1;
    if (sys_switch_shell("") != OS32_ERR_INVAL ||
        sys_switch_shell(overlong) != OS32_ERR_NAMETOOLONG) return 4;
    owner = 2;
    if (sys_switch_shell("/bin/child.bin") != OS32_ERR_INVAL) return 4;
    owner = GUI_SHELL_OWNER;
    two_runs();
    if (verdict || gui_prepares) return verdict ? verdict : 3;

    fallback = 1;
    if (sys_switch_shell(SYS_GSHELL_BIN)) return 1;
    two_runs();
    return gui_prepares == 1 ? verdict : 2;
}
