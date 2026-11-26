# sys/platform/pc98/time/cpu_calibrate.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_SERIAL_VFAST §4 (往復 3「固定費の原因」) の契約を今のソースと照らして要約
ソース: `sys/platform/pc98/time/cpu_calibrate.c` (`cpu_calibrate`・`cpu_delay_us`・`cpu_calibrate_used_fallback`)・`sys/kern/time/cpu_calibrate_math.c`・`sys/kern/time/cpu_calibrate_math.h` (`cpu_calibrate_enough`・`cpu_calibrate_compute`)

`cpu_delay_us` の空回しの回数 (1 tick あたりのループ回数) を起動時に測る。「いつ止めるか」と「いくつか」は副作用の無い
`cpu_calibrate_math.c` に切り出し、ホストで試験する (エミュレータでも 1 周では 5 tick に届かず、丸めは実機でしか目に見えない)。

## 測り方

1. `tick_count` が変わるのを待って tick 境界に同期する。
2. `nop_loop(CALIBRATE_LOOPS)` (200,000 回) を 1 周として回し、合計と経過 tick を数える。
3. `cpu_calibrate_enough(ticks, rounds)` が 1 を返すまで繰り返す:
   - ticks ≥ `CALIBRATE_MIN_TICKS` (5) なら止める (1 tick では ±100%、5 tick で ±20%)。
   - rounds ≥ `CALIBRATE_MAX_ROUNDS` (200) なら打ち切る (PIT が止まっていても起動を止めない)。
4. `cpu_calibrate_compute(合計, ticks)`: ticks = 0 なら**合計で割らずに** `CALIBRATE_FALLBACK_LPT` (10,000)。合計 / ticks が
   `CALIBRATE_MIN_LPT` (1,000) 未満も「測れていない」としてフォールバック。それ以外は合計 / ticks。

1 周の結果で決めない (266MHz では 1 周 ≒ 0.1 tick で、0 を 1 に丸めると値が実際の 1/7〜1/13 になり、`cpu_delay_us(5)` が
0.5µs しか待たない)。8MHz なら 1 周目で確定する (例: 1 周 12 tick → 200,000 / 12)。

フォールバックに落ちたかは値から見分けず、`cpu_calibrate_used_fallback()` が記録を返す。

## 校正に寄りかからない所

送信の待ち予算 (`sys/dev/char/serial_plan.c`) と ATAPI の上限は、校正の誤差 (±10% 程度) を上限の余裕で吸う。シリアルの送信予算は
校正に依存させず tick で測る。

## 確かめ

`make check-cpu-calibrate-host` (`test_cpu_calibrate.py (旧版 fe32cedfe0f1)`、`cpu_calibrate_host.c (旧版 905228a28899)`) のケース `fast_cpu`・`slow_cpu`・
`give_up`・`delay_scale`。起動時は kselftest が校正の整合を見る。
