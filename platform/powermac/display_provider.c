/* Compile-only display provider. PowerMac display hardware is not implemented.
 * The target profile continues to reject kernel links and boot products. */
#include "display_provider.h"

/* すべての口が NULL: probe も mode も無い。描画の入口は何もしない。 */
GfxBackend gfx_backend_unsupported = { .name = "none" };

int display_provider_pref(int pref)
{
    (void)pref;
    return GFX_PREF_AUTO;
}

GfxBackend *display_provider_select(int pref, int legacy200,
                                    display_select_mode_fn select_mode)
{
    (void)pref;
    select_mode(&gfx_backend_unsupported, legacy200);
    return &gfx_backend_unsupported;
}

u32 display_provider_ledger(const GfxBackend *backend)
{
    (void)backend;
    return 0;
}

int gfx_cirrus_probed(void)
{
    return 0;
}
