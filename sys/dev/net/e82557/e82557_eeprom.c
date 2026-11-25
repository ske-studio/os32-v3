/* EEPROM pin sequencing and MAC decoding, no hardware I/O or global state.
 * LB-1 reads only MAC words 0..2; width detection is a single fallback. */
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
