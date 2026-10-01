#ifndef OS32_REDIR_ACCESS_H
#define OS32_REDIR_ACCESS_H
#include "types.h"

#define REDIR_TRUSTED 0
#define REDIR_USER    1
struct addrspace;
typedef struct {
    int origin, app_id;
    struct addrspace *as; /* Identity only; resolve live slot before dereference. */
    u32 pd_phys, owner, generation;
} RedirAccess;

int redir_access_capture(RedirAccess *out);
int redir_access_check(const RedirAccess *a, u32 va, u32 len, int write);
/* Preflight the entire range, then copy with a fresh live/walk check per page.
 * pos advances per copied page. Ordinary validation failure leaves data/pos
 * unchanged. No callbacks/allocation/yield: R1 reclaims AS only in normal code.
 * peer is the current call's separately checked buffer, not a saved pointer. */
int redir_access_copy(const RedirAccess *a, u8 *base, u32 *pos,
                      void *peer, u32 len, int write);
#endif
