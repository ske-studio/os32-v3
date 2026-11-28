int res_owner_get(void) { return 1; }
int gui_registered(void) { return 1; }
int exec_gui_registered(void) { return 1; }
int ring3_call_from_user(void) { return 0; }
volatile int ring3_wm_depth;
void in2_ring_clear(void) {}
void in2_ring_lost(u8 why) { (void)why; }
void mouse_ext_read(i32 *x, i32 *y) { *x = *y = 0; }
i32 mouse_ext_read_wheel(void) { return 0; }

static int check_foreground(void)
{
    if (av_focus_set(1)) return 73;
    tick(2, milliseconds + 30);
    if (av_focus_set(1)) return 74; /* same owner, new window */
    idle_work_run();
    if (take() != -1) return 75;
    tick(2, milliseconds + 30);
    if (av_focus_set(9) == 0) return 76;
    idle_work_run();
    if (take() != 'c') return 77;
    tick(2, milliseconds + 30);
    if (in2_excl_acquire(1, IN2_EXCL_KBD)) return 78;
    idle_work_run();
    if (take() != -1) return 79;
    tick(2, milliseconds + 30);
    if (in2_excl_unacquire(1)) return 80;
    idle_work_run();
    if (take() != -1) return 81;
    tick(2, milliseconds + 30);
    in2_reclaim(2); /* background reclaim preserves foreground work */
    idle_work_run();
    if (take() != 'c') return 82;
    tick(2, milliseconds + 30);
    in2_reclaim(1);
    idle_work_run();
    if (take() != -1) return 83;
    if (input_context_owner_reserve(2, 7, 1, 1, INPUT_CONTEXT_RUNNING)) return 84;
    tick(2, milliseconds + 30);
    input_context_owner_activate(2, 1);
    idle_work_run();
    if (take() != -1) return 85;
    tick(2, milliseconds + 30);
    input_context_owner_quiesce(2, 7);
    if (input_context_owner_release(2, 7)) return 86;
    if (input_context_owner_reserve(2, 8, 1, 1, INPUT_CONTEXT_RUNNING)) return 87;
    input_context_owner_activate(2, 1);
    idle_work_run();
    if (take() != -1) return 88;
    tick(2, milliseconds + 30);
    idle_work_run();
    if (take() != 'c') return 89;
    return 0;
}
