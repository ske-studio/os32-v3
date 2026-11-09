/* ILP32 real common mm + PowerMac provider. Only privileged operations fake. */
#include "types.h"
static u32 host_cr3;
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
#include "memory_boot_host_source.c"
#include "kboot_info.h"
#include "memory.h"
#include "bootinfo.h"
#include "platform_boot.h"
__asm__(".globl __kernel_image_start\n.set __kernel_image_start, 0x100000\n"
        ".globl __kernel_file_end\n.set __kernel_file_end, 0x170000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n"
        ".globl __kernel_resident_end\n.set __kernel_resident_end, 0x261000\n"
        ".globl __ledger_fixed_start\n.set __ledger_fixed_start, 0x17e000\n"
        ".globl __ledger_fixed_end\n.set __ledger_fixed_end, 0x180000\n");
static void emit(const char *s, u32 n)
{ __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
#define SAY(s) emit(s "\n", sizeof(s "\n")-1)
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); for (;;) {} }
#define EXPECT(x, tag, code) do { if (!(x)) { SAY(tag); finish(code); } } while (0)
static struct boot_common published;
static u8 source;
void boot_common_set(const struct boot_common *p, u8 s) { published=*p; source=s; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static int ram_page(const struct physmem *m, u32 first, u32 end)
{ u32 pages=0; return physmem_count(m,first,end,PHYSMEM_RAM,&pages) && pages; }
static void map_ram(void)
{
    u32 a[6]={0x100000,0x2000000,3,0x32,0xffffffffUL,0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(a) : "memory");
    EXPECT(result==a[0], "FAIL:MMAP", 98);
}
/* Firmware's final available query, the only B2 hardware boundary. */
void pm_kboot_refresh_available(struct pm_kboot_info *info)
{
    info->available.count=1;
    info->available.v[0].start=0x261000;
    info->available.v[0].size=0x2000000-0x261000;
}
void _start(void)
{
    struct pm_kboot_info info={0};
    info.ram.count=1; info.ram.v[0].start=0; info.ram.v[0].size=0x8000000;
    EXPECT(pm_kboot_reserve(&info)==PM_KBOOT_RESERVE_OK, "FAIL:B2",19);
    EXPECT(info.hpt_pa==0x1000000 && info.hpt_bytes==0x20000, "FAIL:B2-HPT",19);
    EXPECT(pm_memory_validate(&info), "FAIL:PROVIDER", 20);
    info.admitted_end--;
    EXPECT(!pm_memory_validate(&info), "FAIL:ADMISSION", 21);
    info.admitted_end++;
    info.hpt_pa=0xe00000;
    EXPECT(!pm_memory_validate(&info), "FAIL:HPT-VALIDATION", 22);
    info.hpt_pa=0x1000000;
    platform_boot_capture();
    EXPECT(!published.image_valid && !published.image_crc &&
           published.image_size==0x70000 && source==BOOTINFO_SRC_NONE,
           "FAIL:BOOTINFO",23);
    map_ram(); _disable();
    EXPECT(memory_boot_detect(16384)==16384, "FAIL:DETECT",24);
    paging_init(16384);
    SAY("REACHED:PAGING");
    int ok=memory_boot_init(16384);
    SAY("REACHED:INIT");
    for (u32 p=0x1000;p<0x1020;p++)
        EXPECT(!ram_page(&boot_memory,p,p+1), "FAIL:HPT-RAM",34);
    EXPECT(ok, "FAIL:INIT",32);
    for (u32 p=0xf00;p<0x1000;p++)
        EXPECT(!(page_tables[p/PTE_COUNT][p%PTE_COUNT]&PTE_PRESENT), "FAIL:SYSTEM-PTE",31);
    SAY("REACHED:SYSTEM-NP");
    unsigned background=0;
    for (u32 i=0;i<ledger_region_count;i++) {
        const struct ledger_region *r=&ledger_regions[i];
        if (r->type==LEDGER_R_BACKGROUND && r->first==0xf00 && r->end==0x1000) background++;
    }
    EXPECT(background==1, "FAIL:BACKGROUND",35);
    for (u32 p=0xf00;p<0x1000;p++)
        EXPECT(!ram_page(&boot_memory,p,p+1), "FAIL:SYSTEM-RAM",36);
    for (u32 p=0;p<0x100;p++)
        EXPECT(!ram_page(&boot_memory,p,p+1), "FAIL:LOW",37);
    u32 p;
    while (pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL,1,0,0x1020,LEDGER_BOTTOM_UP,&p))
        EXPECT(p>=0x100 && p<0xf00, "FAIL:ALLOC",38);
    SAY("GREEN:MEMORY"); finish(0);
}
