/* Host fixtures implement the external platform contract themselves. */
#ifndef OS32_HOST_LINK_PLATFORM_INLINE_H
#define OS32_HOST_LINK_PLATFORM_INLINE_H
#include "sys/hal/net/link_platform.h"
/* Match the PC-98 tick RX policy used by these fixtures. */
#define LINK_PLATFORM_TICK_RX_LGY98_SLOT 0
static inline void link_platform_session(u32 generation) { (void)generation; }
#endif
