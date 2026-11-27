static void gfx2_register_other(GfxBackend *be, void *arg)
{
    (void)arg;
    u32 kind = display_provider_ledger(be);
    if (kind && be != g_backend) (void)gfx2_adapter_register(be, kind, 0);
}

static int gfx2_register_selected(void)
{
    static int enumerated;
    u32 kind = display_provider_ledger(g_backend);
    if (!kind) return 0;
    int rc = gfx2_adapter_register(g_backend, kind, GFX2_AF_ACTIVE);
    if (rc) return rc == OS32_ERR_NOSYS ? 0 : rc; /* v1-only build */
    if (!enumerated) {
        enumerated = 1;
        display_provider_each(gfx2_register_other, NULL);
    }
    return 0;
}

void gfx_shutdown(void)
{
    /* Internal cleanup/WM remains trusted. In GUI, USER may stop only its own
     * fullscreen display, never acquire it merely by calling shutdown. */
    extern int ring3_call_from_user(void);
    if (ring3_call_from_user() && con_sink_is_enabled()) {
        struct caller_access caller;
        if (!caller_access_get_user(&caller) ||
            caller.app_id != appslot_gfx_owner()) return;
    }
    display_provider_raster_reset();
    gfx_started = 0;
    gfx_policy_reeval();
    gfx_reinit_surfaces(0);
    /* Revoke v1 leases even when the console already owns the live scanout. */
    if (display_provider_console_owns_scanout()) goto rebind;
    /* 表示出力を戻し (leave)、バックエンドのハードウェア終了処理へ。
     * 9801 では leave は空、shutdown がフリップ解除 + GDC 表示停止を行う
     * (旧 gfx_shutdown の本体は backend_pc98.c の pc98_shutdown に移設)。 */
    if (g_backend && g_backend->leave) g_backend->leave();
    if (g_backend && g_backend->shutdown) g_backend->shutdown();
    if (display_provider_restore_fallback(g_backend)) {
        gfx_current_height = GFX_HEIGHT;
        gfx_flip_enabled = gfx_display_page = 0;
        /* The selected output is now 98 even while GeForce remains usable. */
        gfx_select_mode(DISPLAY_FALLBACK_BACKEND, 0);
        g_backend = DISPLAY_FALLBACK_BACKEND;
        int rc = gfx2_register_selected();
        gfx_bind_client();
        if (rc) {
            /* Restore the console with graphics leases still unpublished. */
            display_provider_console_return();
            return;
        }
    } else {
        struct gfx2_adapter *a = gfx2_active();
        if (a) gfx2_detach_stopped_heads(a);
    }
rebind:
    gfx_reinit_surfaces(1);
    display_provider_console_return();
}
