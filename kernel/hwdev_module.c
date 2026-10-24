#include "hwdev_module.h"
#include "module.h"
#include "hwdev/generated/hwdev_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "kprintf.h"

static struct module_handle hwdev_handle;
static const struct hwdev_ops *published;
static const struct hwdev_core_api *core_api;

static int contains(u32 base, u32 size, u32 address, u32 bytes)
{
    return address >= base && address - base <= size && bytes <= size - (address - base);
}

static const struct hwdev_ops *current_ops(void)
{
    struct module_record *r = module_record_get(hwdev_handle);
    if (!r || !module_exports(hwdev_handle) || r->dependency_count != 1 ||
        !module_exports(r->dependencies[0])) return 0;
    return published;
}

/* Cached facade pointers still check both loader generations at every entry.
 * The successfully published body is pinned for this boot, including PAUSED. */
static int hwdev_call(u32 op, void *req, u32 size, const struct recover_ops *base,
                      struct recover_transport *transport, const struct recover_profile *s0)
{
    const struct hwdev_ops *ops = current_ops();
    return ops ? ops->call(op, req, size, base, transport, s0) : -1;
}
static void hwdev_release(void)
{
    const struct hwdev_ops *ops = current_ops();
    if (ops) ops->release_work();
}
static const struct hwdev_ops facade = {
    HWDEV_ABI, sizeof(struct hwdev_ops), hwdev_call, hwdev_release
};
const struct hwdev_ops *hwdev_module_ops(void)
{
    return current_ops() ? &facade : 0;
}

static int hwdev_detach(struct module_record *r)
{
    (void)r;
    published = 0;
    return 1;
}

static int hwdev_start(struct module_record *r, u32 entry, u32 *exports)
{
    /* KMOD init checks/copies its export table; initialize only returns RO data. */
    int (*init)(u32 *) = (int (*)(u32 *))(uptr)entry;
    if (init(exports)) return MODULE_INIT_FAILED;
    const struct hwdev_manifest *m = 0;
    const struct hwdev_ops *ops = 0;
    int (*initialize)(const struct hwdev_manifest **, const struct hwdev_ops **) =
        (int (*)(const struct hwdev_manifest **, const struct hwdev_ops **))(uptr)r->exports[1];
    if (!initialize || initialize(&m, &ops)) return MODULE_INIT_FAILED;
    if (!contains(r->ro, r->ro_size, (u32)(uptr)m, sizeof(*m)) ||
        !contains(r->ro, r->ro_size, (u32)(uptr)ops, sizeof(*ops))) return MODULE_INIT_FAILED;
    struct module_record *core = module_record_get(r->dependencies[0]);
    if (!core || !module_exports(r->dependencies[0]) || core->module_id != MODULE_ID_KDEBUG)
        return MODULE_BAD_DEPENDENCY;
    if (m->identity.abi != HWDEV_ABI || m->identity.size != sizeof(m->identity) ||
        m->identity.kind != HWDEV_KIND_GF6200 ||
        m->identity.profile_version != RECOVER_PROFILE_VERSION ||
        m->version != HWDEV_ABI || m->size != sizeof(*m) ||
        m->identity.kernel_pair_crc != r->pair_crc ||
        m->identity.core_image_crc != core->image_crc ||
        m->identity.contract_crc != core_api->contract_crc ||
        m->layout_crc != core_api->layout_crc ||
        m->lifetime_flags != HWDEV_LIFETIME_FLAGS ||
        m->work_max_pages != HWDEV_WORK_MAX_PAGES || m->dma_max_pages ||
        m->ops_abi != HWDEV_ABI || m->ops_size != sizeof(*ops) || m->reserved ||
        ops->abi != HWDEV_ABI || ops->size != sizeof(*ops)) return MODULE_ABI_MISMATCH;
    if (!contains(r->text, r->text_size, (u32)(uptr)ops->call, 1) ||
        !contains(r->text, r->text_size, (u32)(uptr)ops->release_work, 1)) return MODULE_INIT_FAILED;
    published = ops; /* externally available only after module_start commits */
    return 0;
}

int hwdev_module_load(struct module_handle core_handle)
{
    /* F9: no lookup, logging, allocation or start when KDEBUG is not running. */
    if (kdebug.state != KDEBUG_RUNNING) return 0;
    if (hwdev_handle.generation) return MODULE_BUSY;
    struct module_record *core = module_record_get(core_handle);
    if (!core || !module_exports(core_handle) || core->module_id != MODULE_ID_KDEBUG)
        return MODULE_BAD_DEPENDENCY;
    if (!kdebug.ops.hwdev_core_get) return MODULE_ABI_MISMATCH;
    core_api = kdebug.ops.hwdev_core_get();
    if (!contains(core->ro, core->ro_size, (u32)(uptr)core_api, sizeof(*core_api)))
        return MODULE_ABI_MISMATCH;
    if (core_api->abi != HWDEV_ABI || core_api->size != sizeof(*core_api) ||
        core_api->contract_crc != HWDEV_CONTRACT_CRC ||
        core_api->layout_crc != HWDEV_LAYOUT_CRC) return MODULE_ABI_MISMATCH;
    u32 imports[HWDEV_IMPORT_COUNT];
    hwdev_fill_imports(imports, core_api);
    for (u32 i = 0; i < HWDEV_IMPORT_COUNT; i++) {
        if (!imports[i]) return MODULE_IMPORT_INVALID;
        if (i < HWDEV_CORE_SLOTS && !contains(core->text, core->text_size, imports[i], 1))
            return MODULE_IMPORT_INVALID;
    }
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_HWDEV_MOD, .name = "hwdev",
        .policy = {KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, HWDEV_CONTRACT_CRC,
                   bi->img_crc, HWDEV_ABI, bi->img_valid, HWDEV_IMPORT_COUNT,
                   hwdev_import_contracts, hwdev_export_contracts, MODULE_ID_HWDEV},
        .imports = imports,
        .dependencies = &core_handle, .dependency_count = 1,
        .lifecycle = {.start = hwdev_start, .detach = hwdev_detach}
    };
    int rc = module_load(&request, &hwdev_handle);
    if (!rc) rc = module_start(hwdev_handle);
    if (!rc) {
        struct module_record *r = module_record_get(hwdev_handle);
        /* BOOT_BODY: no hot unload; rollback before publication remains free. */
        if (!module_ref_acquire(hwdev_handle, r->owner, MODULE_REF_REGISTRATION, 0)) {
            published = 0;
            rc = MODULE_INIT_FAILED;
        }
    }
    if (rc) kprintf(KLOG_ATTR, "[hwdev] rejected: %s\n", kdebug_state_name(rc));
    return rc;
}
