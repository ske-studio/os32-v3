/* Acceptance-only link-SH probe. See KAPI_SPEC §4-8. */
#include "os32api.h"
#include <stdio.h>

static int cell_is(KernelAPI *api, int x, int y, u16 expected_code, u8 expected_attr)
{
    u16 code = 0;
    u8 attr = 0;
    api->tvram_readchar_at(x, y, &code, &attr);
    return code == expected_code && attr == expected_attr;
}

static int check_cells(KernelAPI *api)
{
    const int x = 3, y = 4;
    const u16 jis = 0x2422;
    const u8 attr = 0xe1, reversed = attr ^ 0x04;

    api->tvram_clear();
    api->tvram_putchar_at(x, y, 'A', attr);
    if (!cell_is(api, x, y, 'A', attr))
        return 11;
    api->tvram_putkanji_at(x + 1, y, jis, attr);
    if (!cell_is(api, x + 1, y, jis, attr) || !cell_is(api, x + 2, y, jis, attr))
        return 12;
    if (api->tvram_reverse_cell(x, y) != 1 ||
        api->tvram_reverse_cell(x + 2, y) != 2 ||
        !cell_is(api, x, y, 'A', reversed) ||
        !cell_is(api, x + 1, y, jis, reversed) ||
        !cell_is(api, x + 2, y, jis, reversed))
        return 13;
    api->tvram_scroll();
    if (!cell_is(api, x, y - 1, 'A', reversed) ||
        !cell_is(api, x + 1, y - 1, jis, reversed) ||
        !cell_is(api, x + 2, y - 1, jis, reversed) ||
        !cell_is(api, x, y, ' ', attr))
        return 14;
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;
    int status = check_cells(api);
    printf("celltest=%d\n", status);
    return status;
}
