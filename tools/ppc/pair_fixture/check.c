#include "bootinfo.h"
#include "platform_module_pair.h"
extern int pm_verify_entry_pair(void);
static struct boot_common info;
const struct boot_common *boot_common_get(void) { return &info; }

void _start(void)
{
    u32 value = 123;
    int ok = pm_verify_entry_pair() && !platform_module_pair(&value) && value == 123;
    info.image_valid = 1;
    info.image_crc = 0x04030201;
    info.image_size = 0x0c0b0a09;
    ok = ok && platform_module_pair(&value) && value == 0x66aa040c;
    ++info.image_crc;
    ok = ok && !platform_module_pair(&value);
    register int status __asm__("r3") = ok ? 0 : 7;
    register int syscall __asm__("r0") = 1;
    __asm__ volatile("sc" : : "r"(status), "r"(syscall) : "memory");
    __builtin_unreachable();
}
