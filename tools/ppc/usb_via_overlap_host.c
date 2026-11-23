/* Expectations: PPC_RESET_DESIGN 確定後の追記, §2 and §7.
 * Linux/PPC user-mode fixture, with real platform code and fake hardware. */
#include <kernel_boot.c>
#include "drivers/usb/ohci_regs.h"
#include "drivers/net/gem/gem.h"
#include "pci_config_contract.h"
#include "kdebug/hwb_wire.h"
#include "drivers/usb/usb_core.h"
#include "endian_dma.h"
#include "kprintf.h"
#include "tick.h"
#include "mouse_pointer_contract.h"
#include "input/hal/keymap.h"
#include "input/hal/usb_input.h"

#define VIA_FIRST_PA 0x80016000u
#define VIA_LAST_PA  0x80018000u
#define PCI_BASE     0x10000000u
#define CPU_BASE     0x80000000u
#define NEXT_PA      0x80020000u

enum scenario {
    FIRST_PAGE, LAST_PAGE, BEFORE_VIA, AFTER_VIA, VIA_UNPUBLISHED,
    CFG_WRITE_FAIL, CFG_ADDR_OVERLAP, CFG_DATA_OVERLAP, TWO_ROOTS
};
static enum scenario scenario;
static struct usb_pci_fn saved_controller;
static u32 mapped_pa[PM_USB_CONTROLLERS];
static u32 first_pa, ohci_maps, unmaps, skips, cfg_writes[2], command[2], starts, registrations;
static u32 regs[PM_USB_PAGE / sizeof(u32)];
static u32 directory[PAGE_SIZE / sizeof(u32)] __attribute__((aligned(PAGE_SIZE)));
static u32 table[PAGE_SIZE / sizeof(u32)] __attribute__((aligned(PAGE_SIZE)));
static const struct pm_kboot_info info;
struct pm_usb_root pm_usb_roots[PM_USB_ROOTS];
u32 pm_usb_root_count;

static __attribute__((noreturn)) void quit(int status)
{
    register int nr __asm__("r0") = 1;
    register int code __asm__("r3") = status;
    __asm__ volatile("sc" : "+r"(nr), "+r"(code) : : "memory", "cr0");
    __builtin_unreachable();
}

void ppc_stop(void) { quit(90); }

int paging_map_phys(u32 va, u32 pa, u32 pages, u32 flags)
{
    if (pages != 1 || flags != (PAGE_RW | PTE_PCD)) quit(91);
    if (va >= PPC_BOOT_USB_VA + 2 * PM_USB_PAGE) {
        mapped_pa[(va - PPC_BOOT_USB_VA) / PM_USB_PAGE - 2] = pa;
        if (scenario != TWO_ROOTS && pa != first_pa && pa != NEXT_PA) quit(92);
        ohci_maps++;
    }
    return 0;
}

int paging_set_not_present(u32 start, u32 end)
{
    if (start != PPC_BOOT_USB_VA + 2 * PM_USB_PAGE ||
        end != start + PM_USB_PAGE - 1) quit(93);
    unmaps++;
    return 0;
}

int __real_usbp_mmio_map(const struct usb_pci_fn *, u32, volatile void **);
int __wrap_usbp_mmio_map(const struct usb_pci_fn *f, u32 bar, volatile void **va)
{
    int rc = __real_usbp_mmio_map(f, bar, va);
    if (!rc) {
        if (scenario != TWO_ROOTS && (u32)*va != PPC_BOOT_USB_VA + 2 * PM_USB_PAGE) quit(94);
        *va = regs;
    }
    return rc;
}

u32 __real_usbp_cfg_read32(const struct usb_pci_fn *, u32);
u32 __wrap_usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg)
{
    if (f->fn || f->dev < 11 || f->dev > 12) return 0xffffffffu;
    if (f->dev == 12 && scenario != FIRST_PAGE && scenario != LAST_PAGE &&
        scenario != CFG_WRITE_FAIL) return 0xffffffffu;
    switch (reg) {
    case 0: return 0x0019106b;
    case 8: return 0x0c031000;
    case 0xc: return 0;
    case 0x10: return PCI_BASE + (f->dev == 11 ? first_pa : NEXT_PA) - CPU_BASE;
    default: quit(103);
    }
}

static u32 selected[2], lane_calls;
static int fail_deselect;
int pm_fake_lane(u32 va, u32 width, int write, u32 *value)
{
    lane_calls++;
    unsigned host = va >= 0x3000;
    va -= host * 0x2000;
    if (va == 0x1000 && width == 4) {
        if (write && !*value && fail_deselect) return 1;
        if (write) selected[host] = *value;
        else *value = 0;
        return 0;
    }
    if (va == 0x2000 && !write && width == 4) {
        *value = host ? 0x0019106b : 0x0021106b;
        return 0;
    }
    if (width != 2 || va != 0x2004) quit(104);
    u32 dev = selected[host] >> 12 ? 12 : 11;
    if (scenario == TWO_ROOTS && registrations >= 2 && host) quit(105);
    if (!write) { *value = command[dev - 11]; return 0; }
    if (*value != 2) quit(95);
    if (dev == 11 && (scenario == FIRST_PAGE || scenario == LAST_PAGE)) quit(31);
    if (!ohci_maps) quit(96);
    cfg_writes[dev - 11]++;
    if (scenario == CFG_WRITE_FAIL && dev == 11) return 1;
    command[dev - 11] = *value;
    return 0;
}

u32 pm_pci_mmio_read32(u32 va) { (void)va; quit(109); }
int pm_gmac_clock(const struct pm_gmac_boot *b, u32 va) { (void)b; (void)va; return 0; }
int pm_gmac_dma_sync(void *p, u32 n, enum dma_sync op)
{ (void)p; (void)n; (void)op; return 0; }

/* HW_OP §5 6b. Bind every consumer before the failed deselect. */
static void isolation_case(void)
{
    struct pm_pci pci = pm_usb_roots[0].pci;
    pci.target_bdf = 11u << 11;
    struct pm_gmac_boot gem = {.node=1, .root_node=2, .bdf=pci.target_bdf,
        .vendor_device=0x0021106b, .profile=PM_GMAC_PROFILE_UNINORTH};
    struct gem_io io;
    u32 value;
    pm_pci_boot(&pci, pci.target_bdf);
    pm_gmac_quiesce_setup(&gem, &pci);
    if (pm_lan_gem_io(&gem, &io)) quit(107);
    fail_deselect = 1;
    if (pm_hwb_cfg_read(pci.target_bdf >> 8, 0, 4, &value) != HW_IO_ERROR) quit(107);
    fail_deselect = 0;
    lane_calls = 0;
    if (io.master_enable(io.ctx) != GEM_EIO || pm_gmac_early_quiesce() != LAN_EIO ||
        pci_config_read32(0,11,0,0) != U32_MAX || usbp_cfg_cmd(&saved_controller,2,4) == 0 ||
        pm_hwb_cfg_write(pci.target_bdf >> 8,0,4,0) != HW_IO_ERROR || lane_calls) quit(107);
    pci.bridge = pm_bridge_lookup(3);
    if (pm_pci_read32(&pci,pci.target_bdf,0,&value) || !lane_calls) quit(108);
}

void kvprintf(u8 attr, const char *fmt, const u32 *args)
{
    (void)attr;
    if (!kstrcmp(fmt, "USB SKIP bdf=%02x:%02x.%u pa=%08x reason=via-overlap")) {
        if (args[0] || args[1] != 11 || args[2] || args[3] != first_pa ||
            ohci_maps || cfg_writes[0] || starts) quit(97);
        skips++;
    }
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

static int start(struct usb_hc *hc)
{
    if (hc->regs != regs ||
        mmio_rd32_le(regs + OHCI_INTERRUPT_DISABLE / sizeof(u32)) != OHCI_INTR_ALL)
        quit(98);
    starts++;
    return 0;
}
const struct usb_hc_ops ohci_ops = { .start = start };

int usb_hc_register(struct usb_hc *hc) { if (!registrations) saved_controller = hc->pci; registrations++; return 0; }
void usb_hc_dead(struct usb_hc *hc, u32 why) { (void)hc; (void)why; quit(99); }
void usb_service(void) { quit(100); }
int tick_register(unsigned int slot, tick_fn fn) { (void)slot; (void)fn; return 0; }
/* The USB mouse wiring (stage 3c) registers PointerOps; this host only drives the OHCI/VIA overlap. */
const KmBackend hid_jp_backend;
int km_backend_register(const KmBackend *map) { (void)map; return 0; }
int usb_input_mouse_available(void) { return 0; }
const PointerOps empty_pointer;
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 flags) { (void)flags; }
void pointer_register(const PointerOps *ops) { (void)ops; }
void mouse_pointer_init_position(void) {}
void mouse_pointer_backend_register(const MousePointerBackend *b) { (void)b; }
void mouse_poll(MouseState *s) { (void)s; }
void mouse_cursor_show(void) {}
void mouse_legacy_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{ (void)dx; (void)dy; (void)buttons; (void)x; (void)y; }
void mouse_set_bounds(i16 x0, i16 y0, i16 x1, i16 y1) { (void)x0; (void)y0; (void)x1; (void)y1; }
void mouse_cursor_set_mode(int mode) { (void)mode; }
void mouse_cursor_hide(void) { }

void entry(const u32 *stack) __attribute__((noreturn, used));
void entry(const u32 *stack)
{
    if (stack[0] != 2) quit(101);
    scenario = *(const char *)stack[2] - '0';
    const u32 test_pa[] = { VIA_FIRST_PA, VIA_LAST_PA, VIA_FIRST_PA - PM_USB_PAGE,
                            VIA_LAST_PA + PM_USB_PAGE, VIA_FIRST_PA, NEXT_PA - PM_USB_PAGE };
    pm_usb_root_count = 2;
    pm_usb_roots[0] = (struct pm_usb_root){
        .host_id = 2, .pa = 0xf2000000u, .nwin = 1,
        .win = {{2, PCI_BASE, CPU_BASE, 0x100000u}},
    };
    pm_usb_roots[1].pa = 0xf4000000u;
    if (scenario == CFG_ADDR_OVERLAP) pm_usb_roots[1].pa = 0x7f816000u;
    if (scenario == CFG_DATA_OVERLAP) pm_usb_roots[1].pa = 0x7f418000u;

    boot_info = &info;
    struct pm_via_boot via = {
        .kind = PM_VIA_CUDA, .pa = VIA_FIRST_PA + 0x420, .bytes = PM_VIA_REG_BYTES,
    };
    plan_via(&via);
    directory[PDE_INDEX(PM_VIA_VA)] = V2P(table) | PTE_PRESENT;
    install_via(V2P(directory));
    if (scenario == CFG_ADDR_OVERLAP || scenario == CFG_DATA_OVERLAP)
        quit(via_window.rc == PM_VIA_OVERLAP && !via_window.va &&
             !table[PTE_INDEX(PM_VIA_VA)] ? 0 : 32);
    if (via_window.rc != PM_VIA_NOT_READY || !table[PTE_INDEX(PM_VIA_VA)]) quit(102);
    if (scenario != VIA_UNPUBLISHED)
        via_window = (struct pm_via_window){ .va = via_alias.va };
    struct pm_via_window saved = *pm_via_window();

    pm_bridge_register(2, 0x1000, 0x2000);
    if (scenario == TWO_ROOTS) {
        pm_usb_roots[1] = pm_usb_roots[0];
        pm_usb_roots[1].host_id = 3;
        pm_usb_roots[1].win[0].cpu_base += PM_USB_PAGE;
        pm_bridge_register(3, 0x3000, 0x4000);
        first_pa = NEXT_PA;
    } else first_pa = test_pa[scenario];
    pm_usb_boot(1000);
    if (scenario == TWO_ROOTS) {
        volatile void *va;
        if (registrations != 2 || saved_controller.root != pm_usb_roots ||
            __real_usbp_cfg_read32(&saved_controller, 0) != 0x0021106b ||
            usbp_cfg_cmd(&saved_controller, 2, 4) ||
            __real_usbp_mmio_map(&saved_controller, 0, &va) ||
            (u32)va != PPC_BOOT_USB_VA + 2 * PM_USB_PAGE || ohci_maps != 2 ||
            mapped_pa[0] != first_pa || mapped_pa[1] != first_pa + PM_USB_PAGE) quit(106);
        isolation_case();
        quit(0);
    }
    int collides = scenario == FIRST_PAGE || scenario == LAST_PAGE;
    int cfg_fail = scenario == CFG_WRITE_FAIL;
    if (skips != (u32)collides || ohci_maps != 1u + cfg_fail ||
        unmaps != (u32)cfg_fail ||
        cfg_writes[0] != (collides ? 0u : cfg_fail ? 1u : 2u) ||
        cfg_writes[1] != (collides || cfg_fail ? 2u : 0u) ||
        starts != 1 || registrations != 1 ||
        pm_via_window()->rc != saved.rc || pm_via_window()->va != saved.va)
        quit(collides ? 31 : 33);
    quit(0);
}

__asm__(".global _start\n_start:\n mr 3,1\n clrrwi 1,1,4\n li 0,0\n stwu 0,-16(1)\n b entry\n");
