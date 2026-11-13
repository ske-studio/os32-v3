/* ILP32 product C; only CPU/root/stop boundaries are replaced. */
#include "types.h"
#include "kernel_boot_source.c"
#if KBOOT_SH || KBOOT_SHELL
static struct pm_rh_fb stub_fb = { .pa = 0xa0000004, .bytes = 0x1000 };
const struct pm_rh_fb *pm_rh_framebuffer(void) { return &stub_fb; }
void pm_rh_discard_full(void) { }
#endif
static unsigned expect_stop;
static void emit(const char *s, u32 n)
{ u32 result = 4; __asm__ volatile("int $0x80" : "+a"(result) : "b"(1), "c"(s), "d"(n) : "memory"); }
#define SAY(s) emit(s "\n",sizeof(s "\n")-1)
static void finish(unsigned code) __attribute__((noreturn));
static void finish(unsigned code)
{ __asm__ volatile("int $0x80" :: "a"(1), "b"(code)); for (;;) {} }
void ppc_stop(void) { SAY("FAIL:STOP"); if (expect_stop) { SAY("GREEN:REJECT"); finish(0); } finish(99); }
void ppc_boot_walk_stop(void)
{
#if defined(LAN_CASE)
    if (!gmac_walk_valid(MEM_FIXED_PD_BASE) &&
        range_valid(MEM_FIXED_PD_BASE, escc_alias.va & PTE_FRAME_MASK,
                    (escc_alias.va & PTE_FRAME_MASK) + escc_alias.bytes,
                    escc_alias.pa, PPC_BOOT_IO_ATTR)) SAY("FAIL:GMAC-WALK");
#endif
    SAY("STOP:WALK");
    if (expect_stop) { SAY("GREEN:REJECT"); finish(0); }
    finish(99);
}
u32 ppc_root_current(void) { return 0; }
void ppc_root_load(u32 root)
{
    (void)root;
    if (expect_stop) { SAY("FAIL:WALK-SKIPPED"); finish(31); }
    if (!boot_root_ready) finish(98);
    if (!observe_walk_valid(root, &fb_alias) || !observe_walk_valid(root, &clock_alias) ||
        fb_alias.bytes != PPC_BOOT_FB_LIMIT || fb_alias.va != PPC_BOOT_FB_VA + 4 ||
        clock_alias.bytes != PAGE_SIZE || clock_alias.va != PPC_BOOT_GMAC_CLOCK_VA + 0x20)
        finish(44);
    SAY("REACHED:FB-CLOCK-IG");
#if defined(LAN_CASE)
    if ((LAN_CASE == 0) != boot_devices.gmac_ready) { SAY("FAIL:READY"); finish(41); }
    if (LAN_CASE == 0) {
        if (!gmac_walk_valid(root) || gmac_alias[0].bytes != 0x10000 ||
            gmac_alias[0].pa != 0x90000000 || gmac_alias[1].pa != 0xf2800000 ||
            gmac_alias[2].pa != 0xf2c00000 || boot_devices.pci_addr_va != 0xfe3c0020 ||
            boot_devices.pci_data_va != 0xfe3c1020) { SAY("FAIL:ALIAS"); finish(42); }
    } else {
        const u32 starts[] = { PPC_BOOT_GMAC_VA, PPC_BOOT_PCI_ADDR_VA, PPC_BOOT_PCI_DATA_VA };
        const u32 sizes[] = { PPC_BOOT_GMAC_BYTES, PAGE_SIZE, PAGE_SIZE };
        for (u32 i = 0; i < 3; i++)
            for (u32 off = 0; off < sizes[i]; off += PAGE_SIZE)
                if (*leaf(root, starts[i] + off) & PTE_PRESENT) {
                    SAY("FAIL:DISABLED-ALIAS"); finish(43);
                }
    }
    SAY("REACHED:LAN");
#endif
    SAY("GREEN:ROOT"); finish(0);
}
static struct pm_kboot_info info;
void _start(void)
{
    struct ppc_boot_alias a;
    if (!ppc_boot_alias_plan(0x80013020,0x40,PPC_BOOT_ESCC_VA,&a) ||
        a.pa!=0x80013000 || a.bytes!=4096 || a.va!=PPC_BOOT_ESCC_VA+0x20 ||
        ppc_boot_alias_plan(0xfffffff0,32,PPC_BOOT_ESCC_VA,&a) ||
        ppc_boot_alias_plan(0x80000001,PPC_BOOT_ALIAS_BYTES,PPC_BOOT_ESCC_VA,&a) ||
        ppc_boot_alias_plan(0,0,PPC_BOOT_ESCC_VA,&a) ||
        !ppc_boot_alias_plan(0x80000000,PPC_BOOT_ALIAS_BYTES,PPC_BOOT_MPIC_VA,&a)) finish(20);
    if (!ppc_boot_alias_plan(0xa0000004, PPC_BOOT_FB_LIMIT - 4, PPC_BOOT_FB_VA, &fb_alias) ||
        ppc_boot_alias_plan(0xa0000004, PPC_BOOT_FB_LIMIT, PPC_BOOT_FB_VA, &a) ||
        !ppc_boot_alias_plan(0xf8000020, 4, PPC_BOOT_GMAC_CLOCK_VA, &clock_alias) ||
        ppc_boot_alias_plan(0xf8000020, PAGE_SIZE, PPC_BOOT_GMAC_CLOCK_VA, &a)) finish(21);
    SAY("REACHED:ALIAS");
    u32 args[6]={0x100000,0x2000000,3,0x32,~0U,0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    if (result!=args[0]) finish(97);
    u32 *pd=(u32 *)MEM_FIXED_PD_BASE;
    for (u32 i=0;i<PAGING_BOOT_PT_COUNT;i++) {
        pd[i]=(MEM_FIXED_BOOT_PT_BASE+i*PAGE_SIZE)|PAGE_RW;
        u32 *pt=(u32 *)(pd[i]&PTE_FRAME_MASK);
        for (u32 j=0;j<PTE_COUNT;j++) pt[j]=((i*PTE_COUNT+j)*PAGE_SIZE)|PAGE_RW;
    }
    pd[PDE_INDEX(MEM_DEVICE_APERTURE_BASE)]=MEM_FIXED_APERTURE_PT_BASE|PAGE_RW;
    info.image_first=0x100000; info.image_end=0x120000;
    info.hpt_pa=0x1000000; info.hpt_bytes=0x20000;
    boot_info=&info;
    boot_devices.fb_full_rc = PM_RH_FULL_ABSENT;
#if defined(LAN_CASE)
    info.gmac_rc = LAN_CASE == 1 ? -1 : 0;
    info.gmac.bar_pa = LAN_CASE == 2 ? 0x100000 : LAN_CASE == 3 ? 0x1000000 : LAN_CASE == 5 ? 0x90000001 : 0x90000000;
    info.gmac.bar_bytes = 0x200000;
    info.gmac.root_pa = 0xf2000020;
    info.ram.count = 1; info.ram.v[0].start = LAN_CASE == 4 ? 0x90000000 : 0;
    info.ram.v[0].size = LAN_CASE == 4 ? 0x10000 : 0x800000;
    plan_gmac(&info);
    SAY("REACHED:PLAN");
#endif
    ppc_boot_alias_plan(0x80013020,0x40,PPC_BOOT_ESCC_VA,&escc_alias);
    ppc_boot_alias_plan(0x80040000,0x40000,PPC_BOOT_MPIC_VA,&mpic_alias);
    for (u32 x=0xf00000;x<0x1000000;x+=PAGE_SIZE) *leaf(MEM_FIXED_PD_BASE,x)=0;
    for (u32 x=MEM_STACK_GUARD;x<MEM_KSTACK_BASE;x+=PAGE_SIZE) *leaf(MEM_FIXED_PD_BASE,x)=0;
    for (u32 x=MEM_FIXED_PAGING_GUARD_BASE;x<MEM_FIXED_PAGING_BASE;x+=PAGE_SIZE) *leaf(MEM_FIXED_PD_BASE,x)=0;
#if BOOT_BAD_WALK == 1
    *leaf(MEM_FIXED_PD_BASE,info.image_first)=0;
    expect_stop=1;
#endif
    SAY("REACHED:ROOT");
#if BOOT_BAD_WALK == 2
    expect_stop=1;
    ppc_boot_load_root(MEM_FIXED_PD_BASE+PAGE_SIZE);
#else
    ppc_boot_load_root(MEM_FIXED_PD_BASE);
#endif
    finish(96);
}
