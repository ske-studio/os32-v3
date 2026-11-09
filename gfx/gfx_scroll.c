#include "gfx_internal.h"
#include "display_restore.h"

int vram_scroll_y = 0;

void gfx_scroll_init(void)
{
    vram_scroll_y = 0;
    gfx_hardware_scroll(0);
}

void gfx_hardware_scroll(int lines)
{
    if (!bb_b) return;
    vram_scroll_y += lines % gfx_current_height;
    if (vram_scroll_y < 0) vram_scroll_y += gfx_current_height;
    vram_scroll_y %= gfx_current_height;

    display_scroll(vram_scroll_y, gfx_current_height,
                   gfx_flip_enabled, gfx_display_page);
}
