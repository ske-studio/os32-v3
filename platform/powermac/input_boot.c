/* LAN/USB provider; all readers share one cooked lookahead. */
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

static PmInputDecoder decoder = { .pending = -1 };
static int lookahead = -1;

/* Last selected source: 0=LAN, 1=USB. Start by trying USB. */
static int last_source;
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
static int peek(void)
{
    if (lookahead >= 0) return lookahead;
    pm_terminal_poll();
    for (int n = 0; n < 2; n++) {
        int source = (last_source + 1 + n) & 1;
        int key = source ? pm_input_usb_take() : lan_key();
        if (key >= 0) {
            last_source = source;
            lookahead = key;
            break;
        }
    }
    return lookahead;
}
static int try_key(void) { int c = peek(); lookahead = -1; return c; }
static u32 wait_ticks(void) { u32 lo, hi; platform_clock_read_us(&lo, &hi); return lo; }
static u32 wait_timeout(void) { return 30000000u; }
static void wait_step(void) { idle_work_run(); _idle(); }
static const struct InputLegacyProvider provider = {
    .try_key = try_key, .peek_key = peek,
    .wait_ticks = wait_ticks, .wait_timeout_ticks = wait_timeout,
    .capture_active = con_sink_capture_active, .wait_pending = ring3_wait_pending,
    .wait_step = wait_step,
};
static int key(void) { return input_legacy_wait(IL_KEY, 0); }
static int try_char(void) { return input_legacy_try(IL_CHAR); }
static int character(void) { return input_legacy_wait(IL_CHAR, 0); }
static int wait_key(int interruptible) { return input_legacy_wait(IL_KEY, interruptible); }
static int character_kapi(void) { return input_legacy_wait(IL_CHAR, 1); }
static int key_kapi(void) { return input_legacy_wait(IL_KEY, 1); }
static int peek_key(void) { return input_legacy_peek(IL_KEY); }
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
    .trygetkey = try_key, .peekkey = peek_key, .trygetchar_local = try_char,
    .getchar_kapi = character_kapi, .getkey_kapi = key_kapi, .getkey_wait = wait_key,
    .has_key = has_key, .is_pressed = pressed, .trygetrawkey = raw,
    .modifiers = zero32, .dropped_count = pm_input_usb_dropped, .shift_state = zero8,
    .gui_mode = gui, .discard_stop = discard, .diag = diag, .diag_log = diag_log,
};
void pm_input_boot(void)
{
    decoder = (PmInputDecoder){ .pending = -1 };
    lookahead = -1;
    last_source = 0;
    input_legacy_register(&provider);
    input_register(&ops);
}
