#ifndef KDEBUG_WINDOW_CONTRACT_H
#define KDEBUG_WINDOW_CONTRACT_H
#include "recover_window_contract.h"
/* Begin holds IRQs until cleanup. Caller supplies a page-aligned aperture VA;
 * at most two PTEs, no logging/waiting while owned. Cleanup is idempotent. */
int kdebug_window_begin(struct recover_window *w, u32 pa, u32 len, bool write);
#endif
