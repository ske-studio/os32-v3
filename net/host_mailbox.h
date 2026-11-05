/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_NET_HOST_MAILBOX_H
#define OS32_NET_HOST_MAILBOX_H
#include "types.h"
#include "../sdk/include/os32/os32_host_notice.h"

int host_mailbox_init(void); int host_mailbox_put(u32 generation, u16 service, u16 kind, const void *data, u16 len, int alive);
int host_mailbox_stage(u32 generation, u32 service, HostNotice *kernel_out);
void host_mailbox_commit(u32 generation, u32 serial);
/* net/host_mailbox.h */
struct HostAlive { u32 host_generation, observation; int valid; };
void host_alive_snapshot(struct HostAlive *out);

/* B3: init once, heap 64 slots + latest ALIVE; NOSYS on allocation failure.
 * put synchronously copies; accepted notifications are ACKed even if dropped.
 * stage is non-consuming; commit removes only the same generation/serial.
 * Both preserve IF separately. ALIVE snapshot is non-consuming and atomic;
 * valid starts 0, observation advances for every accepted ALIVE, independent
 * of allocation, app consumption and link reset. */

#endif
