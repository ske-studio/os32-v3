/* Recording PC98 serial operations for the real platform table. */
#include "../../drivers/char/serial.h"
#include "../../kernel/port_service.c"
#include "../../kernel/serial_diag.c"
static int port_seen, port_char, emergency_seen, emergency_gate;
static unsigned long port_baud;
static const char *port_string;
void serial_init(unsigned long baud) { port_seen = 1; port_baud = baud; }
void serial_puts_kapi(const char *s) { port_seen = 2; port_string = s; }
int serial_getchar_kapi(void) { port_seen = 3; return -123; }
int serial_putchar_kapi(char c) { port_seen = 4; port_char = c; return -124; }
int serial_trygetchar(void) { port_seen = 5; return -125; }
int serial_is_initialized(void) { port_seen = 6; return 126; }
int serial_init_vfast(unsigned long baud) { port_seen = 7; port_baud = baud; return -127; }
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{ port_seen = 8; if (mode) *mode = 11; if (baud) *baud = 12; if (fifo) *fifo = 13; return -128; }
void serial_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{ port_seen = 9; if (oe) *oe = 21; if (fe) *fe = 22; if (pe) *pe = 23; if (overflow) *overflow = 24; }
int serial_putchar(char c) { port_seen = 10; port_char = c; return -130; }
void pc98_serial_puts_polled(const char *s) { emergency_seen++; port_string = s; }
int pc98_serial_gate_active(void) { return emergency_gate; }
