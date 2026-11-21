/* Stage 5c A7/A8. OF displays the INDEX8 shadow on present and the
 * fixed-size SYSMEM scanout on mode_set/flip. See PPC_KAPI_FILL_DESIGN. */
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
        api->shell_putchar(*text++, ATTR_WHITE);
    return status;
}

static void checkpoint(KernelAPI *api, u8 *shadow, int key)
{
    volatile u32 *word = (volatile u32 *)shadow;
    u32 saved = *word;
    *word = 0x50524200u + key;
    while ((api->kbd_getkey() & 255) != key) {}
    *word = saved;
}

static int drawing(KernelAPI *api, u8 white)
{
    GFX_Framebuffer fb = {0};
    GFX_Stats before, after;
    u32 alias, gen, done, fence, scan, small;
    GFX2_Op fill = {.op = GFX2_OP_FILL, .dx = 705, .dy = 560,
                    .dw = 14, .dh = 2, .rop = GFX2_ROP_PATCOPY, .fg = {.v = white}};
    api->gfx_init();
    api->gfx_get_framebuffer(&fb);
    if (!fb.planes[0] || fb.pitch < 800) return 20;
    u8 *shadow = fb.planes[0], *row = shadow + 560 * fb.pitch;
    for (int y = 0; y < 2; y++) memset(row + y * fb.pitch + 700, white ^ 255, 24);
    api->gfx_present_rect(700, 560, 24, 2);
    if (api->gfx2_screen_surface(0, 0, GFX2_ROLE_DISPLAY, &alias) ||
        api->gfx2_adapter_gen(0, &gen)) return 21;
    /* v1_bridge_end records a full DISPLAY write for the setup present. */
    if (api->gfx2_present(0, 0, 0, 0, FB_WIDTH, FB_HEIGHT)) return 23;
    fill.dst = alias;
    if (api->gfx2_submit(0, &fill, 1, gen, &done, &fence) || done != 1 ||
        api->gfx2_fence_wait(0, fence, 1000)) return 22;
    checkpoint(api, shadow, 'b');
    if (api->gfx_stats(&before) || api->gfx2_present(0, 0, 705, 560, 6, 2) ||
        api->gfx_stats(&after)) return 23;
    if (after.commits - before.commits != 1 || after.present_bytes - before.present_bytes != 16)
        return report(api, "GFX2 present counters failed\n", 24);
    checkpoint(api, shadow, 'p');
    if (api->gfx2_present(0, 0, 705, 560, 14, 2)) return 25;
    for (int y = 0; y < 2; y++)
        for (int x = 700; x < 724; x++)
            if (row[y * fb.pitch + x] != (x >= 705 && x < 719 ? white : (white ^ 255)))
                return 26;
    if (api->gfx_stats(&before) || api->gfx2_present(0, 0, 705, 560, 14, 2) ||
        api->gfx_stats(&after) || after.commits != before.commits ||
        after.present_bytes != before.present_bytes) return 27;
    checkpoint(api, shadow, 'e');
    fill.dy = 0;
    if (api->gfx2_submit(0, &fill, 1, gen, &done, &fence) || done != 1 ||
        api->gfx2_fence_wait(0, fence, 1000) || api->gfx_stats(&before) ||
        api->gfx2_present(0, 0, 705, 0, 14, 2) || api->gfx_stats(&after) ||
        after.present_bytes != before.present_bytes || after.commits - before.commits != 1)
        return 35;
    fill.dy = 560;
    if (api->gfx2_surface_create(0, 800, 600, GFX2_FMT_INDEX8, GFX2_POOL_SYSMEM,
                                 GFX2_USE_SCANOUT | GFX2_USE_RENDER2D, &scan) ||
        api->gfx2_surface_create(0, 8, 2, GFX2_FMT_INDEX8, GFX2_POOL_SYSMEM,
                                 GFX2_USE_SCANOUT, &small)) return 28;
    if (api->gfx2_mode_set(0, 0, 1, 2, scan) != OS32_ERR_INVAL ||
        api->gfx2_mode_set(0, 0, 1, 1, small) != OS32_ERR_INVAL ||
        api->gfx2_flip(0, 0, scan, GFX2_FLIP_VBLANK, 1000) != OS32_ERR_NOSYS) return 29;
    fill.dst = scan;
    fill.dx = 700;
    fill.dw = 24;
    if (api->gfx2_submit(0, &fill, 1, gen, &done, &fence) || done != 1 ||
        api->gfx2_mode_set(0, 0, 1, 1, scan)) return 30;
    checkpoint(api, shadow, 'm');
    fill.fg.v = white ^ 255;
    if (api->gfx2_submit(0, &fill, 1, gen, &done, &fence) || done != 1 ||
        api->gfx2_flip(0, 0, scan, 0, 1000)) return 31;
    checkpoint(api, shadow, 'f');
    api->gfx_shutdown();
    return report(api, "GFX2 drawing OK\n", 0);
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

    if (argc == 2 && !strcmp(argv[1], "lease")) {
        /* A9d: after gfx_shutdown the returned framebuffer lease is revoked,
         * so this store must be refused by the page protection. */
        GFX_Framebuffer fb = {0};
        const char *marker = "lease released\n";
        api->gfx_init();
        api->gfx_get_framebuffer(&fb);
        volatile u32 *plane = (volatile u32 *)fb.planes[0];
        api->gfx_shutdown();
        if (!plane) return 1;
        while (*marker) api->shell_putchar(*marker++, ATTR_WHITE);
        *plane = 0;
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "draw"))
        return drawing(api, !strcmp(argv[2], "255") ? 255 : 0);
    if (argc == 2 && (!strcmp(argv[1], "shutdown") || !strcmp(argv[1], "shutdown-off"))) {
        u32 scan;
        api->gfx_init();
        if (api->gfx2_surface_create(0, 800, 600, GFX2_FMT_INDEX8, GFX2_POOL_SYSMEM,
                                     GFX2_USE_SCANOUT, &scan)) return 33;
        api->gfx_shutdown();
        if (api->gfx2_mode_set(0, 0, 1, 1, scan) != OS32_ERR_BUSY ||
            api->gfx2_flip(0, 0, scan, 0, 1000) != OS32_ERR_BUSY ||
            api->gfx2_present(0, 0, 704, 560, 8, 2) != OS32_ERR_BUSY) return 34;
        if (!strcmp(argv[1], "shutdown-off")) return 0;
        report(api, "GFX2 display restored\n", 0);
        while ((api->kbd_getkey() & 255) != 'r') {}
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "owner"))
        return api->gfx2_present(0, 0, 0, 0, 0, 0) == OS32_ERR_BUSY ? 0 : 32;
    if (argc == 2 && !strcmp(argv[1], "owned-child")) {
        int kind, code;
        api->gfx_init();
        api->exec_run("/bin/gfx2prob.bin owner");
        if (api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code) return 36;
        api->gfx_shutdown();
        return 0;
    }
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
        mode.id != 1 || mode.fmt != GFX2_FMT_INDEX8 || head.mode_id != mode.id)
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
