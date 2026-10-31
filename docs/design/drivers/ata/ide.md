# drivers/ata/ide.c — IDE PIO の入口と検出

> 状態: **現行 (2026-10-31)**

内蔵 IDE の既存 16bit PIO と IDENTIFY の検出状態を管理する。
公開 API と `IdeInfo` の配置は維持し、DMA は使わない。通常512B ATA の MULTIPLE 設定は
[ide_multiple](ide_multiple.md) が担当する。
`ide.h` は 1 コマンド最大 256 セクタと失敗契約を定義する。

転送は [ide_pio](ide_pio.md)、要求分割と公開入口は
[ide_request](ide_request.md) に分け、同じ翻訳単位へ include する。
IDENTIFY の待機・語読取りは従来の独立した経路を保つ。
ドライブ選択後は既存の代替ステータス 4 読みで 400ns 待ち、
発行前は既存の上限付き BSY 待機を行う。

存在フラグ・情報・幾何に加え、4台分の MULTIPLE 状態を12B保持する。
init は全ドライブの存在・幾何・設定を消して再検出する。ヒープは使わない。
ホスト検証は [IDE ポート模型](../../tools/tests/ide_pio_host.md)。
NP21/W と実機の転送状態遷移・時間は PM が別途受け入れる。

## 資料

確定設計 TASK_BLOCKDEV_PERF D2 に従う。ATA 手順の根拠は
[ATA/ATAPI-6 draft 1410D rev.3a](https://pdos.csail.mit.edu/6.828/2005/readings/hardware/ATA-d1410r3a.pdf)
§8.34、§8.62、§9.5、§9.6。
PC-98 の 16bit DATA と count・コマンドの根拠は、既存資料
`docs/hw/undocumented/io_ide.md:137–145,192–201,296–301`。
MULTIPLE の一次資料・BSD 手本の現物照合は [ide_multiple](ide_multiple.md#資料) に記録する。
GPL 実装は参照していない。

## 落とし穴

失敗時の READ 出力は未定義、WRITE は途中まで媒体を変更し得る。
成功セクタ数や自動再試行は追加しない。全範囲の検査と途中 I/O 失敗は別の契約。
