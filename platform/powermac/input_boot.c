/* LAN/USB provider; all readers share one cooked lookahead. */
#include "input_boot.h"
#include "input_usb.h"
#include "input_service.h"
#include "clock_source.h"
#include "lan_boot.h"

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
    .modifiers = zero32, .dropped_count = pm_input_usb_dropped, .shift_state = zero8,
    .gui_mode = gui, .discard_stop = discard, .diag = diag, .diag_log = diag_log,
};
void pm_input_boot(void)
{
    decoder = (PmInputDecoder){ .pending = -1 };
    lookahead = -1;
    last_source = 0;
    input_register(&ops);
}
