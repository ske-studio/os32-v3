/* HW_BASIC_DESIGN §3/E. Standalone module-import source header. */
#ifndef PCI_HWB_H
#define PCI_HWB_H
#include "kdebug/hwb_wire.h"

/* bdf uses HW_BDF, offset is within 256 bytes, width is 1/2/4 bytes,
 * naturally aligned and contained. Return HW_OK/HW_ARG/HW_NOSYS.
 * Read clears *value on failure; no mechanism differs from absent BDF (all 1).
 * Both preserve CF8 and IF around precisely one width/lane bus operation. */
u32 pci_hwb_read(u32 bdf, u32 offset, u32 width, u32 *value);
/* Value must fit width. Failure sends no write. 8/16-bit writes MUST use
 * outb/outw: DWORD RMW would acknowledge adjacent Status W1C bits (pci.h).
 * No implicit read, readback, BAR probe, Command change or device allowlist. */
u32 pci_hwb_write(u32 bdf, u32 offset, u32 width, u32 value);
#endif
