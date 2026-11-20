#include "platform_session_shell.h"
#include "appslot.h"
#include "exec.h"
#include "gui.h"

static int consumer;
static char next_shell[OS32_MAX_PATH];

int pm_session_shell_caller(void)
{
    if (!consumer || !ring3_call_from_user()) return 0;
    AppSlot *slot = appslot_get(appslot_cur());
    return slot && slot->parent == APP_ID_SHELL;
}

const char *pm_shell_select(const char *def, int *switched)
{
    consumer = 1;
    *switched = gui_take_next_shell(next_shell, sizeof(next_shell));
    return *switched ? next_shell : def;
}
