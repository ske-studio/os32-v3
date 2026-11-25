/* HWOP_COMMON_DESIGN §2.1: LIST and POINT use the published typed imports. */
#include "sys/kern/kdebug/hwb_wire.h"
#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "kstring.h"
#include "module_runtime.h"
#include "platform_module_pair.h"
#include "arch_exec_cache.h"
#include "probe_data.h"
#include "sys/kern/module/module.h"
#include "sys/kern/kdebug/probe_wire.h"

static unsigned probe_starts;
static int probe_value;
static int probe_start(struct module_record *record, u32 entry, u32 *exports)
{
    (void)record;
    (void)exports;
    ++probe_starts;
    return ((int (*)(void))entry)() != 45 + 2 * probe_value;
}

static int probe_module_loads(u32 provider)
{
    struct module_request req = {
        .path = "/sys/a.mod", .name = "probe",
        .policy = {.kapi_abi_gen = PROBE_KAPI, .layout_gen = PROBE_LAYOUT,
                   .module_abi_gen = PROBE_ABI, .ops_version = 1,
                   .exports = &probe_export, .imports = &probe_import, .import_count = 1, .module_id = MODULE_ID_KCG},
        .imports = &provider, .lifecycle.start = probe_start,
    };
    req.policy.img_valid = platform_module_pair(&req.policy.pair_image_crc);
    struct module_handle first, second, reused;
    int rc = module_load(&req, &first);
    if (rc) {
        kprintf(KBOOT_ATTR, "[module] first rc=%d starts=%u\n", rc, probe_starts);
        return rc;
    }
    u32 base = module_record_get(first)->base;
    /* REL24 is signed and address arithmetic wraps on PPC32. */
    u32 distance = provider - base;
    if (distance < 0x02000000u || 0u - distance <= 0x02000000u) return -2;
    probe_value = 11;
    if (module_start(first)) return -3;
    u32 prefix = base / PAGE_SIZE - 1, claimed;
    if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, prefix, prefix + 1,
                               LEDGER_BOTTOM_UP, &claimed)) return -10;
    kmemset(P2V(claimed * PAGE_SIZE), 0xcc, PAGE_SIZE);
    const u32 *exports = module_exports(first);
    if (!exports || ((int (*)(void))exports[1])() != 67) return -11;
    if (!pgalloc_free_n_owner(LEDGER_OWNER_BOOT, claimed, 1)) return -12;
    req.path = "/sys/b.mod";
    if (module_load(&req, &second) || module_record_get(second)->base == base) return -4;
    probe_value = 27;
    if (module_start(second) || module_abort(first)) return -5;
    if (module_load(&req, &reused) || module_record_get(reused)->base != base) return -6;
    if (module_start(reused) || probe_starts != 3) return -7;
    if (module_abort(reused) || module_abort(second)) return -8;
    return 0;
}

static int probe_modules(void)
{
    struct probe_wire wire = {.version = PROBE_VERSION, .size = sizeof(wire),
                              .action = PROBE_CMD_LIST};
    const KernelAPI *api = (const KernelAPI *)exec_kapi_table();
    int list = api->kdebug_call(PROBE_OP, &wire, sizeof(wire)) ? -1 : wire.status;
    wire = (struct probe_wire){.version = PROBE_VERSION, .size = sizeof(wire),
                               .action = PROBE_CMD_ARM};
    int unsupported = api->kdebug_call(PROBE_OP, &wire, sizeof(wire)) ? -1 : wire.status;
    struct hw_req point = {.version=HW_VERSION, .action=HW_POINT};
    int point_rc = api->kdebug_call(HW_OP, &point, sizeof(point));
    kprintf(KBOOT_ATTR, "[module] point rc=%d status=%u value=%u\n", point_rc, point.status, point.value);
    kprintf(KBOOT_ATTR, "[module] diag state=%u list=%d unsupported=%d\n",
            kdebug.state, list, unsupported);

    /* The allocator admits 16 MiB. Use the first page of the preallocated
     * aperture PT for a distant supervisor-only instruction address. */
    u32 provider = MEM_DEVICE_APERTURE_BASE;
    u32 phys = pgalloc_alloc_phys(LEDGER_OWNER_BOOT, 1);
    if (!phys) return -1;
    int rc = -1;
    if (!paging_pte_raw(provider) && !paging_set_page(provider, phys, PAGE_RO)) {
        kmemcpy(P2V(phys), probe_code, sizeof(probe_code));
        arch_exec_publish(P2V(phys), sizeof(probe_code));
        rc = probe_module_loads(provider);
        if (paging_pte_restore(provider, 0)) return -9;
    }
    return pgalloc_free_n_owner(LEDGER_OWNER_BOOT, phys / PAGE_SIZE, 1) ? rc : -9;
}
