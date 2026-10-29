#ifndef KDEBUG_GFINIT_RESOURCES_H
#define KDEBUG_GFINIT_RESOURCES_H
#include "recover.h"
#define GF_RES_GPU_DEV RECOVER_GF_DEV
#define GF_RES_GPU_ID RECOVER_GF_DEVICE /* NV44A, vendor 10de device 0221 */
/* GPU is bus 0:13.0. Returns complete bus-0 exclusions or fails with count=0.
 * No PCI data writes. Intel 82557 uses exact lengths; approved Trident
 * rev d3 uses conservative bounds. Unknown enabled memory BARs fail closed. */
int gfinit_resources(struct recover_range *exclude, u32 *count);
#ifdef GF_PRODUCTION
/* struct gfinit_resource_reject: hwdev/gf/production.h. Production copies never print: the boot/acquire check runs before the 98
 * decode is proven safe, and kprintf reaches 98 text VRAM. The diagnostic
 * (kdebug gfinit) copy keeps its console line. */
struct gfinit_resource_reject;
void gfinit_resources_rejected(struct gfinit_resource_reject *out);
#endif
#endif
