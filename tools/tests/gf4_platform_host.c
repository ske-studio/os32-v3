/* Kernel-side dependencies for UNIT14 display tests. Device/service APIs
 * remain real. Weak seams let the C1 fixture's observed PCI/IRQ model win.
 * Unexpected PCI writes and IRQ changes are failures, never fake success. */
#include "gf_command_runtime_host.h"
#include "../../drivers/pci/pci.h"
#include "../../kernel/irq/irq.h"
#include "../../drivers/video/geforce/geforce_module.h"
#include "../../mm/pgalloc.h"
#include "../../include/sys.h"
/* The display fixtures have no kernel page arena. F5 ROM acquisition may
 * fail before publishing its view (PM decision 8); F4 must still start.
 * Module fixtures supply their existing strong page allocator instead. */
__attribute__((weak)) u32 pgalloc_alloc_phys(u32 owner,int pages)
{ (void)owner;(void)pages;return 0; }
__attribute__((weak)) int pgalloc_free_n_owner(u32 owner,u32 pfn,int pages)
{ (void)owner;(void)pfn;(void)pages;assert(0);return 0; }
/* These fixtures do not dispatch a live F5 foreground operation. */
__attribute__((weak)) int sys_time_now(u32 *lo,u32 *hi)
{ (void)lo;(void)hi;assert(0);return -1; }
__attribute__((weak)) void serial_puts_polled(const char *s) { (void)s; }
__attribute__((weak)) int serial_gate_active(void) { return 0; }
__attribute__((weak)) int irq_unregister(unsigned irq,irq_handler_fn fn,void *arg)
{ (void)irq;(void)fn;(void)arg;assert(0);return -1; }
__attribute__((weak)) u16 pci_cfg_read16(u32 bus,u32 dev,u32 fn,u32 reg)
{ (void)bus;(void)dev;(void)fn;(void)reg;assert(0);return 0; }
__attribute__((weak)) u8 pci_cfg_read8(u32 bus,u32 dev,u32 fn,u32 reg)
{ (void)bus;(void)dev;(void)fn;(void)reg;assert(0);return 0; }
__attribute__((weak)) int pci_cfg_write16(u32 bus,u32 dev,u32 fn,u32 reg,u16 value)
{ (void)bus;(void)dev;(void)fn;(void)reg;(void)value;assert(0);return -1; }
__attribute__((weak)) int geforce_tick_bind(void (*poll)(void *),void *arg)
{ (void)poll;(void)arg;assert(0);return -1; }
__attribute__((weak)) void geforce_display_failed(void) { assert(0); }
/* C2-scan exercises only metadata; no live HAL dispatch is published. */
static GfxBackend platform_backend;
static struct geforce_platform platform_info;
static int platform_height;
static GeforceModuleContext platform_context={.backend=&platform_backend,.platform=&platform_info,.height=&platform_height};
__attribute__((weak)) const GeforceModuleContext *geforce_context=&platform_context;
