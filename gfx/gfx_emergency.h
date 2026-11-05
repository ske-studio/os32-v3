/* Resident display recovery, TRIDENT_S3_DESIGN §3. Internal, no KAPI change. */
#ifndef GFX_EMERGENCY_H
#define GFX_EMERGENCY_H
#include "types.h"
/* Zero denotes kernel fatal (exec owners are nonzero). Other owners only
 * dispatch when equal to the armed display owner; child faults do not return
 * the parent's display. reason is diagnostic rc, not a recovery rc override. */
#define GFX_EMERGENCY_KERNEL_OWNER 0u
int gfx_arm_emergency(int (*restore)(void *), void *opaque, u32 owner);
int gfx_return_emergency(u32 owner, int reason);
/* Arm only with pinned, validated module text and resident data/maps, before
 * first indexed write. Single task/cooperative; dispatcher prevents recursion.
 * Return 0 for no applicable arm/already returned, otherwise restore's rc.
 * IF=0 allowed: no allocation, logging/drawing, tick or engine waits. Caller
 * must emit TVRAM output even on failure. Arm/pin survives normal shutdown. */
#endif
