/* Publish /host without waiting for a connected host. */
#include "hostfs.h"
#include "sys/fs/serialfs/serialfs.h"
#include "kprintf.h"
#if KBOOT_LINK
#include "sys/kern/idle_work.h"
#endif

int pm_hostfs_boot(u32 tb_hz)
{
    struct SfsPersistentConfig cfg = {
        .io = &pm_hostfs_io, .io_ctx = &pm_hostfs_transport, .lan_enabled = 1,
#if KBOOT_LINK
        .describe = pm_hfs_describe,
#endif
    };
    int rc = pm_hfs_set_clock(tb_hz);
#if KBOOT_LINK
    if (!rc) rc = idle_work_register(pm_hfs_link_poll);
#endif
    if (!rc) rc = serialfs_init();
    if (!rc) rc = sfs_persistent_start(&cfg);
    if (rc) kprintf(0x07, "HOSTFS init rc=%d\r\n", rc);
    return rc;
}
