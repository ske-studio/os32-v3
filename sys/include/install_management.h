#ifndef OS32_INSTALL_MANAGEMENT_H
#define OS32_INSTALL_MANAGEMENT_H

#include "os32_kapi_shared.h"

int install_begin(const InstallPlanV1 *plan, u32 *handle);
int install_rescan(u32 handle);
int install_end(u32 handle, u32 outcome);
void install_reclaim_owner(int owner);

#endif
