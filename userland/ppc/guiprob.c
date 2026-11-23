/* C-GUI acceptance client. Advance only on actual foreground and WM events. */
#include <stdio.h>
#ifdef C_GUI_ACCEPT
#include <string.h>
#endif
#include "os32api.h"
#include "os32/os32_gui_shared.h"

static KernelAPI *api;
static volatile GuiSlotHeader *header;
static volatile GuiEvent *ring;
static volatile GuiWinSpec *request;
static volatile GuiRespRect *response;

static int failed(const char *call, int value, int stage)
{
    char line[96];
    snprintf(line, sizeof(line), "C-GUI %s=%d\n", call, value);
    api->serial_puts(line);
    return stage;
}

/* USB discovery continues after the resident shell starts. */
static int wait_devices(int *kb, int *mouse)
{
    u32 start = api->get_tick();
    for (;;) {
        *kb = *mouse = 0;
        int count = api->in2_dev_count();
        if (count < 0) return failed("in2_dev_count", count, 3);
        for (int i = 0; i < count; i++) {
            IN2_DevInfo dev;
            int rc = api->in2_dev_info(i, &dev);
            if (rc) return failed("in2_dev_info", rc, 3);
            if (dev.kind == IN2_DEV_KBD) *kb = dev.dev;
            if (dev.kind == IN2_DEV_MOUSE) *mouse = dev.dev;
        }
        if (*kb && *mouse) return 0;
        if (api->get_tick() - start >= 1000)
            return failed(*kb ? "missing mouse, in2_dev_count" :
                                "missing keyboard, in2_dev_count", count, 4);
        api->sys_halt();
    }
}

#ifdef C_GUI_ACCEPT
static int probe_owner, probe_window;
static const char *trial_name;
static int seamless_dev;

static int seamless_sample(const IN2_Event *e)
{
    return seamless_dev && e->dev == seamless_dev &&
        (e->type == IN2_EV_ABS ||
         (e->type == IN2_EV_MOVE && !e->u.move.dx && !e->u.move.dy));
}

#endif

static int next_input(int step, int kind, int dev, int a, int b)
{
#ifdef C_GUI_ACCEPT
    u32 last_empty = api->get_tick();
#endif
    for (;;) {
        IN2_Event e;
        int n = api->in2_read(&e, 1);
        if (n < 0) return 0;
        if (!n) {
#ifdef C_GUI_ACCEPT
            u32 now = api->get_tick();
            if ((step == 2 || step == 6) && now - last_empty >= 25) {
                last_empty = now;
                api->serial_puts(step == 2 ? "C-GUI empty op=2\n" : "C-GUI empty op=6\n");
            }
#endif
            api->gui_call(GUI_OP_WAIT, 1);
            continue;
        }
#ifdef C_GUI_ACCEPT
        if (seamless_sample(&e)) continue;
        char line[128];
        snprintf(line, sizeof(line), "C-GUI %s op=%d owner=%d window=%d dev=%u type=%u\n",
                 trial_name, step, probe_owner, probe_window, (unsigned)e.dev, (unsigned)e.type);
        api->serial_puts(line);
#else
        (void)step;
#endif
        if (e.dev != dev || e.type != kind) return 0;
        if (kind == IN2_EV_KEY) return e.u.key.sc == a && e.u.key.down == b;
        if (kind == IN2_EV_MOVE) return e.u.move.dx == a && e.u.move.dy == b;
        return e.u.btn.down == a && e.u.btn.changed == b;
    }
}

static int wm_event(int kind, int down, u32 id)
{
    for (;;) {
        api->gui_call(GUI_OP_WAIT, 1);
        if (api->gui_call(GUI_OP_POLL, 0) < 0) return 0;
        while (header->ring_head != header->ring_tail) {
            GuiEvent e = ring[header->ring_head % GUI_RING_CAPACITY];
            header->ring_head++;
            if (e.kind == kind && e.sub == down)
                return e.window == id && (kind != GUI_EV_KEY || e.payload.key.scan == 0x1d);
        }
    }
}

static int window(const char *name, int x, int y)
{
    GuiWinSpec spec = {.rect = {x, y, 180, 96}, .flags = GUI_WF_DEFAULT};
    for (int i = 0; name[i]; i++) spec.title[i] = name[i];
    *request = spec;
    return api->gui_call(GUI_OP_WIN_CREATE, 0);
}

#ifdef C_GUI_ACCEPT
#include "guiprob_exclusive.h"
#endif

int main(int argc, char **argv, KernelAPI *kernel)
{
#ifdef C_GUI_ACCEPT
    if (argc != 2) return 1;
    trial_name = strcmp(argv[1], "d-input") ? argv[1] : "normal";
#else
    (void)argc; (void)argv;
#endif
    api = kernel;
    int slot = api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot == OS32_ERR_NOSYS) api->serial_puts("C-GUI INIT NOSYS\n");
    if (slot < 0 || slot >= GUI_SLOT_MAX) return failed("INIT", slot, 1);
    u8 *base = (u8 *)api->shm_base + GUI_SHM_OFFSET + slot * GUI_SLOT_SIZE;
    header = (void *)base;
    ring = (void *)(base + GUI_SLOT_RING_OFF);
    request = (void *)(base + GUI_SLOT_REQ_OFF);
    response = (void *)(base + GUI_SLOT_RESP_OFF);

#ifdef C_GUI_ACCEPT
    if (strcmp(argv[1], "d-input")) return exclusive_trial(argv[1]);
#endif
    int kb, mouse;
    int rc = wait_devices(&kb, &mouse);
    if (rc) return rc;
    MouseInfo pos;
    api->mouse_poll(&pos);
    int left = window("First", pos.x - 30, pos.y - 16);
    if (left < 0) return failed("WIN_CREATE First", left, 2);
    int right = window("Second", pos.x + 160, pos.y + 40);
    if (right < 0 || right == left) return failed("WIN_CREATE Second", right, 2);
    /* WAIT runs the registered WM cycle, including focus selection and input settling. */
    for (int i = 0; i < 2; i++) {
        rc = api->gui_call(GUI_OP_WAIT, 1);
        if (rc < 0) return failed("WAIT", rc, 3);
    }
    rc = api->gui_call(GUI_OP_WIN_CLIENT_RECT, left);
    if (rc) return failed("WIN_CLIENT_RECT", rc, 3);
    GuiRect16 original = response->rect;
    IN2_Status before, after;
    rc = api->in2_status(&before);
    if (rc) return failed("in2_status before", rc, 4);
    if (before.acquired) return failed("before.acquired", before.acquired, 4);
#ifdef C_GUI_ACCEPT
    api->serial_puts("C-GUI acquire before\n");
    rc = api->gui_call(GUI_OP_WAIT, 2);
    if (rc < 0) return failed("WAIT before acquire", rc, 4);
#endif
    IN2_Event queued[8];
    int count;
    do { count = api->in2_read(queued, 8); } while (count > 0);
    if (count < 0) return failed("in2_read", count, 4);
    rc = api->in2_acquire(3);
    if (rc) return failed("in2_acquire", rc, 4);
    if (api->in2_read(queued, 1) != 1 || queued[0].type != IN2_EV_LOST ||
        queued[0].u.lost.reason != IN2_LOST_EXCL) return failed("acquire LOST", 0, 4);
#ifdef C_GUI_ACCEPT
    api->serial_puts("C-GUI acquire after\n");
    rc = api->gui_call(GUI_OP_WAIT, 2);
    if (rc < 0) return failed("WAIT after acquire", rc, 4);
#endif
    rc = api->in2_unacquire();
    if (rc) return failed("in2_unacquire", rc, 4);
    if (api->in2_read(queued, 1) != 1 || queued[0].type != IN2_EV_LOST ||
        queued[0].u.lost.reason != IN2_LOST_EXCL) return failed("release LOST", 0, 4);
#ifdef C_GUI_ACCEPT
    api->serial_puts("C-GUI acquire released\n");
    rc = api->gui_call(GUI_OP_WAIT, 2);
    if (rc < 0) return failed("WAIT after release", rc, 4);
#endif
    rc = api->in2_status(&after);
    if (rc) return failed("in2_status after", rc, 4);
    if (after.acquired) return failed("after.acquired", after.acquired, 4);
    if (!before.focus_owner || before.focus_owner != after.focus_owner)
        return failed("focus_owner", after.focus_owner, 4);
    if (before.focus_gen + 2 != after.focus_gen) return failed("focus_gen", after.focus_gen, 4);
#ifdef C_GUI_ACCEPT
    probe_owner = before.focus_owner;
    probe_window = right;
    char ids[100];
    snprintf(ids, sizeof(ids), "C-GUI normal P=%u Q=%u owner=%u kb=%d mouse=%d\n",
             (unsigned)right, (unsigned)left, (unsigned)before.focus_owner, kb, mouse);
    api->serial_puts(ids);
#endif
    api->serial_puts("C-GUI READY\n");
    if (!next_input(2, IN2_EV_KEY, kb, 0x1d, 1) || !api->kbd_is_pressed(0x1d) ||
        !wm_event(GUI_EV_KEY, 1, right)) return 5;
    api->serial_puts("C-GUI MAKE\n");
    if (!next_input(4, IN2_EV_KEY, kb, 0x1d, 0) || api->kbd_is_pressed(0x1d) ||
        !wm_event(GUI_EV_KEY, 0, right)) return 6;
    api->serial_puts("C-GUI BREAK\n");
    if (!next_input(5, IN2_EV_MOVE, mouse, 12, -7)) return 7;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI MOVE\n");
#ifdef C_GUI_ACCEPT
    /* Give the foreground reader its turn before a WAIT can process the focus click. */
    IN2_MouseState state;
    do {
        if (api->in2_mouse_state(&state)) return 8;
        if (!state.buttons) api->sys_halt();
    } while (!state.buttons);
#endif
    if (!next_input(6, IN2_EV_BUTTON, mouse, 1, 1)) return 8;
#ifdef C_GUI_ACCEPT
    api->serial_puts("C-GUI BUTTON DOWN\n");
#endif
    if (!wm_event(GUI_EV_FOCUS, 1, left)) return 8;
#ifdef C_GUI_ACCEPT
    probe_window = left;
#endif
    api->serial_puts("C-GUI FOCUS\n");
    if (!next_input(8, IN2_EV_BUTTON, mouse, 0, 1)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI CLICK UP\n");
    if (!next_input(9, IN2_EV_BUTTON, mouse, 1, 1)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI DRAG START\n");
    if (!next_input(10, IN2_EV_MOVE, mouse, 10, 3)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
#ifdef C_GUI_ACCEPT
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left) ||
        response->rect.x != original.x || response->rect.y != original.y) return 9;
#endif
    api->serial_puts("C-GUI DRAG\n");
    if (!next_input(11, IN2_EV_BUTTON, mouse, 0, 1)) return 10;
    api->gui_call(GUI_OP_WAIT, 1);
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left)) return 11;
    GuiRect16 released = response->rect;
    if (released.x != original.x + 10 || released.y != original.y + 3) {
#ifdef C_GUI_ACCEPT
        snprintf(ids, sizeof(ids), "C-GUI unexpected released rect dx=%d dy=%d\n",
                 released.x - original.x, released.y - original.y);
        api->serial_puts(ids);
#endif
        return 11;
    }
    api->serial_puts("C-GUI UP\n");
    if (!next_input(12, IN2_EV_MOVE, mouse, 12, -7)) return 12;
    api->gui_call(GUI_OP_WAIT, 1);
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left) ||
        released.x != response->rect.x || released.y != response->rect.y) return 13;
    api->serial_puts("C-GUI STABLE\n");
    if (!next_input(13, IN2_EV_KEY, kb, 0x1d, 1) || !wm_event(GUI_EV_KEY, 1, left)) return 14;
    api->serial_puts("C-GUI FRONT KEY\n");
    if (!next_input(13, IN2_EV_KEY, kb, 0x1d, 0) || !wm_event(GUI_EV_KEY, 0, left)) return 15;
#ifdef C_GUI_ACCEPT
    api->serial_puts("C-GUI FRONT BREAK\n");
    if (!next_input(13, IN2_EV_BUTTON, mouse, 1, 1)) return 16;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI HELD\n");
    while (api->mouse_available()) api->gui_call(GUI_OP_WAIT, 1);
    IN2_DevInfo remaining;
    if (api->in2_dev_count() != 1 || api->in2_dev_info(0, &remaining) ||
        remaining.kind != IN2_DEV_KBD || remaining.dev != kb) return 17;
    if (api->in2_mouse_state(&state)) return 17;
    if (state.buttons) {
        api->gui_call(GUI_OP_WAIT, 1);
        failed("detach buttons", state.buttons, 17);
        do {
            api->gui_call(GUI_OP_WAIT, 1);
            if (api->in2_mouse_state(&state)) return 17;
        } while (state.buttons);
        return 17;
    }
    if (!next_input(13, IN2_EV_BUTTON, mouse, 0, 1)) return 17;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI GONE\n");
    if (wait_devices(&kb, &mouse) || api->in2_dev_count() != 2) return 18;
    snprintf(ids, sizeof(ids), "C-GUI re-enumerated kb=%d mouse=%d\n", kb, mouse);
    api->serial_puts(ids);
    api->serial_puts("C-GUI REATTACHED\n");
    if (!next_input(13, IN2_EV_MOVE, mouse, 12, -7)) return 19;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI FINAL MOVE\n");
#endif
    api->serial_puts("C-GUI DONE\n");
    return 0;
}
