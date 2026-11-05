/* Resident display recovery, TRIDENT_S3_DESIGN §3. Internal, no KAPI change. */
#ifndef GFX_EMERGENCY_H
#define GFX_EMERGENCY_H
#include "types.h"
/* Zero denotes kernel fatal (exec owners are nonzero). Other owners only
 * dispatch when equal to the CURRENT display owner from appslot_gfx_owner(); child faults do not return
 * the parent's display. reason is diagnostic rc, not a recovery rc override. */
#define GFX_EMERGENCY_KERNEL_OWNER 0u
/* Shared dispatch predicate. display_owner is the live callback, never a
 * captured owner. Arm occurs once; later init may change display ownership. */
#define GFX_EMERGENCY_MATCHES_OWNER(owner, display_owner) \
    ((owner) == GFX_EMERGENCY_KERNEL_OWNER || (owner) == (u32)(display_owner)())
int gfx_arm_emergency(int (*restore)(void *), void *opaque, u32 owner);
int gfx_return_emergency(u32 owner, int reason);
/* Arm only with pinned, validated module text and resident data/maps, before
 * first indexed write. Arm owner is diagnostic only, never the return filter.
 * Return evaluates GFX_EMERGENCY_MATCHES_OWNER with appslot_gfx_owner.
 * Single task/cooperative; dispatcher prevents recursion.
 * Return 0 for no applicable arm/already returned, otherwise restore's rc.
 * IF=0 allowed: no allocation, logging/drawing, tick or engine waits. Caller
 * must emit TVRAM output even on failure. Arm/pin survives normal shutdown. */
#endif
