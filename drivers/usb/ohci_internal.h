#ifndef OHCI_INTERNAL_H
#define OHCI_INTERNAL_H
#include "drivers/usb/ohci_priv.h"
/* HCD-private helpers; the frozen 1A core/platform interfaces are unchanged. */
#define OHCI_BM 4u
#define OHCI_STARTED_BM 8u
#define OHCI_WATCH (OHCI_INTR_WDH | OHCI_INTR_RD | OHCI_INTR_UE | OHCI_INTR_RHSC)
static inline u32 ohci_rd(struct usb_hc *hc, u32 off)
{
    return mmio_rd32_le((volatile u8 *)hc->regs + off);
}
static inline void ohci_wr(struct usb_hc *hc, u32 off, u32 v)
{
    mmio_wr32_le((volatile u8 *)hc->regs + off, v);
}
static inline void ohci_sync(struct usb_hc *hc, u32 bus, u32 len, int dir)
{
    u32 i;
    for (i = 0; i < USB_HC_DMA_MAX; ++i) {
        struct usb_dma *d = &hc->dma[i];
        if (bus >= d->bus && bus - d->bus < d->size) {
            usbp_dma_sync(d, bus - d->bus, len, dir);
            return;
        }
    }
}
static inline void ohci_zero(void *v, u32 n)
{
    u8 *p = v;
    while (n--) *p++ = 0;
}
static inline int ohci_dma_page_ok(const struct usb_dma *d)
{
    return !(d->bus & (USB_DMA_PAGE_SIZE - 1u)) &&
           (d->bus & 0xffffu) + d->size <= 0x10000u &&
           d->size == USB_DMA_PAGE_SIZE;
}
static inline void ohci_free_record(struct usb_hc *hc, void *va, u32 bus)
{
    struct usb_dma d = { bus, va, USB_DMA_BLOCK_SIZE, 0 };
    usb_dmamem_free(hc, &d);
}
#endif
