/* Selected through the platform include path; PPC uses external boundaries. */
#ifndef OS32_POWERMAC_LINK_PLATFORM_INLINE_H
#define OS32_POWERMAC_LINK_PLATFORM_INLINE_H
#include "net/link_platform.h"
/* PowerMac GEM borrows the LGY98 adapter slot (P1f) and is drained in the tick. */
#define LINK_PLATFORM_TICK_RX_LGY98_SLOT 1
#endif
