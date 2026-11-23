/* HWOP_COMMON_DESIGN §2.1: validate the packed PPC module against its kernel. */
#define MODULE_SCHEMA_IMPORT_VALIDATION
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
    static const struct module_component_policy components[] = MODULE_COMPONENT_POLICIES;
    int rc = module_image_validate(packed, packed_end-packed, &policy, 0);
    if (module_positional_imports(0)) rc = 1;
    for (u32 i = 0; i < sizeof(components) / sizeof(components[0]); i++) {
        u32 id = components[i].module_id;
        if (module_positional_imports(id) != (id == MODULE_ID_KDEBUG))
            rc = 1;
    }
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
