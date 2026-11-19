#include "os32api.h"
#include <string.h>

static int check(KernelAPI *api, const char *command)
{
    if (api->mouse_available() != 0) return 1;
    if (!strcmp(command, "start")) {
        api->mouse_set_bounds(80, 160, 80, 160);
        api->mouse_set_bounds(72, 144, 88, 176);
        return 0;
    }
    if (!strcmp(command, "cursor")) {
        api->mouse_cursor_set_mode(MOUSE_CURSOR_TEXT);
        api->mouse_cursor_hide();
        return 0;
    }
    if (!strcmp(command, "key")) return api->kbd_getkey() == 'a' ? 0 : 6;
    return (!strcmp(command, "gone") || !strcmp(command, "present")) ? 0 : 7;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int rc = argc == 2 ? check(api, argv[1]) : 7;
    api->shell_putchar('0' + rc, 7);
    api->shell_putchar('\n', 7);
    return rc;
}
