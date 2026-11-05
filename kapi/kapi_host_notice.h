/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_KAPI_KAPI_HOST_NOTICE_H
#define OS32_KAPI_KAPI_HOST_NOTICE_H
#include "types.h"
#include "../sdk/include/os32/os32_host_notice.h"

int kapi_host_notice_read(u32 generation, u32 service, HostNotice *out);
u32 kapi_host_notice_serial(void);

#endif
