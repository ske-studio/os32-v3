#ifndef OS32_EXEC_LEASE_H
#define OS32_EXEC_LEASE_H
#include "paging.h"
#include "pgalloc.h"
struct surface_ref { u32 sid, generation; };
struct lease_view { u32 token, base, bytes, planes[4]; };
/* Kernel-resolved authorization; no public caller or user-selected AS in T2b. */
struct lease_authority { u32 owner, backend, role; };
#define LEASE_INVAL (-1)
#define LEASE_NOMEM (-2)
#define LEASE_FULL  (-3)
int lease_acquire(struct addrspace *as, const struct lease_authority *auth,
                  const struct surface_ref *refs, u32 count, u32 access,
                  struct lease_view *out);
int lease_release(struct addrspace *as, u32 token);
int lease_revoke_all(struct addrspace *as);
int lease_check(const struct addrspace *as);
int lease_selftest(void);
extern u32 lease_selftest_result;
#endif
