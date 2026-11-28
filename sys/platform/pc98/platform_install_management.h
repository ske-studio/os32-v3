#ifndef PLATFORM_INSTALL_MANAGEMENT_H
#define PLATFORM_INSTALL_MANAGEMENT_H

#include "install_medium.h"

/* No V1 installation provider for this machine yet. */
#define PLATFORM_INSTALL_FORMAT_HD 1
#define platform_install_format_hd(drv, sectors) (-1)
#define platform_install_medium() ((const struct install_medium *)0)

#endif
