/* PLN1 local provider: decoded LAN first, then USB. */
#include "input_boot.h"
#include "input_usb.h"
#include "input_service.h"
#include "clock_source.h"
#include "lan_boot.h"
#include "input/hal/in2_int.h"
#include "con_sink.h"
#include "exec/exec.h"
#include "kernel/idle_work.h"
#include "arch_io.h"

#if !KBOOT_LINK
static PmInputDecoder decoder = { .pending = -1 };
static int lan_lookahead = -1;

static int lan_key(void)
{
    for (unsigned n = 0; n < 32; n++) {
        int c;
        u32 now, hi;
        if (decoder.pending >= 0) {
            c = decoder.pending;
            decoder.pending = -1;
        } else c = pm_terminal_trygetkey();
        platform_clock_read_us(&now, &hi);
        int key = pm_input_decode(&decoder, c, now);
        if (key >= 0 || c < 0) return key;
    }
    return -1;
}
/* Finite byte collection is allowed; a completed LAN key stays until try. */
static int lan_peek(void)
{
    if (lan_lookahead >= 0) return lan_lookahead;
    lan_lookahead = lan_key();
    return lan_lookahead;
}
static int peek(void)
{
    int key = lan_peek();
    return key >= 0 ? key : pm_input_usb_peek();
}
static int try_key(void)
{
    int key = lan_peek();
    lan_lookahead = -1;
    return key >= 0 ? key : pm_input_usb_take();
}
#endif
static u32 wait_ticks(void) { u32 lo = 0, hi; platform_clock_read_us(&lo, &hi); return lo; }
static u32 wait_timeout(void) { return 30000000u; }
static void wait_step(void) { idle_work_run(); _idle(); }
static const struct InputLegacyProvider provider = {
#if KBOOT_LINK
    .try_key = pm_input_usb_take, .peek_key = pm_input_usb_peek,
#else
    .try_key = try_key, .peek_key = peek,
#endif
    /* No terminal or injected source in either image. */
    .terminal_try = NULL, .terminal_peek = NULL, .terminal_enabled = NULL,
    .wait_ticks = wait_ticks, .wait_timeout_ticks = wait_timeout,
    .capture_active = con_sink_capture_active, .wait_pending = ring3_wait_pending,
    .wait_step = wait_step,
};
static int key(void) { return input_legacy_wait(IL_KEY, 0); }
static int try_key_bridge(void) { return input_legacy_try(IL_KEY); }
static int try_local_char(void) { return input_legacy_try(IL_LOCAL_CHAR); }
static int try_char(void) { return input_legacy_try(IL_CHAR); }
static int character(void) { return input_legacy_wait(IL_CHAR, 0); }
static int wait_key(int interruptible) { return input_legacy_wait(IL_KEY, interruptible); }
static int character_kapi(void) { return input_legacy_wait(IL_CHAR, 1); }
static int key_kapi(void) { return input_legacy_wait(IL_KEY, 1); }
/* Busy pollers (e.g. a CUI script checking for ESC) never halt, so advance the
 * foreground work once per peek; idle_work_run refuses EE=0, IRQ and re-entry. */
static int peek_key(void)
{
#if !KBOOT_LINK
    idle_work_run();
#endif
    return input_legacy_peek(IL_KEY);
}
static int has_key(void) { return peek_key() >= 0; }
static int pressed(int scancode) { (void)scancode; return 0; }
static int raw(void) { return -1; }
static u32 zero32(void) { return 0; }
static u8 zero8(void) { return 0; }
static void gui(int on) { (void)on; }
static void discard(void) { }
static int diag(KbdDiag *out) { (void)out; return OS32_ERR_NOSYS; }
static int diag_log(u32 seq, KbdDiagLogEnt *out, int max)
{ (void)seq; (void)out; (void)max; return OS32_ERR_NOSYS; }
static const InputOps ops = {
    .getchar = character, .getkey = key, .trygetchar = try_char,
    .trygetkey = try_key_bridge, .peekkey = peek_key, .trygetchar_local = try_local_char,
    .getchar_kapi = character_kapi, .getkey_kapi = key_kapi, .getkey_wait = wait_key,
    .has_key = has_key, .is_pressed = pressed, .trygetrawkey = raw,
    .modifiers = zero32, .dropped_count = pm_input_usb_dropped, .shift_state = zero8,
    .gui_mode = gui, .discard_stop = discard, .diag = diag, .diag_log = diag_log,
};
void pm_input_boot(void)
{
#if !KBOOT_LINK
    decoder = (PmInputDecoder){ .pending = -1 };
    lan_lookahead = -1;
#endif
    input_legacy_register(&provider);
    input_register(&ops);
}
