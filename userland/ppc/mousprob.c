#include "os32api.h"
#include <string.h>

static int check(KernelAPI *api, const char *command)
{
    int gone = !strcmp(command, "gone");
    if (api->mouse_available() != !gone) return 1;
    MouseInfo m;
    memset(&m, 0xa5, sizeof(m));
    api->mouse_poll(&m);
    if (m.mode != !gone) return 2;
    if (!strcmp(command, "edge"))
        return m.x == 799 && m.y == 599 ? 0 : 3;
    if (!strcmp(command, "motion"))
        return m.x == 88 && m.y == 153 && m.dx == 12 && m.dy == -7 &&
               m.buttons == 1 ? 0 : 4;
    if (gone || !strcmp(command, "present"))
        return m.dx || m.dy || m.buttons ? 5 : 0;
    if (!strcmp(command, "start")) {
        api->mouse_set_bounds(80, 160, 80, 160);
        api->mouse_set_bounds(72, 144, 88, 176);
        return 0;
    }
    if (!strcmp(command, "cursor")) {
        int key;
        api->mouse_cursor_set_mode(MOUSE_CURSOR_TEXT);
        /* Hold each frame until the host has sampled it, before RSH output. */
        /* A doubled or repeated key from the previous step is skipped. */
        if ((api->kbd_getkey() & 0xff) != 's') return 6;
        api->mouse_cursor_show();
        while (((key = api->kbd_getkey()) & 0xff) == 's') {}
        if ((key & 0xff) != 'h') return 6;
        api->mouse_cursor_hide();
        while (((key = api->kbd_getkey()) & 0xff) == 'h') {}
        return (key & 0xff) == 'a' ? 0 : 6;
    }
    return 7;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int rc = argc == 2 ? check(api, argv[1]) : 7;
    api->shell_putchar('0' + rc, 7);
    api->shell_putchar('\n', 7);
    return rc;
}
