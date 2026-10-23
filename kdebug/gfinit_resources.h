#ifndef KDEBUG_GFINIT_RESOURCES_H
#define KDEBUG_GFINIT_RESOURCES_H
#include "recover.h"
#define GF_RES_GPU_DEV 13U
#define GF_RES_GPU_ID 0x022110deU /* NV44A, vendor 10de device 0221 */
/* GPU is bus 0:13.0. Returns complete bus-0 exclusions or fails with count=0.
 * No PCI data writes. Intel 82557 uses exact lengths; approved Trident
 * rev d3 uses conservative bounds. Unknown enabled memory BARs fail closed. */
int gfinit_resources(struct recover_range *exclude, u32 *count);
#endif
