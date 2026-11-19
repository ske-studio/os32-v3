#ifndef OS32_FONT_SOURCE_KCG_H
#define OS32_FONT_SOURCE_KCG_H

#include "kcg_contract.h"

/* Load after the root filesystem is available; unload before freeing users. */
int kcg_file_load(const char *path);
int kcg_file_ready(void);
void kcg_file_unload(void);
extern const KcgModuleApi kcg_file_api;

#endif
