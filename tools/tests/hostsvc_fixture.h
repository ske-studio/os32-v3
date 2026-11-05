/* Shared test seams, NET_HOSTSVC_DESIGN #12. No product replacement here.
 * UNIT implementations supply NIC/SFS/heap/copyout/media in their own stage.
 * Reset product owners separately: reset never pretends to bind/HELLO/ALIVE. */
#ifndef OS32_HOSTSVC_FIXTURE_H
#define OS32_HOSTSVC_FIXTURE_H
#include "net/nic.h"
#include "fs/serialfs/sfs_client.h"

void hs_reset(u32 start_tick);
void hs_advance(u32 ticks);
void hs_set_if(int enabled);
u32 hs_now(void);
int hs_if(void);
/* One pending failure per point, consumed only by the matching seam. */
void hs_fail(const char *point, int rc);
int hs_failure_take(const char *point, int *rc);
void hs_copyout_hook(void (*hook)(void));
void hs_copyout_run_hook(void);

/* Implemented by NIC/ADAPTER and SFS units, not success stubs. */
int hs_rx(enum NetNicId nic, const void *frame, u32 len);
int hs_tx_take(void *out, u32 cap, u32 *len);
extern const struct NetAdapter hs_adapter;
extern const SfsIo hs_sfs_io;
#endif
