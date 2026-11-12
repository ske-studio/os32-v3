# TASK_PPC_REALHW — PowerMac G4 QuickSilver の実機で OS32 を起動し、QEMU の到達点を受け入れる

> 上位: [TASK_PPC_SHELL](TASK_PPC_SHELL.md)

> 状態: **実装中 (2026-11-13)** — 設計は確定。計画と詳細設計は [PPC_REALHW_G4_DESIGN](../../decisions/ppc/PPC_REALHW_G4_DESIGN.md)。実機ではまだ一度も起動していない (持越し台帳 PPC-BOOT-RH・PPC-MMU-RH・PPC-KBOOT-RH・PPC-LAN-RH)。

## 0. ユーザー決定 (2026-11-13)
- 次の段は「G4 実機の受入」(USB の段 1 の後の選択)。
- 起動は LAN の TFTP (OF の `boot enet:...`)。観測は右のモニターをノートのカメラで。
- G4 はノートと同じスイッチにつなぐ。TFTP とホストの道具 (AF_PACKET) はノート (PM 決定)。
- ノートの BOOTP/DHCP は G4 の MAC だけに答える形なら可。OF の NVRAM (setenv) は要る時に戻し方と一緒に先に聞く ([D2])。

## 1. 段
| 段 | 中身 | 状態 |
|---|---|---|
| RA | 実機向けの道具: ELF の配布の検査 (PT_LOAD の終端 < load-base 等) と実機の試験の道具 (AF_PACKET) | 実装中 |
| RB | RH1: FB の上の帯 (0xFE200000) に段の印、切り離しの後の観測路、kernel_boot の FB・clock の別名 | 実装中 |
| RC | RH2: UniNorth の GMAC の clock gate の接続 (実機と QEMU の判定)、実機の PHY (ID の採取と profile) | RB の後 |
| RH0〜RH3 | 実機の準備 (ユーザーの物理操作・ノート) → 初回起動 → LAN → S-B・S-C・S-D | RA〜RC の後 |
| RH4 | uPD720101 のカードで USB | RH3 の後 |
