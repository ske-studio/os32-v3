#include "cirrus_module.h"
const CirrusModuleContext *cirrus_context;
int cirrus_module_init(GfxBackend *out, const CirrusModuleContext *context)
{
    cirrus_context = context;
    *out = cirrus_backend_ops;
    return 0; /* Register only. probe/init belong to the graphics boundary. */
}
const struct {
    u32 version;
    int (*initialize)(GfxBackend *, const CirrusModuleContext *);
} cirrus_module_ops = {CIRRUS_MODULE_OPS_VERSION, cirrus_module_init};
