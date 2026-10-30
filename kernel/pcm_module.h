/* Private paired PCM facade; public KAPI and S4 signatures stay unchanged. */
#ifndef PCM_MODULE_H
#define PCM_MODULE_H
#include "pcm_contract.h"
#include "module/module_runtime.h"
#include "irq/irq.h"
#include "../mm/dma_pool.h"
#define PCM_MODULE_OPS_VERSION 1u
typedef struct {
    int (*open)(u32 rate);
    int (*write_check)(void);
    int (*write)(const void *buf, u32 bytes);
    int (*status)(u32 *free_bytes, u32 *counters);
    int (*close)(void);
    int (*set_volume)(u32 percent);
    int (*state)(void);
    int (*present)(void);
    void (*reclaim)(int owner);
    void (*tick)(void);
    int (*quiet)(void);
} PcmModuleApi;
#define PCM_MODULE_API_SLOTS 11u
int pcm_module_load(void);
int pcm_module_abort(void);
int pcm_module_quiet(void);
int pcm_module_irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags);
int pcm_module_irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg);
int pcm_module_dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out);
int pcm_module_dma_free(const struct dma_buf *buf);
#endif
