#include "gfinit_resources.h"
#include "kprintf.h"
#include "../drivers/pci/pci.h"
#include "../drivers/pci/pci_barsize.h"
#ifdef GF_PRODUCTION
#include "../hwdev/gf/production.h"
#endif
#if !defined(GFINIT_RESOURCES_HOST) && !defined(GF_PRODUCTION)
#ifdef HWDEV_MODULE
#include "hwdev/generated/hwdev_import_macros.h"
#else
#include "generated/kdebug_import_macros.h"
#endif
#endif
/* Lengths are measured once by boot PCI enumeration. */

#include "gfinit_resources/bounds.inc"

#include "gfinit_resources/bars.inc"

#include "gfinit_resources/windows.inc"

#include "gfinit_resources/scan.inc"
