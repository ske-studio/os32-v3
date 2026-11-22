#ifndef PM_KDEBUG_IMPORTS_H
#define PM_KDEBUG_IMPORTS_H
#include "debug_probe_snapshot.h"
#define KDEBUG_IMPORT_COUNT 1U
#define KDEBUG_ABI_GEN 1U
static void (*const kdebug_kexports[])(struct probe_list *) = {debug_probe_snapshot};
static const struct module_symbol_contract kdebug_import_contracts[] = {
    {0xd3bbe246U, 1, 1} /* debug_probe_snapshot */
};
static const struct module_symbol_contract kdebug_export_contracts[MODULE_OPS_SLOTS] = {
    [__builtin_offsetof(struct kdebug_ops, dbg_call) / sizeof(u32) - 1] = {0x2283f8adU, 1, 1}
};
#endif
