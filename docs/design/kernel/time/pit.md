# kernel/time/pit.c — PIT 周期設定

> 状態: **現行 (2026-10-31)**

保存されたクロックからタイマの reload と周期を設定する。

## 落とし穴

- **クロックの決め打ち**: 1.9968MHz 用 reload を 2.4576MHz 機で使うと、100Hz のつもりが約 123Hz になる。
  `kernel/time/pit.c:26` は検出済みクロックを `pit_compute` に渡す。100Hz の reload はそれぞれ 19968 / 24576。
  `make check-pit-clock-host` は両クロックの算術を検査するが、実機の実時間は tick 差と単調時計で別に照合する。
