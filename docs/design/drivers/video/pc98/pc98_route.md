# drivers/video/pc98/pc98_route.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `drivers/video/pc98/pc98_route.c`

0FACh、decode 確認、98 再描画
0FACh は 8bit、GF=01、98=00、照合は下位 2bit。上位 bit を保存再書込みせず、既に 00 なら無書込み（`T/TASK_GEFORCE6200.md:75–86`）。
終了/障害/reboot 前は **局所 index/lock/窓復元→1850=0→1854=0→PMC140=0→route00→decode 安全確認→98 復元・予約セル往復・再描画**（`D/GF_STAGES_DESIGN.md:115–117`）。
identity/BAR が変わった時は危険な GF MMIO を省いても可能な route00 を試す。cfg50/54・1850/1854・Command の legacy 解放未確認なら 98 VRAM に触れない。失敗を記録して再起動へ進み、ログ失敗で戻しを止めない。
表示中も Command=0102、cfg50/54・1850/1854=0、VGA は BAR0 別名を使い実 3Cxh I/O は使わない。98 の同期/31kHz を不要に送り直さない（`O/kdebug/gfinit_bus.c:91–99`）。CUI ログは 98 に残し、復帰後の文字/入力更新を確かめる。
本番が共有する戻し部品は quiesce と 98 セル手順の小さな二つ。記録/画面復帰 adapter は各層に置く。
無応答/同期外れ/98 復帰失敗なら中止し、既存 S0→reboot/物理復帰へ進む。ログ欠落・判定器エラーで戻しを止めず、現場で新しい修復列/失敗分類を追加しない。

## 索引

- [./](../../../../../README.md)
