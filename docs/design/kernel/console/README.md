# kernel/console/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D_DESIGN (D1 で移した)
ソース: `kernel/console/`

|所有者|残す状態・責務|呼ばれる文脈|
|---|---|---|
|kernelの障害処理|文章・16進整形・表示行・停止／復帰の判断|既存スタックとIFのまま。backendはログ/通常consoleを呼ばない|
|gfx・con_sink・v86低位保存・kcg module|各既存状態の唯一の所有者|platformから参照・更新してよい。ドライバ→kernel依存は残す|

### display_restore

```c
/* kernel/console/display_restore.h: 既存5関数の署名を保ち、機種includeを除く。 */
void display_text_start(void);
void display_restore_98(void);
int display_recover_ready(void);
int display_recover_cell_read(u32 *value);
int display_recover_cell_write(u32 value);
```

## 索引

- [kernel/console/console.c](console.md)
- [kernel/console/console_backend.h](console_backend.md)
