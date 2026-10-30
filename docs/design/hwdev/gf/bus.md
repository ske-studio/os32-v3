# hwdev/gf/bus.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/bus.c`

read/write の別名、AR の FF reset→index→data、DAC の write-only auto-increment、LIVE RMW の実行時読取り、既存の幅/範囲/有限待ちを保持する。新しい汎用 IR・計画 VM は作らない。
identity/BAR が変わった時は危険な GF MMIO を省いても可能な route00 を試す。cfg50/54・1850/1854・Command の legacy 解放未確認なら 98 VRAM に触れない。失敗を記録して再起動へ進み、ログ失敗で戻しを止めない。
表示中も Command=0102、cfg50/54・1850/1854=0、VGA は BAR0 別名を使い実 3Cxh I/O は使わない。98 の同期/31kHz を不要に送り直さない（`O/kdebug/gfinit_bus.c:91–99`）。CUI ログは 98 に残し、復帰後の文字/入力更新を確かめる。
baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。

## 索引

- [hwdev/gf/](README.md)
