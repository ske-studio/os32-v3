/* Reuse the destructive page/VFS fixture, then run the real CD adapter from
 * RELOCATED onward. The body init is injected; real PIO/body init is exercised
 * separately by cd_read_host.c. No host mock is claimed to boot PC-98. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#ifndef CD_ADAPTER
#define CD_ADAPTER "../../kernel/cd_module.c"
#endif
#include CD_ADAPTER

static u32 mode;
static int init_cd(CdModuleApi *out)
{
    struct module_record *r = module_record_get(cd_handle);
    CHECK(r->state == MODULE_STARTING && !module_exports(cd_handle));
    CHECK(!fs_registered && !device_registered && !r->registration_refs);
    out->fs = (VfsOps *)(uptr)(r->base + 128);
    out->device = (Device *)(uptr)(r->base + 512);
    *out->fs = fs_ops;
    *out->device = device;
    if (mode == 4) out->device = 0;
    return mode == 3 ? -1 : 0;
}

void _start(void)
{
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    if (mode != 5) input[0] = 11; /* Assert discovery is gone before page poison. */
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = cd_start, .detach = cd_detach};
    CHECK(!module_load(&q, &cd_handle));
    struct module_record *r = module_record_get(cd_handle);
    r->init = (u32)(uptr)init_cd;
    VfsOps fillers[VFS_MAX_FS];
    Device devices[MAX_DEVICES];
    char names[MAX_DEVICES][2];
    for (u32 i = 0; i < MAX_DEVICES; i++) {
        names[i][0] = 'A' + i; names[i][1] = 0;
        devices[i] = (Device){.name = names[i], .type = DEV_BLOCK};
        if (i < VFS_MAX_FS) {
            fillers[i] = (VfsOps){.name = names[i]};
            if (mode == 1) CHECK(!vfs_register_fs(&fillers[i]));
        }
        if (mode == 2) CHECK(!dev_register(&devices[i]));
    }
    int rc = module_start(cd_handle);
    if (mode == 1 || mode == 2 || mode == 3 || mode == 5) {
        CHECK(rc == MODULE_INIT_FAILED);
        CHECK(r->state == MODULE_ABSENT && !r->registration_refs);
        CHECK(!fs_registered && !device_registered && !dev_find("cd0"));
        pc98_boot_names_bind();
        CHECK(vfs_mount("/cd0", "cd0", "fixture") == VFS_ERR_INVAL);
        empty(); finish(0);
    }
    CHECK(!rc && module_exports(cd_handle));
    CHECK(r->registration_refs == (mode == 4 ? 1U : 2U));
    CHECK(!r->prefix_pages && mask[0] == 2);
    CHECK((dev_find("cd0") != 0) == (mode != 4));
    pc98_boot_names_bind();
    CHECK(vfs_mount("/cd0", "cd0", "fixture") == VFS_OK);
    if (mode == 6) {
        u32 before = returned;
        CHECK(cd_module_abort() == MODULE_ISOLATED);
        CHECK(r->state == MODULE_QUARANTINED && r->registration_refs == 2);
        CHECK(returned == before && mask[0] == 2 && !retired);
        CHECK(dev_find("cd0") && fs_registered);
        finish(0);
    }
    vfs_umount("/cd0");
    CHECK(!cd_module_abort());
    CHECK(!r->registration_refs && !dev_find("cd0"));
    pc98_boot_names_bind();
    CHECK(vfs_mount("/cd0", "cd0", "fixture") == VFS_ERR_INVAL);
    CHECK(!cd_module_abort());
    empty(); finish(0);
}
