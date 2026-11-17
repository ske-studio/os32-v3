/* Host Services notice KAPI: NET_HOSTSVC_DESIGN §5 and addendum #4. */
#include "kapi_host_notice.h"
#include "../net/host_mailbox.h"
#include "redir_access.h"
#include "os32_kapi_shared.h"
#include "range_internal.h"
#include "kstring.h"

int kapi_host_notice_read(u32 generation, u32 service, HostNotice *out)
{
    struct caller_access caller;
    HostNotice notice;
    int rc, managed;

    managed = caller_access_get(&caller);
    /* The resident CPL0 shell has neither a saved USER caller nor WM scope.
     * Use the same range check and direct copy as kapi_host_read there. */
    if (!out || (managed ? !check_caller_write_range(&caller, out, sizeof(*out))
                         : !kapi_user_range_ok(out, sizeof(*out))))
        return OS32_ERR_INVAL;
    rc = host_mailbox_stage(generation, service, &notice);
    if (rc) return rc;
    if (managed) {
        if (!copy_to_caller(&caller, out, &notice, sizeof(notice)))
            return OS32_ERR_INVAL;
    } else kmemcpy(out, &notice, sizeof(notice));
    host_mailbox_commit(notice.host_generation, notice.serial);
    return 0;
}

u32 kapi_host_notice_serial(void)
{
    return host_mailbox_latest_serial();
}
