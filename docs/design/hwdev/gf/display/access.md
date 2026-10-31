# hwdev/gf/display/access.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/access.h`

read/write の別名、AR の FF reset→index→data、DAC の write-only auto-increment、LIVE RMW の実行時読取り、既存の幅/範囲/有限待ちを保持する。

## 移し替えた設計

差分：PC-98ではVGA実I/Oを使わずBAR0の8bit別名を使い、I/O/BM=0・Mem=1を保つ。
