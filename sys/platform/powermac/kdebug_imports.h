#ifndef PM_KDEBUG_IMPORTS_H
#define PM_KDEBUG_IMPORTS_H
#include "diag_slots.h"
#include "hwb_resource.h"
#include "sys/kern/exec/exec_hwb.h"
#include "lan_boot.h"
#define KDEBUG_IMPORT_COUNT 8U
#define KDEBUG_ABI_GEN 1U

static const struct pm_diag_slots kdebug_kexports[] = {{
    .snapshot = debug_probe_snapshot,
    .abort_take = exec_hwb_abort_take,
    .cfg_read = pm_hwb_cfg_read,
    .cfg_write = pm_hwb_cfg_write,
    .bar = pm_hwb_bar,
    .phys = pm_hwb_phys,
    .block = pm_hwb_block,
    .phys_check = pm_hwb_phys_check,
}};
static const struct module_symbol_contract kdebug_import_contracts[] = {
    {0xd3bbe246U, 1, 1}, /* debug_probe_snapshot */
    {0xf6e013c7U, 1, 1}, /* exec_hwb_abort_take */
    {0x0c09c712U, 1, 1}, /* pm_hwb_cfg_read */
    {0xcfac2415U, 1, 1}, /* pm_hwb_cfg_write */
    {0x3b62f8caU, 1, 1}, /* pm_hwb_bar */
    {0xe3613055U, 1, 1}, /* pm_hwb_phys */
    {0xc289f872U, 1, 1}, /* pm_hwb_block */
    {0x2f5a81ceU, 1, 1}, /* pm_hwb_phys_check */
};
static const struct module_symbol_contract kdebug_export_contracts[MODULE_OPS_SLOTS] = {
    [__builtin_offsetof(struct kdebug_ops, dbg_call) / sizeof(u32) - 1] = {0x2283f8adU, 1, 1}
};
#endif
