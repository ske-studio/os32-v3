#include "os32api.h"
#include "../../include/app_ids.h"

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    const u32 marker = 0xa5a5a5a5u;
    u32 seen = marker;
    const u32 rgb = 0x00ffffff;
    static const u16 gamma[768] = {0};
    GFX2_HeadInfo before, after;

    (void)argc;
    (void)argv;
    if (api->gfx_screen_owner() != APP_ID_SHELL)
        return report(api, "DISPLAY owner failed\n", 1);
    if (api->gfx2_head_info(0, 0, &before) || !before.attached)
        return report(api, "DISPLAY OF head missing\n", 2);
    if (api->gfx2_vblank_wait(0, 1, 0, 1, &seen) != OS32_ERR_INVAL ||
        seen != marker)
        return report(api, "DISPLAY vblank validation failed\n", 3);
    if (api->gfx2_vblank_wait(0, 0, 0, 1, &seen) != OS32_ERR_NOSYS ||
        seen != marker)
        return report(api, "DISPLAY vblank support failed\n", 4);

    /* RSH runs this application as a child of the CUI display owner. */
    if (api->gfx2_palette(0, 0, 1, &rgb) != OS32_ERR_BUSY ||
        api->gfx2_gamma(0, 0, gamma) != OS32_ERR_BUSY ||
        api->gfx2_cursor_move(0, 0, 5, 5) != OS32_ERR_BUSY ||
        api->gfx2_cursor_show(0, 0, 1) != OS32_ERR_BUSY ||
        api->gfx2_dpms(0, 0, GFX2_DPMS_OFF) != OS32_ERR_BUSY)
        return report(api, "DISPLAY ownership checks failed\n", 5);
    if (api->gfx2_head_info(0, 0, &after) ||
        before.mode_id != after.mode_id || before.connector_mask != after.connector_mask ||
        before.w != after.w || before.h != after.h || before.attached != after.attached ||
        api->gfx_screen_owner() != APP_ID_SHELL)
        return report(api, "DISPLAY state changed\n", 6);
    return report(api, "DISPLAY OK\n", 0);
}
