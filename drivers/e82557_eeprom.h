#ifndef E82557_EEPROM_H
#define E82557_EEPROM_H
#include "pci.h"
#include "e82557_regs.h"

enum e82557_window_reason { E82557_WIN_OK, E82557_BAR_KIND, E82557_BAR_ZERO,
    E82557_BAR_ALIGN, E82557_BAR_RANGE, E82557_CMD_IO };
enum e82557_form { E82557_OK, E82557_ZERO, E82557_FF, E82557_MCAST,
    E82557_NOWIN, E82557_WIDTH };
enum e82557_method { E82557_STOP, E82557_A, E82557_F, E82557_SA, E82557_SF };
#define E82557_EE_AUTO  0
#define E82557_EE_FIXED 1
struct e82557_ee_pins {
    void *ctx;
    void (*write)(void *, u8);
    u8 (*read)(void *);
    void (*delay)(void *, u32);
    u32 step_us;
};
struct e82557_observation {
    int method, width, dummy, form;
    u16 sum;
    u8 mac[E82557_MAC_LEN];
};
int e82557_io_window(const struct pci_dev *, u16 *, int *);
const char *e82557_window_name(int why);
int e82557_ee_frame(int width, u16 addr, u32 *bits, int *n);
/* AUTO detects width only and closes CS; FIXED reads 16 data bits at *width.
 * Return OK / NOWIN / WIDTH. All paths leave CS low. */
int e82557_ee_read_word(const struct e82557_ee_pins *, int mode, int *width,
                       u16 addr, u16 *out, int *dummy);
/* Returns word 0's dummy (0/1); invalid width/cap returns -1 without pins. */
int e82557_ee_read_all(const struct e82557_ee_pins *, int width,
                      u16 *buf, int cap, u16 *sum);
int e82557_ee_checksum_ok(u16 sum);
void e82557_ee_mac(const u16 *buf, u8 *mac);
int e82557_mac_form(const u8 *mac);
int e82557_r5_next(const struct e82557_observation *, int n);
int e82557_r5_verdict(const struct e82557_observation *, int n);
void e82557_r5_observe(const struct e82557_ee_pins *, int method, u16 *buf,
                       struct e82557_observation *);
/* R5 only: bounded, NUL-terminated line builders; cap may be zero. */
void e82557_r5_observation_line(char *, int cap, const struct e82557_observation *, int k);
void e82557_r5_final_line(char *, int cap, const struct e82557_observation *, int n);
void e82557_r5_dump_line(char *, int cap, const u16 *, int row, int width);
void e82557_r5_begin_line(char *, int cap, u16 base);
#endif
