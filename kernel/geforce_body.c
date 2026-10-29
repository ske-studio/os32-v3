#include "geforce_module.h"
#include "hwdev/gf/production.h"
#include "pc98_route.h"
const GeforceModuleContext *geforce_context;
static struct gf_production production;
static int identify(void) { return production.prepared && !gf_production_identity(&production); }
static int safe(void) { return !production.attempted || !gf_production_decode_safe(&production); }
static int stop(void) { return gf_production_quiesce(&production); }
static int route98(void *opaque) { (void)opaque; return pc98_route_select_98(); }
int geforce_module_init(GfxBackend *out, const GeforceModuleContext *context)
{
    geforce_context = context;
    struct geforce_platform *p = context->platform;
    if (p && p->resources) {
        if (gf_production_prepare(&production, p->resources, route98, 0)) return -1;
        p->device = &production.video;
        p->identify = identify;
        p->pc98_safe = safe;
        p->point = gf_production_point;
        p->opaque = &production;
        p->stop = stop;
    }
    if (p) {
        geforce_backend_bind(p->device, p->identify, p->bar0_phys, p->point, p->opaque);
        geforce_backend_stop_bind(p->stop);
    }
    *out = geforce_backend_ops;
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(GfxBackend *, const GeforceModuleContext *);
} geforce_module_ops = {GEFORCE_MODULE_OPS_VERSION, geforce_module_init};
