#ifndef OS32_KCG_FILE_H
#define OS32_KCG_FILE_H

#include "kcg_module.h"

/* Load after the root filesystem is available; unload before freeing users. */
int kcg_file_load(const char *path);
void kcg_file_unload(void);
extern const KcgModuleApi kcg_file_api;

#endif
