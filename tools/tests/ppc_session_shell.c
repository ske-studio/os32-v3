#include "session_shell.h"
#include "appslot.h"
#include "exec.h"
#include "gui.h"

static int current, from_user;
static AppSlot slots[APP_SLOT_COUNT];
static int request;

int appslot_cur(void) { return current; }
int ring3_call_from_user(void) { return from_user; }

AppSlot *appslot_get(int id)
{
    if (id < APP_ID_SHELL || id > APP_ID_MAX || !slots[id].state) return 0;
    return &slots[id];
}

int gui_take_next_shell(char *out, int cap)
{
    if (!request || cap < 2) return 0;
    out[0] = 'x';
    out[1] = 0;
    request = 0;
    return 1;
}

static int check(void)
{
    int switched;
    const char *def = "/bin/sh.bin";
    from_user = 1;
    current = APP_ID_MIN;
    slots[current].state = APP_STATE_RUNNING;
    slots[current].parent = APP_ID_SHELL;
    if (SESSION_SHELL_CALLER()) return 11; /* resident CLI, no consumer */
    if (pm_shell_select(def, &switched) != def || switched) return 12;

    from_user = 0;
    if (SESSION_SHELL_CALLER()) return 13;
    from_user = 1;
    for (current = APP_ID_MIN; current <= APP_ID_MAX; ++current) {
        slots[current].state = APP_STATE_RUNNING;
        slots[current].parent = APP_ID_SHELL;
        if (!SESSION_SHELL_CALLER()) return 14;
    }
    slots[3].parent = 2;
    slots[4].parent = 3;
    for (current = 3; current <= 4; ++current)
        if (SESSION_SHELL_CALLER()) return 15;

    slots[APP_ID_SHELL].state = APP_STATE_RUNNING;
    current = APP_ID_SHELL;
    if (SESSION_SHELL_CALLER()) return 16;
    current = APP_ID_MAX + 1;
    if (SESSION_SHELL_CALLER()) return 17;
    current = APP_ID_MIN;
    slots[current].state = APP_STATE_FREE;
    if (SESSION_SHELL_CALLER()) return 18;

    request = 1;
    const char *path = pm_shell_select(def, &switched);
    if (!switched || path[0] != 'x' || path[1]) return 19;
    if (pm_shell_select(def, &switched) != def || switched) return 20;
    return 0;
}

void _start(void)
{
    int status = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status));
    __builtin_unreachable();
}
