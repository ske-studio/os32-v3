/* Kernel/module loan contract, TRIDENT_S3_DESIGN §3. */
#ifndef TRIDENT_MODULE_H
#define TRIDENT_MODULE_H
#include "gfx_hal.h"
#include "trident.h"
#define TRIDENT_MODULE_OPS_VERSION 1u
/* All loans (including backend, counters and scalar addresses) are kernel
 * resident. resources is immutable after publication. display_owner points
 * to the current exec display owner, distinct from DEVICE resource owner.
 * Loader validates initialize/prepare/restore text and pins before arm.
 * prepare only inspects descriptors; it must not invoke Init or indexed I/O.
 * Optional GfxBackend slots remain NULL, including fill/blit/flip/vsync. */
typedef struct TridentModuleContext {
    GfxBackend *backend;
    GfxCounters *counters;
    const struct trident_resources *resources;
    int (*arm)(int (*restore)(void *), void *, u32 owner);
    int *height, *flip, *page;
    const u32 *display_owner;
} TridentModuleContext;
struct TridentModuleOps {
    u32 version;
    int (*initialize)(GfxBackend *, const TridentModuleContext *);
    int (*prepare)(void); /* rc boundary behind HAL's void prepare */
    int (*restore)(void *);
};
extern const TridentModuleContext *trident_context;
extern const struct TridentModuleOps trident_module_ops;
extern GfxBackend trident_backend_ops;
int trident_module_init(GfxBackend *out, const TridentModuleContext *context);
int trident_backend_prepare(void);
int trident_module_load(void);
int trident_module_abort(void); /* BUSY after arm, retains pin/maps/save */
#endif
