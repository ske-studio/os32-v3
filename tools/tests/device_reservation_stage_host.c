/* Real staged sys -> allocator -> broker. Only privileged CPU I/O is replaced.
 * Synthetic RAM is not machine detection; inspect real PTEs for capability. */
#include "types.h"
static u32 host_cr3;
static unsigned int host_if = 0x202U;
static unsigned int host_irq_save(void)
{
    unsigned int f = host_if;
    host_if &= ~0x200U;
    return f;
}
static void host_irq_restore(unsigned int f) { host_if = f; }
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    if (!code && host_if != 0x202U) code = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { report("FAIL " #x "\n", sizeof("FAIL " #x "\n") - 1); die(1); } } while (0)
void _start(void)
{
    u32 args[6] = {0x800000, 0x800000, 3, 0x32, 0xffffffffUL, 0};
    u32 result, p, total, free, i;
    struct physmem m;
    struct pgalloc_layout l;
    struct sys_device_span s[2] = {{4096,4224,SYS_DEVICE_RAM},
                                   {4400,4912,SYS_DEVICE_MMIO}};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,4096,4224};
    static u32 old[512];
    static struct device_claim ledger_before[SYS_DEVICE_MAX_SPANS];
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x800000);
    paging_init(16384);
    physmem_bootstrap_legacy(&m,16384);
    CHECK(physmem_add_trusted(&m,4096,8192,PHYSMEM_SOURCE_SYNTHETIC));
    l.capacity = pgalloc_metadata_bytes(&m);
    l.metadata_first = 4096 - l.capacity / PAGE_SIZE;
    l.metadata = (void *)(l.metadata_first * PAGE_SIZE);
    l.workspace_end = l.metadata_first;
    l.workspace_first = l.workspace_end - 16;
    CHECK(sys_memory_bootstrap_model(&m,&l,paging_verify_identity));
    CHECK(!sys_device_reserve_core(1,s,2,&cap));
    CHECK(sys_memory_stage_online());
    CHECK(paging_boot_context());
    CHECK(paging_verify_identity(4096,128,(void *)(4096 * PAGE_SIZE)));
    cap.flags |= SYS_DEVICE_RAM_MAPPED;
    CHECK(pgalloc_alloc_n_pfn(1,4911,4912,&p));
    total = pgalloc_total_pages(); free = pgalloc_free_pages();
    for (i = 0; i < 512; i++) old[i] = ((u32 *)l.metadata)[i];
    for (i = 0; i < SYS_DEVICE_MAX_SPANS; i++) ledger_before[i] = device_ledger[i];
    CHECK(!sys_device_reserve_core(1,s,2,&cap));
    CHECK(!device_claims && total == pgalloc_total_pages() && free == pgalloc_free_pages());
    for (i = 0; i < 512; i++) CHECK(old[i] == ((u32 *)l.metadata)[i]);
    for (i = 0; i < SYS_DEVICE_MAX_SPANS; i++) {
        CHECK(ledger_before[i].owner == device_ledger[i].owner);
        CHECK(ledger_before[i].span.first == device_ledger[i].span.first);
        CHECK(ledger_before[i].span.end == device_ledger[i].span.end);
        CHECK(ledger_before[i].span.kind == device_ledger[i].span.kind);
    }
    CHECK(pgalloc_free_n_pfn(p,1));
    CHECK(sys_device_reserve_core(1,s,2,&cap));
    CHECK(pgalloc_total_pages() == total - 640 && pgalloc_free_pages() == free + 1 - 640);
    CHECK(sys_device_reserve_core(1,s,2,&cap));
    CHECK(!pgalloc_free_n_pfn(4096,128));
    CHECK(!pgalloc_alloc_n_pfn(1,4911,4912,&p));
    CHECK(paging_verify_identity(4096,128,(void *)(4096 * PAGE_SIZE)));
    CHECK(paging_verify_identity(4400,512,(void *)(4400 * PAGE_SIZE)));
    report("PASS real ONLINE broker atomic BB+aperture; no device mapping\n",
           sizeof("PASS real ONLINE broker atomic BB+aperture; no device mapping\n") - 1);
    die(0);
}
