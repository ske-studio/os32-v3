# kernel/sys.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kernel/sys.c`

本番 context/復帰 callback は起動中保持して module を pin し、正常終了後も予約は保持する。owner の核移設、共通装置 registry、N1 lease の再設計は行わない。

## 内部設計と契約 (D1)

```c
/* kernel/sys.c: 既存公開名・halt の位置を維持 */
void sys_reboot(void) {
    platform_reset();
    for (;;) { _halt(); }
}
void buz_on(void)  { platform_buzzer_on(); }
void buz_off(void) { platform_buzzer_off(); }
/* sys_time: ClockDate t; clock_read_date(&t); datetime_to_epoch(t.year, ...); */
```

F0h による CPU リセット (`sys_reboot`) は PCI RST# を保証しない
