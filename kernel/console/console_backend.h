/* Fixed resident grid/cursor boundary. Attributes use console_attr.h. */
#ifndef OS32_CONSOLE_BACKEND_H
#define OS32_CONSOLE_BACKEND_H
#include "types.h"
#include "console_attr.h"

/* 格子の寸法。backend が持ち、console は読むだけ (PC-98: 80×25)。 */
struct con_geometry { int cols, rows; };
const struct con_geometry *con_backend_geometry(void);
/* 役割 → その backend の生の属性値。既存の raw 属性の ABI は変えない。 */
#define CON_COLOR_DEFAULT 0u   /* 通常の文字 (PC-98: 白 + 表示) */
u8 con_backend_color(unsigned role);
/* 表示の出力がこの backend に向いていて、文字とカーソルを書いてよいか
 * (PC-98: 表示の経路が 98 側。別の画面へ切り替わっている間は 0)。 */
int con_backend_safe(void);
/* 全角 1 文字のセル数と、表せない文字の置換 (JIS X 0208 の □)。 */
#define CON_WIDE_COLS 2
#define CON_JIS_REPLACEMENT 0x2222

void con_backend_clear(void);
void con_backend_scroll(int rows);
void con_backend_putchar(int x, int y, char ch, u8 raw_attr);
void con_backend_readchar(int x, int y, u16 *code, u8 *attr);
void con_backend_putkanji(int x, int y, u16 jis, u8 raw_attr);
int con_backend_reverse(int x, int y);
void con_backend_cursor_at(int x, int y);
void con_backend_cursor_shape(int visible);
#endif
