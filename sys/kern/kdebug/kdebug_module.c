/* POST_ROOT debug module: validate privately, publish once, retain body only. */
#include "../module/module.h"
#ifndef PM_KDEBUG
#include "../../dev/hwdev/hwdev_module.h"
#endif
#include "../module/module_runtime.h"
#include "kdebug_imports.h"
#include "platform_module_pair.h"
#include "config.h"
#include "../init/sysconfig.h"
#include "vfs.h"
#include "../mm/pgalloc.h"
#include "memmap.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_generations.h"

#ifndef PM_KDEBUG
struct kdebug kdebug;
#endif
STATIC_ASSERT(sizeof(struct kdebug_ops) == (MODULE_OPS_SLOTS + 1) * 4,
              kdebug_ops_wire_size);
STATIC_ASSERT(MODULE_PAGE_SIZE == PAGE_SIZE, module_page_matches_allocator);

static int kdebug_start(struct module_record *record, u32 entry, u32 *candidate)
{
    (void)record;
    int (*init)(struct kdebug_ops *) = (int (*)(struct kdebug_ops *))(uptr)entry;
    return init((struct kdebug_ops *)candidate);
}

const char *kdebug_state_name(int state)
{
    static const char *const names[] = {
        "OFF", "RUNNING", "NOT_FOUND", "BAD_HEADER", "PAIR_MISMATCH",
        "NO_IMAGE_CRC", "ABI_MISMATCH", "NO_PAGES", "SHORT_READ",
        "BAD_CRC", "INIT_FAILED", "RELOC_INVALID", "IMPORT_INVALID",
        "RECORD_FULL", "BAD_DEPENDENCY", "BUSY", "QUARANTINED"
    };
    if (state < 0 || (u32)state >= sizeof(names) / sizeof(names[0]))
        return "UNKNOWN";
    return names[state];
}

void kdebug_load(void)
{
    struct module_handle handle = {0, 0};
    if (kdebug.open_count ||
        sysconfig_get_int(SYS_SYSTEM_CFG, "KDEBUG", 0) != 1) return;
    kdebug.open_count++;
    u32 pair = 0;
    int pair_valid = platform_module_pair(&pair);
    const struct module_request request = {
        .path = SYS_KDEBUG_BIN,
        .name = "kdebug",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, KDEBUG_ABI_GEN,
            pair, KDEBUG_OPS_VERSION, pair_valid, KDEBUG_IMPORT_COUNT,
            kdebug_import_contracts, kdebug_export_contracts, MODULE_ID_KDEBUG
        },
        .imports = (const u32 *)kdebug_kexports,
        .lifecycle = {.start = kdebug_start}
    };
    int rc = module_load(&request, &handle);
    struct module_record *record = module_record_get(handle);
    if (record) {
        kdebug.owner = record->owner;
        kdebug.pair_crc = record->pair_crc;
    }
    if (!rc) rc = module_start(handle);
    const u32 *exports = module_exports(handle);
    /* Publish only an available RUNNING export table. */
    if (!rc && !exports) rc = MODULE_INIT_FAILED;
    if (rc) {
        kdebug.state = rc;
        kprintf(KLOG_ATTR, "[kdebug] rejected: %s\n", kdebug_state_name(rc));
        return;
    }
    kmemcpy(&kdebug.ops, exports, sizeof(kdebug.ops));
    kdebug.base = record->base;
    kdebug.base_pa = record->backing;
    kdebug.pages = record->pages;
    kdebug.state = KDEBUG_RUNNING;
#ifndef PM_KDEBUG
    (void)hwdev_module_load(handle);
#endif
    kprintf(KLOG_ATTR, "[kdebug] running pair %08x\n", kdebug.pair_crc);
}
