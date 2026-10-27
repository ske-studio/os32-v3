/* Fixed resident grid/cursor boundary. Attributes use console_attr.h. */
#ifndef OS32_CONSOLE_BACKEND_H
#define OS32_CONSOLE_BACKEND_H
#include "types.h"
#include "console_attr.h"

void con_backend_clear(void);
void con_backend_scroll(int rows);
void con_backend_putchar(int x, int y, char ch, u8 raw_attr);
void con_backend_readchar(int x, int y, u16 *code, u8 *attr);
void con_backend_putkanji(int x, int y, u16 jis, u8 raw_attr);
int con_backend_reverse(int x, int y);
void con_backend_cursor_at(int x, int y);
void con_backend_cursor_shape(int visible);
#endif
