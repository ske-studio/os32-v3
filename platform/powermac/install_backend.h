#ifndef PM_INSTALL_BACKEND_H
#define PM_INSTALL_BACKEND_H

#include "install_medium.h"
#include "dev.h"

int pm_install_view_unit(unsigned logical, u32 *unit);
int pm_install_resolve(Device *, u32, u32, u32 *, u32 *, u32 *);
int pm_install_bind(const InstallPlanV1 *, u32 generation);
void pm_install_invalidate(u32 unit);
void pm_install_publish(u32 unit);
void pm_format_register(void);
#endif
