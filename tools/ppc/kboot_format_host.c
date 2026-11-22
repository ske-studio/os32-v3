/* Exercise both public diagnostic entry points with the target's native ABI. */
#include "kprintf.h"
#include "kboot_log.h"

static const char expected[] = "-012|日|ab   |42|A|1|2|3|4|5|6|7|8|9|\n";
static const char format[] = "%04d|%s|%-5s|%u|%c|%u|%u|%u|%u|%u|%u|%u|%u|%u|\n";
static u32 used, failed;

static void finish(int status)
{
#ifdef __PPC__
    register long nr __asm__("r0") = 1;
    register long rc __asm__("r3") = status;
    __asm__ volatile("sc" : : "r"(nr), "r"(rc) : "memory");
#else
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status));
#endif
    __builtin_unreachable();
}

void pm_log_write(const char *bytes, u32 n)
{
    for (u32 i = 0; i < n; i++, used++)
        if (used >= sizeof(expected)-1 || bytes[i] != expected[used]) failed = 1;
}

u8 kprintf_attr_to_pc98(u8 attr) { return attr; }
void console_write(const char *p, u32 n, u8 c) { (void)p; (void)n; (void)c; finish(52); }

void _start(void)
{
    const u32 args[] = {(u32)-12, (u32)"日", (u32)"ab", 42, 'A', 1,2,3,4,5,6,7,8,9};
    kprintf(7, format, -12, "日", "ab", 42U, 'A', 1,2,3,4,5,6,7,8,9);
    if (failed || used != sizeof(expected)-1) finish(51);
    used = 0;
    kvprintf(7, format, args);
    finish(failed || used != sizeof(expected)-1 ? 51 : 0);
}
