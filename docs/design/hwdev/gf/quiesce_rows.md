# hwdev/gf/quiesce_rows.h — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/quiesce_rows.h`

終了/障害/reboot 前は **局所 index/lock/窓復元→1850=0→1854=0→PMC140=0→route00→decode 安全確認→98 復元・予約セル往復・再描画**（`D/GF_STAGES_DESIGN.md:115–117`）。
本番が共有する戻し部品は quiesce と 98 セル手順の小さな二つ。記録/画面復帰 adapter は各層に置く。

## 移し替えた設計

- GeForce は PBUS 1850h を 0 にして出力を停止し読戻し、1854h を 0 にして legacy を解放し読戻す。BAR0 の decode を先に閉じて自分の復帰口を失わない。
