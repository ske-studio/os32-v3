/* Host contract test only. No IDT delivery, task switching or #PF simulation.
 * Privileged loads are seams; descriptors and diagnostic code are the product. */
#include "gdt.c"
#include "tss.c"
#include "idt.c"
#include "emergency.c"
#include "double_fault.c"
#include "cpu_shadow.h"
u32 host_observed_cr3;
static u32 mode;
static char serial_line[160];
static u32 serial_len;
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n));
}
#define CHECK(x) do { if (!(x)) { say("FAIL " #x "\n"); finish(1); } } while (0)
void *kmemset(void *dst, int c, u32 n)
{ u8 *p = dst; while (n--) *p++ = c; return dst; }
u32 paging_sqlite_stack_guard;
void double_fault_entry(void) { finish(3); }
void serial_puts_polled(const char *s)
{ while (*s) { CHECK(serial_len + 1 < sizeof(serial_line)); serial_line[serial_len++] = *s++; } }
void host_stop(void)
{
    const char *expected[] = {
        "DOUBLE FAULT: prev EIP=12345678 ESP=003FC000 CR2=003FBFFC CR3=00600000 kernel\n",
        "DOUBLE FAULT: prev EIP=12345678 ESP=003FC000 CR2=00700FFF CR3=00600000 engine\n",
        "DOUBLE FAULT: prev EIP=12345678 ESP=003FC000 CR2=00701000 CR3=00600000 unknown\n"};
    const char *p = expected[mode];
    u32 i = 0;
    for (; p[i]; i++) CHECK(serial_line[i] == p[i]);
    CHECK(serial_len == i && kernel_tss.esp0 == 0x123400);
    volatile u16 *text = P2V_IO(TVRAM_BASE), *attr = P2V_IO(TVRAM_ATTR);
    for (i = 0; p[i] != '\n'; i++) CHECK(text[i] == (u8)p[i] && attr[i] == 0x41);
    for (; i < TVRAM_COLS; i++) CHECK(text[i] == ' ' && attr[i] == 0x41);
    say(serial_line);
    say("PASS descriptors/ESP0/I/O bitmap/diagnostic fail-stop (host seams only)\n");
    finish(0);
}
void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(3), "b"(0), "c"(&mode), "d"(sizeof(mode)) : "memory");
    CHECK(mode < 3);
    gdt_init(); tss_init(0x123400); double_fault_init(); idt_init();
    CHECK(GDT_TSS_SEL == 0x18 && USER_CS == 0x23 && USER_DS == 0x2b);
    CHECK(idt[8].type_attr == 0x85 && idt[8].selector == 0x30);
    CHECK(!idt[8].offset_low && !idt[8].offset_high && !idt[8].zero);
    CHECK(gdt[6].access == 0x89 && gdt[6].limit_low == 103 && !gdt[6].granularity);
    CHECK(((u32)gdt[6].base_low | (u32)gdt[6].base_middle << 16 |
           (u32)gdt[6].base_high << 24) == (u32)(uptr)&df_tss);
    CHECK(df_tss.cr3 == MEM_FIXED_PD_BASE);
    /* Only MOV CR3 is substituted; product shadow update executes. */
    arch_mmu_load_root(0x600000);
    CHECK(host_observed_cr3 == 0x600000 && kernel_tss.cr3 == host_observed_cr3);
    arch_mmu_load_root(MEM_FIXED_PD_BASE);
    CHECK(kernel_tss.cr3 == MEM_FIXED_PD_BASE);
    CHECK(df_tss.eip == (u32)(uptr)double_fault_entry);
    CHECK(df_tss.esp == (u32)(uptr)df_stack + sizeof(df_stack) && sizeof(df_stack) >= 4096);
    CHECK(df_tss.eflags == 2 && df_tss.cs == 8 && df_tss.ss == 16 &&
          df_tss.ds == 16 && df_tss.es == 16 && df_tss.fs == 16 && df_tss.gs == 16);
    CHECK(!df_tss.ldt && !df_tss.trap && df_tss.iomap_base == 104);
    CHECK(gdt[3].limit_low == sizeof(kernel_tss)-1 && kernel_tss.esp0 == 0x123400);
    tss_set_esp0(0x456700); CHECK(tss_get_esp0() == 0x456700); tss_set_esp0(0x123400);
    tss_iomap_allow(0x188); CHECK(tss_iomap_is_allowed(0x188)); tss_iomap_deny(0x188);
    CHECK(!tss_iomap_is_allowed(0x188) && kernel_tss.iomap[TSS_IOMAP_SIZE] == 0xff);
    for (u32 i = 0; i < 32; i++) if (i != 8)
        CHECK(idt[i].type_attr == 0x8e && idt[i].selector == 8);
    CHECK(idt[0x80].type_attr == 0xee && idt[0x80].selector == 8);
    u32 args[] = {TVRAM_BASE, 0x4000, 3, 0x32, (u32)-1, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(90), "b"(args) : "memory");
    CHECK(result == TVRAM_BASE);
    kernel_tss.eip = 0x12345678; kernel_tss.esp = MEM_KSTACK_BASE; kernel_tss.cr3 = 0x600000;
    paging_sqlite_stack_guard = 0x700000;
    double_fault_halt(mode == 0 ? MEM_STACK_GUARD_END - 3 :
                      paging_sqlite_stack_guard + PAGE_SIZE - (mode == 1));
    finish(4);
}
