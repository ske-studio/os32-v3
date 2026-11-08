#include "gfx_policy.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "con_sink.h"
#include "pgalloc.h"
#include "../exec/appslot.h"

u16 gfx_policy_gen = 1;
int gfx_scan_changed;

int gfx_fullscreen_owner(void)
{
    return appslot_gfx_owner() != GFX_OWNER_WM;
}

int gfx_direct_wanted(void)
{
    const GfxDirectOps *d = g_backend ? g_backend->direct_ops : 0;
    return con_sink_is_enabled() && !gfx_fullscreen_owner() &&
           d && d->fill_rect && d->blit && d->fill_rop && d->put && d->get &&
           g_backend->direct_ok && g_backend->direct_ok();
}

static void gfx_policy_gen_bump(void)
{
    if (++gfx_policy_gen == 0) gfx_policy_gen = 1;
}

void gfx_policy_reeval(void)
{
    if (!g_backend) return;
    u8 want = gfx_direct_wanted() ? GFX_POLICY_DIRECT : GFX_POLICY_SHADOW;
    if (want != g_backend->policy) {
        if (want == GFX_POLICY_DIRECT) {
            gfx_reinit_surface_roles(0, 0);
            gfx_reinit_surface_roles(1, 0);
        }
        /* Publish hooks only after old mappings and tokens are invalidated. */
        const GfxDirectOps *d = g_backend->direct_ops;
        g_backend->fill_rect = want ? d->fill_rect : g_backend->shadow_fill_rect;
        g_backend->blit = want ? d->blit : g_backend->shadow_blit;
        g_backend->fill_rop = want ? d->fill_rop : 0;
        g_backend->put = want ? d->put : 0;
        g_backend->mono = want ? d->mono : 0;
        g_backend->get = want ? d->get : 0;
        g_backend->policy = want;
        gfx_policy_gen_bump();
    } else if (gfx_scan_changed) {
        gfx_policy_gen_bump();
    }
    gfx_scan_changed = 0;
}

int gfx_policy_blocks_map(u32 role)
{
    return g_backend && g_backend->policy == GFX_POLICY_DIRECT &&
           (role == LEDGER_ROLE_CLIENT || role == LEDGER_ROLE_DISPLAY);
}
