/* Registration only; ScreenInit is deferred to the HAL boundary. */
#include "trident_module.h"
const TridentModuleContext *trident_context;
int trident_module_init(GfxBackend *out, const TridentModuleContext *context)
{
    if (!out || !context || !context->backend || !context->counters ||
        !context->resources || !context->arm || !context->height ||
        !context->flip || !context->page || !context->display_owner)
        return TRI_E_INVAL;
    trident_context = context;
    *out = trident_backend_ops;
    return 0;
}
const struct TridentModuleOps trident_module_ops = {
    TRIDENT_MODULE_OPS_VERSION, trident_module_init,
    trident_backend_prepare, trident_restore98
};
