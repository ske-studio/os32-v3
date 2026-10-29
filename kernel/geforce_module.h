/* P5 connection to P4's boot/resource owner. No register/ROM policy here. */
#ifndef GEFORCE_MODULE_H
#define GEFORCE_MODULE_H
#include "gfx_hal.h"
#include "video_service.h"
#include "module_runtime.h"
#define GEFORCE_MODULE_OPS_VERSION 1u
struct geforce_platform {
    struct video_device *device;
    /* reserve owns permanent BAR reservations/maps (1 ready, 0 unavailable);
     * identify and pc98_safe return 1/0 and perform read-only checks.
     * Install before gfx_boot_reserve; retain context for the whole boot. */
    int (*reserve)(void);
    int (*identify)(void);
    int (*pc98_safe)(void);
    int (*point)(void *);
    void *opaque;
    u32 bar0_phys, display_phys;
};
/* The unconnected P5 build returns NULL: no GF hardware is touched. P4 can
 * supply the strong implementation; host tests supply a fake owner. */
const struct geforce_platform *geforce_platform_get(void);
int geforce_boot_reserve(void) __attribute__((weak));
int geforce_pc98_safe(void);
void geforce_display_failed(void);
typedef struct {
    GfxBackend *backend;
    GfxCounters *counters;
    int *height, *flip, *page;
    const struct geforce_platform *platform;
} GeforceModuleContext;
extern const GeforceModuleContext *geforce_context;
extern GfxBackend geforce_backend_ops;
void geforce_backend_bind(struct video_device *, int (*)(void), u32,
                          int (*)(void *), void *);
int geforce_module_load(void);
int geforce_module_abort(void);
#endif
