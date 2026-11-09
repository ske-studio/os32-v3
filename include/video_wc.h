/* Internal video cache interface; one GPU, cooperative switching. */
#ifndef VIDEO_WC_H
#define VIDEO_WC_H
#include "types.h"
int arch_video_wc_active(void);
/* Caller checks identity, BAR ownership, ledger/surface and RAM overlap. */
int arch_video_wc_eligible(u32 fb, u32 mapped, u32 extent, u32 regs, u32 reg_bytes);
/* Active start returns the saved result without acquiring again.
 * 0: unavailable, positive: WC, negative: rollback unverified. */
int arch_video_wc_start(u32 base, u32 bytes);
/* Does not require device identity; also drains borrowed WC. */
int arch_video_wc_stop(void);
#endif
