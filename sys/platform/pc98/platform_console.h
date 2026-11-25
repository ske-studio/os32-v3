/* コンソールの機種ごとの方針 (PC-98)。固定名のヘッダで、共通の
 * sys/kern/console/console.c・sys/fs/vfs_fd.c が読む (設計は docs/design/lib/kprintf.md)。 */
#ifndef PLATFORM_CONSOLE_H
#define PLATFORM_CONSOLE_H

#include "console.h"

/* 1 文字の出力 (shell_putchar) は遠隔の鏡へ流さない。 */
#define CONSOLE_PUTCHAR_MIRROR 0
/* UTF-8 の文字列 (shell_print_utf8) は漢字の描画つきで出す。 */
#define CONSOLE_UTF8_AS_STREAM 0

/* fd 1 の既定の出力先。 */
static inline void platform_fd1_write(const char *buf, u32 size, u8 color)
{
    console_write(buf, size, color);
}

#endif /* PLATFORM_CONSOLE_H */
