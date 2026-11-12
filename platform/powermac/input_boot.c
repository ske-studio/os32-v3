/* Foreground LAN terminal provider; all readers share one cooked lookahead. */
#include "input_boot.h"
#include "input_service.h"
#include "clock_source.h"
#include "lan_boot.h"

static PmInputDecoder decoder = { .pending = -1 };
static int lookahead = -1;

static int peek(void)
{
    int c;
    u32 now, hi;
    if (lookahead >= 0) return lookahead;
    pm_terminal_poll();
    for (;;) {
        if (decoder.pending >= 0) {
            c = decoder.pending;
            decoder.pending = -1;
        } else c = pm_terminal_trygetkey();
        platform_clock_read_us(&now, &hi);
        lookahead = pm_input_decode(&decoder, c, now);
        if (lookahead >= 0 || c < 0) return lookahead;
    }
}
static int try_key(void) { int c = peek(); lookahead = -1; return c; }
static int key(void) { int c; while ((c = try_key()) < 0) {} return c; }
static int try_char(void) { int c = try_key(); return c < 0 ? c : c & 255; }
static int character(void) { return key() & 255; }
static int wait_key(int interruptible) { (void)interruptible; return key(); }
static int has_key(void) { return peek() >= 0; }
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
    .trygetkey = try_key, .peekkey = peek, .trygetchar_local = try_char,
    .getchar_kapi = character, .getkey_kapi = key, .getkey_wait = wait_key,
    .has_key = has_key, .is_pressed = pressed, .trygetrawkey = raw,
    .modifiers = zero32, .dropped_count = zero32, .shift_state = zero8,
    .gui_mode = gui, .discard_stop = discard, .diag = diag, .diag_log = diag_log,
};
void pm_input_boot(void)
{
    decoder = (PmInputDecoder){ .pending = -1 };
    lookahead = -1;
    input_register(&ops);
}
