#include "exec_surface.h"
struct ledger_surface *exec_surface_at(u32 sid) { (void)sid; return 0; }
u32 exec_surface_backend(void) { return 0; }
u32 exec_surface_bridge_access(u32 flags) { (void)flags; return 0; }
void exec_surface_bridge_begin(u32 access) { (void)access; }
void exec_surface_bridge_end(u32 access) { (void)access; }
int exec_surface_map_audit(struct addrspace *as) { (void)as; return 0; }
void exec_surface_policy_reeval(void) { }
void exec_surface_restore_on_leave(void) { }
