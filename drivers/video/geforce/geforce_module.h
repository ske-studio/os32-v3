/* P5 connection to P4's boot/resource owner. No register/ROM policy here. */
#ifndef GEFORCE_MODULE_H
#define GEFORCE_MODULE_H
#include <stdbool.h>
#include "gfx_hal.h"
#include "video_service.h"
#include "../../../kernel/module/module_runtime.h"
#define GEFORCE_MODULE_OPS_VERSION 5u
#define GEFORCE_WC_BYTES 0x80000U
struct gf_production_resources;
struct gf3_test_ops;
struct gf1_test_ops;
struct gf4_data_ops;
struct gf4_state;
struct gf4_test_ops;
struct gf4_restore_ops;
struct gf5;
struct gf5_test_ops;
const struct gf3_test_ops *geforce_accel_ops(void);
const struct gf1_test_ops *geforce_foundation_ops(void);
/* Read the normal/restore slot on every call; do not retain across unload. */
const struct gf4_test_ops *geforce_display_ops(void);
const struct gf4_restore_ops *geforce_display_restore_ops(void);
/* Version 5 uses the reserved feature5_out; reread before each dispatch. */
const struct gf5_test_ops *geforce_feature5_ops(void);
/* Borrow the same production context until module stop completes. */
void geforce_backend_feature5_bind(struct gf5 *);
void gf_hook_feature5_bind(struct gf5 *);
int geforce_foundation_display(bool start);
/* Resident foreground adapter: reread the F5 slot on every dispatch, never
 * cache module ctx/ops. No IRQ calls. Consume pending heat stop at the common
 * exit while retaining reentry protection; backend stop owns active/routed. */
void geforce_foreground_poll(void);
/* Value-only stop request; do not retain any module pointers. The consumer
 * owns no-route sync/halt; geforce_return must retain its reboot semantics. */
void geforce_heat_stop_request(i32 reason, i32 milli_c, i32 source_rc);
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
    /* Kernel-owned lease: 0=UC, 1=WC, negative=fatal. Stop is idempotent. */
    int (*wc_start)(void);
    int (*wc_stop)(void);
    /* Acceptance records only, after timing; no sample storage in production. */
    void (*report)(const char *record);
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
    const struct gf3_test_ops **accel_out;
    const struct gf1_test_ops **foundation_out;
    /* Kernel-owned helper/state loans, valid until module stop completes. */
    const struct gf4_data_ops *display_data;
    struct gf4_state *display_state;
    /* Version 5 appends to the version 4 prefix (including accel/foundation).
     * Kernel static slot addresses; C2 owns publication. Reread per dispatch.
     * The restore loan survives a saved display or retained-resource failure;
     * processing and completed unload close it. */
    const struct gf4_test_ops **display_out;
    const struct gf5_test_ops **feature5_out;
    const struct gf4_restore_ops **display_restore_out;
} GeforceModuleContext;
extern const GeforceModuleContext *geforce_context;
extern GfxBackend geforce_backend_ops;
void geforce_backend_bind(struct video_device *, int (*)(void), u32,
                          int (*)(void *), void *);
void geforce_backend_stop_bind(int (*stop)(void));
void geforce_backend_wc_bind(int (*start)(void));
int geforce_wc_start(void);
int geforce_wc_stop(void);
void geforce_wc_report(const char *record);
int geforce_module_load(void);
int geforce_module_abort(void);
#endif
