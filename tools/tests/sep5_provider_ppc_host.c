/* B4: the compile-only PowerMac provider keeps its unsupported contract. */
#include "types.h"
#include "gfx_hal.h"
#include "display_provider.h"
#include "display_provider_ppc.inc"
static int calls, seen_legacy;
static GfxBackend *seen;
static int select_mode(GfxBackend *backend, int legacy200)
{
    calls++; seen = backend; seen_legacy = legacy200;
    return 0;
}
static int binds;
static void bind(void) { binds++; }
int main(void)
{
    GfxBackend *b = DISPLAY_FALLBACK_BACKEND;
    if (b != &gfx_backend_unsupported) return 1;
    if (b->probe || b->init || b->prepare || b->modes || b->present_rect || b->shutdown) return 2;
    if (display_provider_pref(GFX_PREF_PEGC) != GFX_PREF_AUTO ||
        display_provider_pref(GFX_PREF_AUTO) != GFX_PREF_AUTO) return 3;
    if (display_provider_select(GFX_PREF_GEFORCE, 1, select_mode) != b || calls != 1 ||
        seen != b || seen_legacy != 1) return 4;
    if (display_provider_ledger(b) != 0) return 5;
    if (gfx_cirrus_probed()) return 6;
    /* B5: nothing to reserve; the common client binding is not called. */
    calls = 0;
    display_provider_boot_reserve(GFX_PREF_AUTO, bind);
    if (binds || calls) return 7;
    if (display_provider_display_records(0) != 1 || display_provider_display_records(1) != 1 ||
        display_provider_display_first(0) != 0) return 8;
    return 0;
}
