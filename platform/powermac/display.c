/* OF keeps its firmware mode and CLUT. Legacy planar ports have no
 * equivalent on PowerMac; packed output is owned by backend_of_fb. */
#include "display_restore.h"

void display_palette4_write(int index, u8 r, u8 g, u8 b)
{
    (void)index;
    (void)r;
    (void)g;
    (void)b;
}

/* Scrolling is performed in the console shadow. */
void display_scroll(int y, int height, int flip, int page)
{
    (void)y;
    (void)height;
    (void)flip;
    (void)page;
}

/* No planar hardware on this platform. */
void display_text_hide(void) { }
void display_planar16(void) { }
void display_planar_start(int legacy200) { (void)legacy200; }
void display_flip_pages(int display, int access) { (void)display; (void)access; }
void gfx_clear_planar_pages(u32 plane_size) { (void)plane_size; }
/* OF offers no vblank or planar access. */
void display_wait_vsync(void) { }
u8 *display_planar_plane(unsigned index) { (void)index; return 0; }
