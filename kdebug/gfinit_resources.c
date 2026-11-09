#include "gfinit_resources.h"
#include "kprintf.h"
#include "../drivers/pci/pci.h"
#ifdef GF_PRODUCTION
#include "../hwdev/gf/production.h"
#endif
#if !defined(GFINIT_RESOURCES_HOST) && !defined(GF_PRODUCTION)
#ifdef HWDEV_MODULE
#include "hwdev/generated/hwdev_import_macros.h"
#else
#include "generated/kdebug_import_macros.h"
#endif
#endif
/* PCI Type 1 memory windows: PCI Local Bus configuration header, matching
 * FreeBSD releng/14.3 sys/dev/pci/pcireg.h PCIR_MEMBASE_1/PCIR_MEMLIMIT_1 and
 * PCIR_PMBASEL_1/PCIR_PMLIMITL_1; TI PCI2050B SCPS076G sections
 * 4.20-4.25 (memory/prefetch base, limit and upper bits).
 * No sizing writes to other devices.
 * docs/design/kdebug/gfinit_resources.md: exact Intel apertures, conservative Trident bounds. */

/* Local layer: Intel 8255x rev.1.3 section 4.1.10 Table 3 (4 KiB CSR,
 * 1 MiB Flash aperture, independent of Flash population). BSD comparison:
 * FreeBSD sys/dev/fxp/if_fxpreg.h FXP_PCI_MMBA/IOBA. Trident is NOT an
 * exact-length table: only the approved 9660 rev d3 endpoint uses bounds. */
#define GF_RES_TRIDENT_ID 0x96601023U
#define GF_RES_TRIDENT_REV 0xd3U
#define GF_RES_BAR0 0U
#define GF_RES_BAR2 2U
#define GF_RES_FXP_CSR_LEN 0x1000U
#define GF_RES_FXP_FLASH_LEN 0x100000U
#define GF_RES_NEC_VENDOR 0x1033U
#define GF_RES_OHCI_DEVICE 0x0035U
#define GF_RES_EHCI_DEVICE 0x00e0U
#define GF_RES_OHCI_LEN 0x1000U
#define GF_RES_EHCI_LEN 0x100U

struct resource_bar_spec { u32 id, bar, len, attr; };
static const struct resource_bar_spec resource_bar_specs[] = {
    {((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL), GF_RES_BAR0, GF_RES_FXP_CSR_LEN, PCI_BAR_MEM_PREFETCH},
    {((PCI_DEVICE_82557 << 16) | PCI_VENDOR_INTEL), GF_RES_BAR2, GF_RES_FXP_FLASH_LEN, PCI_BAR_MEM_TYPE32},
    /* uPD720101: OHCI 1.0a §7 (4 KiB), EHCI 1.0 §2.3; measured 2026-11-09.
     * FreeBSD sys/dev/usb/controller/{ohci,ehci}_pci.c use BAR0. */
    {((GF_RES_OHCI_DEVICE << 16) | GF_RES_NEC_VENDOR), GF_RES_BAR0, GF_RES_OHCI_LEN, PCI_BAR_MEM_TYPE32},
    {((GF_RES_EHCI_DEVICE << 16) | GF_RES_NEC_VENDOR), GF_RES_BAR0, GF_RES_EHCI_LEN, PCI_BAR_MEM_TYPE32}
};
struct resource_bounds { u32 base[RECOVER_RANGE_MAX]; u32 count; };

#include "gfinit_resources/bounds.inc"

#include "gfinit_resources/bars.inc"

#include "gfinit_resources/windows.inc"

#include "gfinit_resources/scan.inc"
