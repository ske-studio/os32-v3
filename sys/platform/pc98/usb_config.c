#include "usb_platform.h"
#include "sys/dev/pci/pci.h"
#include "sys/hal/time/cpu_calibrate.h"
#include "io.h"

/* The BIOS semaphore shares a dword with the OS byte. Select only its lane. */
int usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 value)
{
    unsigned flags = irq_save();
    outpd(PCI_CFG_ADDR_PORT, pci_cfg_addr(f->bus, f->dev, f->fn, reg));
    outp(PCI_CFG_DATA_PORT + (reg & 3), value);
    irq_restore(flags);
    return 0;
}

int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    u16 cmd = usbp_cfg_read32(f, PCI_CFG_COMMAND);
    return pci_cfg_write16(f->bus, f->dev, f->fn, PCI_CFG_COMMAND, (cmd | set) & ~clear);
}

void usbp_udelay(u32 us)
{
    cpu_delay_us(us);
}
