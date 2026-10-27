/* L14a: observe the real boot adapter, without privileged host instructions.
 * Physical accesses are rewritten only in the copied source by the runner.
 * Trace records: tag followed by four unsigned 32-bit hexadecimal columns.
 */
#include "types.h"
#include "memmap.h"
#include "pc98.h"

static u32 host_cr3, host_if;
static u32 host_irq_save(void)
{
    u32 saved = host_if;
    host_if = 0;
    return saved;
}
static void host_irq_restore(u32 saved) { host_if = saved; }
static void die(u32 code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void record(char tag, u32 a, u32 b, u32 c, u32 d)
{
    char line[38];
    const u32 words[4] = {a, b, c, d};
    line[0] = tag;
    for (u32 i = 0; i < 4; i++) {
        line[1 + i * 9] = ' ';
        for (u32 j = 0; j < 8; j++)
            line[2 + i * 9 + j] = "0123456789abcdef"[(words[i] >> (28 - j * 4)) & 15];
    }
    line[37] = '\n';
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(line), "d"(sizeof(line)) : "memory");
}
#define REQUIRE(x) do { if (!(x)) { record('!', __LINE__, 0, 0, 0); die(1); } } while (0)

#define HOST_HIGH_WORDS ((MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB)
#define HOST_LOW_WORDS (MEM_HIGH_RAM_BASE / MEM_1MB)
static u32 high[HOST_HIGH_WORDS], low[HOST_LOW_WORDS];
static u8 bios[MEM_PAGE_SIZE];
static u32 fail_after = TEST_FAIL;
static u32 operation, bios_reads;
const u8 *v86_bios_real_lowmem(void)
{
    record('B', 0, 0, 0, 0);
    return bios;
}
static u8 host_bda_read(const u8 *source, u32 addr)
{
    /* GCC may choose either operand evaluation order for the BIOS OR.
     * Keep the read values and offsets; the comparator canonicalizes only
     * these two adjacent reads, never the volatile probe operations. */
    u8 value = source[addr];
    bios_reads++;
    record('b', addr, value, 0, 0);
    return value;
}
static void *host_boot_ptr(u32 addr)
{
    return (void *)addr;
}
static u32 *physical_word(u32 addr)
{
    if (addr < MEM_HIGH_RAM_BASE) return &low[addr / MEM_1MB];
    u32 index = (addr - MEM_HIGH_RAM_BASE) / MEM_1MB;
    REQUIRE(index < HOST_HIGH_WORDS);
    if (TEST_MODE == 1 && index >= fail_after) return &low[index % HOST_LOW_WORDS];
    if (TEST_MODE == 3) index %= fail_after;
    if (TEST_MODE == 4 && index >= fail_after) index -= fail_after - 1;
    return &high[index];
}
static u32 host_read(u32 addr)
{
    u32 value = *physical_word(addr);
    record('r', operation++, addr, value, 0);
    return value;
}
static void host_write(u32 addr, u32 value)
{
    record('w', operation++, addr, value, 0);
    if (!(TEST_MODE == 2 && addr >= MEM_HIGH_RAM_BASE &&
          (addr - MEM_HIGH_RAM_BASE) / MEM_1MB >= fail_after))
        *physical_word(addr) = value;
}
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
static void dump_model(char tag, const struct physmem *m);
static int observe_bootstrap(struct physmem *m, const struct pgalloc_layout *layout,
                             int (*verify)(u32, u32, void *))
{
    record('A', layout->kind, layout->capacity, layout->metadata_first, (u32)layout->metadata);
    record('W', layout->workspace_first, layout->workspace_end, 0, 0);
    dump_model('a', m);
    int result = sys_memory_bootstrap_model(m, layout, verify);
    record('Z', result, 0, 0, 0);
    dump_model('z', m);
    return result;
}
static int observe_online(void)
{
    int result = sys_memory_stage_online();
    record('O', result, 0, 0, 0);
    return result;
}
#define sys_memory_bootstrap_model observe_bootstrap
#define sys_memory_stage_online observe_online
#include "platform/pc98/memory_host_source.c"
#include "memory_boot_host_source.c"
#undef sys_memory_bootstrap_model
#undef sys_memory_stage_online

/* Fixed symbol scenario, separate from numeric machine boundaries. */
__asm__(".globl __kernel_resident_end\n.set __kernel_resident_end, 0x261000\n"
        ".globl __ledger_fixed_start\n.set __ledger_fixed_start, 0x17e000\n"
        ".globl __ledger_fixed_end\n.set __ledger_fixed_end, 0x180000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
void kprintf(u8 color, const char *format, ...) { (void)color; (void)format; }
void exec_addrspace_abort(struct addrspace *as) { (void)as; }
static void map_ram(void)
{
    u32 args[6] = {MEM_LEDGER_META_BASE, MEM_SYSTEM_SPACE_BASE - MEM_LEDGER_META_BASE,
                   3, 0x32, 0xffffffffU, 0};
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    REQUIRE(result == MEM_LEDGER_META_BASE);
    host_map_fixed_paging();
}
static void dump_model(char tag, const struct physmem *m)
{
    record(tag, m->count, m->legacy_ceiling, 0, 0);
    for (u32 i = 0; i < m->count; i++) {
        const struct physmem_range *r = &m->ranges[i];
        record(tag, r->first, r->end, r->kind, r->sources);
    }
}
static void dump_ptes(u32 phase)
{
    record('P', phase, PAGING_PT_COUNT, PTE_COUNT, 0);
    for (u32 i = 0; i < PAGING_PT_COUNT; i++) {
        record('D', i, page_directory[i], !!page_tables[i], 0);
        if (!page_tables[i]) continue;
        /* Lossless arithmetic runs: first PTE, increment, exclusive end.
         * Every entry in every allocated PT is visited, including NP frames. */
        for (u32 first = 0; first < PTE_COUNT;) {
            u32 end = first + 1, value = page_tables[i][first], step = 0;
            if (end < PTE_COUNT) {
                step = page_tables[i][end] - value;
                end++;
                while (end < PTE_COUNT &&
                       page_tables[i][end] == value + (end - first) * step) end++;
            }
            record('T', i * PTE_COUNT + first, i * PTE_COUNT + end, value, step);
            first = end;
        }
    }
}
void _start(void)
{
    for (u32 i = 0; i < HOST_LOW_WORDS; i++) {
        low[i] = 0x12340000U + i;
        record('l', i, low[i], 0, 0);
    }
    for (u32 i = 0; i < HOST_HIGH_WORDS; i++) high[i] = 0x56780000U + i;
    bios[BIOS_WORK_MEM_HIGH_MB] = TEST_REPORTED & 255;
    bios[BIOS_WORK_MEM_HIGH_MB + 1] = TEST_REPORTED >> 8;
    u32 detected = memory_boot_detect(TEST_KB);
    record('R', TEST_KB, detected, boot_high_end, bios_reads);
    /* A second empty detection must preserve the already attested extent. */
    if (TEST_REPEAT) {
        bios[BIOS_WORK_MEM_HIGH_MB] = bios[BIOS_WORK_MEM_HIGH_MB + 1] = 0;
        record('E', memory_boot_detect(TEST_KB), boot_high_end, 0, 0);
    }
    for (u32 i = 0; i < HOST_LOW_WORDS; i++) record('L', i, low[i], 0, 0);
    for (u32 i = 0; i < HOST_HIGH_WORDS; i++) record('H', i, high[i], 0, 0);
    map_ram();
    paging_init(detected);
    dump_ptes(0);
    sys_mem_kb = detected;
    u32 ok = memory_boot_init(detected);
    record('I', ok, sys_mem_kb, memory_boot_ram_kb(), pgalloc_model_state());
    record('S', 0x261000, 0x17e000, 0x180000, memory_boot_slack_first());
    record('M', boot_meta_first, boot_meta_end, workspace_first, workspace_end);
    dump_model('m', &boot_memory);
    dump_model('n', &device_boot_map);
    record('G', ledger_region_count, pgalloc_limit_pfn(), pgalloc_arena_end(), memory_boot_dma_available());
    for (u32 i = 0; i < ledger_region_count; i++) {
        const struct ledger_region *r = &ledger_regions[i];
        record('g', i, r->first, r->end, r->type);
        record('o', r->owner, r->cache, r->flags, 0);
    }
    if (ok) {
        struct memory_page_counts c;
        memory_boot_page_counts(0, 0, &c);
        record('c', c.fixed, c.slack, c.pool, c.meta);
        record('c', c.bb, c.kdebug, c.other, c.total);
        record('c', c.phys, (u32)c.diff, pgalloc_total_pages(), pgalloc_free_pages());
    }
    dump_ptes(1);
    die(0);
}
