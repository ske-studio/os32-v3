# arch/x86/kernel/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D1_DESIGN・DEP_DIR_D_DESIGN (D1 で移した)
ソース: `arch/x86/kernel/`

### isr_stub

ASM の tick 更新・EOI・V86 timeout/反射・IRQ 深さ、R1 fixture→tick_dispatch の順は対象外として保存。

`IRQ_COMMON 11`

共通入口の cld・IRQ_ENTER/LEAVE・IRETD_USER は専用入口と同じ。V86_REFLECT は追加しない。

### isr_handlers

```c
/* timer_handler: fixture は現在の場所を維持。 */
    r1_fixture_timer();
    tick_dispatch();
```

- isrのputsループはposの進行と整形をkernelに残し、2 storeだけ`platform_fault_cell(pos, (u16)(u8)*str, attr)`へ。

### idt

IDT を `irq_stub_common_11` へ。

### double_fault

DFはline→guard→残列空白の順
文字列全体の確保・結合なし。

## 索引

- [kentry.md](kentry.md)
- [ring3_entry.md](ring3_entry.md)
