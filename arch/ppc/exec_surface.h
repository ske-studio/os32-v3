#ifndef OS32_EXEC_SURFACE_H
#define OS32_EXEC_SURFACE_H
#include "pgalloc.h"
struct addrspace;
struct ledger_surface *exec_surface_at(u32 sid);
u32 exec_surface_backend(void);
u32 exec_surface_bridge_access(u32 flags);
void exec_surface_bridge_begin(u32 access);
void exec_surface_bridge_end(u32 access);
int exec_surface_map_audit(struct addrspace *as);
void exec_surface_policy_reeval(void);
#endif
