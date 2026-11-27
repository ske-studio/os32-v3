#include "shell_loop.h"
#include "config.h"
#include "exec.h"
#include "gui.h"
#include "kstring.h"

#define GUI_FAULT_STREAK_MAX 3

void shell_loop_run(const struct shell_loop_ops *ops)
{
    const char *cur_shell = ops->gui_default() ? SYS_GSHELL_BIN : SYS_SHELL_BIN;
    char consumed[OS32_MAX_PATH];
    int gui_fault_streak = 0;

    for (;;) {
        if (gui_take_next_shell(consumed, sizeof(consumed))) {
            cur_shell = consumed;
            gui_fault_streak = 0;   /* 別シェルへ切替 */
        }

        int is_gui = kstrcmp(cur_shell, SYS_GSHELL_BIN) == 0;
        int reason = ops->prepare(is_gui);
        if (reason) {
            cur_shell = SYS_SHELL_BIN;
            is_gui = 0;
        }
        ops->loading(cur_shell, is_gui);
        int rc = exec_run_initial_shell(reason == 1 ? SYS_SHELL_BIN " --kcg-unavailable" : cur_shell, is_gui);
        ops->result(cur_shell, rc, is_gui, gui_fault_streak);

        if (rc < 0 && rc != EXEC_ERR_FAULT) {
            ops->failure(is_gui, rc, gui_fault_streak);
            if (is_gui) {
                cur_shell = SYS_SHELL_BIN;
                gui_fault_streak = 0;
                continue;   /* ウェイト無しで即 CUI を起動 */
            }
        } else if (is_gui) {
            if (rc == EXEC_ERR_FAULT) {
                gui_fault_streak++;
                if (gui_fault_streak >= GUI_FAULT_STREAK_MAX) {
                    ops->failure(is_gui, rc, gui_fault_streak);
                    cur_shell = SYS_SHELL_BIN;
                    gui_fault_streak = 0;
                }
            } else {
                gui_fault_streak = 0;   /* 正常終了 */
            }
        }
        ops->delay_clear();
    }
}
