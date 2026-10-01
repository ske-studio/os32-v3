#ifndef OS32_REDIR_ACCESS_H
#define OS32_REDIR_ACCESS_H
#include "types.h"

/* Shared identity for a current caller and a registered redirect buffer.
 * Origin is selected only by kernel entry paths, never by pointer/CR3 value. */
enum caller_origin { CALLER_TRUSTED = 0, CALLER_USER = 1 };
#define REDIR_TRUSTED CALLER_TRUSTED
#define REDIR_USER    CALLER_USER
struct addrspace;
typedef struct caller_access {
    enum caller_origin origin;
    int app_id;
    struct addrspace *as; /* Identity only; resolve live slot before dereference. */
    u32 pd_phys, owner, generation;
} RedirAccess;

/* Value snapshots; never retain a pointer into an abandoned dispatch stack. */
typedef struct {
    struct caller_access access;
    int valid;
} CallerAccessFrame;
int caller_access_enter(CallerAccessFrame *previous, enum caller_origin origin);
void caller_access_leave(const volatile CallerAccessFrame *previous);
/* Copies the fixed entry identity only if it still matches the live caller.
 * No implicit trusted fallback; rejection leaves out unchanged. */
int caller_access_get(struct caller_access *out);
/* WM uses this explicitly for the suspended USER's pointers, never TRUSTED. */
int caller_access_get_user(struct caller_access *out);
void caller_access_save(volatile CallerAccessFrame *out);
void caller_access_invalidate(void);

int redir_access_capture(RedirAccess *out);
int redir_access_check(const RedirAccess *a, u32 va, u32 len, int write);
/* Preflight the entire range, then copy with a fresh live/walk check per page.
 * pos advances per copied page. Ordinary validation failure leaves data/pos
 * unchanged. No callbacks/allocation/yield: R1 reclaims AS only in normal code.
 * peer is the current call's separately checked buffer, not a saved pointer. */
int redir_access_copy(const RedirAccess *a, u8 *base, u32 *pos,
                      void *peer, u32 len, int write);
#endif
