# platform/pc98/d2_bind.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_D2_DESIGN (D1 で移した)
ソース: `platform/pc98/d2_bind.c`

```c
/* platform側: 既存driver関数をそのまま表に入れる（型の違う所だけadapter）。 */
void pc98_d2_bind(void) {
    input_register(&pc98_input); pointer_register(&pc98_pointer);
    port_register(&pc98_port); clock_register(&pc98_clock);
    con_output_register(&pc98_console_output);
}
static u8 pc98_shift_state(void) { return kbd_shift_state; }
/* unsigned long baudとの関数ポインタ型不一致をcastで隠さない。 */
static void pc98_port_init(u32 baud) { serial_init((unsigned long)baud); }
/* platform/pc98/d2_bind.c: 登録前から有効、書換なし。 */
const EmergencyOps emergency_output = {
    pc98_serial_puts_polled, pc98_serial_gate_active
};
```
