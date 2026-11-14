#include "platform_host.h"
#include "net/host_mailbox.h"
#include "os32_kapi_shared.h"

/* PLN1 has no Host Services transport or notice mailbox. */
i32 platform_host_open(const char *req, u32 len, int owner)
{
    (void)req;
    (void)len;
    (void)owner;
    return OS32_ERR_NOSYS;
}

i32 platform_host_status(i32 h, u32 *status, u32 *length, int owner)
{
    (void)h;
    (void)status;
    (void)length;
    (void)owner;
    return OS32_ERR_NOSYS;
}

i32 platform_host_read_stage(i32 h, u32 cap, const u8 **out, int owner)
{
    (void)h;
    (void)cap;
    (void)out;
    (void)owner;
    return OS32_ERR_NOSYS;
}

i32 platform_host_write(i32 h, const void *buf, u32 len, int owner)
{
    (void)h;
    (void)buf;
    (void)len;
    (void)owner;
    return OS32_ERR_NOSYS;
}

i32 platform_host_close(i32 h, int owner)
{
    (void)h;
    (void)owner;
    return OS32_ERR_NOSYS;
}

void platform_host_owner_exit(int owner)
{
    (void)owner;
}

int host_mailbox_stage(u32 generation, u32 service, HostNotice *out)
{
    (void)generation; (void)service; (void)out;
    return OS32_ERR_NOSYS;
}

void host_mailbox_commit(u32 generation, u32 serial)
{
    (void)generation; (void)serial;
}

u32 host_mailbox_latest_serial(void) { return 0; }
