/* HWOP_COMMON_DESIGN §2.1: validate the packed PPC module against its kernel. */
#include "module.h"
#include "module_image.h"
#include "kdebug_imports.h"
#include "os32_generations.h"

extern const u8 packed[], packed_end[];
void _start(void)
{
    const struct module_image_policy policy = {
        KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, KDEBUG_ABI_GEN, 0,
        KDEBUG_OPS_VERSION, 1, KDEBUG_IMPORT_COUNT, kdebug_import_contracts,
        kdebug_export_contracts, MODULE_ID_KDEBUG
    };
    int rc = module_image_validate(packed, packed_end-packed, &policy, 0);
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
