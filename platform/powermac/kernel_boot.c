#include "ide_boot.h"
/* OF-PPC 4.2.4/8.2; OpenBSD macppc/locore0.S:87; FreeBSD
 * aim/mmu_oea.c:685-695. Plan D uses software tables, never RAM BATs.
 * Static MMIO slots belong to the platform, outside the device ledger. */
#include "../../arch/ppc/kernel_entry.h"
#include "../../arch/ppc/kernel_boot.h"
#include "kernel_start.h"
#include "../../arch/ppc/ppc_trap.h"
#include "../../arch/ppc/ppc_hpt.h"
#include "../../arch/ppc/ppc_oea.h"
#include "../../arch/ppc/arch_cpu.h"
#include "kboot_info.h"
#include "platform_kboot_layout.h"
#include "memory.h"
#include "mpic_tree.h"
#include "memory_boot.h"
#include "paging.h"
#include "boot_cpu.h"
#include "platform_memory.h"
#include "rh_observe.h"
#include "kstring.h"
#include "via_window.h"
#if KBOOT_SHELL || KBOOT_SH
#include "platform_ver.h"
#endif
#if KBOOT_SH
#include "usb_boot.h"
_Static_assert(PM_VIA_VA + PM_VIA_BYTES <= PPC_BOOT_USB_VA ||
    PPC_BOOT_USB_VA + (2 + PM_USB_CONTROLLERS) * PM_USB_PAGE <= PM_VIA_VA,
    "VIA and USB VA slots must be disjoint");
#endif

#define PPC_BOOT_MEM_KB 16384U
#define PPC_BOOT_RAM_ATTR 0x10U
#define PPC_BOOT_IO_ATTR 0x28U
static const struct pm_kboot_info *boot_info;
static struct ppc_boot_alias escc_alias, mpic_alias, gmac_alias[3];
static struct ppc_boot_alias fb_alias, clock_alias;
#if OS32_FEATURE_IDE
static struct ppc_boot_alias ide_alias[PM_IDE_BANKS];
#endif
static struct ppc_boot_alias via_alias;
static struct pm_via_window via_window = { .rc = PM_VIA_NOT_READY };
const struct pm_via_window *pm_via_window(void) { return &via_window; }
static unsigned boot_root_ready;
static struct ppc_boot_devices boot_devices;
/* Valid after arch_boot_after_paging(): mapped I/G aliases and the snapshot. */
const struct ppc_boot_devices *ppc_boot_devices(void) { return &boot_devices; }
const struct pm_kboot_info *ppc_boot_info(void) { return boot_info; }
void ppc_kernel_exception_enable(void);
void ppc_boot_walk_stop(void) __attribute__((noreturn));

int ppc_boot_alias_plan(u32 pa, u32 bytes, u32 va,
                        struct ppc_boot_alias *out)
{
    static const struct { u32 va, limit; } slots[] = {
        { PPC_BOOT_FB_VA, PPC_BOOT_FB_LIMIT },
        { PPC_BOOT_GMAC_CLOCK_VA, PAGE_SIZE },
        { PPC_BOOT_ESCC_VA, PPC_BOOT_ALIAS_BYTES },
        { PPC_BOOT_MPIC_VA, PPC_BOOT_ALIAS_BYTES },
        { PPC_BOOT_GMAC_VA, PPC_BOOT_GMAC_BYTES },
        { PPC_BOOT_PCI_ADDR_VA, PAGE_SIZE },
        { PPC_BOOT_PCI_DATA_VA, PAGE_SIZE },
        { PM_VIA_VA, PM_VIA_BYTES },
#if OS32_FEATURE_IDE
        { PM_IDE_VA, PAGE_SIZE },
        { PM_IDE_VA + PAGE_SIZE, PAGE_SIZE },
#endif
    };
    u32 offset = pa & (PAGE_SIZE - 1), limit = 0;
    for (u32 i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        if (slots[i].va == va) limit = slots[i].limit;
        for (u32 j = 0; j < i; j++)
            if (slots[i].va < slots[j].va + slots[j].limit &&
                slots[j].va < slots[i].va + slots[i].limit) return 0;
    }
    if (!out || !limit || !bytes || bytes - 1 > ~pa ||
        offset > limit || bytes > limit - offset) return 0;
    out->pa = pa & PTE_FRAME_MASK;
    out->bytes = (offset + bytes + PAGE_SIZE - 1) & PTE_FRAME_MASK;
    out->va = va + offset;
    return out->bytes <= limit && out->pa <= ~0U - out->bytes;

}

static u32 mapped_word(u32 pa, void *context)
{
    (void)context;
    return *(const u32 *)P2V(pa);
}
static u32 *leaf(u32 root, u32 va)
{
    u32 pde = ((u32 *)P2V_BOOT(root))[PDE_INDEX(va)];
    if (!(pde & PTE_PRESENT) || (pde & PTE_PS)) ppc_stop();
    return &((u32 *)P2V_BOOT(pde & PTE_FRAME_MASK))[PTE_INDEX(va)];
}
static void install_alias(u32 root, const struct ppc_boot_alias *a)
{
    u32 first = a->va & PTE_FRAME_MASK;
    for (u32 off = 0; off < a->bytes; off += PAGE_SIZE) {
        u32 *pte = leaf(root, first + off);
        if (*pte & PTE_PRESENT) ppc_stop();
        *pte = (a->pa + off) | PAGE_RW | PTE_PCD;
    }
}
static int range_valid(u32 root, u32 first, u32 end, u32 pa, u32 attr)
{
    struct ppc_translation t;
    for (u32 va = first; va < end; va += PAGE_SIZE, pa += PAGE_SIZE) {
        if (!ppc_pd_lookup(root, va, 1, 0, &t) ||
            t.pa != pa || t.attr != attr) return 0;
        /* Supervisor entries must not silently become user mappings. */
        if ((*leaf(root, va) & (PTE_USER | PTE_RW)) != PTE_RW) return 0;
    }
    return 1;
}
static int absent(u32 root, u32 first, u32 end)
{
    struct ppc_translation t;
    for (u32 a = first; a < end; a += PAGE_SIZE)
        if (ppc_pd_lookup(root, a, 0, 0, &t)) return 0;
    return 1;
}
static int overlap(u32 a, u32 n, u32 b, u32 m)
{
    return n && m && (a <= b ? b - a < n : a - b < m);
}
int pm_via_phys_overlap(u32 pa, u32 bytes)
{
    return via_window.rc == 0 &&
           overlap(pa, bytes, via_alias.pa, via_alias.bytes);
}
static int gmac_aliases_safe(u32 root)
{
    for (u32 i = 0; i < 3; i++) {
        const struct ppc_boot_alias *a = &gmac_alias[i];
        u32 va = a->va & PTE_FRAME_MASK;
        if (overlap(a->pa, a->bytes, 0, boot_info->admitted_end) ||
            overlap(a->pa, a->bytes, boot_info->image_first, boot_info->image_end - boot_info->image_first) ||
            overlap(a->pa, a->bytes, boot_info->hpt_pa, boot_info->hpt_bytes) ||
            overlap(a->pa, a->bytes, escc_alias.pa, escc_alias.bytes) ||
            overlap(a->pa, a->bytes, mpic_alias.pa, mpic_alias.bytes) ||
            !absent(root, va, va + a->bytes)) return 0;
        for (u32 j = 0; j < boot_info->ram.count; j++)
            if (overlap(a->pa, a->bytes, boot_info->ram.v[j].start,
                        boot_info->ram.v[j].size)) return 0;
        for (u32 j = 0; j < i; j++)
            if (overlap(a->pa, a->bytes, gmac_alias[j].pa, gmac_alias[j].bytes) ||
                overlap(va, a->bytes, gmac_alias[j].va & PTE_FRAME_MASK, gmac_alias[j].bytes)) return 0;
        /* Reject any existing identity mapping, including WB mappings. */
        if (!absent(root, a->pa, a->pa + a->bytes)) return 0;
    }
    return 1;
}
static int gmac_walk_valid(u32 root)
{
    for (u32 i = 0; i < 3; i++) {
        u32 va = gmac_alias[i].va & PTE_FRAME_MASK;
        if (!range_valid(root, va, va + gmac_alias[i].bytes,
                         gmac_alias[i].pa, PPC_BOOT_IO_ATTR)) return 0;
    }
    return 1;
}
static int observe_alias_safe(u32 root, const struct ppc_boot_alias *a)
{
    if (!a->bytes) return 1;
    u32 va = a->va & PTE_FRAME_MASK;
    if (!absent(root, va, va + a->bytes) ||
        !absent(root, a->pa, a->pa + a->bytes) ||
        overlap(a->pa, a->bytes, escc_alias.pa, escc_alias.bytes) ||
        overlap(a->pa, a->bytes, mpic_alias.pa, mpic_alias.bytes) ||
        overlap(a->pa, a->bytes, boot_info->hpt_pa, boot_info->hpt_bytes) ||
        overlap(a->pa, a->bytes, boot_info->image_first,
                boot_info->image_end - boot_info->image_first)) return 0;
    for (u32 i = 0; i < boot_info->ram.count; i++)
        if (overlap(a->pa, a->bytes, boot_info->ram.v[i].start,
                    boot_info->ram.v[i].size)) return 0;
    for (u32 i = 0; i < 3; i++)
        if (overlap(a->pa, a->bytes, gmac_alias[i].pa, gmac_alias[i].bytes)) return 0;
    return 1;
}
static int observe_walk_valid(u32 root, const struct ppc_boot_alias *a)
{
    u32 va = a->va & PTE_FRAME_MASK;
    return !a->bytes || range_valid(root, va, va + a->bytes, a->pa, PPC_BOOT_IO_ATTR);
}
static void plan_via(const struct pm_via_boot *via)
{
    via_window = (struct pm_via_window){ .rc = via->capture_rc };
    if (via_window.rc) return;
    via_window.rc = PM_VIA_NOT_READY;
    if ((via->kind != PM_VIA_PMU && via->kind != PM_VIA_CUDA) ||
        via->bytes < PM_VIA_REG_BYTES ||
        via->bytes > ~via->pa ||
        !ppc_boot_alias_plan(via->pa, PM_VIA_REG_BYTES, PM_VIA_VA, &via_alias))
        via_window.rc = PM_VIA_BAD_PLAN;
}

static void install_via(u32 root)
{
    if (via_window.rc != PM_VIA_NOT_READY || !via_alias.bytes) return;
    if (!observe_alias_safe(root, &via_alias) ||
        overlap(via_alias.pa, via_alias.bytes, fb_alias.pa, fb_alias.bytes) ||
        overlap(via_alias.pa, via_alias.bytes, clock_alias.pa, clock_alias.bytes)) {
        via_window.rc = PM_VIA_OVERLAP;
        return;
    }
#if KBOOT_SH
    for (u32 i = 0; i < pm_usb_root_count; i++) {
        u32 cfg_page = pm_usb_roots[i].pa & PTE_FRAME_MASK;
        if (overlap(via_alias.pa, via_alias.bytes,
                    cfg_page + PM_USB_CFG_ADDR_OFFSET, PM_USB_PAGE) ||
            overlap(via_alias.pa, via_alias.bytes,
                    cfg_page + PM_USB_CFG_DATA_OFFSET, PM_USB_PAGE)) {
            via_window.rc = PM_VIA_OVERLAP;
            return;
        }
    }
#endif
    install_alias(root, &via_alias);
    if (!observe_walk_valid(root, &via_alias)) {
        u32 va = via_alias.va & PTE_FRAME_MASK;
        for (u32 off = 0; off < via_alias.bytes; off += PAGE_SIZE)
            *leaf(root, va + off) = 0;
        via_window.rc = PM_VIA_BAD_WALK;
    }
}

static int boot_walk_valid(u32 root)
{
    u32 image = boot_info->image_first;
    u32 end = (boot_info->image_end + PAGE_SIZE - 1) & PTE_FRAME_MASK;
    return observe_walk_valid(root, &fb_alias) &&
        observe_walk_valid(root, &clock_alias) &&
        (!boot_devices.gmac_ready || gmac_walk_valid(root)) &&
        range_valid(root, image, end, image, PPC_BOOT_RAM_ATTR) &&
        range_valid(root, MEM_FIXED_PAGING_BASE, MEM_FIXED_PAGING_FRAME_END,
                    MEM_FIXED_PAGING_BASE, PPC_BOOT_RAM_ATTR) &&
        range_valid(root, escc_alias.va & PTE_FRAME_MASK,
                    (escc_alias.va & PTE_FRAME_MASK) + escc_alias.bytes,
                    escc_alias.pa, PPC_BOOT_IO_ATTR) &&
        range_valid(root, mpic_alias.va & PTE_FRAME_MASK,
                    (mpic_alias.va & PTE_FRAME_MASK) + mpic_alias.bytes,
                    mpic_alias.pa, PPC_BOOT_IO_ATTR) &&
        absent(root, 0, MEM_1MB) &&
        absent(root, boot_info->hpt_pa, boot_info->hpt_pa + boot_info->hpt_bytes) &&
        absent(root, MEM_STACK_GUARD, MEM_KSTACK_BASE) &&
        absent(root, MEM_FIXED_PAGING_GUARD_BASE, MEM_FIXED_PAGING_BASE) &&
        absent(root, platform_memory_geometry.system_first,
                    platform_memory_geometry.system_end);
}
void ppc_boot_load_root(u32 root)
{
    if (boot_info && !boot_root_ready) {
        if (ppc_root_current() || root != MEM_FIXED_PD_BASE) ppc_stop();
        install_alias(root, &escc_alias);
        install_alias(root, &mpic_alias);
#if KBOOT_SHELL || KBOOT_SH
        if (boot_devices.fb_full_rc == PM_RH_FULL_OK &&
            (!observe_alias_safe(root, &fb_alias) ||
             overlap(fb_alias.pa, fb_alias.bytes, clock_alias.pa, clock_alias.bytes))) {
            const struct pm_rh_fb *band = pm_rh_framebuffer();
            pm_rh_discard_full();
            boot_devices.fb_full_rc = PM_RH_FULL_ALIAS;
            if (!ppc_boot_alias_plan(band->pa, band->bytes, PPC_BOOT_FB_VA, &fb_alias))
                ppc_boot_walk_stop();
            boot_devices.fb_va = fb_alias.va;
        }
#endif
        if (!observe_alias_safe(root, &fb_alias) ||
            !observe_alias_safe(root, &clock_alias) ||
            overlap(fb_alias.pa, fb_alias.bytes, clock_alias.pa, clock_alias.bytes))
            ppc_boot_walk_stop();
        install_alias(root, &fb_alias);
        install_alias(root, &clock_alias);
        if (boot_devices.gmac_ready && !gmac_aliases_safe(root))
            boot_devices.gmac_ready = 0;
        if (boot_devices.gmac_ready)
            for (u32 i = 0; i < 3; i++) install_alias(root, &gmac_alias[i]);
        for (u32 a = 0; a < MEM_1MB; a += PAGE_SIZE) *leaf(root, a) = 0;
        for (u32 a = boot_info->hpt_pa;
             a < boot_info->hpt_pa + boot_info->hpt_bytes; a += PAGE_SIZE)
            *leaf(root, a) = 0;
        install_via(root);
#if OS32_FEATURE_IDE
        for (unsigned i = 0; i < PM_IDE_BANKS; i++) {
            struct ppc_boot_alias *a = &ide_alias[i];
            if (!a->bytes || !observe_alias_safe(root, a) ||
                overlap(a->pa, a->bytes, via_alias.pa, via_alias.bytes) ||
                overlap(a->pa, a->bytes, fb_alias.pa, fb_alias.bytes) ||
                overlap(a->pa, a->bytes, clock_alias.pa, clock_alias.bytes) ||
                (i && overlap(a->pa, a->bytes, ide_alias[0].pa, ide_alias[0].bytes))) continue;
            install_alias(root, a);
            if (observe_walk_valid(root, a)) pm_ide_base[i] = a->va;
            else *leaf(root, a->va & PTE_FRAME_MASK) = 0;
        }
#endif
        if (!boot_walk_valid(root)) ppc_boot_walk_stop();
        boot_root_ready = 1;
    }
    ppc_root_load(root);
    if (root == MEM_FIXED_PD_BASE && boot_root_ready &&
        via_window.rc == PM_VIA_NOT_READY && via_alias.bytes)
        via_window = (struct pm_via_window){ .va = via_alias.va };
}
void arch_boot_after_paging(void)
{
    if (!boot_root_ready || !paging_enabled() ||
        ppc_root_current() != MEM_FIXED_PD_BASE) ppc_stop();
    ppc_trap_set_reader(mapped_word, 0);
    ppc_trap_set_fatal_ready(1);
}
void ppc_kernel_real_start(const struct pm_kboot_info *info)
{
    u32 pvr;
    boot_info = info;
    if (!ppc_cpu_identify(&pvr)) ppc_stop();
#if KBOOT_SHELL || KBOOT_SH
    pm_ver_note_pvr(pvr);
#endif
    ppc_cpu_disable_bats(pvr);
    ppc_cpu_power_init(pvr);
    /* IP was cleared by takeover; copy/synchronize the D1 low-vector image. */
    ppc_cache_copy((void *)0, ppc_kernel_vectors.src, ppc_kernel_vectors.size);
    ppc_cpu_load_segments();
    /* D1 stacks are resident BSS symbols, selected by the vector prologue. */
    if (!ppc_hpt_init(info->hpt_pa, info->hpt_bytes)) ppc_stop();
    ppc_cpu_load_sdr1(ppc_hpt_sdr1());
    ppc_kernel_exception_enable();
    if (memory_boot_detect(PPC_BOOT_MEM_KB) != PPC_BOOT_MEM_KB) ppc_stop();
    paging_init(PPC_BOOT_MEM_KB);
    arch_boot_after_paging();
    kernel_start();
}

static void plan_gmac(const struct pm_kboot_info *info)
{
    if (info->gmac_rc == 0 && info->gmac.bar_bytes >= PPC_BOOT_GMAC_BYTES &&
        info->gmac.root_pa <= ~0U - 0xc00007U &&
        ppc_boot_alias_plan(info->gmac.bar_pa, PPC_BOOT_GMAC_BYTES, PPC_BOOT_GMAC_VA, &gmac_alias[0]) &&
        ppc_boot_alias_plan(info->gmac.root_pa + 0x800000U, 8, PPC_BOOT_PCI_ADDR_VA, &gmac_alias[1]) &&
        ppc_boot_alias_plan(info->gmac.root_pa + 0xc00000U, 8, PPC_BOOT_PCI_DATA_VA, &gmac_alias[2])) {
        boot_devices.gmac_ready = 1;
        boot_devices.gmac_va = gmac_alias[0].va;
        boot_devices.pci_addr_va = gmac_alias[1].va;
        boot_devices.pci_data_va = gmac_alias[2].va;
    }
}

/* Real-hardware triage: name every pre-takeover stop on the OF console. */
static void kb_stop(int out, const char *why)
{
    (void)ofw_write(out, why, kstrlen(why));
    ppc_stop();
}

static void kb_hex(int out, u32 v)
{
    char t[9];
    for (int i = 0; i < 8; i++) t[i] = "0123456789abcdef"[(v >> (28 - 4*i)) & 15];
    t[8] = ' ';
    (void)ofw_write(out, t, 9);
}
static void kb_regions(int out, const char *name, const struct pm_regions *r)
{
    (void)ofw_write(out, name, kstrlen(name));
    kb_hex(out, r->count);
    for (u32 i = 0; i < r->count && i < 8; i++) { kb_hex(out, r->v[i].start); kb_hex(out, r->v[i].size); }
    (void)ofw_write(out, "\r\n", 2);
}
/* Real-hardware triage: reserve code, image bounds and both firmware maps. */
static void kb_reserve_stop(int out, int rc, const struct pm_kboot_info *info)
{
    (void)ofw_write(out, "FAIL:KB:RESERVE ", 16);
    kb_hex(out, (u32)rc);
    kb_hex(out, (u32)__kernel_image_start);
    kb_hex(out, (u32)&__kernel_resident_end);
    (void)ofw_write(out, "\r\n", 2);
    kb_regions(out, "RAM ", &info->ram);
    kb_regions(out, "AVL ", &info->available);
    ppc_stop();
}

void ppc_kernel_start(ppc_ci ci)
{
    struct ofw_env e;
    struct pm_escc s;
    ofw_init(ci);
    if (ofw_setup(&e)) ppc_stop();
    pm_rh_of_mark(0);
    int fb_rc = pm_rh_capture(e.out);
    if (fb_rc < 0) kb_stop(e.out, "FAIL:KB:FB\r\n");
    pm_rh_of_mark(1);
    if (pm_escc_prepare(&e, &s)) {
        pm_rh_of_mark(PM_RH_ESCC_PREPARE);
        ppc_stop();
    }
    struct pm_kboot_info *info = (struct pm_kboot_info *)pm_kboot_capture(ci);
    /* E1 owns the MPIC window decoding (one resolver, H2). */
    if (pm_mpic_resolve(info, &info->mpic_pa, &info->mpic_bytes,
                        &boot_devices.mpic_order) != PM_MPIC_OK) kb_stop(e.out, "FAIL:KB:MPIC\r\n");
    if (s.data < s.csr || s.data - s.csr >= PM_ESCC_WINDOW) kb_stop(e.out, "FAIL:KB:ESCC-WINDOW\r\n");
    info->escc_pa = s.csr;
    if (!ppc_boot_alias_plan(info->escc_pa, PM_ESCC_WINDOW, PPC_BOOT_ESCC_VA, &escc_alias) ||
        !ppc_boot_alias_plan(info->mpic_pa, info->mpic_bytes, PPC_BOOT_MPIC_VA, &mpic_alias))
        kb_stop(e.out, "FAIL:KB:ALIAS-ESCC-MPIC\r\n");
#if OS32_FEATURE_IDE
    u32 ata_pa[PM_IDE_BANKS] = {0};
    pm_ide_capture(ata_pa);
    for (unsigned i = 0; i < PM_IDE_BANKS; i++)
        if (ata_pa[i] && !ppc_boot_alias_plan(ata_pa[i], PM_IDE_CTL + 1,
                PM_IDE_VA + i * PAGE_SIZE, &ide_alias[i])) ide_alias[i].bytes = 0;
#endif
    plan_gmac(info);
    plan_via(&info->via);
    const struct pm_rh_fb *fb = pm_rh_framebuffer();
#if KBOOT_SHELL || KBOOT_SH
    boot_devices.fb_full_rc = pm_rh_capture_full();
    boot_devices.fb_color_rc = pm_rh_full_colors()->rc;
    if (boot_devices.fb_full_rc == PM_RH_FULL_OK) fb = pm_rh_full_framebuffer();
#endif
    if (fb->bytes) {
        if (!ppc_boot_alias_plan(fb->pa, fb->bytes, PPC_BOOT_FB_VA, &fb_alias)) kb_stop(e.out, "FAIL:KB:ALIAS-FB\r\n");
        boot_devices.fb_va = fb_alias.va;
    }
    if (info->gmac.clock.valid) {
        if (!ppc_boot_alias_plan(info->gmac.clock.pa, sizeof(u32),
                    PPC_BOOT_GMAC_CLOCK_VA, &clock_alias)) kb_stop(e.out, "FAIL:KB:ALIAS-CLOCK\r\n");
        boot_devices.clock_va = clock_alias.va;
    }
    boot_devices.escc_csr_va = escc_alias.va;
    boot_devices.escc_data_va = escc_alias.va + (s.data - s.csr);
    boot_devices.mpic_va = mpic_alias.va;
    boot_devices.mpic_bytes = info->mpic_bytes;
    int reserve_rc = pm_kboot_reserve(info);
    if (reserve_rc != PM_KBOOT_RESERVE_OK) kb_reserve_stop(e.out, reserve_rc, info);
    if (!pm_memory_validate(info)) kb_stop(e.out, "FAIL:KB:MEMORY-VALIDATE\r\n");
    pm_rh_of_mark(2);
    static const char mark[] = "TAKEOVER\r\n";
    if (ofw_write(e.out, mark, sizeof(mark)-1) != sizeof(mark)-1 ||
        pm_escc_drain(&s)) ppc_stop();
    info->of_calls_at_detach = ofw_detach();
    pm_kernel_takeover(info);
}
