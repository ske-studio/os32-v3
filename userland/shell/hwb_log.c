/* HW-BASIC record formatting; HW_BASIC_DESIGN §4. */
#include "hwb.h"

struct logbuf { char s[HWB_LOG_BYTES]; u32 n; };
static void ch(struct logbuf *b, char c) { if (b->n < HWB_LOG_BYTES - 1U) b->s[b->n++] = c; }
static void str(struct logbuf *b, const char *s) { while (*s) ch(b, *s++); }
static void dec(struct logbuf *b, u32 v)
{
    char tmp[10]; u32 n = 0;
    do { tmp[n++] = '0' + v % 10U; v /= 10U; } while (v);
    while (n) ch(b, tmp[--n]);
}
static void hex(struct logbuf *b, u32 v, u32 digits)
{
    static const char h[] = "0123456789ABCDEF";
    char tmp[8]; u32 n = 0;
    do { tmp[n++] = h[v & 15U]; v >>= 4; } while (v);
    while (n < digits) tmp[n++] = '0';
    str(b, "0x"); while (n) ch(b, tmp[--n]);
}
static void elapsed(struct logbuf *b, u32 lo, u32 hi)
{
    /* Hexadecimal represents the complete 64-bit value without libgcc. */
    if (hi) { hex(b, hi, 1); for (int i = 7; i >= 0; i--) ch(b, "0123456789ABCDEF"[(lo >> (i * 4)) & 15U]); }
    else hex(b, lo, 1);
}
static void quoted(struct logbuf *b, const char *s, u32 n)
{
    static const char h[] = "0123456789ABCDEF";
    ch(b, '"');
    for (u32 i = 0; i < n; i++) {
        u8 c = (u8)s[i];
        if (c == '"' || c == '\\') { ch(b, '\\'); ch(b, c); }
        else if (c <= 32U || c == 127U) { str(b, "\\x"); ch(b, h[c >> 4]); ch(b, h[c & 15U]); }
        else ch(b, c);
    }
    ch(b, '"');
}
const char *hwb_rc_name(enum hwb_rc rc)
{
    switch (rc) {
    case HWB_OK: return "OK"; case HWB_ARG: return "ARG";
    case HWB_NOSYS: return "NOSYS"; case HWB_WINDOW: return "WINDOW";
    case HWB_TIMEOUT: return "TIMEOUT"; case HWB_ABORT: return "ABORT";
    default: return "IO";
    }
}
int hwb_exit_status(enum hwb_rc rc)
{
    switch (rc) {
    case HWB_OK: return HWB_EXIT_OK; case HWB_ARG: return HWB_EXIT_ARG;
    case HWB_NOSYS: return HWB_EXIT_NOSYS; case HWB_WINDOW: return HWB_EXIT_WINDOW;
    case HWB_TIMEOUT: return HWB_EXIT_TIMEOUT; case HWB_ABORT: return HWB_EXIT_ABORT;
    default: return HWB_EXIT_IO;
    }
}
static void op(struct logbuf *b, const struct hwb_log_record *r)
{
    if (r->op == HWB_READ || r->op == HWB_WRITE) {
        str(b, r->space == HW_PCI ? "pci" : r->space == HW_IO ? "io" : "mmio");
        str(b, r->op == HWB_READ ? "read" : "write"); dec(b, r->width * 8U);
    } else if (r->op == HWB_LET) str(b, "let");
    else if (r->op == HWB_DEVICE_BDF || r->op == HWB_DEVICE_ID) str(b, "device");
    else if (r->op == HWB_WINDOW_BAR) str(b, "window");
    else if (r->op == HWB_LABEL) str(b, "label");
    else if (r->op == HWB_GOTO) str(b, "goto");
    else if (r->op == HWB_IF) str(b, "if");
    else if (r->op == HWB_CALL) str(b, "call");
    else if (r->op == HWB_RETURN) str(b, "return");
    else if (r->op == HWB_END) str(b, "end");
    else if (r->op == HWB_POLL) str(b, "poll");
    else if (r->op == HWB_WAIT) str(b, "wait");
    else if (r->op == HWB_PRINT) str(b, "print");
    else if (r->op == HWB_LOG) str(b, "log");
    else str(b, "unknown");
}
static void prefix(struct logbuf *b, const struct hwb_log_record *r, const char *phase)
{
    b->n = 0; str(b, "HW run="); dec(b, r->run); str(b, " seq="); dec(b, r->seq);
    str(b, " line="); dec(b, r->line); str(b, " phase="); str(b, phase);
    str(b, " op="); op(b, r);
}
static void emit(const struct hwb_backend *backend, struct logbuf *b)
{
    b->s[b->n] = 0; backend->line(backend->ctx, b->s);
}
static void address(struct logbuf *b, const struct hwb_log_record *r, int target)
{
    if (target && r->target) { str(b, " target=");
        for (u32 i = 0; i < HWB_LINE_BYTES && r->target[i]; i++) ch(b, r->target[i]); }
    if (r->space == HW_PCI) { str(b, " bdf="); dec(b, HW_BDF_BUS(r->bdf)); ch(b, ':'); dec(b, HW_BDF_DEV(r->bdf)); ch(b, '.'); dec(b, HW_BDF_FN(r->bdf)); }
    str(b, " addr=");
    hex(b, r->addr, 1);
    str(b, " val="); hex(b, r->value, r->width * 2U <= 8U ? r->width * 2U : 1U);
}
static void suffix(struct logbuf *b, enum hwb_rc rc) { str(b, " rc="); str(b, hwb_rc_name(rc)); }
void hwb_log_begin(const struct hwb_backend *backend, const struct hwb_log_record *r)
{
    struct logbuf b; prefix(&b, r, "begin");
    if (r->op == HWB_READ || r->op == HWB_WRITE || r->op == HWB_POLL) address(&b, r, 1);
    if (r->op == HWB_WAIT) { str(&b, " requested_us="); dec(&b, r->requested_us); }
    str(&b, " rc=PENDING"); emit(backend, &b);
}
void hwb_log_end(const struct hwb_backend *backend, const struct hwb_log_record *r)
{
    struct logbuf b; prefix(&b, r, "end");
    if (r->op == HWB_READ || r->op == HWB_WRITE) address(&b, r, 0);
    if (r->op == HWB_WAIT) { str(&b, " requested_us="); dec(&b, r->requested_us); str(&b, " elapsed_us="); elapsed(&b, r->elapsed_lo, r->elapsed_hi); }
    suffix(&b, r->rc); emit(backend, &b);
}
void hwb_log_poll(const struct hwb_backend *backend, const struct hwb_log_record *r, const struct hwb_poll_result *result)
{
    struct logbuf b; prefix(&b, r, "end"); str(&b, " val="); hex(&b, result->value, 1);
    str(&b, " reads="); dec(&b, result->reads); str(&b, " elapsed_us="); elapsed(&b, result->elapsed_lo, result->elapsed_hi);
    suffix(&b, r->rc); emit(backend, &b);
}
void hwb_log_print(const struct hwb_backend *backend, const struct hwb_log_record *r, const char *tag, u32 bytes, u32 value)
{
    struct logbuf b; prefix(&b, r, "end"); str(&b, " tag="); quoted(&b, tag, bytes); str(&b, " val="); hex(&b, value, 1); suffix(&b, r->rc); emit(backend, &b);
}
void hwb_log_text(const struct hwb_backend *backend, const struct hwb_log_record *r, const char *text, u32 bytes)
{
    struct logbuf b; prefix(&b, r, "end"); str(&b, " text="); quoted(&b, text, bytes); suffix(&b, r->rc); emit(backend, &b);
}
void hwb_log_finish(const struct hwb_state *state)
{
    struct logbuf b; b.n = 0; str(&b, "HW run="); dec(&b, state->run); str(&b, " op=end"); suffix(&b, state->rc);
    str(&b, " lines="); dec(&b, state->lines); str(&b, " writes="); dec(&b, state->writes); emit(state->backend, &b);
}
