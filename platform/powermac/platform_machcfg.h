#ifndef POWERMAC_PLATFORM_MACHCFG_H
#define POWERMAC_PLATFORM_MACHCFG_H

#include "machcfg_contract.h"

/* Return 1 for a PVR-specific row, 0 for the machine default. */
int pm_machcfg_seed(u32 pvr, struct machcfg_seed *out);

#endif
