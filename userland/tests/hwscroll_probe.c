/* Public-API-only, fixed-step PC-98 hardware scroll observation harness.
 * Row code at x=32..319 is binary y, MSB first; each bit is 32 pixels.
 * TVRAM labels do not present or alter the graphics buffer being observed.
 */
#include "os32api.h"
#include "libos32gfx.h"
#include <stdio.h>

static KernelAPI *api;

static int pause_at(int height, int step, int lines, int offset,
                    const char *phase)
{
    char label[80];
    int x;
    snprintf(label, sizeof(label), "H=%d step=%d delta=%d sum=%d %s SPACE=next ESC=quit",
             height, step, lines, offset, phase);
    for (x = 0; x < 80; x++) {
        char ch = label[x];
        if (!ch) break;
        api->tvram_putchar_at(x, 0, ch, ATTR_WHITE);
    }
    for (; x < 80; x++) api->tvram_putchar_at(x, 0, ' ', ATTR_WHITE);
    for (;;) {
        int key = libos32gfx_getchar();
        if (key == 27) return 0;
        if (key == ' ') return 1;
    }
}

static void draw_rows(int height)
{
    int y, bit;
    gfx_clear(0);
    for (y = 0; y < height; y++) {
        gfx_fill_rect(0, y, 640, 1, (u8)(1 + y % 15));
        for (bit = 0; bit < 9; bit++) {
            gfx_fill_rect(32 + bit * 32, y, 32, 1,
                          (y & (1 << (8 - bit))) ? 15 : 0);
        }
        gfx_fill_rect(0, y, 8, 1, (y & 1) ? 15 : 0);
        gfx_fill_rect(632, y, 8, 1, (y & 1) ? 0 : 15);

    }
    for (y = 0; y + 16 <= height; y += 16) {
        char number[12];
        snprintf(number, sizeof(number), "row %03d", y);
        kcg_draw_utf8(352, y, number, 15, 0);
    }
}

static int probe_mode(int height)
{
    int values[7] = {-1, 0, 1, height - 1, height, height + 1,
                     -3 * height - 1};
    int i, offset = 0;
    if (height == 200) api->gfx_init_200();
    else api->gfx_init();
    if (libos32gfx_attach_checked() != 0) return 0;
    if (gfx_fb.width != 640 || gfx_fb.height != height) return 0;
    /* Deterministic grayscale, shared by both revisions. */
    for (i = 0; i < 16; i++) api->gfx_set_palette(i, i, i, i);
    draw_rows(height);
    /* Populate both present targets before the first scroll observation. */
    for (i = 0; i < 2; i++) {
        api->gfx_add_dirty_rect(0, 0, 640, height);
        api->gfx_present_dirty();
    }
    /* Drain the previous full-screen dirty snapshot before scroll begins. */
    api->gfx_present_dirty();
    if (!pause_at(height, 0, 0, 0, "INIT")) return 0;
    for (i = 0; i < 7; i++) {
        int p;
        api->gfx_hardware_scroll(values[i]);
        offset = (offset + values[i] % height + height) % height;
        if (!pause_at(height, i + 1, values[i], offset, "CALL")) return 0;
        for (p = 1; p <= 2; p++) {
            /* A small changed marker forces a dirty present, hence a flip
             * on the planar backend. Keep the row code and edges intact. */
            gfx_fill_rect(560, 32, 48, 16, (u8)(p == 1 ? 15 : 4));
            api->gfx_add_dirty_rect(560, 32, 48, 16);
            api->gfx_present_dirty();
            if (!pause_at(height, i + 1, values[i], offset,
                          p == 1 ? "PRESENT1" : "PRESENT2")) return 0;
        }
    }
    return 1;
}

void __cdecl main(int argc, char **argv, KernelAPI *kapi)
{
    (void)argc;
    (void)argv;
    api = kapi;
    libos32gfx_init(api);
    if (probe_mode(200)) {
        api->gfx_shutdown();
        (void)probe_mode(400);
    }
    api->gfx_shutdown();
    api->gfx_init();
    api->gfx_shutdown();
    api->tvram_clear();
    api->kprintf(ATTR_WHITE, "hwscroll_probe ended (guest comparison required).\r\n");
}
