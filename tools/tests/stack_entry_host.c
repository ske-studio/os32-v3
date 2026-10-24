/* T3ab-B: product entry, stack watermark, TSS and SQLite trampoline.
 * Linux supplies mapped RAM; only privileged CR0/LTR operations are omitted.
 * The linker symbols and zero intervals come from the product linker fixture.
 */
#include "memmap.h"
#include "pc98.h"
#include "fixture_values.h"
#include "ksetjmp.h"
#include "module_runtime.h"
struct module_record module_records[MODULE_RECORD_CAP];
static u8 test_stack[128 * 1024] __attribute__((aligned(4096)));
#define test_stack_top ((u32)test_stack + sizeof(test_stack) - 16)
#include "kernel/kstack_hw.c"
#include "kernel/tss.c"

static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
#define CHECK(cond, code) do { if (!(cond)) finish(code); } while (0)
void *kmemset(void *dst, int val, u32 n)
{
    u8 *p = dst;
    while (n--) *p++ = val;
    return dst;
}
void gdt_set_tss(u32 base, u32 limit)
{
    CHECK(base == (u32)&kernel_tss && limit == sizeof(kernel_tss) - 1, 10);
}
static void bytes(u32 begin, u32 end, u8 value, int code)
{
    while (begin < end) CHECK(*(volatile u8 *)begin++ == value, code);
}
static void guards(void)
{
    for (u32 i = 0; i < sizeof(guard_ranges) / sizeof(guard_ranges[0]); i++)
        bytes(guard_ranges[i][0], guard_ranges[i][1], 0xCC, 11);
}
static void __attribute__((noinline)) deep_stack(void)
{
    volatile u8 data[8192];
    for (u32 i = 0; i < sizeof(data); i++) data[i] = 0;
}
extern void sqlite_call_on_alt_stack(void (*func)(void *), u32 top, void *arg);
static void sqlite_body(void *arg)
{
    volatile u32 local;
    CHECK((u32)&local >= (u32)test_stack &&
          (u32)&local < test_stack_top, 12);
    *(int *)arg = 73;
}
static void shell_body(void *arg)
{
    volatile u32 local;
    CHECK((u32)&local >= SHELL_STACK_BASE && (u32)&local < MEM_SHELL_STACK_TOP, 13);
    deep_stack();
    *(int *)arg = 81;
}
extern u32 entry_sp, entry_bp;
void kernel_main(u32 mem_kb, u32 boot_drive)
{
    CHECK(entry_sp == MEM_KSTACK_TOP - 12 && entry_bp == 0, 14);
    CHECK(mem_kb == 65536 && boot_drive == 0x80, 15);
    bytes(ZERO_CORE_START, ZERO_CORE_END, 0, 16);
    bytes(ZERO_CORE_START - 4, ZERO_CORE_START, 0xCC, 18);
    bytes(ZERO_CORE_END, ZERO_CORE_END + 4, 0xCC, 31);
    guards();
    /* Calls extracted verbatim from kernel_main, so a stale argument is visible. */
    boot_tss();
    CHECK(tss_get_esp0() == entry_sp + 12, 21);
    kstack_hw_init();
    u32 before = kstack_high_water();
    CHECK(before > 0 && before < 4096, 22);
    deep_stack();
    u32 after = kstack_high_water();
    CHECK(after > before && after >= 8192 && after <= MEM_KSTACK_TOP - MEM_KSTACK_BASE, 23);
    CHECK(kstack_high_water() == after, 24);
    CHECK(boot_sqlite() == 73, 25);
    int result=0;
    sqlite_call_on_alt_stack(shell_body, MEM_SHELL_STACK_TOP, &result);
    CHECK(result == 81, 26);
    CHECK(tss_get_esp0() == entry_sp + 12, 34);
    u32 shell = kstack_high_water() >> 16;
    CHECK(shell >= 8192 && shell <= MEM_SHELL_STACK_SIZE, 27);
    guards();
    check_classification();
    check_fault_return();
    finish(0);
}
extern void enter_kernel(void);
void _start(void)
{
    /* mmap(2), fixed anonymous RAM window covers both selections. */
    u32 args[6] = {0x100000, 0x400000, 3, 0x32, (u32)-1, 0};
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == args[0], 28);
    kmemset((void *)args[0], 0xCC, args[1]);
    enter_kernel();
    finish(29);
}
