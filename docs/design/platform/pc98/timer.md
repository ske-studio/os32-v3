# platform/pc98/timer.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN (D1 で移した)
ソース: `platform/pc98/timer.c`

```c
/* platform/pc98/timer.c: sample 内に tick や補間状態は置かない */
void platform_timer_sample(struct platform_timer_sample *out) {
    u8 lo, hi;
    out->p1 = platform_irq_timer_pending();
    outp(PIT_MODE, PIT_LATCH_TIMER0);
    lo = (u8)inp(PIT_CNTR0); hi = (u8)inp(PIT_CNTR0);
    out->count = ((unsigned int)hi << 8) | lo;
    out->p2 = platform_irq_timer_pending();
}
```

|platform/pc98/timer.c、起動前景 IF=0|kernel/time/pit.c が状態公開後に呼ぶ program(reload) の実体。mode→LSB→MSB だけ、s_pit を持たず再計算・待ちを追加しない|

- sample の I/O は **W IRR/R pending → W latch/R lo/R hi → W IRR/R pending** の7操作。割込み禁止区間を短縮・分割しない。
