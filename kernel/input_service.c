/* Stateless forwarding; rings, waits and IRQ policy remain in the provider. */
#include "input_service.h"
#include "pointer_service.h"

static int empty_getchar(void) { return -1; }
static int empty_getkey(void) { return -1; }
static int empty_trygetchar(void) { return -1; }
static int empty_trygetkey(void) { return -1; }
static int empty_peekkey(void) { return -1; }
static int empty_trygetchar_local(void) { return -1; }
static int empty_getchar_kapi(void) { return -1; }
static int empty_getkey_kapi(void) { return -1; }
static int empty_getkey_wait(int interruptible) { (void)interruptible; return -1; }
static int empty_has_key(void) { return 0; }
static int empty_is_pressed(int scancode) { (void)scancode; return 0; }
static int empty_trygetrawkey(void) { return -1; }
static u32 empty_modifiers(void) { return 0; }
static u32 empty_dropped_count(void) { return 0; }
static u8 empty_shift_state(void) { return 0; }
static void empty_gui_mode(int on) { (void)on;  }
static void empty_discard_stop(void) {  }
static int empty_diag(KbdDiag *out) { (void)out; return OS32_ERR_NOSYS; }
static int empty_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max) { (void)after_seq; (void)out; (void)max; return OS32_ERR_NOSYS; }

static const InputOps empty_input = {
    .getchar = empty_getchar,
    .getkey = empty_getkey,
    .trygetchar = empty_trygetchar,
    .trygetkey = empty_trygetkey,
    .peekkey = empty_peekkey,
    .trygetchar_local = empty_trygetchar_local,
    .getchar_kapi = empty_getchar_kapi,
    .getkey_kapi = empty_getkey_kapi,
    .getkey_wait = empty_getkey_wait,
    .has_key = empty_has_key,
    .is_pressed = empty_is_pressed,
    .trygetrawkey = empty_trygetrawkey,
    .modifiers = empty_modifiers,
    .dropped_count = empty_dropped_count,
    .shift_state = empty_shift_state,
    .gui_mode = empty_gui_mode,
    .discard_stop = empty_discard_stop,
    .diag = empty_diag,
    .diag_log = empty_diag_log,
};
static const InputOps *input_provider = &empty_input;

u32 input_irq(void) { return input_provider->irq; }
void input_register(const InputOps *ops) { input_provider = ops; }
int input_getchar(void) { return input_provider->getchar(); }
int input_getkey(void) { return input_provider->getkey(); }
int input_trygetchar(void) { return input_provider->trygetchar(); }
int input_trygetkey(void) { return input_provider->trygetkey(); }
int input_peekkey(void) { return input_provider->peekkey(); }
int input_trygetchar_local(void) { return input_provider->trygetchar_local(); }
int input_getchar_kapi(void) { return input_provider->getchar_kapi(); }
int input_getkey_kapi(void) { return input_provider->getkey_kapi(); }
int input_getkey_wait(int interruptible) { return input_provider->getkey_wait(interruptible); }
int input_has_key(void) { return input_provider->has_key(); }
int input_is_pressed(int scancode) { return input_provider->is_pressed(scancode); }
int input_trygetrawkey(void) { return input_provider->trygetrawkey(); }
u32 input_get_modifiers(void) { return input_provider->modifiers(); }
u32 input_dropped_count(void) { return input_provider->dropped_count(); }
u8 input_shift_state(void) { return input_provider->shift_state(); }
void input_set_gui_mode(int on) { input_provider->gui_mode(on); }
void input_discard_stop(void) { input_provider->discard_stop(); }
int input_diag(KbdDiag *out) { return input_provider->diag(out); }
int input_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max) { return input_provider->diag_log(after_seq, out, max); }

static void empty_pointer_poll(MouseState *state)
{
    if (!state) return;
    state->x = state->y = state->dx = state->dy = 0;
    state->buttons = state->mode = 0;
}
static int empty_pointer_available(void) { return 0; }
static void empty_pointer_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max)
{ (void)x_min; (void)y_min; (void)x_max; (void)y_max; }
static void empty_pointer_mode(int mode) { (void)mode; }
static void empty_pointer_show(void) { }
static void empty_pointer_hide(void) { }
static const PointerOps empty_pointer = {
    .poll = empty_pointer_poll,
    .available = empty_pointer_available,
    .bounds = empty_pointer_bounds,
    .cursor_mode = empty_pointer_mode,
    .cursor_show = empty_pointer_show,
    .cursor_hide = empty_pointer_hide,
};
static const PointerOps *pointer_provider = &empty_pointer;

void pointer_register(const PointerOps *ops) { pointer_provider = ops; }
void pointer_poll(MouseState *state) { pointer_provider->poll(state); }
int pointer_available(void) { return pointer_provider->available(); }
void pointer_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max)
{ pointer_provider->bounds(x_min, y_min, x_max, y_max); }
void pointer_cursor_set_mode(int mode) { pointer_provider->cursor_mode(mode); }
void pointer_cursor_show(void) { pointer_provider->cursor_show(); }
void pointer_cursor_hide(void) { pointer_provider->cursor_hide(); }
