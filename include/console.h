/* ======================================================================== */
/*  CONSOLE.H — テキストコンソールドライバ宣言                              */
/* ======================================================================== */

#ifndef __CONSOLE_H
#define __CONSOLE_H

#include "types.h"

/* rshellフラグ */
extern int rshell_active;

/* コンソール出力 */
void shell_putchar(char ch, u8 color);
void shell_print(const char *str, u8 color);
void shell_print_dec(u32 val, u8 color);
void shell_print_hex32(u32 val, u8 color);
void shell_print_utf8(const char *utf8_str, u8 color);
void console_write(const char *buf, u32 size, u8 color);

/* カーソル制御 */
int  console_get_cursor_x(void);
int  console_get_cursor_y(void);
void console_set_cursor(int x, int y);
void console_hw_cursor_enable(void);   /* GDC カーソルを表示にして論理位置へ同期 */

void console_get_size(int *w, int *h);

#endif /* __CONSOLE_H */
