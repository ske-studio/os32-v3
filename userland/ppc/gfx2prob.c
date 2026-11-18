#include "os32api.h"
#include <string.h>

enum {
    FB_WIDTH = 800,
    FB_HEIGHT = 600,
    SYNC_SHADOW = 1,
};

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    GFX2_AdapterInfo adapter;
    GFX2_HeadInfo head;
    GFX2_ConnectorInfo connector;
    GFX2_ModeInfo mode;
    GFX2_Caps caps;
    u32 generation = 0;
    u8 edid[128];
    const u32 index8 = 1u << GFX2_FMT_INDEX8;

    (void)argc;
    (void)argv;
    if (api->gfx2_adapter_count() != 1)
        return report(api, "GFX2 adapter count failed\n", 1);
    if (api->gfx2_adapter_info(0, &adapter) || strcmp(adapter.driver, "of_fb") ||
        adapter.heads != 1 || adapter.connectors != 1 || !(adapter.flags & GFX2_AF_ACTIVE) ||
        api->gfx2_adapter_gen(0, &generation) || !generation)
        return report(api, "GFX2 adapter info failed\n", 2);
    if (api->gfx2_head_info(0, 0, &head) || head.head != 0 || !head.attached ||
        head.connector_mask != 1 || head.w != FB_WIDTH || head.h != FB_HEIGHT ||
        head.fmt_scanout != index8)
        return report(api, "GFX2 head failed\n", 3);
    if (api->gfx2_connector_info(0, 0, &connector) || connector.id != 0 ||
        connector.type != GFX2_CONN_VGA || connector.status != GFX2_CONN_ON ||
        connector.possible_heads != 1 || connector.edid_blocks != 0)
        return report(api, "GFX2 connector failed\n", 4);
    if (api->gfx2_mode_count(0, 0, GFX2_FMT_INDEX8) != 1 ||
        api->gfx2_mode_count(0, 0, GFX2_FMT_RGB565) != 0 ||
        api->gfx2_mode_get(0, 0, GFX2_FMT_INDEX8, 0, &mode) ||
        mode.id != 1 || mode.fmt != GFX2_FMT_INDEX8)
        return report(api, "GFX2 mode failed\n", 5);
    if (mode.w != FB_WIDTH || mode.h != FB_HEIGHT)
        return report(api, "GFX2 mode dimensions failed\n", 6);
    if (api->gfx2_caps(0, &caps, sizeof(caps)) || caps.size != sizeof(caps) ||
        caps.adapter != 0 || caps.backend_kind != adapter.backend_kind ||
        caps.heads != 1 || caps.connectors != 1 || caps.palette_entries != 0 ||
        caps.fmt_surface != index8 || caps.fmt_render2d != index8 ||
        caps.fmt_scanout != index8 || !caps.caps2d_sw || caps.caps2d_hw ||
        caps.max_w != FB_WIDTH || caps.max_h != FB_HEIGHT ||
        caps.cpu_direct != 1 || caps.sync_mode != SYNC_SHADOW || caps.sync_profile[0])
        return report(api, "GFX2 caps failed\n", 7);

    /* The public query rejects block zero when the connector has no EDID. */
    memset(edid, 0xa5, sizeof(edid));
    if (api->gfx2_connector_edid(0, 0, 0, edid) != OS32_ERR_INVAL)
        return report(api, "GFX2 EDID result failed\n", 8);
    for (unsigned i = 0; i < sizeof(edid); i++)
        if (edid[i] != 0xa5)
            return report(api, "GFX2 EDID buffer changed\n", 9);
    return report(api, "GFX2 OK\n", 0);
}
