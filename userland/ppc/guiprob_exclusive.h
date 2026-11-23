/* Acceptance-only reply from a completed GUI_OP_WAIT cycle. */
typedef struct {
    int rc, front, foreground, owner, flags, generation, drag;
    int left, right, synthetic, frame_empty, deliveries, caller, settled;
} WmSample;

static int wm_sample(WmSample *state)
{
    if (api->gui_call(GUI_OP_WAIT, 3) < 0) return 0;
    *state = *(volatile WmSample *)response;
    return state->rc == 0 && state->settled;
}

static int read_lost(int reason)
{
    IN2_Event event;
    return api->in2_read(&event, 1) == 1 && event.type == IN2_EV_LOST &&
           event.u.lost.reason == reason;
}

static int cleared(const WmSample *s)
{
    return s->drag < 0 && s->frame_empty && !s->left && !s->right && !s->synthetic;
}

static void report_state(const char *role, const char *step, const WmSample *s)
{
    char line[160];
    snprintf(line, sizeof(line), "D-GUI %s %s front=%d fg=%d owner=%d flags=%d gen=%d caller=%d\n",
             role, step, s->front, s->foreground, s->owner, s->flags, s->generation, s->caller);
    api->serial_puts(line);
}

static int client_rect(int id, GuiRect16 *rect)
{
    if (api->gui_call(GUI_OP_WIN_CLIENT_RECT, id)) return 0;
    *rect = response->rect;
    return 1;
}

/* A seamless poll emits an ABS sample and its pixel MOVE even at rest.
 * Drain just those samples before posting an exclusive boundary. */
static int idle_before_boundary(void)
{
    if (!seamless_dev) return 1;
    IN2_Event events[8];
    int n;
    do {
        n = api->in2_read(events, 8);
        for (int i = 0; i < n; i++) if (!seamless_sample(&events[i])) return 0;
    } while (n > 0);
    return n == 0;
}

static int exclusive_trial(const char *role)
{
    WmSample s, before;
    if (!strcmp(role, "busy-Q")) {
        if (!wm_sample(&before) || before.flags != 3 || before.owner != before.foreground ||
            !before.caller || before.caller == before.owner) return 20;
        report_state(role, "BACKGROUND", &before);
        int rc = api->in2_acquire(3);
        if (rc != OS32_ERR_BUSY) return failed("background acquire rc", rc, 20);
        if (!wm_sample(&s) || memcmp(&s, &before, sizeof(s))) return 20;
        report_state(role, "BUSY", &s);
        return 0;
    }

    int explicit_release = !strcmp(role, "explicit");
    int focus = !strcmp(role, "focus");
    int focus_q = !strcmp(role, "focus-Q");
    int exit_p = !strcmp(role, "exit-P");
    int exit_q = !strcmp(role, "exit");
    if (!(explicit_release || focus || focus_q || exit_p || exit_q)) return 21;

    MouseInfo pos;
    api->mouse_poll(&pos);
    int kb, mouse;
    if (wait_devices(&kb, &mouse)) return 22;
    if (pos.mode == 2) {
        seamless_dev = mouse;
        if (!focus_q && !exit_p) {
            GFX_ScreenInfo screen;
            api->gfx_screen_info(&screen);
            char line[80];
            snprintf(line, sizeof(line), "D-GUI seamless %u %u\n",
                     (unsigned)screen.width, (unsigned)screen.height);
            api->serial_puts(line);
            do {
                api->gui_call(GUI_OP_WAIT, 1);
                api->mouse_poll(&pos);
            } while (pos.x != screen.width / 2 || pos.y != screen.height / 2);
        }
    } else if (!focus_q && !exit_p) {
        api->serial_puts("D-GUI relative\n");
    }
    int is_q = exit_q || focus_q;
    int id = window(is_q ? "Q" : "P", pos.x - 48, pos.y - (explicit_release ? 10 : 48));
    if (id < 0 || !wm_sample(&before) || before.front != id || !before.foreground)
        return failed("foreground", id, 23);
    int owner = before.foreground;
    if (before.caller != owner) return failed("caller", before.caller, 23);
    probe_owner = owner;
    probe_window = id;
    if (before.flags || before.owner) {
        report_state(role, "FOREGROUND EXCLUSIVE", &before);
        return failed("foreground flags", before.flags, 23);
    }
    if (!focus_q) {
        IN2_Event events[8];
        int n;
        do { n = api->in2_read(events, 8); } while (n > 0);
        if (n < 0) return 24;
    }
    report_state(role, "FRONT", &before);
    if (exit_q) {
        if (api->exec_run("/bin/guiprob.bin exit-P")) return 25;
        if (!wm_sample(&s) || s.front != id || s.foreground != owner) return 26;
    } else if (!focus_q) {
        int button = exit_p ? 2 : 1;
        if (!next_input(1, IN2_EV_BUTTON, mouse, button, button)) return 27;
        if (!wm_sample(&s) || (explicit_release ? s.drag < 0 || s.frame_empty :
                                (exit_p ? s.right : s.left) != id)) return 28;
        GuiRect16 held_rect, settled_rect;
        if (!client_rect(id, &held_rect)) return 28;
        report_state(role, "HELD", &s);
        if (!idle_before_boundary()) return 29;
        int rc = api->in2_acquire(3);
        if (rc) return failed("foreground acquire rc", rc, 29);
        if (!read_lost(IN2_LOST_EXCL) || !wm_sample(&s) ||
            s.owner != owner || s.flags != 3 || s.front != id ||
            s.generation != before.generation + 1) return failed("settle", id, 29);
        if (s.drag >= 0 || !s.frame_empty) return failed("residual drag", id, 29);
        if (s.left) return failed("residual left", id, 29);
        if (s.right) return failed("residual right", id, 29);
        if (s.synthetic) return failed("residual synthetic", id, 29);
        if (!client_rect(id, &settled_rect) || memcmp(&held_rect, &settled_rect, sizeof(held_rect)))
            return failed("settle geometry", id, 29);
        report_state(role, "SETTLED", &s);
        if (!next_input(2, IN2_EV_BUTTON, mouse, 0, button)) return 30;
        if (exit_p) {
            report_state(role, "EXIT", &s);
            return 0;
        }
        if (focus) return api->exec_run("/bin/guiprob.bin focus-Q");
        if (api->exec_run("/bin/guiprob.bin busy-Q") || !wm_sample(&s) ||
            s.front != id || s.owner != owner || s.flags != 3 ||
            s.generation != before.generation + 1) return 31;
        report_state(role, "RESUMED", &s);
        int deliveries = s.deliveries;
        const int scans[] = {0x1d, 0x60, 0x74, 0x73, 0x0f};
        for (int i = 0; i < 5; i++) {
            for (int down = 1; down >= 0; down--) {
                if (!next_input(3, IN2_EV_KEY, kb, scans[i], down) || !wm_sample(&s) || s.deliveries != deliveries) return 32;
                char line[60];
                snprintf(line, sizeof(line), "D-GUI exclusive KEY %d %d\n", scans[i], down);
                api->serial_puts(line);
            }
        }
        if (!next_input(4, IN2_EV_MOVE, mouse, 12, -7) || !wm_sample(&s) || s.deliveries != deliveries) return 33;
        api->serial_puts("D-GUI exclusive MOVE\n");
        for (int down = 1; down >= 0; down--) {
            if (!next_input(5, IN2_EV_BUTTON, mouse, down, 1) || !wm_sample(&s) || s.deliveries != deliveries) return 34;
            api->serial_puts(down ? "D-GUI exclusive DOWN\n" : "D-GUI exclusive UP\n");
        }
        if (!idle_before_boundary() || api->in2_unacquire()) return 35;
    }
    if (is_q && !read_lost(IN2_LOST_FOCUS)) return failed("first FOCUS", id, 36);
    if (!read_lost(IN2_LOST_EXCL)) return failed("missing release LOST_EXCL", id, 37);
    if (!wm_sample(&s) || s.front != id || s.foreground != owner ||
        s.owner || s.flags || !cleared(&s)) return failed("release state", id, 37);
    char reader[96];
    snprintf(reader, sizeof(reader), "D-GUI %s reader=%d LOST=%sEXCL\n",
             role, owner, is_q ? "FOCUS," : "");
    api->serial_puts(reader);
    report_state(role, "RELEASED", &s);
    for (int down = 1; down >= 0; down--) {
        if (!next_input(6, IN2_EV_KEY, kb, 0x1d, down) || !wm_event(GUI_EV_KEY, down, id))
            return 38;
        api->serial_puts(down ? "D-GUI ordinary MAKE\n" : "D-GUI ordinary BREAK\n");
    }
    GuiRect16 original, moved;
    if (!client_rect(id, &original)) return 39;
    api->mouse_poll(&pos);
    int dx = original.x + 32 - pos.x, dy = original.y - 8 - pos.y;
    if (!dx && !dy) dx = 1;
    char line[80];
    snprintf(line, sizeof(line), "D-GUI title delta %d %d\n", dx, dy);
    api->serial_puts(line);
    if (!next_input(7, IN2_EV_MOVE, mouse, dx, dy) || !wm_sample(&s) ||
        !client_rect(id, &moved) || memcmp(&moved, &original, sizeof(moved))) return 39;
    api->serial_puts("D-GUI stationary\n");
    if (!next_input(8, IN2_EV_BUTTON, mouse, 1, 1) || !wm_sample(&s) ||
        s.drag < 0 || s.frame_empty) return 40;
    api->serial_puts("D-GUI drag started\n");
    if (!next_input(9, IN2_EV_MOVE, mouse, 8, 4) || !wm_sample(&s)) return 41;
    api->serial_puts("D-GUI drag moved\n");
    if (!next_input(10, IN2_EV_BUTTON, mouse, 0, 1) || !wm_sample(&s) ||
        !cleared(&s) || !client_rect(id, &moved) || moved.x != original.x + 8 ||
        moved.y != original.y + 4) return 42;
    report_state(role, "DONE", &s);
    return 0;
}
