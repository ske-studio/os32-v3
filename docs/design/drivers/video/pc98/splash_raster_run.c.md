# drivers/video/pc98/splash_raster_run.c — 画像の再生の残り時間を有理数で管理する

> 状態: **現行 (2026-11-18)** — 出所: RASTER_SPLASH_*_DESIGN・TASK_RASTER_SPLASH (要約して移した)
ソース: `drivers/video/pc98/splash_raster_run.c`・`drivers/video/pc98/splash_raster_run.h`

## 何をするか

`docs/design/drivers/video/pc98/splash_raster_play.c.md` のフレームを何回まわしてよいかを決める。
可視区間は IF=0 なので PIT の tick が遅れる。そのため、実際の tick の経過とフレーム数から求めたモデルの経過の大きい方を「使った時間」とする。
起動 (`platform/pc98/splash_image.c`) とビューアー (`drivers/video/pc98/backend_pc98_osri.c`) が共有する。

## データの形と単位

- `struct PlRun { start, budget_us, divisor, period, consumed, frames, good }`。
- 時間はすべて「µs × divisor」(divisor は line_den) の整数。1 フレームの周期は period = total_lines × line_num_us。
- tick は 100Hz (`PL_RUN_TICK_US` = `TICK_MS` × 1000)。

## 口の契約

- `pl_run_timing(s)`: 状態が判定済み・dynamic_ok・入口 IF=1 で、line_den と total_lines が SYNC の表から求めた値と一致すれば 0。動的な画像はこれが通らなければ使わない。
- `pl_run_init(r, s, start, budget_us)`: 開始の tick と予算を置く。
- `pl_run_used(r, now)` = max(tick の経過 × tick の µs × divisor, consumed)。両方を足さない。
- `pl_run_more(r, now)`: `used + period <= budget_us × divisor` なら次のフレームを許す。tick が止まっても consumed が増えるので必ず終わる。
- `pl_run_charge(r, frame)`: OK でも DROP でも 1 周期を消費する。OK は good も数える。
- `pl_run_remaining(r, now, unit_us, cap)`: 残りを unit_us 単位で切り下げて返す。有界の二分探索の掛け算の比較で求め、64bit の割り算を使わない (120 秒のビューアーにも使える)。
- `pl_run_extend(r, now, extension_us)`: ビューアーが表示を延ばすとき、残りと延長の大きい方で基点を置き直す。

## 落とし穴

- 期限はモデルの時間で守るが、実際の壁時計は tick の遅れのぶん長くなりうる。PIT の tick へ推定値を足す補正はしない (ユーザー決定 2026-11-04「補正しない」)。
- 新しい関数に 64bit の割り算 (`__udivdi3`) を足さない。

## 検査

- `tools/tests/test_splash_v2.py` (splash-v2-host): 残り 0・preview 不足・読み込みの超過・全 DROP・IF=0・NO_SYNC・tick の停止と wrap・fallback の残り時間を確かめる。DROP の無課金・期限の再起算は実行時 RED。
- `tools/tests/test_splash_wire.py`: 起動の結線の中で使う。
