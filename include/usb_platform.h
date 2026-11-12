#ifndef USB_PLATFORM_H
#define USB_PLATFORM_H
#include "types.h"
/* PCI identity and measured BAR resources, supplied by the platform. */
struct usb_pci_fn {
    u8 bus, dev, fn, irq;
    u16 vendor, device;
    u32 bar_base[6], bar_size[6];
};
struct usb_dma { u32 bus; void *va; u32 size, cookie; };
/* Page allocation, alignment and no 64KB crossing [HW2]; failure < 0.
 * Quarantine retains storage for the entire boot lifetime. */
int usbp_dma_alloc(u32 size, u32 align, struct usb_dma *out);
int usbp_dma_free(struct usb_dma *d);
int usbp_dma_quarantine(struct usb_dma *d);
enum { USBP_TO_DEV, USBP_TO_CPU };
/* Transfer ownership for this range. ED/TD are 16B and may share a PPC 32B
 * cache line. Common code must not assume partial-line invalidation works.
 * Platform supplies coherent USB DMA mappings (uncached or hardware snooped),
 * with sync acting only as a barrier. Otherwise it must align allocation
 * blocks to cache-line size. Architecture LE accessors own no cache policy. */
void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir);
void *usbp_mem_alloc(u32 bytes);
void usbp_mem_free(void *p);
int usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va);
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar);
u32 usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg);
int usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 v);
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear);
#define USBP_IRQ_NONE 0
#define USBP_IRQ_HANDLED 1
/* ISR returns USBP_IRQ_NONE or USBP_IRQ_HANDLED. */
int usbp_irq_attach(const struct usb_pci_fn *f, int (*isr)(void *), void *arg);
void usbp_irq_detach(const struct usb_pci_fn *f);
int usbp_tick_attach(void (*fn)(void));
u32 usbp_ms(void);
/* Foreground startup/stop only; never from tick. Common code assumes no IF state. */
void usbp_udelay(u32 us);
u32 usbp_irq_save(void);
void usbp_irq_restore(u32 f);
enum { USBP_INPUT_KBD, USBP_INPUT_MOUSE };
/* Input calls require IF=0. Sources 1..4; exhausted allocation returns <0. */
int usbp_input_src_alloc(int kind);
void usbp_input_key(u8 src, u8 sc, int down);
void usbp_input_key_repeat(u8 src, u8 sc);
void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 buttons);
void usbp_input_release(int kind, u8 src);
/* One-line startup/change diagnostics, also used by usbstat. */
void usbp_log(const char *fmt, ...);
#endif
