# kernel/time/sysclk.c — システムクロック判定

> 状態: **現行 (2026-10-31)**

プラットフォームの判定値を起動時に保存し、PIT とシリアルへ提供する。

## 落とし穴

- **判定前に使わない**: 未判定の既定値を機種の実クロックと取り違えると tick や通信速度がずれる。
  `platform/pc98/clock.c:7` は BIOS 0501h bit7 の 1 を 1.9968MHz、0 を 2.4576MHz とする。
  8MHz 系の SCLK1 7.9872MHz、5/10MHz 系の 9.8304MHz をそれぞれ 4 分周した値と整合する。
  `init/kernel_boot_cpu.inc:52` は PIT 初期化より先に判定する。`tools/tests/dep_dir_b_timer_host.c:86` が両系統を検査する。
