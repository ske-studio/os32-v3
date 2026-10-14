#include "kdebug_imports.h"
void (*const kdebug_kexports[])(void) = {
    (void (*)(void))kprintf,
};
const unsigned long kdebug_abi_gen = KDEBUG_ABI_GEN;
const unsigned long kdebug_kexport_count = KDEBUG_IMPORT_COUNT;
const struct module_symbol_contract kdebug_import_contracts[] = {
    {0x70485fd1u, 1, 1},
};
const struct module_symbol_contract kdebug_export_contracts[MODULE_OPS_SLOTS] = {
    [4] = {0x28752bcbu, 1, 0},
};
