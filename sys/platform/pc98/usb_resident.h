#ifndef PC98_USB_RESIDENT_H
#define PC98_USB_RESIDENT_H
#include "sys/dev/usb/usb_core.h"
#include "sys/kern/module/module_runtime.h"

/* Module-owned controller storage and core operations live for the boot. */
#define PC98_USB_HCS 3
struct pc98_usb_core {
    struct usb_hc hc[PC98_USB_HCS];
    const struct usb_hc_ops *ops;
    int (*isr)(void *);
    int (*register_hc)(struct usb_hc *);
    void (*dead)(struct usb_hc *, u32);
    void (*service)(void);
};

/* Foreground module start, after PCI reservation and EHCI handoff. */
int pc98_usb_module_start(struct module_record *record, u32 entry, u32 *exports);
int usb_module_init(struct module_handle owner);
int pc98_usb_resident_start(struct module_handle owner, struct pc98_usb_core *core);
int usb_resident_irq_attach(struct module_handle owner, const struct usb_pci_fn *f,
                            int (*isr)(void *), void *arg);
int usb_resident_irq_detach(const struct usb_pci_fn *f);
int usb_resident_tick_set(void (*service)(void));
void pc98_usb_service_log(const char *fmt, const u32 *args);
void pc98_usb_tick_vprintf(u8 attr, const char *fmt, const u32 *args);
int pc98_usb_load(void);
void pc98_usb_flush_boot_log(void);
void pc98_usb_boot(void);
#endif
