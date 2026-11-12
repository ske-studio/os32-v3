/* OF PCI Binding 2.1 ranges; NetBSD macppc/pci/uninorth.c:191-271.
 * Capture before OF detach. No dependency on a GMAC node or its DMA init. */
#include "usb_boot.h"
#include "ofw.h"
#include "kstring.h"
struct pm_usb_root pm_usb_roots[PM_USB_ROOTS];
u32 pm_usb_root_count;
static u8 prop[512];
static u32 word(const u8 *p)
{ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static int get(int node, const char *name)
{
    cell a[3] = {(cell)node, ppc_ptr_cell(name), 0};
    if (ofw_call("getproplen", 2, 1, a) || (int)a[2] < 0 || a[2] > sizeof(prop)) return -1;
    return ofw_getprop(node, name, prop, sizeof(prop)) == (int)a[2] ? (int)a[2] : -1;
}
static int uninorth(int node)
{
    int n = get(node, "compatible");
    for (int i = 0; i < n;) {
        int end = i;
        while (end < n && prop[end]) end++;
        if (end == n) return 0;
        if (end - i == 9 && !kstrncmp((char *)prop + i, "uni-north", 9)) return 1;
        i = end + 1;
    }
    return 0;
}
void pm_usb_capture(void)
{
    int root = ofw_finddevice("/");
    u32 ac = 2;
    if (get(root, "#address-cells") == 4) ac = word(prop);
    if (ac < 1 || ac > 2) return;
    for (int node = ofw_child(root); node && node != -1; node = ofw_peer(node)) {
        struct pm_usb_root r = {0};
        int n = get(node, "device_type");
        if (n < 3 || kstrncmp((char *)prop, "pci", 3) || !uninorth(node)) continue;
        /* UniNorth host bridges have a 16MB register aperture. */
        n = get(node, "reg");
        if (n < (int)((ac + 1) * 4) || (ac == 2 && word(prop))) continue;
        r.pa = word(prop + (ac - 1) * 4);
        if (r.pa > 0xff000000u || word(prop + ac * 4) < PM_USB_DMA_WINDOW) continue;
        if (get(node, "bus-range") != 8) continue;
        r.first = word(prop); r.last = word(prop + 4);
        if (r.first > r.last || r.last > 255) continue;
        n = get(node, "ranges");
        u32 stride = (3 + ac + 2) * 4;
        if (n <= 0 || (u32)n % stride || (u32)n / stride > 8) continue;
        r.nwin = (u32)n / stride;
        for (u32 i = 0; i < r.nwin; i++) {
            const u8 *p = prop + i * stride;
            struct pm_pci_window *w = &r.win[i];
            w->ss = (word(p) >> 24) & 3;
            w->pci_base = word(p + 8); w->cpu_base = word(p + (ac + 2) * 4);
            w->bytes = word(p + (ac + 4) * 4);
            if (word(p + 4) || (ac == 2 && word(p + 12)) || word(p + (ac + 3) * 4) ||
                w->bytes > ~0u - w->pci_base || w->bytes > ~0u - w->cpu_base) {
                r.nwin = 0; break;
            }
        }
        if (r.nwin && pm_usb_root_count < PM_USB_ROOTS)
            pm_usb_roots[pm_usb_root_count++] = r;
    }
}
