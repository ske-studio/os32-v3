/* Intel SDM 325384-081 Vol.3A: 6.15 (#DF), 8.2.1, 8.2.5, 8.3.
 * This is the only hardware task switch in OS32. Never return from it. */
#include <stddef.h>
#include "double_fault.h"
#include "gdt.h"
#include "tss.h"
#include "memmap.h"
#include "paging.h"
#include "io.h"
#include "tvram.h"
#include "pc98.h"
#include "serial.h"
#include "sqlite_engine.h"

/* Core BSS, below APP_BAND: shared supervisor identity PTs in every PD.
 * No I/O bitmap: limit=103, iomap_base=104. CPL0 polled I/O still works. */
struct df_tss_state {
    u32 prev_tss, esp0, ss0, esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
} __attribute__((packed));
STATIC_ASSERT(sizeof(struct df_tss_state) == offsetof(struct tss_entry, iomap), df_tss_size);
STATIC_ASSERT(offsetof(struct df_tss_state, cr3) == offsetof(struct tss_entry, cr3), df_cr3);
STATIC_ASSERT(offsetof(struct df_tss_state, esp) == offsetof(struct tss_entry, esp), df_esp);
static struct df_tss_state df_tss;
#define DF_STACK_BYTES PAGE_SIZE
static u8 df_stack[DF_STACK_BYTES] __attribute__((aligned(16)));
extern void double_fault_entry(void);
extern void double_fault_touch_guard(u32 bottom) __attribute__((noreturn));

void double_fault_init(void)
{
    /* The permanent master PD is built before paging is enabled. Before that,
     * PG=0 so loading this physical CR3 does not cause a page-table walk. */
    df_tss.cr3 = MEM_FIXED_PD_BASE;
    df_tss.eip = (u32)(uptr)double_fault_entry;
    df_tss.esp = (u32)(uptr)(df_stack + sizeof(df_stack));
    df_tss.eflags = 2; /* reserved bit 1; IF/TF/DF/VM clear */
    df_tss.cs = GDT_KERNEL_CS;
    df_tss.ss = df_tss.ds = df_tss.es = df_tss.fs = df_tss.gs = GDT_KERNEL_DS;
    df_tss.iomap_base = sizeof(df_tss);
    gdt_set_df_tss((u32)(uptr)&df_tss, sizeof(df_tss) - 1);
}

static void hex_at(char *out, u32 value)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) { out[i] = hex[value & 15]; value >>= 4; }
}

void double_fault_halt(u32 cr2)
{
    _disable();
    /* CPU saved the outgoing task in kernel_tss, not on its broken stack.
     * CR3 is static in a TSS; the software CR3 writers keep its shadow current.
     * EIP is diagnostic only: SDM explicitly leaves saved #DF CS/EIP undefined.
     * CR2 may name the failed exception-frame write, not the first access. */
    char line[] = "DOUBLE FAULT: prev EIP=00000000 ESP=00000000 CR2=00000000 CR3=00000000 ";
    const char *guard = "unknown";
    if (cr2 >= MEM_STACK_GUARD && cr2 <= MEM_STACK_GUARD_END) guard = "kernel";
    else if (paging_sqlite_stack_guard && cr2 >= paging_sqlite_stack_guard &&
             cr2 - paging_sqlite_stack_guard < PAGE_SIZE) guard = "engine";
    hex_at(line + 23, kernel_tss.eip);
    hex_at(line + 36, kernel_tss.esp);
    hex_at(line + 49, cr2);
    hex_at(line + 62, kernel_tss.cr3);
    volatile u16 *text = P2V_IO(TVRAM_BASE);
    volatile u16 *attr = P2V_IO(TVRAM_ATTR);
    unsigned int col = 0;
    for (const char *p = line; *p; p++, col++) { text[col] = (u8)*p; attr[col] = TATTR_RED; }
    for (const char *p = guard; *p; p++, col++) { text[col] = (u8)*p; attr[col] = TATTR_RED; }
    while (col < TVRAM_COLS) { text[col] = ' '; attr[col++] = TATTR_RED; }
    serial_puts_polled(line);
    serial_puts_polled(guard);
    serial_puts_polled("\n");
    for (;;) _stop();
}

#if OS32_DF_GUARD_TEST == 2
static void engine_guard(void *unused)
{
    (void)unused;
    double_fault_touch_guard(paging_sqlite_stack_guard + PAGE_SIZE);
}
#endif
void double_fault_test(void)
{
#if OS32_DF_GUARD_TEST == 1
    double_fault_touch_guard(MEM_KSTACK_BASE);
#elif OS32_DF_GUARD_TEST == 2
    /* Only enter after successful module allocation / NP guard installation. */
    if (sqlite_engine_stack_top) sqlite_engine_run(engine_guard, 0, 0);
#elif OS32_DF_GUARD_TEST != 0
#error "DF_GUARD_TEST must be 0, 1 or 2"
#endif
}
