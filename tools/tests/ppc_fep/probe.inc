#include "fep_probe_data.h"
#include "platform_module_pair.h"
#include "kernel/ime/fep_module.h"
#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "kdebug/probe_wire.h"

static unsigned fep_probe_starts;

static int fep_probe_start(struct module_record *record, u32 entry, u32 *exports)
{
    char dest[4] = {0};
    __typeof__(&kmemcpy) copy = (__typeof__(&kmemcpy))(record->base + PROBE_COPY);
    __typeof__(&kprintf) print = (__typeof__(&kprintf))(record->base + PROBE_PRINT);
    ++fep_probe_starts;
    if (entry != exports[1] || copy(dest, "fep", 4) != dest || kstrcmp(dest, "fep"))
        return -1;
    print(KBOOT_ATTR, "[module] %s %d %d %d %d %d %d %d\n", dest, 1, 2, 3, 4, 5, 6, 7);
    return 0;
}

static void fep_probe(void)
{
    struct probe_wire wire = {.version = PROBE_VERSION, .size = sizeof(wire),
                              .action = PROBE_CMD_LIST};
    const KernelAPI *api = (const KernelAPI *)exec_kapi_table();
    if (api->kdebug_call(PROBE_OP, &wire, sizeof(wire)) || wire.status) return;
    u32 providers[FEP_IMPORT_COUNT] = {
        [PROBE_COPY_SLOT] = (u32)kmemcpy, [PROBE_PRINT_SLOT] = (u32)kprintf,
    };
    struct module_request req = {
        .path = "/boot/fep.mod", .name = "fep",
        .policy = {.kapi_abi_gen = PROBE_KAPI, .layout_gen = OS32_MEMORY_LAYOUT_GENERATION,
                   .module_abi_gen = FEP_MODULE_ABI_GEN, .ops_version = FEP_MODULE_OPS_VERSION,
                   .imports = fep_import_contracts, .exports = fep_export_contracts,
                   .import_count = FEP_IMPORT_COUNT, .module_id = MODULE_ID_FEP},
        .imports = providers, .lifecycle.start = fep_probe_start,
    };
    struct module_handle handle;
    req.policy.img_valid = platform_module_pair(&req.policy.pair_image_crc);
    req.policy.pair_image_crc ^= 1;
    int rc = module_load(&req, &handle);
    if (!rc) {
        rc = module_start(handle);
        module_abort(handle);
    }
    kprintf(KBOOT_ATTR, "[module] foreign=%d starts=%u\n", rc, fep_probe_starts);
    req.policy.pair_image_crc ^= 1;
    rc = module_load(&req, &handle);
    if (!rc) {
        rc = module_start(handle);
        if (module_abort(handle)) rc = -1;
    }
    kprintf(KBOOT_ATTR, "[module] local=%d starts=%u\n", rc, fep_probe_starts);
}
