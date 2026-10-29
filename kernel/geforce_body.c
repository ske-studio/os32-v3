#include "geforce_module.h"
const GeforceModuleContext *geforce_context;
int geforce_module_init(GfxBackend *out, const GeforceModuleContext *context)
{
    geforce_context=context;
    const struct geforce_platform *p=context->platform;
    if (p) geforce_backend_bind(p->device, p->identify, p->bar0_phys, p->point, p->opaque);
    *out=geforce_backend_ops;
    return 0;
}
const struct {
    u32 version;
    int (*initialize)(GfxBackend *, const GeforceModuleContext *);
} geforce_module_ops = {GEFORCE_MODULE_OPS_VERSION, geforce_module_init};
