#ifndef PLATFORM_INSTALL_MANAGEMENT_H
#define PLATFORM_INSTALL_MANAGEMENT_H

#include "install_medium.h"

#if OS32_FEATURE_IDE
#define PLATFORM_INSTALL_VIEW_WRITE_AUTH 1
const struct install_medium *platform_install_medium(void);
#else
#define platform_install_medium() ((const struct install_medium *)0)
#endif

#endif
