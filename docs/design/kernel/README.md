# kernel/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN・SLIM_KERNEL_DESIGN・SLIM_KERNEL_IMPORTS (D1 で移した)
ソース: `kernel/`

### input_service

```c
/* kernel側: 静的null実装を初期値にし、登録ではポインタだけを交換。 */
static const InputOps *input = &empty_input;
void input_register(const InputOps *p) { input = p; }
int input_getchar_kapi(void) { return input->getchar_kapi(); }
```

- null表は無入力=-1、状態/計数=0、void操作=no-op、エラーを返せる制御=既存のNOSYS。

### port_service

- null表は無入力=-1、状態/計数=0、void操作=no-op、エラーを返せる制御=既存のNOSYS。

### kbd_inject

注入リングとownerは既存kernel。

kernel/kbd_inject.c
kbdはinj_pushと同じ呼出し条件

kbdのpeek/take

### serial_diag

```c
/* kernel/serial_diag.c: 既存名を共通の口として残す。 */
void serial_puts_polled(const char *s) { emergency_output.puts_polled(s); }
int serial_gate_active(void) { return emergency_output.gate_active(); }
```

### kselftest_report

`kernel/kselftest_report.c:9`
結果・監査を外へ移さない。KDEBUG=0でも必要。

kselftest_pass/fail等の観測シンボルはカーネルに残す。

### kaudit

`kernel/kaudit.c:17`
結果・監査を外へ移さない。KDEBUG=0でも必要。

## 索引

- [kernel/console/](console/README.md)
- [kernel/gui.c](gui.md)
- [kernel/ime/](ime/README.md)
- [kernel/irq/](irq/README.md)
- [kernel/module/](module/README.md)
- [kernel/sqlite_engine.c](sqlite_engine.md)
- [kernel/sys.c](sys.md)
- [kernel/time/](time/README.md)
- [kernel/v86/](v86/README.md)
- [kernel/ver_collect.c](ver_collect.md)
- [kernel/ver_format.c](ver_format.md)
- [kernel/ver_info.h](ver_info.md)
