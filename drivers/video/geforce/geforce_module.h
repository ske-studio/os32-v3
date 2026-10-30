/* P5 connection to P4's boot/resource owner. No register/ROM policy here. */
#ifndef GEFORCE_MODULE_H
#define GEFORCE_MODULE_H
#include "gfx_hal.h"
#include "video_service.h"
#include "../../../kernel/module/module_runtime.h"
#define GEFORCE_MODULE_OPS_VERSION 1u
struct gf_production_resources;
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
    const struct gf_production_resources *resources;
    int (*stop)(void);
};
struct geforce_platform *geforce_platform_get(void);
int geforce_boot_reserve(void) __attribute__((weak));
int geforce_pc98_safe(void);
int geforce_range_reserved(u32 base, u32 length);
void geforce_display_failed(void);
typedef struct {
    GfxBackend *backend;
    GfxCounters *counters;
    int *height, *flip, *page;
    struct geforce_platform *platform;
} GeforceModuleContext;
extern const GeforceModuleContext *geforce_context;
extern GfxBackend geforce_backend_ops;
void geforce_backend_bind(struct video_device *, int (*)(void), u32,
                          int (*)(void *), void *);
void geforce_backend_stop_bind(int (*stop)(void));
int geforce_module_load(void);
int geforce_module_abort(void);
#endif
