# sys/dev/hostdrv/hostdrvfs.c — HostDrv の VFS 実装

> 状態: **現行 (2026-10-31)**

エミュレータとの共有メモリと I/O ポートで HostDrv を呼ぶ。
VFS のファイル操作を HostDrv の要求へ変換する。

## 落とし穴

- **共有状態の観測**: エミュレータが書くステータスを通常の変数として読むと、最適化で古い値を見続けうる。
  共有領域は `volatile` とする (`sys/dev/hostdrv/hostdrvfs.c`)。`outp` 自体は `memory` clobber を持たないため、非 volatile の共有データを追加する際は別途コンパイラ境界が必要 (`sys/platform/pc98/platform_io.h`)。
  初期同期ではアドレスをゼロにして識別列を送る (`sys/dev/hostdrv/hostdrvfs.c` の `hostdrvfs_detect()`)。
  状態の番兵をセットして応答を判定する (`sys/dev/hostdrv/hostdrvfs.c` の `setup_create()`, `sys/dev/hostdrv/hostdrvfs.c` の `hostdrv_create()`)。直接の最適化回帰は未確認。
