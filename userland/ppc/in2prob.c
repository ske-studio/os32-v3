#include "os32api.h"

static KernelAPI *api;
static int keyboard, mouse;

static int enumerate(int attached)
{
    keyboard = mouse = 0;
    int count = api->in2_dev_count();
    if (count != 1 + attached) return 0;
    for (int i = 0; i < count; i++) {
        IN2_DevInfo info;
        if (api->in2_dev_info(i, &info)) return 0;
        if (info.kind == IN2_DEV_KBD) keyboard = info.dev;
        if (info.kind == IN2_DEV_MOUSE) mouse = info.dev;
    }
    return keyboard && keyboard != mouse && !!mouse == attached;
}

/* The host advances only after this foreground reader acknowledges a report. */
static int event(int type, int dev, int a, int b)
{
    IN2_Event e;
    for (;;) {
        int n = api->in2_read(&e, 1);
        if (n < 0) return 0;
        if (!n) {
            api->sys_halt();
            continue;
        }
        /* The Enter that launched us may break after main starts. */
        if (e.type == IN2_EV_KEY && e.u.key.sc == 0x1c && !e.u.key.down) continue;
        if (e.type != type || e.dev != dev) return 0;
        if (type == IN2_EV_KEY) return e.u.key.sc == a && e.u.key.down == b;
        if (type == IN2_EV_MOVE) return e.u.move.dx == a && e.u.move.dy == b;
        return e.u.btn.down == a && e.u.btn.changed == b;
    }
}

static int probe(void)
{
    IN2_Event batch[8];
    IN2_MouseState state;
    if (!enumerate(1)) return 0;
    int n;
    do { n = api->in2_read(batch, 8); } while (n > 0);
    if (n < 0) return 0;
    api->serial_puts("IN2 READY\n");
    if (!event(IN2_EV_KEY, keyboard, 0x1d, 1) || !api->kbd_is_pressed(0x1d) ||
        api->kbd_getkey() != 0x1d61) return 0;
    api->serial_puts("IN2 MAKE\n");
    if (api->kbd_getkey() != 0x1d61 || api->in2_read(batch, 8) != 0) return 0;
    api->serial_puts("IN2 HOLD\n");
    if (!event(IN2_EV_KEY, keyboard, 0x1d, 0) || api->kbd_is_pressed(0x1d)) return 0;
    while (api->kbd_trygetkey() >= 0) {}
    api->serial_puts("IN2 BREAK\n");
    if (!event(IN2_EV_MOVE, mouse, 12, -7)) return 0;
    api->serial_puts("IN2 MOVE\n");
    const char *ack[] = {"IN2 DOWN\n", "IN2 UP\n", "IN2 HELD\n"};
    for (int i = 0; i < 3; ++i) {
        if (!event(IN2_EV_BUTTON, mouse, i != 1, 1)) return 0;
        api->serial_puts(ack[i]);
    }
    int detached = mouse;
    while (api->mouse_available()) api->sys_halt();
    if (!enumerate(0) || api->in2_mouse_state(&state)) return 0;
    if (state.buttons == 1) api->serial_puts("IN2 STALE BUTTON 1\n");
    if (state.buttons || !event(IN2_EV_BUTTON, detached, 0, 1)) return 0;
    api->serial_puts("IN2 GONE\n");
    while (!api->mouse_available()) api->sys_halt();
    if (!enumerate(1)) return 0;
    api->serial_puts("IN2 REATTACHED\n");
    if (!event(IN2_EV_MOVE, mouse, 12, -7)) return 0;
    api->serial_puts("IN2 CTRL-C\n");
    if (api->kbd_getkey() != 0x2b03) return 0;
    api->serial_puts("IN2 WAIT\n");
    /* The kernel traces the wait result before syscall exit handles abort. */
    return api->kbd_getkey() == -1 && api->kbd_trygetkey() == -1;
}

int main(int argc, char **argv, KernelAPI *kernel)
{
    (void)argc;
    (void)argv;
    api = kernel;
    int ok = probe();
    api->serial_puts(ok ? "IN2 DONE\n" : "IN2 FAIL\n");
    return !ok;
}
