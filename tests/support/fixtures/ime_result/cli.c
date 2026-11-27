#include "os32api.h"
#include <string.h>

int ime_main(int argc, char **argv, KernelAPI *api);

static int accepts_mode;
static int mode;

static void set_mode(int requested)
{
    if (accepts_mode) mode = requested;
}

static int is_active(void)
{
    return mode != IME_MODE_OFF;
}

static int get_mode(void)
{
    return mode;
}

int main(int argc, char **argv)
{
    KernelAPI api = {
        .ime_set_mode = set_mode,
        .ime_is_active = is_active,
        .ime_get_mode = get_mode,
    };
    accepts_mode = strcmp(argv[1], "available") == 0;
    if (strcmp(argv[1], "stuck") == 0) mode = IME_MODE_KATAKANA;
    return ime_main(argc - 1, argv + 1, &api);
}
