/* Public USER framebuffer compatibility lease bridge. */
/* Public USER compatibility bridge; regen invalidates the held token.
 * One compatibility CLIENT token per AS, inside the existing eight slots.
 * The saved caller origin, never current CPL/CR3, selects TRUSTED vs USER.
 * No callbacks/scheduling from publisher construction through copyout. */
volatile u32 gfx_bridge_user_count, gfx_bridge_last_va;
volatile u32 gfx_bridge_fail_count __attribute__((section(".bss.gfx_fb_bridge")));
__attribute__((section(".text.gfx_fb_bridge")))
void gfx_framebuffer_bridge(void *out)
{
    struct caller_access caller;
    struct surface_query_source source;
    struct gfx_kernel_fb kernel;
    GFX_Framebuffer fb = {0};
    struct lease_view view;
    struct lease_authority auth;
    struct as_lease *held = 0;
    unsigned int flags = irq_save();
    int user = ring3_call_from_user();
    int valid = caller_access_get(&caller), rc = OS32_ERR_INVAL;
    u32 i, acquired = 0;
    int bad_output = !valid || !check_caller_write_range(&caller, out, sizeof(fb));
    irq_restore(flags);
    if (bad_output) goto fail;
    if (caller.origin == CALLER_TRUSTED) {
        if (gfx_kernel_framebuffer(&kernel)) goto fail;
        for (i = 0; i < SURFACE_QUERY_MAX; i++) fb.planes[i] = kernel.planes[i];
        fb.width = kernel.width; fb.height = kernel.height; fb.pitch = kernel.pitch;
        if (copy_to_caller(&caller, out, &fb, sizeof(fb))) return;
        bad_output = 1;
        goto fail;
    }
    /* USER pre-init is not drawable. CPL0 discovery still uses selected alias. */
    if (gfx_surface_source(LEDGER_ROLE_CLIENT, &source)) goto fail;
    flags = irq_save();
    rc = surface_query_authorize(&source, &caller);
    if (!rc) rc = surface_query_refs(&source, source.refs, 1, LEDGER_PERM_RW);
    irq_restore(flags);
    if (rc) goto fail;
    for (i = 0; i < MEM_LEASE_MAX; i++) {
        struct as_lease *l = &caller.as->leases[i];
        if (l->token && (l->flags & AS_LEASE_GFX_COMPAT)) {
            if (l->sid == source.refs[0].sid &&
                l->generation == source.refs[0].generation) held = l;
            else if (lease_release(caller.as, l->token)) goto fail;
        }
    }
    if (!held) {
        auth = (struct lease_authority){caller.owner, source.backend, source.role};
        if (lease_acquire(caller.as, &auth, source.refs, 1, LEDGER_PERM_RW, &view)) goto fail;
        acquired = view.token;
        for (i = 0; i < MEM_LEASE_MAX; i++)
            if (caller.as->leases[i].token == view.token) held = &caller.as->leases[i];
        held->flags |= AS_LEASE_GFX_COMPAT;
    }
    if (gfx_kernel_framebuffer(&kernel)) goto fail;
    const struct ledger_surface *sf = &ledger_surfaces[held->sid];
    for (i = 0; i < sf->planes; i++)
        fb.planes[i] = (u8 *)(held->base + sf->plane_offset[i]);
    fb.width = kernel.width; fb.height = kernel.height; fb.pitch = kernel.pitch;
    if (copy_to_caller(&caller, out, &fb, sizeof(fb))) {
        gfx_bridge_user_count++;
        gfx_bridge_last_va = held->base;
        return;
    }
    bad_output = 1;
fail:
    if (acquired) (void)lease_release(caller.as, acquired);
    fb = (GFX_Framebuffer){0};
    if (!bad_output && !copy_to_caller(&caller, out, &fb, sizeof(fb))) bad_output = 1;
    gfx_bridge_fail_count++;
    /* Clear the checked output before ABORT_PENDING makes B1 refuse writes.
     * As in gfx_kapi_claim, the syscall exit performs the actual termination. */
    int current = appslot_cur();
    AppSlot *slot = appslot_get(current);
    if (bad_output && (valid ? caller.origin == CALLER_USER : user) && current >= APP_ID_MIN &&
        slot && slot->state == APP_STATE_RUNNING) slot->abort_req = 1;
}
