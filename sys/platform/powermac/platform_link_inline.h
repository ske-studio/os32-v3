/* Selected through the platform include path; PPC uses external boundaries. */
#ifndef OS32_POWERMAC_LINK_PLATFORM_INLINE_H
#define OS32_POWERMAC_LINK_PLATFORM_INLINE_H
#include "sys/hal/net/link_platform.h"
/* PowerMac GEM borrows the LGY98 adapter slot (P1f) and is drained in the tick. */
#define LINK_PLATFORM_TICK_RX_LGY98_SLOT 1
#include "platform_hwb_session.h"
#if PLATFORM_HWB_SESSION
void link_platform_session(u32 generation);
#else
static inline void link_platform_session(u32 generation) { (void)generation; }
#endif
#endif
