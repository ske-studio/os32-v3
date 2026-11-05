/* USER string staging belongs here: target:true wrappers do not copy it. */
#include "os32_kapi_shared.h"
#include "redir_access.h"
#include "vfs.h"
#include "kmalloc.h"
#include "../exec/exec.h"
#include "../gfx/gfx_osri_view.h"

int kapi_osri_show(const char *path, u32 duration_ms)
{
    struct caller_access caller;
    if (!caller_access_get(&caller)) return OS32_ERR_INVAL;
    if (!duration_ms || duration_ms > GFX_OSRI_SHOW_MAX_MS) return OS32_ERR_INVAL;
    char *copy = kmalloc(VFS_MAX_PATH);
    if (!copy) return OS32_ERR_NOSPC;
    int rc = OS32_ERR_INVAL;
    for (u32 i = 0; i < VFS_MAX_PATH; i++) {
        /* copy_caller_bytes checks each 1-byte range against the caller band. */
        if (!copy_caller_bytes(&caller, path + i, copy + i, 1)) {
            kfree(copy);
            if (caller.origin == CALLER_USER) ring3_fault_kill();
            return OS32_ERR_INVAL;
        }
        if (!copy[i]) {
            if (i) rc = gfx_osri_view(copy, duration_ms, caller.app_id);
            break;
        }
    }
    kfree(copy);
    return rc;
}
