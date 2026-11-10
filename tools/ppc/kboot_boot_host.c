/* ILP32 product C; only CPU/root/stop boundaries are replaced. */
#include "types.h"
#include "kernel_boot_source.c"
static unsigned expect_stop;
static void emit(const char *s, u32 n)
{ __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
#define SAY(s) emit(s "\n",sizeof(s "\n")-1)
static void finish(unsigned code) __attribute__((noreturn));
static void finish(unsigned code)
{ __asm__ volatile("int $0x80" :: "a"(1), "b"(code)); for (;;) {} }
void ppc_stop(void) { if (expect_stop) { SAY("GREEN:REJECT"); finish(0); } finish(99); }
void ppc_boot_walk_stop(void) { ppc_stop(); }
u32 ppc_root_current(void) { return 0; }
void ppc_root_load(u32 root)
{
    (void)root;
    if (expect_stop) { SAY("FAIL:WALK-SKIPPED"); finish(31); }
    if (!boot_root_ready) finish(98);
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
