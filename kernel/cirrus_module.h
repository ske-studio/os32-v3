/* Paired Cirrus HAL registration. Identification and DEVICE ownership stay core. */
#ifndef CIRRUS_MODULE_H
#define CIRRUS_MODULE_H
#include "gfx_hal.h"
#include "module/module_runtime.h"
#define CIRRUS_MODULE_OPS_VERSION 1u
typedef struct {
    GfxBackend *backend;
    GfxCounters *counters;
    int *height, *flip, *page;
} CirrusModuleContext;
extern const CirrusModuleContext *cirrus_context;
extern GfxBackend cirrus_backend_ops;
int cirrus_module_load(void);
int cirrus_module_abort(void);
#endif
