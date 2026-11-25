/* OpenHCI 1.0a; MPC7450UM ch.5 (I/G mappings, sync/eieio).
 * DMA is remapped cache-inhibited before use, like gmac_dma.c's control page.
 * UniNorth's identity 16MB DMA window is the same as gmac_tree.c/pm_dma_bus. */
#include "usb_boot.h"
#include "rh_observe.h"
#include "via_window.h"
#include "sys/hal/input/usb_input.h"
#include "input_usb.h"
#include "sys/hal/input/in2_int.h"
#include "mouse_pointer_contract.h"
#include "usb_platform.h"
#include "ppc_memmap.h"
#include "sys/kern/mm/paging.h"
#include "sys/kern/mm/kmalloc.h"
#include "sys/kern/irq/tick.h"
#include "sys/hal/time/tick_clock.h"
#include "kprintf.h"
#include <stdarg.h>

static u8 pool[PM_USB_DMA_BYTES] __attribute__((aligned(PM_USB_DMA_BYTES)));
static u8 used[PM_USB_DMA_BYTES / PM_USB_PAGE];

static u32 tb_frequency;
static struct {
    const struct pm_usb_root *root;
    u32 bdf, va, pa;
    int used;
} maps[PM_USB_CONTROLLERS];
void ppc_dcbf_line(void *);
static PointerOps usb_pointer;

static u8 usb_mouse_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    u32 flags = ppc_irq_save();
    int present = usb_input_mouse_available();
    if (present) mouse_legacy_snapshot(dx, dy, buttons, x, y);
    ppc_irq_restore(flags);
    return present ? MOUSE_MODE_BUS : MOUSE_MODE_NONE;
}

static void usb_mouse_text_cell(i16 x, i16 y, int *cx, int *cy)
{
    *cx = x < 0 ? -1 : x / 8;
    *cy = y < (int)PM_RH_FB_ROWS ? -1 : (y - (int)PM_RH_FB_ROWS) / 16;
}

static const MousePointerBackend usb_mouse_backend = {
    .snapshot = usb_mouse_snapshot,
    .text_cell = usb_mouse_text_cell,
};

void pm_usb_platform_init(u32 hz)
{
    tb_frequency = hz;
    usb_pointer = empty_pointer;
    mouse_pointer_backend_register(&usb_mouse_backend);
    mouse_pointer_init_position();
    usb_pointer.poll = mouse_poll;
    usb_pointer.available = usb_input_mouse_available;
    usb_pointer.cursor_show = mouse_cursor_show;
    usb_pointer.bounds = mouse_set_bounds;
    usb_pointer.cursor_mode = mouse_cursor_set_mode;
    usb_pointer.cursor_hide = mouse_cursor_hide;
    pointer_register(&usb_pointer);
}
u32 usbp_irq_save(void) { return ppc_irq_save(); }
void usbp_irq_restore(u32 f) { ppc_irq_restore(f); }
int usbp_dma_alloc(u32 bytes, u32 align, struct usb_dma *out)
{
    if (!out || !bytes || bytes > PM_USB_DMA_BYTES || !align ||
        (align & (align - 1)) || align > PM_USB_DMA_BYTES) return -1;
    u32 pages = (bytes + PM_USB_PAGE - 1) / PM_USB_PAGE;
    for (u32 i = 0; i + pages <= sizeof(used); i++) {
        u32 pa = pm_v2p_ram(pool + i * PM_USB_PAGE), j;
        if (pa & (align - 1)) continue;
        for (j = 0; j < pages && !used[i+j]; j++) {}
        if (j != pages) continue;
        if (pa >= PM_USB_DMA_WINDOW || pages * PM_USB_PAGE > PM_USB_DMA_WINDOW - pa) return -1;
        for (u32 off = 0; off < pages * PM_USB_PAGE; off += PPC_CACHE_LINE)
            ppc_dcbf_line(pool + i * PM_USB_PAGE + off);
        __asm__ volatile("sync" ::: "memory");
        if (paging_map_phys(pa, pa, pages, PAGE_RW | PTE_PCD)) return -1;
        for (j = 0; j < pages; j++) used[i+j] = 1;
        *out = (struct usb_dma){pa, pool + i * PM_USB_PAGE, pages * PM_USB_PAGE, i + 1};
        return 0;
    }
    return -1;
}
int usbp_dma_free(struct usb_dma *d)
{
    if (!d || !d->cookie || d->cookie > sizeof(used)) return -1;
    u32 i = d->cookie - 1, pages = d->size / PM_USB_PAGE;
    if (!pages || pages > sizeof(used) - i || used[i] != 1) return -1;
    for (u32 j = 0; j < pages; j++) used[i+j] = 0;
    d->va = 0; d->cookie = 0;
    return 0;
}
int usbp_dma_quarantine(struct usb_dma *d)
{
    if (!d || !d->cookie || d->cookie > sizeof(used)) return -1;
    u32 i = d->cookie - 1, pages = d->size / PM_USB_PAGE;
    if (!pages || pages > sizeof(used) - i) return -1;
    for (u32 j = 0; j < pages; j++) used[i+j] = 2;
    return 0;
}
void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir)
{
    (void)d; (void)off; (void)len; (void)dir;
    __asm__ volatile("sync; eieio" ::: "memory");
}
void *usbp_mem_alloc(u32 bytes) { return kmalloc(bytes); }
void usbp_mem_free(void *p) { kfree(p); }
static u32 bdf(const struct usb_pci_fn *f)
{ return ((u32)f->bus << 16) | ((u32)f->dev << 11) | ((u32)f->fn << 8); }
u32 usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg)
{
    u32 value;
    if (!f->root || pm_pci_access(&f->root->pci, bdf(f), reg, 4, 0, &value))
        return U32_MAX;
    return value;
}
int usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 value)
{
    u32 word = value;
    return !f->root || pm_pci_access(&f->root->pci, bdf(f), reg, 1, 1, &word) ? -1 : 0;
}
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    u32 command, actual;
    if (!f->root || pm_pci_access(&f->root->pci, bdf(f), 4, 2, 0, &command)) return -1;
    command = (command | set) & ~clear;
    if (pm_pci_access(&f->root->pci, bdf(f), 4, 2, 1, &command) ||
        pm_pci_access(&f->root->pci, bdf(f), 4, 2, 0, &actual)) return -1;
    return ((actual ^ command) & (set | clear)) ? -1 : 0;
}
int usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va)
{
    if (!f->root || bar || !va || !f->bar_size[0]) return -1;
    u32 bus = f->bar_base[0];
    for (u32 i = 0; i < f->root->nwin; i++) {
        const struct pm_pci_window *w = &f->root->win[i];
        if (w->ss != 2 || bus < w->pci_base || bus - w->pci_base >= w->bytes ||
            PM_USB_PAGE > w->bytes - (bus - w->pci_base)) continue;
        u32 pa = w->cpu_base + bus - w->pci_base;
        if (pa & (PM_USB_PAGE - 1)) return -1;
        if (pm_via_phys_overlap(pa, PM_USB_PAGE)) {
            usbp_log("USB SKIP bdf=%02x:%02x.%u pa=%08x reason=via-overlap",
                     f->bus, f->dev, f->fn, pa);
            return -1;
        }
        u32 slot;
        for (slot = 0; slot < PM_USB_CONTROLLERS; slot++)
            if (maps[slot].used && maps[slot].bdf == bdf(f) &&
                maps[slot].root == f->root && maps[slot].pa == pa) {
                *va = (volatile void *)maps[slot].va;
                return 0;
            }
        for (slot = 0; slot < PM_USB_CONTROLLERS && maps[slot].used; slot++) {}
        if (slot == PM_USB_CONTROLLERS) return -1;
        u32 address = PPC_BOOT_USB_VA + (2 + slot) * PM_USB_PAGE;
        if (paging_map_phys(address, pa, 1, PAGE_RW | PTE_PCD)) return -1;
        maps[slot].root = f->root;
        maps[slot].pa = pa;
        maps[slot].bdf = bdf(f);
        maps[slot].va = address;
        maps[slot].used = 1;
        *va = (volatile void *)address;
        usbp_log("USB OHCI BAR0 bdf=%02x:%02x.%u pa=%08x",
                 f->bus, f->dev, f->fn, pa);
        return 0;
    }
    return -1;
}
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar)
{
    if (bar) return;
    for (u32 i = 0; i < PM_USB_CONTROLLERS; i++)
        if (maps[i].used && maps[i].root == f->root && maps[i].bdf == bdf(f)) {
            (void)paging_set_not_present(maps[i].va, maps[i].va + PM_USB_PAGE - 1);
            maps[i].used = 0;
        }
}
int usbp_irq_attach(const struct usb_pci_fn *f, int (*isr)(void *), void *arg)
{ (void)f; (void)isr; (void)arg; return -1; }
void usbp_irq_detach(const struct usb_pci_fn *f) { (void)f; }
int usbp_tick_attach(void (*fn)(void)) { return tick_register(PM_TICK_USB, fn); }
static u64 tb(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((u64)hi << 32) | lo;
}
/* lib/divdi3.c supplies the integer division, as in hostfs_io.c. */
u32 usbp_ms(void) { return (u32)(tb() / (tb_frequency / 1000u)); }
void usbp_udelay(u32 us)
{
    if (us > 100000) us = 100000;
    u64 start = tb(), ticks = ((u64)tb_frequency * us + 999999) / 1000000;
    while (tb() - start < ticks) {}
}
void usbp_log(const char *fmt, ...)
{
    u32 words[16], n = 0;
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p && n < 16; p++) {
        if (*p != '%') continue;
        int width, left; char pad;
        p++;
        char spec = kprintf_spec(&p, &width, &left, &pad);
        if (spec == '%') continue;
        words[n++] = spec == 's' ? (u32)va_arg(ap, const char *) : va_arg(ap, u32);
        if (!*p) break;
    }
    va_end(ap);
    kvprintf(7, fmt, words);
    kprintf(7, "\n");
}
