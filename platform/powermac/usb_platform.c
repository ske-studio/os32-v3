/* OpenHCI 1.0a; MPC7450UM ch.5 (I/G mappings, sync/eieio).
 * DMA is remapped cache-inhibited before use, like gmac_dma.c's control page.
 * UniNorth's identity 16MB DMA window is the same as gmac_tree.c/pm_dma_bus. */
#include "usb_boot.h"
#include "input_usb.h"
#include "usb_platform.h"
#include "ppc_memmap.h"
#include "mm/paging.h"
#include "mm/kmalloc.h"
#include "kernel/irq/tick.h"
#include "kernel/time/tick_clock.h"
#include "kprintf.h"
#include "kboot_log.h"
#include <stdarg.h>

struct pm_pci pm_usb_pci;
const struct pm_usb_root *pm_usb_root;
static u8 pool[PM_USB_DMA_BYTES] __attribute__((aligned(PM_USB_DMA_BYTES)));
static u8 used[PM_USB_DMA_BYTES / PM_USB_PAGE];
static u8 sources[2];
static u32 tb_frequency;
static struct { u32 bdf, va; int used; } maps[PM_USB_CONTROLLERS];
void ppc_dcbf_line(void *);
void pm_usb_platform_init(u32 hz) { tb_frequency = hz; }
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
    u32 value = ~0u;
    struct pm_pci p = pm_usb_pci; p.target_bdf = bdf(f);
    (void)pm_pci_read32(&p, p.target_bdf, reg, &value);
    return value;
}
static int cfg_write(const struct usb_pci_fn *f, u32 reg, u32 value)
{
    struct pm_pci p = pm_usb_pci; p.target_bdf = bdf(f);
    return pm_pci_write32(&p, p.target_bdf, reg, value);
}
int usbp_cfg_write8(const struct usb_pci_fn *f, u32 reg, u8 value)
{
    u32 shift = (reg & 3) * 8, old = usbp_cfg_read32(f, reg & ~3u);
    return cfg_write(f, reg & ~3u, (old & ~(255u << shift)) | ((u32)value << shift));
}
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{
    u32 want = ((usbp_cfg_read32(f, 4) & 0xffffu) | set) & ~(u32)clear;
    if (cfg_write(f, 4, want)) return -1;
    return (usbp_cfg_read32(f, 4) & 0xffffu) == want ? 0 : -1;
}
int usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va)
{
    if (!pm_usb_root || bar || !va || !f->bar_size[0]) return -1;
    u32 bus = f->bar_base[0];
    for (u32 i = 0; i < pm_usb_root->nwin; i++) {
        const struct pm_pci_window *w = &pm_usb_root->win[i];
        if (w->ss != 2 || bus < w->pci_base || bus - w->pci_base >= w->bytes ||
            PM_USB_PAGE > w->bytes - (bus - w->pci_base)) continue;
        u32 pa = w->cpu_base + bus - w->pci_base;
        u32 slot;
        for (slot = 0; slot < PM_USB_CONTROLLERS; slot++)
            if (maps[slot].used && maps[slot].bdf == bdf(f)) { *va = (volatile void *)maps[slot].va; return 0; }
        for (slot = 0; slot < PM_USB_CONTROLLERS && maps[slot].used; slot++) {}
        if (slot == PM_USB_CONTROLLERS) return -1;
        u32 address = PPC_BOOT_USB_VA + (2 + slot) * PM_USB_PAGE;
        if (pa & (PM_USB_PAGE - 1)) return -1;
        if (paging_map_phys(address, pa, 1, PAGE_RW | PTE_PCD)) return -1;
        maps[slot].used = 1; maps[slot].bdf = bdf(f); maps[slot].va = address;
        *va = (volatile void *)address;
        return 0;
    }
    return -1;
}
void usbp_mmio_unmap(const struct usb_pci_fn *f, u32 bar)
{
    if (bar) return;
    for (u32 i = 0; i < PM_USB_CONTROLLERS; i++)
        if (maps[i].used && maps[i].bdf == bdf(f)) {
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
int usbp_input_src_alloc(int kind)
{
    if ((unsigned)kind > 1) return -1;
    for (unsigned i = 0; i < 4; i++)
        if (!(sources[kind] & (1u << i))) { sources[kind] |= 1u << i; return i + 1; }
    return -1;
}
void usbp_input_key(u8 src, u8 sc, int down)
{
    pm_input_usb_key(src, sc, down);
    usbp_log("USB KEY src=%u sc=%02x down=%u", src, sc, down); }
void usbp_input_key_repeat(u8 src, u8 sc)
{
    pm_input_usb_repeat(src, sc);
    usbp_log("USB REPEAT src=%u sc=%02x", src, sc); }
void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 buttons)
{ usbp_log("USB MOUSE src=%u dx=%d dy=%d b=%x", src, dx, dy, buttons); }
void usbp_input_release(int kind, u8 src)
{
    if (kind == 0) pm_input_usb_release(src);
    usbp_log("USB RELEASE kind=%d src=%u", kind, src);
    if ((unsigned)kind < 2 && src >= 1 && src <= 4) sources[kind] &= ~(1u << (src - 1));
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
    int saved = pm_log_stream_only;
    pm_log_stream_only = 1;
    kvprintf(7, fmt, words);
    kprintf(7, "\n");
    pm_log_stream_only = saved;
}
