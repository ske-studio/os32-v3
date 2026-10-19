#ifndef OS32_V86_CALL_H
#define OS32_V86_CALL_H
#include "types.h"
/* Core admission/staging for the four public V86 calls. Low mappings remain
 * the session's responsibility. The caller is stopped until checked copyout. */
int v86_call_init(void);
int v86_call_admit(void);
int v86_call_pinned(void);
/* 0: other slot, 1: handled, -1: bad caller memory (cleaned before FAULT). */
int v86_call_dispatch(u32 slot, void *target, const void *args, u32 bytes,
                      u32 *result);
void v86_call_cleanup(void);
#endif
