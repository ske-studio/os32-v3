#include "pm_console.h"
static struct fbcon terminal;
int pm_console_attach(struct gfx_console_target *t, const KcgModuleApi *font, unsigned top)
{ return fbcon_attach(&terminal, t, font, top); }
void pm_console_detach(void) { fbcon_detach(&terminal); }
const struct con_geometry *con_backend_geometry(void) { return &terminal.geometry; }
int con_backend_safe(void) { return terminal.ready; }
u8 con_backend_color(unsigned role) { (void)role; return FBCON_DEFAULT_ATTR; }
/* The first TEXT target uses white on black for printf attributes. */
u8 con_backend_printf_attr(u8 attr) { (void)attr; return FBCON_DEFAULT_ATTR; }
void con_backend_clear(void) { fbcon_clear(&terminal); }
void con_backend_scroll(int rows) { fbcon_scroll(&terminal, rows); }
void con_backend_putchar(int x, int y, char ch, u8 attr)
{ fbcon_putchar(&terminal, x, y, (u8)ch, attr, 0); }
void con_backend_putkanji(int x, int y, u16 jis, u8 attr)
{ fbcon_putchar(&terminal, x, y, jis, attr, 1); }
void con_backend_readchar(int x, int y, u16 *code, u8 *attr)
{ fbcon_readchar(&terminal, x, y, code, attr); }
int con_backend_reverse(int x, int y) { return fbcon_reverse(&terminal, x, y); }
void con_backend_cursor_at(int x, int y) { fbcon_cursor_at(&terminal, x, y); }
void con_backend_cursor_shape(int visible) { fbcon_cursor_shape(&terminal, visible); }
