#ifndef PLATFORM_KPRINTF_H
#define PLATFORM_KPRINTF_H

void kvprintf(u8 attr, const char *fmt, const u32 *args) __attribute__((alias("console_vprintf")));
void __cdecl kprintf(u8 attr, const char *fmt, ...) __attribute__((alias("console_printf")));

#endif
