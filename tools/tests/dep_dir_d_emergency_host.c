/* MMIO seam records every store's address, width, value and resulting cell.
 * No normal console, registration, allocation, or privileged instruction. */
#include "types.h"
#include "tvram.h"
#include "memmap.h"
#include "platform_emergency.h"
#define PAGE_SIZE MEM_PAGE_SIZE
static u8 memory[0x4000];
static u32 records[12000], used;
static void record(u32 a, u32 w, u32 v, u32 cell)
{
    records[used++] = a; records[used++] = w;
    records[used++] = v; records[used++] = cell;
}
static void store(u32 address, u32 width, u32 value)
{
    u32 offset = address - TVRAM_BASE;
    for (u32 i = 0; i < width; i++) memory[offset + i] = (u8)(value >> (i * 8));
    record(address, width, value, memory[offset & ~1u] | memory[(offset & ~1u) + 1] << 8);
}
static void serial_puts_polled(const char *s)
{ while (*s) record(0, 0, (u8)*s++, 0); }
static void _disable(void) { record(1, 0, 0, 0); }
static void host_stop(void) { record(2, 0, 0, 0); }
static struct { u32 eip, esp, cr3; } kernel_tss;
static u32 paging_sqlite_stack_guard;
static u32 v86_restore_mismatch;
static struct { u32 restore_pte_bad, restore_pde_bad; } v86_session;
#include "emergency_source.inc"
static void reset(const char *name)
{
    serial_puts_polled(name);
    for (u32 i = 0; i < sizeof(memory); i++) memory[i] = 0xa5;
}
void _start(void)
{
    reset("boot-wrap"); tvram_print(79, 0, "A\x80Z", 0x07);
    reset("boot-bottom"); tvram_print(79, 24, "AB", 0x41);
    reset("boot-empty"); tvram_print(0, 25, "X", 0x21); tvram_print(80, 24, "X", 0x21);
    reset("exception"); tvram_puts_at(0, 0, "FAULT", 0x07);
    tvram_put_hex32(24, 68, 0x80abcdef, 0x07);
    kernel_tss.eip = 0x12345678; kernel_tss.esp = 0x003fc000; kernel_tss.cr3 = 0x00600000;
    paging_sqlite_stack_guard = 0x700000;
    reset("df-kernel"); double_fault_halt(MEM_STACK_GUARD_END - 3);
    reset("df-engine"); double_fault_halt(0x700fff);
    reset("df-unknown"); double_fault_halt(0x701000);
    reset("v86"); v86_restore_mismatch = 0x123;
    v86_session.restore_pte_bad = 0x12; v86_session.restore_pde_bad = 0x34;
    v86_diagnostic();
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(records), "d"(used * sizeof(u32)));
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    __builtin_unreachable();
}
