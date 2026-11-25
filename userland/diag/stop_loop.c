/* Manual CTRL+STOP fixture: optional window, then a delayed KAPI-only loop. */
#include "os32api.h"
#include "os32_gui_shared.h"

#define TICKS_PER_SECOND 100u
#define MAX_SECONDS 600u
static int seconds(const char *s, u32 *out);

int main(int argc, char **argv, KernelAPI *api)
{
    u32 delay, duration, start;
    int window;
    if (argc != 4 || !seconds(argv[2], &delay) ||
        !seconds(argv[3], &duration) || !duration)
        return 1; /* stop_loop window|none delay_seconds loop_seconds */
    window = argv[1][0] == 'w' && argv[1][1] == 'i' &&
        argv[1][2] == 'n' && argv[1][3] == 'd' &&
        argv[1][4] == 'o' && argv[1][5] == 'w' && !argv[1][6];
    if (!window && !(argv[1][0] == 'n' && argv[1][1] == 'o' &&
        argv[1][2] == 'n' && argv[1][3] == 'e' && !argv[1][4])) return 1;
    i32 slot = api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot < 0) return 2;
    if (window) {
        GuiWinSpec spec = {0};
        const char *title = "STOP KAPI loop";
        volatile u8 *req = (volatile u8 *)(api->shm_base + GUI_SHM_OFFSET +
            (u32)slot * GUI_SLOT_SIZE + GUI_SLOT_REQ_OFF);
        spec.rect.x = 40; spec.rect.y = 60;
        spec.rect.w = 240; spec.rect.h = 100;
        spec.flags = GUI_WF_DEFAULT;
        for (u32 i = 0; title[i]; i++) spec.title[i] = (u8)title[i];
        for (u32 i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
        if (api->gui_call(GUI_OP_WIN_CREATE, 0) < 0) return 3;
    }
    start = api->get_tick();
    while ((u32)(api->get_tick() - start) < delay * TICKS_PER_SECOND)
        api->sys_yield();
    start = api->get_tick();
    while ((u32)(api->get_tick() - start) < duration * TICKS_PER_SECOND) { }
    return 0;
}

static int seconds(const char *s, u32 *out)
{
    u32 n = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
        n = n * 10u + (u32)(*s - '0');
        if (n > MAX_SECONDS) return 0;
    }
    *out = n;
    return 1;
}
