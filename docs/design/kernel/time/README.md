# kernel/time/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D2_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `kernel/time/`

### ktime

```c
/* kernel/time/ktime.c: irq_save 済みの既存 retry loop 内 */
platform_timer_sample(&hw);
p1 = hw.p1; count = hw.count; p2 = hw.p2;
t = (unsigned int)tick_count;  /* 必ず sample 後、feed 注入前 */
/* 以後 feed→time_decide→time_us_from→clamp→irq_restore→出力をそのまま残す */
```

|所有者／文脈|移動・維持する中身|
|---|---|
|kernel/time/ktime.c、呼出元 IF を保存|ps の valid/mode/reload 検査、retry 上限 TIME_RETRY_MAX、feed、補間、s_last_us と clamp_count を所有。sample は IRQ 内でも待たない|

- p1/p2 不一致の再試行、上限時 AGAIN、未設定時 NOSYS、失敗時出力不変、NULL 出力、feed 中 clamp 無効、成功時の単調性を維持。

### pit_math

|所有者／文脈|移動・維持する中身|
|---|---|
|kernel/time/pit_math.*|構造体の型・配置・mode 値・純粋整数演算をそのまま維持。100Hz: reload 19968/24576、period_us=10000|

### clock_service

時計のnullは0時刻。

### cpu_calibrate

CPU の行の「実測か既定か」は値からは見分けない (正しい実測値が既定の 10000 と同じになりうる)。
「既定の値に落ちたか」(ticks=0 か、計算値が最小より小さくて CALIBRATE_FALLBACK_LPT を使った) を static に記録し、それを返す口 (例 `int cpu_calibrate_used_fallback(void)`)

## 索引

- [kernel/time/pit.c](pit.md)
- [kernel/time/sysclk.c](sysclk.md)
