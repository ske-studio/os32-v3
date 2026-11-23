# drivers/ata/ide.c — IDE PIO の入口と検出

> 状態: **現行 (2026-10-31)**

内蔵 IDE の既存 16bit PIO と IDENTIFY の検出状態を管理する。
公開 API と `IdeInfo` の配置は維持し、DMA は使わない。通常512B ATA の MULTIPLE 設定は
[ide_multiple](ide_multiple.md) が担当する。
`ide.h` は 1 コマンド最大 256 セクタと失敗契約を定義する。

デバイス層の窓口は `kernel/dev/device_ide.c` に置き、共通の宣言は
`include/ide_contract.h` に置く。像の機能 `IDE` が窓口・検出・番地計算を選ぶ。
`include/ide_bus.h` はバンク選択、taskfile、Alt Status、Device Control、Data の契約を定める。
PC-98 の窓は `platform/pc98/platform_ide_bus.h`、ATAPI と共有する番地は同じ場所の
`platform_ide_ports.h` に置く。窓の無いバンクは Alt Status が 0xFF となり、検出を飛ばす。
IDENTIFY は `ide_bus_data_word` の ATA 語の値から容量とモデル名を読む。
セクタは奇数番地でも媒体のバイト順で読み書きする。

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

## ATA 状態待ちの期限

通常 I/O の各待ちは共通の `tick_clock_ms()` の差が 10,000 ms に達すると時間切れになる。
NetBSD のドライブ I/O 用 ATA_DELAY と同じ値を用いる。
[NetBSD ata_wdc.c](https://github.com/NetBSD/src/blob/trunk/sys/dev/ata/ata_wdc.c)

探査・IDENTIFY は BSY 解除を最大 1 秒、解除後の DRQ を最大 50 ms 待つ。IDENTIFY 発行後は 400 ns の整定を待ち、0x00・0x7F・0xFF を未装着として扱う。選択前の 0x00 ではバンクを除外しない。検出直後の SET MULTIPLE 完了待ちにも 1 秒を用いる。コマンド発行後の整定は [NetBSD wdc.c](https://github.com/NetBSD/src/blob/trunk/sys/dev/ic/wdc.c) にも記載がある。期限の根拠は [FreeBSD ata_wait](https://github.com/freebsd/freebsd-src/blob/main/sys/dev/ata/ata-lowlevel.c)。

| ファイル | 関数 | 待つ状態 |
|---|---|---|
| drivers/ata/ide.c | ide_wait_bsy | 発行前・IDENTIFY 後の BSY 解除 |
| drivers/ata/ide.c | ide_wait_drq | IDENTIFY の BSY 解除、次に DRQ の成立（2 ループ） |
| drivers/ata/ide_pio.inc | ide_pio_wait_phase | データ相の BSY 解除と DRQ 成立 |
| drivers/ata/ide_pio.inc | ide_pio_finish | 終了相の BSY・DRQ 解除。SET MULTIPLE も共用 |

独立した DRDY 待ちはない。PC-98 は割込許可後、PowerMac は DEC の起動後に IDE を検出する。
最初の tick の前や割込禁止中に時計が進まない場合は、従来の 50 万回（IDENTIFY の先頭と
PIO データ相は 100 万回）まで状態を読む。待機中に tick が進めば時刻の期限を使う。

装置選択とデータ相の 400 ns は ide.c の ide_select_drive、ide_pio.inc の
ide_pio_settle が代替ステータスの空読みで確保する。メーカーのステータス遷移の
規定は [Quantum Fireball Plus AS §6.5](https://www.seagate.com/staticfiles/maxtor/en_us/documentation/manuals/Fireball_Plus_AS_Product_Manual.pdf)。
ATAPI は atapi.c の atapi_wait_clear（atapi_wait_bsy・atapi_wait_idle が共用）と
atapi_wait_drq が atapi_poll_delay の遅延時間を積算する。atapi_settle は 400 ns、
atapi_delay_us は長い遅延の分割、atapi_capacity_ready は媒体準備の再試行を担う。
atapi_packet_read は各 DRQ 相で atapi_wait_bsy を使い、atapi_read10 は媒体交換時の再試行を制限する。
