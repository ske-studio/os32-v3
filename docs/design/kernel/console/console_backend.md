# kernel/console/console_backend.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D_DESIGN (D1 で移した)
ソース: `kernel/console/console_backend.h`

リンク時固定、全関数PC98常駐。

```c
void con_backend_clear(void);
void con_backend_scroll(int rows); /* 対象行数をkernelが決める */
void con_backend_putchar(int x, int y, char ch, u8 raw_attr);
void con_backend_readchar(int x, int y, u16 *code, u8 *attr);
void con_backend_putkanji(int x, int y, u16 jis, u8 raw_attr);
int  con_backend_reverse(int x, int y);
void con_backend_cursor_at(int x, int y);
void con_backend_cursor_shape(int visible);
```
