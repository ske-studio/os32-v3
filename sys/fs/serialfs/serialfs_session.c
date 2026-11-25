/* Common resident scope and foreground session dispatch. */
#include "serialfs.h"
#include "kprintf.h"
#include "sys/fs/fd_redirect.h"
#include "os32_kapi_shared.h"

static const struct SfsSessionFg *g_fg = NULL;

void serialfs_session_set_fg(const struct SfsSessionFg *fg)
{
    g_fg = fg;
}

int serialfs_session_begin(void)
{
    if (sfs_persistent_enabled()) {
        struct SfsTransportInfo ti = { "?", {0}, 0 };
        int rc = sfs_persistent_scope_begin_owned(res_owner_get());
        if (rc != 0) return rc;
        (void)sfs_persistent_describe(&ti);
        console_printf(KLOG_LEGACY_CYAN,
                "sfs: transport=lan nic=%s peer=%02x:%02x:%02x:%02x:%02x:%02x sid=%u\n",
                ti.nic, (u32)ti.peer[0], (u32)ti.peer[1], (u32)ti.peer[2],
                (u32)ti.peer[3], (u32)ti.peer[4], (u32)ti.peer[5], sfs_persistent_client()->sid);
        return 0;
    }
    return g_fg ? g_fg->begin() : OS32_ERR_NOSYS;
}

int serialfs_session_end(int exit_code)
{
    if (sfs_persistent_enabled()) {
        int rc = sfs_persistent_scope_end_owned(res_owner_get(), exit_code);
        if (rc == 0) console_printf(KLOG_LEGACY_CYAN, "sfs: exit=%d\n", exit_code);
        return rc;
    }
    return g_fg ? g_fg->end(exit_code) : OS32_ERR_NOSYS;
}

void serialfs_session_owner_exit(int owner)
{
    sfs_persistent_scope_owner_exit(owner);
}
