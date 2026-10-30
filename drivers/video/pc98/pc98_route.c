/* GF_DRIVER_DESIGN §4-3. Byte I/O; never replay the port's upper bits. */
#include "pc98_route.h"
#include "io.h"
#include "pc98_cell.h"
#include "geforce_module.h"
#include "display_restore.h"
#include "console.h"
#include "memmap.h"
#include "display_private.h"
#include "../../../gfx/gfx_internal.h"

static int console_safe = 1;
void pc98_console_set_safe(int safe) { console_safe = safe; }
int pc98_console_safe(void) { return console_safe; }

static int route_set(u8 value)
{
    u8 old = inp(PC98_ROUTE_PORT);
    if ((old & PC98_ROUTE_MASK) != value) outp(PC98_ROUTE_PORT, value);
    return (inp(PC98_ROUTE_PORT) & PC98_ROUTE_MASK) == value ? 0 : -1;
}
int pc98_route_safe(void)
{
    return !(inp(PC98_ROUTE_PORT) & PC98_ROUTE_MASK) && geforce_pc98_safe();
}
static int cell_access(void *opaque, u32 step, bool write, u32 *value)
{
    (void)opaque; (void)step;
    volatile u16 *cell = P2V_IO(DISPLAY_RECOVER_CELL);
    if (write) *cell = (u16)*value;
    else *value = *cell;
    return 0;
}
int pc98_route_cell_check(void)
{
    if (!pc98_route_safe()) return -1;
    return pc98_cell_check(cell_access, 0);
}
int pc98_route_to_gf(void)
{
    return route_set(PC98_ROUTE_GF);
}
int pc98_route_select_98(void) { return route_set(PC98_ROUTE_98); }
int pc98_route_to_98(void)
{
    /* Route even when the GF identity is lost. No 98 access until safe. */
    int rc = pc98_route_select_98();
    pc98_console_set_safe(!rc && pc98_route_safe());
    if (!console_safe) return -1;
    int height = gfx_current_height, flip = gfx_flip_enabled, page = gfx_display_page;
    display_restore_98();
    /* Live GUI geometry is retained until its existing shutdown boundary. */
    gfx_current_height = height; gfx_flip_enabled = flip; gfx_display_page = page;
    if (pc98_route_cell_check()) return -1;
    console_hw_cursor_enable();
    return 0;
}
