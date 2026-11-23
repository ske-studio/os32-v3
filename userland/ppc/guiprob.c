/* C-GUI acceptance client. Advance only on actual foreground and WM events. */
#include <stdio.h>
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

static int next_input(int kind, int dev, int a, int b)
{
    for (;;) {
        IN2_Event e;
        int n = api->in2_read(&e, 1);
        if (n < 0) return 0;
        if (!n) { api->gui_call(GUI_OP_WAIT, 1); continue; }
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

int main(int argc, char **argv, KernelAPI *kernel)
{
    (void)argc; (void)argv;
    api = kernel;
    int slot = api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot == OS32_ERR_NOSYS) api->serial_puts("C-GUI INIT NOSYS\n");
    if (slot < 0 || slot >= GUI_SLOT_MAX) return failed("INIT", slot, 1);
    u8 *base = (u8 *)api->shm_base + GUI_SHM_OFFSET + slot * GUI_SLOT_SIZE;
    header = (void *)base;
    ring = (void *)(base + GUI_SLOT_RING_OFF);
    request = (void *)(base + GUI_SLOT_REQ_OFF);
    response = (void *)(base + GUI_SLOT_RESP_OFF);

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
    rc = api->in2_acquire(3);
    if (rc != OS32_ERR_NOSYS) return failed("in2_acquire", rc, 4);
    rc = api->in2_status(&after);
    if (rc) return failed("in2_status after", rc, 4);
    if (after.acquired) return failed("after.acquired", after.acquired, 4);
    if (!before.focus_owner || before.focus_owner != after.focus_owner)
        return failed("focus_owner", after.focus_owner, 4);
    if (before.focus_gen != after.focus_gen) return failed("focus_gen", after.focus_gen, 4);
    IN2_Event queued[8];
    int count;
    do { count = api->in2_read(queued, 8); } while (count > 0);
    if (count < 0) return failed("in2_read", count, 4);
    api->serial_puts("C-GUI READY\n");
    if (!next_input(IN2_EV_KEY, kb, 0x1d, 1) || !api->kbd_is_pressed(0x1d) ||
        !wm_event(GUI_EV_KEY, 1, right)) return 5;
    api->serial_puts("C-GUI MAKE\n");
    if (!next_input(IN2_EV_KEY, kb, 0x1d, 0) || api->kbd_is_pressed(0x1d) ||
        !wm_event(GUI_EV_KEY, 0, right)) return 6;
    api->serial_puts("C-GUI BREAK\n");
    if (!next_input(IN2_EV_MOVE, mouse, 12, -7)) return 7;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI MOVE\n");
    if (!next_input(IN2_EV_BUTTON, mouse, 1, 1) || !wm_event(GUI_EV_FOCUS, 1, left)) return 8;
    api->serial_puts("C-GUI FOCUS\n");
    if (!next_input(IN2_EV_BUTTON, mouse, 0, 1)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI CLICK UP\n");
    if (!next_input(IN2_EV_BUTTON, mouse, 1, 1)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI DRAG START\n");
    if (!next_input(IN2_EV_MOVE, mouse, 10, 3)) return 9;
    api->gui_call(GUI_OP_WAIT, 1);
    api->serial_puts("C-GUI DRAG\n");
    if (!next_input(IN2_EV_BUTTON, mouse, 0, 1)) return 10;
    api->gui_call(GUI_OP_WAIT, 1);
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left)) return 11;
    GuiRect16 released = response->rect;
    if (released.x != original.x + 10 || released.y != original.y + 3) return 11;
    api->serial_puts("C-GUI UP\n");
    if (!next_input(IN2_EV_MOVE, mouse, 12, -7)) return 12;
    api->gui_call(GUI_OP_WAIT, 1);
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left) ||
        released.x != response->rect.x || released.y != response->rect.y) return 13;
    api->serial_puts("C-GUI STABLE\n");
    if (!next_input(IN2_EV_KEY, kb, 0x1d, 1) || !wm_event(GUI_EV_KEY, 1, left)) return 14;
    api->serial_puts("C-GUI FRONT KEY\n");
    if (!next_input(IN2_EV_KEY, kb, 0x1d, 0) || !wm_event(GUI_EV_KEY, 0, left)) return 15;
    api->serial_puts("C-GUI DONE\n");
    return 0;
}
