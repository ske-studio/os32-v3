/* Private paired LAN facade; public KAPI and core PCI/VFS remain unchanged. */
#ifndef LAN_MODULE_H
#define LAN_MODULE_H
#include "lan_contract.h"
#include "sfs_lan.h"
#include "module_runtime.h"
#include "irq.h"
#include "dma_pool.h"
#define LAN_MODULE_OPS_VERSION 1u
typedef struct {
    int (*probe)(const struct pci_dev *dev);
    int (*send)(const void *buf, u32 len);
    int (*recv)(void *buf, u32 cap, u32 *len);
    void (*mac)(u8 *mac);
    int (*state)(void);
    u32 (*rx_free)(void);
    int (*rx_ready)(void);
    int (*loopback)(int on);
    u32 (*stat)(u32 idx);
    void (*tick)(void);
    int (*quiet)(void);
    int (*release)(void);
    void (*lan_init)(SfsLan *, const SfsLanNic *, const SfsIo *, void *);
    int (*lan_peer)(SfsLan *, const char *);
    void (*lan_reset)(SfsLan *);
    SfsIo io;
} LanModuleApi;
#define LAN_MODULE_API_SLOTS 20u
int lan_module_load(void);
void lan_module_pci_bind_fallback(void);
int lan_module_abort(void);
int lan_module_session_acquire(void);
void lan_module_session_release(void);
int lan_module_quiet(void);
int lan_module_release(void);
int lan_module_irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags);
int lan_module_irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg);
int lan_module_dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out);
int lan_module_dma_free(const struct dma_buf *buf);
void lan_module_trace(u32 stage);
#endif
