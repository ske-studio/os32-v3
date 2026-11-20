#ifndef POWERMAC_ROOT_BOOT_H
#define POWERMAC_ROOT_BOOT_H

#include "boot_root.h"

#include "exec/exec_hooks.h"

#if OS32_HOOK_IME
int pm_root_dict_ready(void);
int pm_root_learning_enabled(void);
#endif

int pm_root_boot(const struct boot_root_image *image);

#endif
