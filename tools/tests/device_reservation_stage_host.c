/* 実物の段つき起動 sys -> allocator -> 台帳の MMIO 登録 (T1d)。特権 CPU I/O
 * だけを贋物にする。合成 RAM は機械の検出ではない。予約は ledger_reserve_set
 * (DEVICE owner・検証済み資源レコード、TASK_T1_LEDGER §4-4)。 */
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
    u32 result, p, total, free, i, rid, n0;
    struct physmem m;
    struct pgalloc_layout l;
    struct ledger_resource r = {0};
    struct ledger_span s[2] = {{4096, 4224, LEDGER_SPAN_RAM, 0},
                               {4400, 4912, LEDGER_SPAN_MMIO, 0}};
    static u32 old[512];
    static struct ledger_region before[LEDGER_MAX_REGIONS];
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x800000);
    paging_init(16384);
    physmem_bootstrap_legacy(&m,16384);
    CHECK(physmem_add_trusted(&m,4096,8192,PHYSMEM_SOURCE_SYNTHETIC));
    l.kind = 0;   /* PGALLOC_BACKING_ARENA_TOP */
    l.capacity = pgalloc_metadata_bytes(&m);
    l.metadata_first = 4096 - l.capacity / PAGE_SIZE;
    l.metadata = (void *)(l.metadata_first * PAGE_SIZE);
    l.workspace_end = l.metadata_first;
    l.workspace_first = l.workspace_end - 16;
    CHECK(sys_memory_bootstrap_model(&m,&l,paging_verify_identity));
    r.bus = LEDGER_BUS_FIXED; r.width_basis = LEDGER_WB_DATASHEET;
    r.decode_first = 4096; r.decode_end = 4912;
    CHECK(ledger_resource_add(&r, &rid) && rid == 0);
    CHECK(!ledger_reserve_set(LEDGER_OWNER_GFX,s,2));   /* BOOTSTRAP */
    CHECK(sys_memory_stage_online());
    CHECK(paging_boot_context());
    CHECK(paging_verify_identity(4096,128,(void *)(4096 * PAGE_SIZE)));
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, 1, 4911, 4912, LEDGER_BOTTOM_UP, &p));
    total = pgalloc_total_pages(); free = pgalloc_free_pages();
    n0 = ledger_region_count;
    for (i = 0; i < 512; i++) old[i] = ((u32 *)l.metadata)[i];
    for (i = 0; i < LEDGER_MAX_REGIONS; i++) before[i] = ledger_regions[i];
    /* 窓の最後のページが使用中 → RAM + 窓の全体が不変 */
    CHECK(!ledger_reserve_set(LEDGER_OWNER_GFX,s,2));
    CHECK(n0 == ledger_region_count && total == pgalloc_total_pages() && free == pgalloc_free_pages());
    for (i = 0; i < 512; i++) CHECK(old[i] == ((u32 *)l.metadata)[i]);
    for (i = 0; i < LEDGER_MAX_REGIONS; i++) {
        CHECK(before[i].first == ledger_regions[i].first);
        CHECK(before[i].end == ledger_regions[i].end);
        CHECK(before[i].owner == ledger_regions[i].owner);
        CHECK(before[i].type == ledger_regions[i].type);
    }
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, p, 1));
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX,s,2));
    CHECK(ledger_region_count == n0 + 2);
    CHECK(pgalloc_total_pages() == total - 640 && pgalloc_free_pages() == free + 1 - 640);
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX,s,2));
    CHECK(ledger_region_count == n0 + 2);
    CHECK(!pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, 4096, 128));
    CHECK(!pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, 1, 4911, 4912, LEDGER_BOTTOM_UP, &p));
    /* 予約は写像を変えない (写像は T1e の ⑥ が予約の後に張る) */
    CHECK(paging_verify_identity(4096,128,(void *)(4096 * PAGE_SIZE)));
    CHECK(paging_verify_identity(4400,512,(void *)(4400 * PAGE_SIZE)));
    CHECK(ledger_selfcheck("stage"));
    report("PASS real ONLINE ledger_reserve_set atomic RAM+aperture; no device mapping\n",
           sizeof("PASS real ONLINE ledger_reserve_set atomic RAM+aperture; no device mapping\n") - 1);
    die(0);
}
