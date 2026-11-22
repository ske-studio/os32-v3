/* C-GUI acceptance client. Advance only on actual foreground and WM events. */
#include "os32api.h"
#include "os32/os32_gui_shared.h"

static KernelAPI *api;
static volatile GuiSlotHeader *header;
static volatile GuiEvent *ring;
static volatile GuiWinSpec *request;
static volatile GuiRespRect *response;

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
    if (slot < 0 || slot >= GUI_SLOT_MAX) return 1;
    u8 *base = (u8 *)api->shm_base + GUI_SHM_OFFSET + slot * GUI_SLOT_SIZE;
    header = (void *)base;
    ring = (void *)(base + GUI_SLOT_RING_OFF);
    request = (void *)(base + GUI_SLOT_REQ_OFF);
    response = (void *)(base + GUI_SLOT_RESP_OFF);

    MouseInfo pos;
    api->mouse_poll(&pos);
    int left = window("First", pos.x - 30, pos.y - 16);
    int right = window("Second", pos.x + 160, pos.y + 40);
    if (left < 0 || right < 0 || left == right) return 2;
    /* WAIT runs the registered WM cycle, including focus selection and input settling. */
    for (int i = 0; i < 2; i++)
        if (api->gui_call(GUI_OP_WAIT, 1) < 0) return 3;
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, left)) return 3;
    GuiRect16 original = response->rect;
    int kb = 0, mouse = 0;
    for (int i = 0; i < api->in2_dev_count(); i++) {
        IN2_DevInfo dev;
        if (api->in2_dev_info(i, &dev)) return 3;
        if (dev.kind == IN2_DEV_KBD) kb = dev.dev;
        if (dev.kind == IN2_DEV_MOUSE) mouse = dev.dev;
    }
    IN2_Status before, after;
    if (!kb || !mouse || api->in2_status(&before) || before.acquired ||
        api->in2_acquire(3) != OS32_ERR_NOSYS || api->in2_status(&after) ||
        after.acquired || before.focus_owner != after.focus_owner ||
        before.focus_gen != after.focus_gen) return 4;
    IN2_Event queued[8];
    int count;
    do { count = api->in2_read(queued, 8); } while (count > 0);
    if (count < 0 || !before.focus_owner) return 4;
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
