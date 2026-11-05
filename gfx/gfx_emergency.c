/* Resident recovery dispatch, TRIDENT_S3_DESIGN §3. No hardware policy here. */
#include "gfx_emergency.h"
#include "../exec/appslot.h"
#include "os32_kapi_shared.h"

static int (*emergency_restore)(void *);
static void *emergency_opaque;
static int returning;

int gfx_arm_emergency(int (*restore)(void *), void *opaque, u32 owner)
{
    (void)owner; /* Diagnostic at the device; ownership is read live below. */
    if (!restore) return OS32_ERR_INVAL;
    if (returning) return OS32_ERR_BUSY;
    if (emergency_restore)
        return emergency_restore == restore && emergency_opaque == opaque ?
               0 : OS32_ERR_BUSY;
    emergency_opaque = opaque;
    emergency_restore = restore;
    return 0;
}

int gfx_return_emergency(u32 owner, int reason)
{
    (void)reason; /* Never replace the callback's recovery rc. */
    if (!emergency_restore || returning ||
        !GFX_EMERGENCY_MATCHES_OWNER(owner, appslot_gfx_owner)) return 0;
    returning = 1;
    int rc = emergency_restore(emergency_opaque);
    returning = 0;
    /* Keep the arm across shutdown/reinit. The device's vtSema makes an
     * already returned display a no-op (D:3333); later init can reactivate it. */
    return rc;
}
