#ifndef OS32_EXEC_SURFACE_H
#define OS32_EXEC_SURFACE_H
#include "pgalloc.h"
#include "gfx.h"
#include "gfx_policy.h"
#include "../../gfx/v2/gfx2_int.h"
static inline struct ledger_surface *exec_surface_at(u32 sid) { return &ledger_surfaces[sid]; }
static inline u32 exec_surface_backend(void) { return gfx_sf_backend(); }
static inline u32 exec_surface_bridge_access(u32 flags) { return GFX2_CPU_R | ((flags & PTE_RW) ? GFX2_CPU_W : 0); }
static inline void exec_surface_bridge_begin(u32 access) { (void)v1_bridge_begin(GFX2_ROLE_DISPLAY, GFX2_CPU_R | (access == LEDGER_PERM_RW ? GFX2_CPU_W : 0)); }
static inline void exec_surface_bridge_end(u32 access) { v1_bridge_end(GFX2_ROLE_DISPLAY, access, 0); }
static inline int exec_surface_map_audit(struct addrspace *as) { return gfx2_map_audit(as); }
static inline void exec_surface_policy_reeval(void) { gfx_policy_reeval(); }
static inline void exec_surface_restore_on_leave(void)
{
    struct gfx2_adapter *a = gfx2_active();
    if (a && a->restore_on_leave) {
        a->restore_on_leave = 0;
        for (u32 h = 0; h < a->heads; h++)
            (void)gfx2_head_restore_v1(a, h);
    }
}
#endif
