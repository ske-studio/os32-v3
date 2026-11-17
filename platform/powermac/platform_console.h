/* コンソールの機種ごとの方針 (PowerMac)。固定名のヘッダで、共通の lib/kprintf.c・
 * kernel/console/console.c・fs/vfs_fd.c が読む (設計は docs/design/lib/kprintf.md)。 */
#ifndef PLATFORM_CONSOLE_H
#define PLATFORM_CONSOLE_H

#include "console.h"

/* exec を持つ像は、カーネルの診断 (kprintf・kvprintf) を LAN のログへ出す (log_printf.c)。
 * 持たない像はコンソール (kboot_console.c がログへ流す)。 */
#define PLATFORM_KPRINTF_IS_CONSOLE (!(KBOOT_SH || KBOOT_SHELL))
/* 1 文字の出力 (shell_putchar) も遠隔の鏡へ流す。 */
#define CONSOLE_PUTCHAR_MIRROR 1
/* UTF-8 の文字列 (shell_print_utf8) は fd 1 と同じ console_write の道に流す (鏡も 1 回)。 */
#define CONSOLE_UTF8_AS_STREAM 1

/* fd 1 の既定の出力先 (console_stream.c)。 */
void pm_fd1_write(const char *buf, u32 size, u8 color);
static inline void platform_fd1_write(const char *buf, u32 size, u8 color)
{
    pm_fd1_write(buf, size, color);
}

#endif /* PLATFORM_CONSOLE_H */
