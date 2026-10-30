#ifndef HWDEV_MODULE_H
#define HWDEV_MODULE_H
#include "module_runtime.h"
#include "hwdev_private.h"
/* Boot-only facade; publication never changes the S0 owner/profile. */
int hwdev_module_load(struct module_handle core);
const struct hwdev_ops *hwdev_module_ops(void);
#endif
