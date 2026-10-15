#ifndef E82557_EEPROM_H
#define E82557_EEPROM_H
#include "pci.h"
#include "e82557_regs.h"

enum e82557_window_reason { E82557_WIN_OK, E82557_BAR_KIND, E82557_BAR_ZERO,
    E82557_BAR_ALIGN, E82557_BAR_RANGE, E82557_CMD_IO };
enum e82557_form { E82557_OK, E82557_ZERO, E82557_FF, E82557_MCAST,
    E82557_NOWIN, E82557_WIDTH };
#define E82557_EE_AUTO  0
#define E82557_EE_FIXED 1
struct e82557_ee_pins {
    void *ctx;
    void (*write)(void *, u8);
    u8 (*read)(void *);
    void (*delay)(void *, u32);
    u32 step_us;
};
int e82557_io_window(const struct pci_dev *, u16 *, int *);
const char *e82557_window_name(int why);
int e82557_ee_frame(int width, u16 addr, u32 *bits, int *n);
/* AUTO detects width only and closes CS; FIXED reads 16 data bits at *width.
 * Return OK / NOWIN / WIDTH. All paths leave CS low. */
int e82557_ee_read_word(const struct e82557_ee_pins *, int mode, int *width,
                       u16 addr, u16 *out, int *dummy);
void e82557_ee_mac(const u16 *buf, u8 *mac);
int e82557_mac_form(const u8 *mac);
#endif
