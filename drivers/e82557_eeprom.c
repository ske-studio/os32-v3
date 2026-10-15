/* EEPROM pin sequencing and R5 interpretation, no hardware I/O or global state.
 * Contract: LB_R5_DESIGN.r3 §3–5 + PACK_lb_r5 PM decisions (2026-10-15). */
#include "e82557_eeprom.h"

int e82557_io_window(const struct pci_dev *dev, u16 *base, int *why)
{
    u32 raw = dev->bar[E82557_IO_BAR], b = pci_bar_base(raw);
    int reason = E82557_WIN_OK;
    if (pci_bar_kind(raw) != PCI_BAR_IO) reason = E82557_BAR_KIND;
    else if (b == 0) reason = E82557_BAR_ZERO;
    else if (b & (E82557_CSR_LEN - 1)) reason = E82557_BAR_ALIGN;
    else if (b > E82557_PORT_LIMIT - E82557_CSR_LEN) reason = E82557_BAR_RANGE;
    else if (!(dev->command & E82557_COMMAND_IO)) reason = E82557_CMD_IO;
    *why = reason;
    if (reason != E82557_WIN_OK) return 0;
    *base = (u16)b;
    return 1;
}

const char *e82557_window_name(int why)
{
    switch (why) {
    case E82557_BAR_KIND: return "bar-kind";
    case E82557_BAR_ZERO: return "bar-zero";
    case E82557_BAR_ALIGN: return "bar-align";
    case E82557_BAR_RANGE: return "bar-range";
    case E82557_CMD_IO: return "cmd-io";
    default: return "ok";
    }
}

static int ee_width_ok(int width)
{
    return width == E82557_EE_WIDTH6 || width == E82557_EE_WIDTH8;
}

int e82557_ee_frame(int width, u16 addr, u32 *bits, int *n)
{
    if (!ee_width_ok(width) || addr >= (1u << width)) return 0;
    *bits = (E82557_EE_READ << width) | addr;
    *n = E82557_EE_PREFIX_BITS + width;
    return 1;
}

int e82557_ee_read_word(const struct e82557_ee_pins *p, int mode, int *width,
                       u16 addr, u16 *out, int *dummy)
{
    u32 bits;
    int n, i, found = 0, result = E82557_OK;
    u16 word = 0;
    int w = mode == E82557_EE_AUTO ? E82557_EE_MAX_ADDR : *width;
    *dummy = -1;
    *out = 0;
    if (!e82557_ee_frame(w, addr, &bits, &n)) return E82557_WIDTH;
    p->write(p->ctx, 0);
    p->delay(p->ctx, p->step_us); /* D: initial */
    p->write(p->ctx, E82557_EE_CS);
    p->delay(p->ctx, p->step_us); /* D: select */
    for (i = n - 1; i >= 0; i--) {
        u8 pin = E82557_EE_CS | (((bits >> i) & 1) ? E82557_EE_DI : 0);
        p->write(p->ctx, pin);
        p->delay(p->ctx, p->step_us); /* D: command low */
        p->write(p->ctx, pin | E82557_EE_SK);
        p->delay(p->ctx, p->step_us); /* D: command high */
        p->write(p->ctx, pin);
        p->delay(p->ctx, p->step_us); /* D: command fall */
        if (i < w) {
            *dummy = !!(p->read(p->ctx) & E82557_EE_DO);
            if (mode == E82557_EE_AUTO && !*dummy) {
                *width = w - i;
                found = 1;
                break;
            }
        }
    }
    if (mode == E82557_EE_AUTO) {
        if (!found) { *width = 0; result = E82557_NOWIN; }
        else if (!ee_width_ok(*width)) result = E82557_WIDTH;
    } else {
        for (i = 0; i < E82557_EE_DATA_BITS; i++) {
            p->write(p->ctx, E82557_EE_CS | E82557_EE_SK);
            p->delay(p->ctx, p->step_us); /* D: data high */
            word = (u16)((word << 1) | !!(p->read(p->ctx) & E82557_EE_DO));
            p->write(p->ctx, E82557_EE_CS);
            p->delay(p->ctx, p->step_us); /* D: data low */
        }
        *out = word;
    }
    p->write(p->ctx, 0); /* CS cleanup */
    p->delay(p->ctx, p->step_us); /* D: finish */
    return result;
}

int e82557_ee_read_all(const struct e82557_ee_pins *p, int width,
                      u16 *buf, int cap, u16 *sum)
{
    int a, dummy, first = -1;
    u16 word;
    if (!ee_width_ok(width) || cap < 0) return -1;
    *sum = 0;
    for (a = 0; a < (1 << width); a++) {
        e82557_ee_read_word(p, E82557_EE_FIXED, &width, (u16)a, &word, &dummy);
        if (a == 0) first = dummy;
        if (a < cap) buf[a] = word;
        *sum = (u16)(*sum + word);
    }
    return first;
}

int e82557_ee_checksum_ok(u16 sum) { return sum == E82557_EE_SUM; }

void e82557_ee_mac(const u16 *buf, u8 *mac)
{
    int i;
    for (i = 0; i < E82557_MAC_LEN; i++)
        mac[i] = (u8)(buf[i / 2] >> ((i & 1) * 8));
}

int e82557_mac_form(const u8 *mac)
{
    int i, zero = 1, ff = 1;
    for (i = 0; i < E82557_MAC_LEN; i++) {
        if (mac[i] != 0) zero = 0;
        if (mac[i] != 0xFF) ff = 0;
    }
    if (zero) return E82557_ZERO;
    if (ff) return E82557_FF;
    return (mac[0] & 1) ? E82557_MCAST : E82557_OK;
}

int e82557_r5_verdict(const struct e82557_observation *obs, int n)
{
    return n > 0 && ee_width_ok(obs[n - 1].width) && obs[n - 1].form == E82557_OK;
}

int e82557_r5_next(const struct e82557_observation *obs, int n)
{
    if (n == 0) return E82557_A;
    if (e82557_r5_verdict(obs, n) || n >= E82557_R5_TRIES) return E82557_STOP;
    if (n == 1) return ee_width_ok(obs[0].width) ? E82557_SA : E82557_F;
    return obs[n - 1].method == E82557_F ? E82557_SF : E82557_STOP;
}

void e82557_r5_observe(const struct e82557_ee_pins *pins, int method, u16 *buf,
                       struct e82557_observation *o)
{
    struct e82557_ee_pins p = *pins;
    u16 ignored;
    int i;
    o->method = method;
    o->width = E82557_EE_WIDTH6;
    o->dummy = -1;
    o->sum = 0;
    for (i = 0; i < E82557_MAC_LEN; i++) o->mac[i] = 0;
    if (method == E82557_SA || method == E82557_SF)
        p.step_us *= E82557_EE_SLOW_FACTOR;
    o->form = E82557_OK;
    if (method == E82557_A || method == E82557_SA)
        o->form = e82557_ee_read_word(&p, E82557_EE_AUTO, &o->width, 0, &ignored, &o->dummy);
    if (o->form != E82557_OK) return;
    o->dummy = e82557_ee_read_all(&p, o->width, buf, E82557_EE_DUMP_WORDS, &o->sum);
    e82557_ee_mac(buf, o->mac);
    o->form = e82557_mac_form(o->mac);
}

/* R5 only: remove observation strings and the 64-word dump before L-B. */
typedef struct { char *buf; int cap, len; } EeOut;
static void ee_ch(EeOut *o, char c)
{
    if (o->len + 1 < o->cap) { o->buf[o->len++] = c; o->buf[o->len] = 0; }
}
static void ee_str(EeOut *o, const char *s) { while (*s) ee_ch(o, *s++); }
static void ee_hex(EeOut *o, u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        int nibble = (v >> (i * 4)) & 15;
        ee_ch(o, (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10));
    }
}
static void ee_dec(EeOut *o, unsigned int v)
{
    if (v >= 10) ee_dec(o, v / 10);
    ee_ch(o, (char)('0' + v % 10));
}
static EeOut ee_out(char *buf, int cap)
{
    EeOut o = {buf, cap, 0};
    if (cap > 0) buf[0] = 0;
    ee_str(&o, "[e82557] ");
    return o;
}
static const char *ee_form(int form)
{
    switch (form) {
    case E82557_OK: return "ok";
    case E82557_ZERO: return "zero";
    case E82557_FF: return "ff";
    case E82557_MCAST: return "mcast";
    case E82557_NOWIN: return "nowin";
    default: return "width";
    }
}
static const char *ee_method(int method)
{
    switch (method) {
    case E82557_A: return "A";
    case E82557_F: return "F";
    case E82557_SA: return "S(A)";
    default: return "S(F)";
    }
}
static const char *ee_oui(const u8 *m)
{
    if (m[0] == 0 && m[1] == 0 && m[2] == 0x4C) return "NEC";
    if (m[0] == 0 && ((m[1] == 0xA0 && m[2] == 0xC9) ||
                       (m[1] == 0x90 && m[2] == 0x27))) return "Intel";
    return "?";
}
static void ee_mac_text(EeOut *o, const u8 *mac)
{
    int i;
    for (i = 0; i < E82557_MAC_LEN; i++) {
        if (i) ee_ch(o, ':');
        ee_hex(o, mac[i], 2);
    }
}
void e82557_r5_observation_line(char *buf, int cap, const struct e82557_observation *v, int k)
{
    EeOut o = ee_out(buf, cap);
    ee_str(&o, "try"); ee_dec(&o, k); ee_ch(&o, ' '); ee_str(&o, ee_method(v->method));
    ee_str(&o, " d"); ee_dec(&o, E82557_EE_STEP_US *
        ((v->method == E82557_SA || v->method == E82557_SF) ? E82557_EE_SLOW_FACTOR : 1));
    ee_str(&o, ": addr=");
    if (v->form == E82557_NOWIN) ee_ch(&o, '-'); else ee_dec(&o, v->width);
    ee_str(&o, " n=");
    if (ee_width_ok(v->width)) ee_dec(&o, 1u << v->width); else ee_ch(&o, '-');
    ee_str(&o, " dummy=");
    if (v->dummy < 0 || v->form == E82557_NOWIN) ee_ch(&o, '-'); else ee_dec(&o, v->dummy);
    ee_str(&o, " sum=");
    ee_str(&o, ee_width_ok(v->width) ? (e82557_ee_checksum_ok(v->sum) ? "ok" : "bad") : "-");
    ee_str(&o, " mac=");
    if (ee_width_ok(v->width)) ee_mac_text(&o, v->mac); else ee_ch(&o, '-');
    ee_str(&o, " form="); ee_str(&o, ee_form(v->form));
    ee_str(&o, " oui="); ee_str(&o, ee_width_ok(v->width) ? ee_oui(v->mac) : "?");
}
void e82557_r5_final_line(char *buf, int cap, const struct e82557_observation *v, int n)
{
    EeOut o = ee_out(buf, cap);
    const struct e82557_observation *last = &v[n - 1];
    ee_str(&o, "R5=");
    if (e82557_r5_verdict(v, n)) {
        ee_str(&o, "PASS try="); ee_dec(&o, n); ee_str(&o, " addr="); ee_dec(&o, last->width);
        ee_str(&o, " mac="); ee_mac_text(&o, last->mac);
        ee_str(&o, " sum="); ee_str(&o, e82557_ee_checksum_ok(last->sum) ? "ok" : "bad");
    } else {
        ee_str(&o, "FAIL("); ee_str(&o, ee_form(last->form));
        ee_str(&o, ") tries="); ee_dec(&o, n);
    }
}
void e82557_r5_dump_line(char *buf, int cap, const u16 *words, int row, int width)
{
    EeOut o = ee_out(buf, cap);
    int i;
    ee_str(&o, "ee "); ee_hex(&o, row, 2); ee_ch(&o, ':');
    for (i = 0; i < E82557_EE_DUMP_ROW; i++) { ee_ch(&o, ' '); ee_hex(&o, words[row + i], 4); }
    if (width == E82557_EE_WIDTH8) ee_str(&o, " dump=64/256");
}
void e82557_r5_begin_line(char *buf, int cap, u16 base)
{
    EeOut o = ee_out(buf, cap);
    ee_str(&o, "R5 begin io=0x"); ee_hex(&o, base, 4);
}
