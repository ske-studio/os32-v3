# kernel/time/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: DEP_DIR_B_DESIGN・DEP_DIR_D2_DESIGN・VER_INFO_DESIGN (D1 で移した)
ソース: `kernel/time/`

### ktime と PC98 clock source

`include/clock_source.h` は tick 周波数と counter 入力周波数を別々に公開する。
共通 `kernel/time/ktime.c` は irq_save → platform_clock_read_us → 成功時 clamp → irq_restore → 成功時だけ user 出力の順。
失敗時は出力を触らず、呼出元の IF=0/1 を復元する。`time_clamp` と単調性の保存域・計数は共通側に残る。
`kernel/time/tick.c` が tick_count を所有し、x86 ISR の更新順は変えない。

|所有者／文脈|契約|
|---|---|
|platform/pc98/clock_source.c、IF=0、出力は kernel 局所変数|PIT valid/mode/reload 検査、最大3回の sample → tick → decide → us_from。未設定 NOSYS、上限 AGAIN|
|platform/pc98/pit_time.c/.h|time_decide/time_us_from64、退役した time_branch_hits/reset の互換保存域。判定は計数を更新しない|
|platform/pc98/pit.c・pit_math.c/.h|PIT 設定と純粋整数演算。100Hz: reload 19968/24576、period_us=10000|

退役した time_test_feed の保存域と clear は共通側で保持するが、時計の読取りには使わない。
実呼出元・provider の試験は `tools/tests/test_clock_caller.py`。保護した出力頁で IF 復元前の書込みも検出する。

### clock_service

時計のnullは0時刻。

### cpu_calibrate

CPU の行の「実測か既定か」は値からは見分けない (正しい実測値が既定の 10000 と同じになりうる)。
「既定の値に落ちたか」(ticks=0 か、計算値が最小より小さくて CALIBRATE_FALLBACK_LPT を使った) を static に記録し、それを返す口 (例 `int cpu_calibrate_used_fallback(void)`)

## 索引

- [platform/pc98/pit.c](pit.md)
- [kernel/time/sysclk.c](sysclk.md)
