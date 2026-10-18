#include "cd_module.h"
#include "cd_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "kprintf.h"

extern const u32 cd_kexports[];
extern const struct module_symbol_contract cd_import_contracts[];
extern const struct module_symbol_contract cd_export_contracts[];

static struct module_handle cd_handle;
static CdModuleApi candidate;
static int fs_registered, device_registered;

static int body_contains(struct module_record *r, const void *p, u32 size)
{
    u32 addr = (u32)(uptr)p;
    u32 bytes = (r->pages - r->prefix_pages) * MODULE_PAGE_SIZE;
    return addr >= r->base && size <= bytes && addr - r->base <= bytes - size;
}

static int cd_start(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(CdModuleApi *) = (int (*)(CdModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    if (rc) return rc;
    if (!body_contains(r, candidate.fs, sizeof(*candidate.fs)) ||
        (candidate.device && !body_contains(r, candidate.device, sizeof(*candidate.device))))
        return MODULE_INIT_FAILED;
    if (!module_ref_acquire(cd_handle, r->owner, MODULE_REF_REGISTRATION, 0)) return MODULE_INIT_FAILED;
    rc = vfs_register_fs(candidate.fs);
    if (rc) {
        module_ref_release(cd_handle, MODULE_REF_REGISTRATION);
        return rc;
    }
    fs_registered = 1;
    if (candidate.device) {
        if (!module_ref_acquire(cd_handle, r->owner, MODULE_REF_REGISTRATION, 0)) return MODULE_INIT_FAILED;
        rc = dev_register(candidate.device);
        if (rc) {
            module_ref_release(cd_handle, MODULE_REF_REGISTRATION);
            return rc;
        }
        device_registered = 1;
    }
    return 0;
}

static int cd_detach(struct module_record *r)
{
    (void)r;
    /* unregister_fs refuses while any ISO mount retains body callbacks. In
     * that case keep both registrations and the body, as T5c-0 specifies.
     * PIO is synchronous: there is no IRQ, timer or DMA callback to stop. */
    if (fs_registered) {
        if (vfs_unregister_fs(candidate.fs) != VFS_OK) return 0;
        fs_registered = 0;
        module_ref_release(cd_handle, MODULE_REF_REGISTRATION);
    }
    if (device_registered) {
        if (dev_unregister(candidate.device)) return 0;
        device_registered = 0;
        module_ref_release(cd_handle, MODULE_REF_REGISTRATION);
    }
    candidate.fs = 0;
    candidate.device = 0;
    return 1;
}

int cd_module_load(void)
{
    if (cd_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_CD_MOD, .name = "cd",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, CD_MODULE_ABI_GEN,
            bi->img_crc, CD_MODULE_OPS_VERSION, bi->img_valid,
            CD_IMPORT_COUNT, cd_import_contracts, cd_export_contracts,
            MODULE_ID_ATAPI_ISO
        },
        .imports = cd_kexports,
        .lifecycle = {.start = cd_start, .detach = cd_detach}
    };
    int rc = module_load(&request, &cd_handle);
    if (!rc) rc = module_start(cd_handle);
    if (!rc && !module_exports(cd_handle)) rc = MODULE_INIT_FAILED;
    kprintf(0x07, "[cd] module rc=%d pair=%08x fs=%d device=%d\n",
            rc, bi->img_crc, fs_registered, device_registered);
    return rc;
}

int cd_module_abort(void) { return module_abort(cd_handle); }
