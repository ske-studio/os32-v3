#include "geforce_module.h"
#include "hwdev/gf/production.h"
#include "hwdev/gf/gf1_test.h"
#include "hwdev/gf/gf2_test.h"
#include "hwdev/gf/accel_service.h"
#include "hwdev/gf/display/api.h"
#include "pc98_route.h"
const GeforceModuleContext *geforce_context;
static struct gf_production production;
static int identify(void) { return production.prepared && !gf_production_identity(&production); }
static int safe(void) { return !production.attempted || !gf_production_decode_safe(&production); }
static int stop(void)
{
    const struct geforce_platform *p = geforce_context->platform;
    int rc = p && p->wc_stop ? p->wc_stop() : 0;
    if (gf_production_quiesce(&production)) rc = -1;
    return rc;
}
static int route98(void *opaque) { (void)opaque; return pc98_route_select_98(); }
int geforce_module_init(GfxBackend *out, const GeforceModuleContext *context)
{
    geforce_context = context;
    *context->accel_out=0;
    *context->foundation_out=0;
    *context->display_out=0;
    *context->display_restore_out=0;
    *context->feature5_out=0;
    struct geforce_platform *p = context->platform;
    if (!p || !p->resources) return -1;
    if (gf_production_prepare(&production, p->resources, route98, 0)) return -1;
    if (!production.prepared) return -1;
    p->device = &production.video;
    p->identify = identify;
    p->pc98_safe = safe;
    p->point = gf_production_point;
    p->opaque = &production;
    p->stop = stop;
    geforce_backend_bind(p->device, p->identify, p->bar0_phys, p->point, p->opaque);
    geforce_backend_stop_bind(p->stop);
    geforce_backend_wc_bind(p->wc_start);
    *out = geforce_backend_ops;
    gf1_module_ops.production=&production.foundation;
    gf2_test_bind_production(&production.foundation,&production.chan);
    /* X3/N1: diagnostics retain one F4 owner through STOP/START. The table
     * and display ctx belong to C2-service, not this module adapter.
     * This dispatch is OS32-specific; nouveau c03c1a1's lifecycle analogue
     * is nvkm/engine/fifo/uchan.c:272-298 (unbind/bind then allow).
     * No register sequence or source from that implementation is copied. */
    int rc=gf3_service_bind(&production, context->accel_out);
    if (rc) return rc;
    /* C-group production ctx contract: init fills the owner table's ctx
     * before F3 validates and borrows it (GF_FEAT4_DESIGN, final addendum). */
    rc=gf4_service_init(&production, &production.display);
    if (rc) return rc;
    rc=gf3_service_owner_bind(gf3_service_get(&production.chan),
                              gf4_service_owner_table());
    if (rc) return rc;
    rc=gf5_service_init(&production, production.display.display4, context->feature5_out);
    if (rc) return rc;
    geforce_backend_feature5_bind(&production.feature5);
    gf_hook_feature5_bind(&production.feature5);
    *context->foundation_out=&gf1_module_ops;
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(GfxBackend *, const GeforceModuleContext *);
} geforce_module_ops = {GEFORCE_MODULE_OPS_VERSION, geforce_module_init};
