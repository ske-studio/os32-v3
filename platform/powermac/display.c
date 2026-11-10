/* Compile-only provider. PowerMac display hardware is not implemented.
 * The target profile continues to reject kernel links and boot products. */
#include "display_restore.h"

void display_palette4_write(int index, u8 r, u8 g, u8 b)
{
    (void)index;
    (void)r;
    (void)g;
    (void)b;
}

/* Unsupported compile-only scroll port; boot remains rejected. */
void display_scroll(int y, int height, int flip, int page)
{
    (void)y;
    (void)height;
    (void)flip;
    (void)page;
}

/* Unsupported compile-only planar bring-up ports; boot remains rejected. */
void display_text_hide(void) { }
void display_planar16(void) { }
void display_planar_start(int legacy200) { (void)legacy200; }
void display_flip_pages(int display, int access) { (void)display; (void)access; }
void gfx_clear_planar_pages(u32 plane_size) { (void)plane_size; }
