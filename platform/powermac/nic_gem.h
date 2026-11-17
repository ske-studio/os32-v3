#ifndef POWERMAC_NIC_GEM_H
#define POWERMAC_NIC_GEM_H
#include "net/nic.h"
/* Platform-exclusive registration slot; PowerMac has no LGY98.
 * P1g registers this adapter here; AUTO already falls back to this slot. */
#define PM_NET_NIC_GEM NET_NIC_LGY98
extern const struct NetAdapter net_gem_adapter;
#endif
