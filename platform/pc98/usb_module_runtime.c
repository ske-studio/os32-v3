/* Platform services linked into usb.mod. A live or quarantined DMA page
 * pins its module even on a controller serviced entirely by polling. */
#include "usb_resident.h"
#include "drivers/usb/usb_internal.h"
#include "mm/dma_pool.h"
#include "mm/kmalloc.h"
#include "input/hal/in2_int.h"
#include "io.h"
#include "kprintf.h"
#include <stdarg.h>

extern struct module_handle pc98_usb_module_owner;

int usbp_dma_alloc(u32 size, u32 align, struct usb_dma *out)
{
    struct module_handle h = pc98_usb_module_owner;
    struct module_record *r = module_record_get(h);
    struct dma_buf buf;
    if (!r || !module_ref_acquire(h, r->owner, MODULE_REF_DMA, 0))
        return -USB_E_HC;
    int rc = dma_alloc(size, align, DMA_PHYS_LIMIT, &buf);
    if (rc) {
        module_ref_release(h, MODULE_REF_DMA);
        return rc;
    }
    *out = (struct usb_dma){.bus = buf.pa, .va = buf.va, .size = buf.size};
    return 0;
}

int usbp_dma_free(struct usb_dma *d)
{
    struct dma_buf buf = {.pa = d->bus, .va = d->va, .size = d->size};
    int rc = dma_free(&buf);
    if (!rc) module_ref_release(pc98_usb_module_owner, MODULE_REF_DMA);
    return rc;
}

int usbp_dma_quarantine(struct usb_dma *d)
{
    struct dma_buf buf = {.pa = d->bus, .va = d->va, .size = d->size};
    return dma_mark_leaked(&buf);
}

void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir)
{
    (void)d; (void)off; (void)len; (void)dir;
    __asm__ volatile("" ::: "memory"); /* PC-98 PCI snoops main memory. */
}

void *usbp_mem_alloc(u32 bytes) { return kmalloc(bytes); }
void usbp_mem_free(void *p) { kfree(p); }
u32 usbp_irq_save(void) { return irq_save(); }
void usbp_irq_restore(u32 flags) { irq_restore(flags); }
u32 usbp_ms(void) { return in2_time_ms(); }

void usbp_log(const char *fmt, ...)
{
    /* i386 cdecl passes arguments as contiguous 32-bit stack slots,
     * matching the resident kvprintf interface. */
    va_list args;
    va_start(args, fmt);
    if (usb_busy) {
        pc98_usb_tick_vprintf(KLOG_ATTR, fmt, (const u32 *)args);
        pc98_usb_tick_vprintf(KLOG_ATTR, "\n", NULL);
    } else {
        kvprintf(KLOG_ATTR, fmt, (const u32 *)args);
        kprintf(KLOG_ATTR, "\n");
    }
    va_end(args);
}
