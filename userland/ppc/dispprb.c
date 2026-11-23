/* Stage 5c A5/A6: OF INDEX8 mode, CLIENT surface descriptor and OF
 * four-byte transfer accounting, including the reserved rows. */
#include "os32api.h"
#include "../../include/app_ids.h"
#include <stdio.h>
#include <string.h>

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

static int report_head(KernelAPI *api, const GFX2_HeadInfo *head)
{
    char line[160];

    snprintf(line, sizeof(line),
             "DISPLAY HEAD %lu %lu %lu %lu %d %d %u %u %lu %lu %lu %u %u\n",
             head->mode_id, head->head, head->attached, head->connector_mask,
             head->x, head->y, (unsigned)head->w, (unsigned)head->h,
             head->rotation, head->scanout_sid, head->fmt_scanout,
             (unsigned)head->max_w, (unsigned)head->max_h);
    return report(api, line, 0);
}

static int create_tile(KernelAPI *api, u32 *sid)
{
    return api->gfx2_surface_create(0, 8, 2, GFX2_FMT_INDEX8,
                                    GFX2_POOL_SYSMEM, GFX2_USE_RENDER2D, sid);
}

static int lifecycle(KernelAPI *api, const char *mode)
{
    const u32 mark = 0xa5a5a5a5;
    u32 sid = mark, gen = mark, pitch = mark, done = mark, fence = mark;
    u32 completed = mark, failed = mark, after = mark;
    void *va = (void *)mark;
    if (!strcmp(mode, "capacity")) {
        unsigned count = 0;
        int rc;
        while (!(rc = create_tile(api, &sid))) count++;
        if (!count || rc != OS32_ERR_NOSPC) return 20;
        char line[48];
        snprintf(line, sizeof(line), "free tiles %u\n", count);
        return report(api, line, 0);
    }
    if (!strcmp(mode, "child-exit")) {
        /* Observe owner teardown before the resident shell can restore the screen. */
        if (api->gfx2_adapter_gen(0, &gen)) return 29;
        api->exec_run("/bin/dispprb.bin leave");
        int kind, code;
        if (api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code) return 28;
        if (api->gfx2_adapter_gen(0, &after)) return 29;
        if (gen == after) return report(api, "child left display active\n", 31);
        return report(api, "child display restored\n", 0);
    }
    if (!strcmp(mode, "shutdown")) {
        api->gfx_shutdown();
        return report(api, "shutdown only\n", 0);
    }
    api->gfx_init();
    if (api->gfx2_adapter_gen(0, &gen) || gen == mark) return 21;
    int rc = create_tile(api, &sid);
    if (rc == OS32_ERR_NOSYS) return report(api, "tile provider absent\n", 22);
    if (rc || sid == mark) return 23;
    if (!strcmp(mode, "fault")) {
        report(api, "tile allocated\n", 0);
        __asm__ volatile("trap" : : : "memory");
        return 24;
    }
    if (!strcmp(mode, "leave")) return report(api, "tile left\n", 0);
    GFX2_Op fill = {.op = GFX2_OP_FILL, .dst = sid, .dw = 8, .dh = 2,
                    .rop = GFX2_ROP_PATCOPY, .fg = {.v = 7}};
    if (api->gfx2_submit(0, &fill, 1, gen, &done, &fence) || done != 1 ||
        fence == mark || api->gfx2_fence_wait(0, fence, 1000) ||
        api->gfx2_fence_query(0, &completed, &failed) ||
        completed == mark || failed == mark || (i32)(completed - fence) < 0 ||
        (failed && (i32)(failed - fence) >= 0)) return 25;
    if (api->gfx2_lock(sid, GFX2_LOCK_READONLY, &va, &pitch) ||
        !va || va == (void *)mark || pitch == mark || pitch < 8) return 26;
    int drawn = ((u8 *)va)[pitch + 7] == 7;
    if (api->gfx2_unlock(sid) || !drawn || api->gfx2_surface_free(sid)) return 27;
    api->exec_run("/bin/hello.bin ok");
    int kind = -1, code = -1;
    if (api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code != 7) return 28;
    if (api->gfx2_adapter_gen(0, &after)) return 29;
    if (after != gen) return report(api, "child changed generation\n", 30);
    api->gfx_shutdown();
    return report(api, "tile drawn\n", 0);
}

/* The CLIENT lease maps the INDEX8 RAM shadow. The host reads the first
 * word as a rendezvous in the reserved band, and samples pixels by QMP. */
static void checkpoint(KernelAPI *api, volatile u8 *shadow, int key)
{
    volatile u32 *word = (volatile u32 *)shadow;
    u32 saved = *word;
    *word = 0x50524200u + key;
    while ((api->kbd_getkey() & 0xff) != key) {}
    *word = saved;
}

static void present_six(KernelAPI *api)
{
    static GFX_RasterPalTable raster = {.count = 1};
    api->gfx_add_dirty_rect(704, 560, 8, 16);
    api->gfx_present_dirty();
    api->gfx_present_rect(704, 560, 8, 16);
    api->gfx_present_nosync();
    api->gfx_present_raster(&raster);
    api->gfx_present();
}

static int present_probe(KernelAPI *api, const char *mode, u8 white)
{
    OS32_SurfaceQueryResult q;
    OS32_LeaseView view;
    GFX2_SurfaceDesc desc;
    u32 sid = 0xa5a5a5a5, app, owner, generation;
    int child = !strcmp(mode, "v1-child");
    if (api->caller_identity(&app, &owner, &generation)) return 40;
    if (child) {
        if (api->gfx_screen_owner() == (int)app ||
            api->gfx_screen_owner() == APP_ID_SHELL) return 41;
        present_six(api);
        return 0;
    }
    int shutdown = !strcmp(mode, "v1-shutdown");
    if (!strcmp(mode, "v1-init")) api->gfx_init_200();
    else api->gfx_init();
    if (api->gfx_screen_owner() != (int)app)
        return report(api, "DISPLAY init absent\n", 42);
    if (api->gfx2_screen_surface(0, 0, GFX2_ROLE_CLIENT, &sid) ||
        api->gfx2_surface_desc(sid, &desc) || desc.w != 800 || desc.h != 600)
        return 43;
    if (api->surface_query(OS32_SURFACE_CLIENT, &q) || q.count != 1 ||
        q.desc[0].pitch < 800 || q.desc[0].height != 600 ||
        api->surface_lease(OS32_SURFACE_CLIENT, &q.desc[0].ref, OS32_SURFACE_RW, &view))
        return 44;
    u32 pitch = q.desc[0].pitch;
    u8 *shadow = (u8 *)view.planes[0];
    if (!strcmp(mode, "v1-framebuffer")) {
        GFX_ScreenInfo info = {0};
        GFX_Framebuffer fb = {0};
        GFX_Stats before, after;
        api->gfx_screen_info(&info);
        if (info.width != 800 || info.height != 600 || info.bpp != 8 ||
            info.format != GFX_FMT_PACKED8 || info.lease_mask ||
            info.lease_first != 16 || info.lease_count != 240) return 49;
        api->gfx_get_framebuffer(&fb);
        if (!fb.planes[0] || fb.planes[1] || fb.planes[2] || fb.planes[3] ||
            fb.width != desc.w || fb.height != desc.h || (u32)fb.pitch != pitch)
            return report(api, "DISPLAY framebuffer failed\n", 50);
        shadow = fb.planes[0];
        if (api->gfx_stats(&before)) return 51;
        api->gfx_present_rect(705, 560, 6, 2);
        if (api->gfx_stats(&after) || after.present_bytes - before.present_bytes != 16 ||
            after.commits - before.commits != 1)
            return report(api, "DISPLAY counters failed\n", 52);
        before = after;
        api->gfx_present_rect(705, 0, 6, 1);
        if (api->gfx_stats(&after) || after.present_bytes != before.present_bytes ||
            after.commits - before.commits != 1) return 53;
    }
    for (int y = 560; y < 576; y++)
        memset(shadow + y * pitch + 704, white ^ 255, 96);
    api->gfx_present_rect(704, 560, 96, 16);
    if (shutdown) {
        if (api->surface_unlease(view.token)) return 45;
        api->gfx_shutdown();
        if (api->surface_query(OS32_SURFACE_CLIENT, &q) || q.count != 1 ||
            api->surface_lease(OS32_SURFACE_CLIENT, &q.desc[0].ref, OS32_SURFACE_RW, &view))
            return 46;
        shadow = (u8 *)view.planes[0];
        pitch = q.desc[0].pitch;
    }
    checkpoint(api, shadow, 'b');
    for (int y = 560; y < 576; y++)
        memset(shadow + y * pitch + 704, white, 96);
    if (shutdown) present_six(api);
    else if (!strcmp(mode, "v1-owner")) {
        int kind, code;
        api->exec_run("/bin/dispprb.bin v1-child");
        if (api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code)
            return 47;
    } else if (!strcmp(mode, "v1-init") || !strcmp(mode, "v1-framebuffer")) api->gfx_present_rect(704, 560, 8, 16);
    else if (!strcmp(mode, "v1-full")) api->gfx_present();
    else {
        int edge = !strcmp(mode, "v1-edge");
        api->gfx_add_dirty_rect(edge ? 799 : 704, 560, edge ? 1 : 64, 16);
        api->gfx_present_dirty();
    }
    checkpoint(api, shadow, 'e');
    return api->surface_unlease(view.token) ? 48 : 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    const u32 marker = 0xa5a5a5a5u;
    u32 seen = marker;
    const u32 rgb = 0x00ffffff;
    static const u16 gamma[768] = {0};
    GFX2_HeadInfo before, after;

    if ((argc == 2 || argc == 3) && !strncmp(argv[1], "v1-", 3))
        return present_probe(api, argv[1], argc == 3 && !strcmp(argv[2], "255") ? 255 : 0);
    if (argc == 2 && strcmp(argv[1], "head")) return lifecycle(api, argv[1]);
    if (api->gfx_screen_owner() != APP_ID_SHELL)
        return report(api, "DISPLAY owner failed\n", 1);
    if (api->gfx2_head_info(0, 0, &before) || !before.attached)
        return report(api, "DISPLAY OF head missing\n", 2);
    if (argc == 2 && !strcmp(argv[1], "head"))
        return report_head(api, &before);
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
    report_head(api, &before);
    return report(api, "DISPLAY OK\n", 0);
}
