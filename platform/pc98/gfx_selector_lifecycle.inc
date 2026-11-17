/* PC-98 init failure/route policy, moved from gfx_core.c. */
#include "gfx_emergency.h"
#include "../../drivers/video/pc98/pc98_route.h"
#include "../../drivers/video/pc98/splash_raster.h"

int display_provider_client_before_init(const GfxBackend *backend)
{
    return backend == &gfx_backend_geforce || backend == &gfx_backend_trident;
}

int display_provider_init_failed(GfxBackend *backend)
{
    if (backend == &gfx_backend_trident) {
        GFX_ScreenInfo info;
        int rc = backend->query ? backend->query(&info) : OS32_ERR_NOSYS;
        int restore_rc = gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, rc);
        kprintf(KLOG_ATTR, "[gfx] trident stage=init rc=%d restore=%d\n", rc, restore_rc);
        /* Trident's D:738 byte write 00 precedes Restore. Do not
         * substitute route_set(): it can omit a write and read back.
         * PC98_ROUTE_MASK=3 also rejects Trident's Enable value 02. */
        if (restore_rc || (pc98_route_safe && !pc98_route_safe())) return -1;
    }
    if (backend == &gfx_backend_geforce && !pc98_route_safe()) return -1;
    kprintf(KLOG_ATTR, "[gfx] backend=%s init failed -> pc98\n", backend->name);
    return 0;
}

int display_provider_idle_safe(int pref)
{
    return (pref != GFX_PREF_GEFORCE && pref != GFX_PREF_TRIDENT) ||
           !pc98_route_safe || pc98_route_safe();
}

void display_provider_raster_reset(void)
{
    pc98_splash_raster_reset();
}

int display_provider_restore_fallback(GfxBackend *backend)
{
    return backend == &gfx_backend_geforce && pc98_route_safe();
}

u32 display_provider_ver_kind(const GfxBackend *backend)
{
    return backend == &gfx_backend_pc98 ? GFX_VER_PC98 :
           backend == &gfx_backend_pegc ? GFX_VER_PEGC :
           backend == &gfx_backend_cirrus ? GFX_VER_CIRRUS :
           backend == &gfx_backend_geforce ? GFX_VER_GEFORCE :
           backend == &gfx_backend_trident ? GFX_VER_TRIDENT : GFX_VER_UNKNOWN;
}

void display_provider_prepare_failed(GfxBackend *backend)
{
    if (backend == &gfx_backend_trident) {
        GFX_ScreenInfo info;
        int rc = backend->query ? backend->query(&info) : OS32_ERR_NOSYS;
        kprintf(KLOG_ATTR, "[gfx] trident stage=prepare rc=%d -> pc98\n", rc);
    }
}
