/* EHCI 1.0 §2.3: a deliberately small independent register model.
 * Reset while running is an error even when hardware would tolerate it. */
#include "drivers/usb/ohci_priv.h"
static unsigned int regs[64];
static u32 bad, reset_running, stuck_halt, stuck_reset, sticky_cf, bm_cleared;
static u32 stage, writes;
static u32 get(u32 off) { return usb_fake_native_rd((u8 *)regs + off); }
static void put(u32 off, u32 v) { usb_fake_native_wr((u8 *)regs + off, v); }
u32 mmio_rd32_le(const volatile void *p) { return usb_fake_native_rd(p); }
void mmio_wr32_le(volatile void *p, u32 v)
{
    u32 off = (u32)((volatile u8 *)p - (volatile u8 *)regs);
    ++writes;
    if (off == 0x20u) {
        if (v & 2u) {
            if (!(get(0x24u) & 0x1000u)) ++reset_running;
            if (stage != 1) ++bad;
            stage = 2;
        } else {
            if (get(0x28u)) ++bad;
            stage = 1;
        }
    }
    if (off == 0x60u) {
        if (stage != 2 || (get(0x20u) & 2u)) ++bad;
        stage = 3;
        if (sticky_cf) return;
    }
    if (off == 0x24u) put(off, get(off) & ~v);
    else put(off, v);
}
void usbp_udelay(u32 us)
{
    (void)us;
    if (!(get(0x20u) & 1u) && !stuck_halt) put(0x24u, get(0x24u) | 0x1000u);
    if ((get(0x20u) & 2u) && !stuck_reset) put(0x20u, 0);
    /* CF intentionally does NOT reset automatically, to require the explicit write. */
}
int usbp_cfg_cmd(const struct usb_pci_fn *f, u16 set, u16 clear)
{ (void)f; (void)set; if (clear & 4u) ++bm_cleared; return 0; }
static int test(void)
{
    struct usb_hc hc = { .regs = regs };
    u32 why, mode, i;
    for (mode = 0; mode < 4; ++mode) {
        for (i = 0; i < 64; ++i) regs[i] = 0;
        bad = reset_running = bm_cleared = stage = writes = 0;
        stuck_halt = mode == 1; stuck_reset = mode == 2; sticky_cf = mode == 3;
        put(0, 0x20u); put(0x20u, 1u); put(0x28u, 0x3fu); put(0x60u, 1u);
        if (!mode) {
            if (ehci_quiesce(&hc, &why) || get(0x60u) || why || stage != 3) return 1;
        } else if (ehci_quiesce(&hc, &why) != -USB_E_HC || why != 1u || hc.state != HC_DEAD)
            return 2;
        if (bad || reset_running || get(0x28u) || !bm_cleared) return 3;
    }
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
