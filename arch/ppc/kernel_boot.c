/* OF-PPC 4.2.4/8.2; OpenBSD macppc/locore0.S:87; FreeBSD
 * aim/mmu_oea.c:685-695. Plan D uses software tables, never RAM BATs.
 * Static MMIO slots belong to the platform, outside the device ledger. */
#include "kernel_entry.h"
#include "kernel_boot.h"
#include "ppc_trap.h"
#include "ppc_hpt.h"
#include "ppc_oea.h"
#include "arch_cpu.h"
#include "kboot_info.h"
#include "kboot_layout.h"
#include "memory.h"
#include "memory_boot.h"
#include "paging.h"
#include "boot_cpu.h"
#include "platform_memory.h"

#define PPC_BOOT_MEM_KB 16384U
#define PPC_BOOT_RAM_ATTR 0x10U
#define PPC_BOOT_IO_ATTR 0x28U
static const struct pm_kboot_info *boot_info;
static struct ppc_boot_alias escc_alias, mpic_alias;
static unsigned boot_root_ready;
void ppc_kernel_exception_enable(void);
void ppc_boot_walk_stop(void) __attribute__((noreturn));

int ppc_boot_alias_plan(u32 pa, u32 bytes, u32 va,
                        struct ppc_boot_alias *out)
{
    u32 offset = pa & (PAGE_SIZE - 1);
    if (!out || !bytes || bytes - 1 > ~pa ||
        bytes > PPC_BOOT_ALIAS_BYTES - offset ||
        (va != PPC_BOOT_ESCC_VA && va != PPC_BOOT_MPIC_VA)) return 0;
    out->pa = pa & PTE_FRAME_MASK;
    out->bytes = (offset + bytes + PAGE_SIZE - 1) & PTE_FRAME_MASK;
    out->va = va + offset;
    return out->bytes <= PPC_BOOT_ALIAS_BYTES;
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
static int boot_walk_valid(u32 root)
{
    u32 image = boot_info->image_first;
    u32 end = (boot_info->image_end + PAGE_SIZE - 1) & PTE_FRAME_MASK;
    return range_valid(root, image, end, image, PPC_BOOT_RAM_ATTR) &&
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
        for (u32 a = 0; a < MEM_1MB; a += PAGE_SIZE) *leaf(root, a) = 0;
        for (u32 a = boot_info->hpt_pa;
             a < boot_info->hpt_pa + boot_info->hpt_bytes; a += PAGE_SIZE)
            *leaf(root, a) = 0;
        if (!boot_walk_valid(root)) ppc_boot_walk_stop();
        boot_root_ready = 1;
    }
    ppc_root_load(root);
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
    ppc_cpu_disable_bats(pvr);
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
    ppc_kernel_main();
}

/* D3 needs only the KeyLargo mac99 parent window, not E1's MPIC driver.
 * Use the captured ranges and assigned BAR together; reject other layouts.
 * NetBSD macppc/dev/obio.c:214-234 (BAR); OpenBSD macppc/dev/openpic.c:224
 * adds the child reg to that BAR. The captured ranges must agree with it.
 */
static u32 be_word(const unsigned char *p)
{
    return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3];
}
static int mpic_window(struct pm_kboot_info *info)
{
    const struct pm_kboot_device *d = &info->mpic;
    if (d->machine != PM_KBOOT_MACHINE_KEYLARGO || d->count < 2) return 0;
    const struct pm_kboot_node *parent = &d->v[1];
    const struct pm_kboot_property *reg = &d->v[0].reg;
    const struct pm_kboot_property *bar = &parent->assigned;
    const struct pm_kboot_property *r = &parent->ranges;
    if (parent->ac != 1 || parent->sc != 1 ||
        !reg->present || reg->bytes != 8 || !bar->present || bar->bytes != 20 ||
        !r->present || r->bytes != 20 || be_word(bar->data+4) ||
        be_word(bar->data+12) || be_word(r->data) ||
        (be_word(r->data+4) & 0x03000000U) != 0x02000000U ||
        (be_word(r->data+4) & 0x00ffffffU) !=
            (be_word(bar->data) & 0x00ffffffU) || be_word(r->data+8) ||
        be_word(r->data+12) != be_word(bar->data+8) ||
        be_word(r->data+16) != be_word(bar->data+16)) return 0;
    u32 base = be_word(bar->data+8), size = be_word(bar->data+16);
    u32 off = be_word(reg->data), bytes = be_word(reg->data+4);
    if (!size || size-1 > ~base || off >= size || !bytes || bytes > size-off)
        return 0;
    info->mpic_pa = base + off;
    info->mpic_bytes = bytes;
    return 1;
}
void ppc_kernel_start(ppc_ci ci)
{
    struct ofw_env e;
    struct pm_escc s;
    ofw_init(ci);
    if (ofw_setup(&e) || pm_escc_prepare(&e, &s)) ppc_stop();
    struct pm_kboot_info *info = (struct pm_kboot_info *)pm_kboot_capture(ci);
    if (!mpic_window(info)) ppc_stop();
    info->escc_pa = s.csr;
    if (!ppc_boot_alias_plan(info->escc_pa, PM_ESCC_WINDOW, PPC_BOOT_ESCC_VA, &escc_alias) ||
        !ppc_boot_alias_plan(info->mpic_pa, info->mpic_bytes, PPC_BOOT_MPIC_VA, &mpic_alias))
        ppc_stop();
    if (pm_kboot_reserve(info) != PM_KBOOT_RESERVE_OK || !pm_memory_validate(info)) ppc_stop();
    static const char mark[] = "TAKEOVER\r\n";
    if (ofw_write(e.out, mark, sizeof(mark)-1) != sizeof(mark)-1 ||
        pm_escc_drain(&s)) ppc_stop();
    info->of_calls_at_detach = ofw_detach();
    pm_kernel_takeover(info);
}
