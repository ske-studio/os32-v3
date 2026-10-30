# hwdev/gf/resources.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/resources.c`

**ID 10de:0221・subsys 10fc:1270、Ra266 の 0:13.0 に限り** BAR0=16 MiB、BAR1=256 MiB、BAR2=16 MiB の定数長を使う（F6、`O/tools/accept/gf_a4.md:25`）。長さは既知ボードの測定値であり任意の同系列製品へ一般化しない。
起動予約は ID と BAR raw/base を読むだけ。属性・base の非零/境界合わせ・加算の範囲・RAM/他装置との重なり・橋窓への収まりを既存資源検査で確認する。**本番では BAR に書かず、A1 の sizing・保存/復元を一切実行しない**。
読取り検査を共有し、F6 の ID/定数長接続を加える。A1 sizing は診断専用。
