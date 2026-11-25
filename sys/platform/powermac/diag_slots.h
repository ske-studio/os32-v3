#ifndef PM_DIAG_SLOTS_H
#define PM_DIAG_SLOTS_H
#include "debug_probe_snapshot.h"
#include "sys/kern/kdebug/hwb_wire.h"

struct pm_diag_slots {
    void (*snapshot)(struct probe_list *list);
    u32 (*abort_take)(void);
    u32 (*cfg_read)(u32 bdf, u32 offset, u32 width, u32 *value);
    u32 (*cfg_write)(u32 bdf, u32 offset, u32 width, u32 value);
    u32 (*bar)(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr);
    u32 (*phys)(struct hw_req *req);
    u32 (*block)(struct hw_block_req *block);
    u32 (*phys_check)(u32 addr, u32 width, u32 bytes);
};
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, snapshot) == 0 * 4, pm_slot_snapshot);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, abort_take) == 1 * 4, pm_slot_abort_take);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, cfg_read) == 2 * 4, pm_slot_cfg_read);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, cfg_write) == 3 * 4, pm_slot_cfg_write);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, bar) == 4 * 4, pm_slot_bar);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, phys) == 5 * 4, pm_slot_phys);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, block) == 6 * 4, pm_slot_block);
STATIC_ASSERT(__builtin_offsetof(struct pm_diag_slots, phys_check) == 7 * 4, pm_slot_phys_check);
STATIC_ASSERT(sizeof(struct pm_diag_slots) == 32, pm_slot_size);
extern struct pm_diag_slots kdebug_import_slots;
#endif
