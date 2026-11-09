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
