/* Machine independent INDEX8 text grid. Zero initialize before attach. */
#ifndef OS32_FBCON_H
#define OS32_FBCON_H
#include "gfx/gfx_console.h"
#include "drivers/video/kcg/kcg_module.h"
#include "console_backend.h"
#define FBCON_DEFAULT_ATTR 0xe1u
#define FBCON_REVERSE 0x04u
struct fbcon_cell { u16 code; u8 attr, part; }; /* part: 0=ANK, 1/2=wide */
struct fbcon {
    struct gfx_console_target *target;
    const KcgModuleApi *font;
    struct fbcon_cell *cells;
    struct con_geometry geometry;
    unsigned top;
    u8 fg, bg;
    int cursor_x, cursor_y, visible, ready;
};
int fbcon_attach(struct fbcon *, struct gfx_console_target *, const KcgModuleApi *, unsigned);
void fbcon_set_colors(struct fbcon *, u8, u8);
void fbcon_detach(struct fbcon *);
void fbcon_clear(struct fbcon *);
void fbcon_scroll(struct fbcon *, int);
void fbcon_putchar(struct fbcon *, int, int, u16, u8, int);
void fbcon_readchar(struct fbcon *, int, int, u16 *, u8 *);
int fbcon_reverse(struct fbcon *, int, int);
void fbcon_cursor_at(struct fbcon *, int, int);
void fbcon_cursor_shape(struct fbcon *, int);
#endif
