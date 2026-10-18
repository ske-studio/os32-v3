/* Paired POST_ROOT CD source reader; core owns registration and rollback. */
#ifndef CD_MODULE_H
#define CD_MODULE_H
#include "module_runtime.h"
#include "dev.h"
#include "vfs.h"

#define CD_MODULE_OPS_VERSION 1u
typedef struct {
    VfsOps *fs;
    Device *device;              /* NULL when no readable medium was found */
} CdModuleApi;

int cd_module_load(void);
int cd_module_abort(void);
#endif
