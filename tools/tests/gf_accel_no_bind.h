/* Display/WC fixtures model F2 and deliberately leave F3 unregistered.
 * The real module->service->video/command wiring has its own integration
 * oracle in gf_accel_wire_host.c; never use this seam in that oracle. */
#ifndef GF_ACCEL_NO_BIND_H
#define GF_ACCEL_NO_BIND_H
#include "hwdev/gf/accel_service.h"
static i32 gf3_host_no_bind(struct gf_production *p,
                           const struct gf3_test_ops **out)
{
    (void)p;
    if (!out) return GF3_EINVAL;
    *out = 0;
    return 0;
}
#endif
