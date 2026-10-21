#ifndef KDEBUG_GFINIT_RESOURCES_H
#define KDEBUG_GFINIT_RESOURCES_H
#include "recover.h"
/* GPU is bus 0:13.0. Returns complete bus-0 exclusions or fails with count=0.
 * No PCI data writes. Unknown enabled memory BAR sizes are not inferred. */
int gfinit_resources(struct recover_range *exclude, u32 *count);
#endif
