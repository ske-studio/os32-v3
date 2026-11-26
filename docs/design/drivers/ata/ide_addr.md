# sys/dev/ata/ide_addr.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 1 (§1-v3「ATA 側の変換」) の契約を今のソースと照らして要約
ソース: `sys/dev/ata/ide_addr.c`・`sys/dev/ata/ide_addr.h` (`ide_addr_mode`・`ide_addr_range_ok`・`ide_addr_make`)・使う側 `sys/dev/ata/ide.c`

ATA のセクタ指定 (LBA28 か CHS) を IDENTIFY の申告から決め、タスクファイルに書く値と範囲の検査を作る純粋関数。
`sys/dev/ata/ide.c` がレジスタに書く値は全部ここで作る。区画表と IPL の CHS (BIOS 幾何) とは別の幾何で、ここは
**ドライブの現在の変換**だけを扱う。

## 方式の選び方 (`ide_addr_mode`)

| 条件 (上から順) | 方式 | 上限 (セクタ数) |
|---|---|---|
| 幾何が無効 | `IDE_AMODE_NONE` | — |
| word 49 bit9 (LBA 対応) | `IDE_AMODE_LBA28` | word 60-61 の総数。0 か 2^28 超なら 2^28 (`IDE_LBA28_LIMIT`) |
| word 53 bit0 が立ち、現在の幾何 (word 54-56) がヘッド 1〜16・セクタ 1〜255・シリンダ ≥ 1 | `IDE_AMODE_CHS_CUR` | cyl × heads × spt |
| それ以外 | `IDE_AMODE_CHS_DEF` | 既定 (word 1/3/6)。0 の欄は 8 ヘッド / 17 セクタ / 65536 シリンダで補う。ヘッド 17 以上かセクタ 256 以上は `NONE` |

現在の幾何が使えるときは既定より優先する (既定と現在が違うドライブがある)。

## 範囲 (`ide_addr_range_ok`)

`lba + count <= 上限` を足し算をせずに見る (`lba >= 上限` か `count > 上限 - lba` で偽)。count = 0 は真。方式が `NONE` なら偽。
`lba = 268435456` (2^28) が LBA 0 に化けない。

## 番地の値 (`ide_addr_make`)

- 方式が `NONE` なら `IDE_ERR_NO_DRIVE`、`lba >= 上限` なら `IDE_ERR_RANGE` (何も書かない)。
- LBA28: SECTOR = LBA[7:0]、CYL_LO = [15:8]、CYL_HI = [23:16]、DRV/HEAD = 0xA0 | 0x40 (LBA) | スレーブ 0x10 | LBA[27:24]。
- CHS: セクタ = (lba % spt) + 1、ヘッド = (lba / spt) % heads、シリンダ = (lba / spt) / heads (16 ビットを超えれば `IDE_ERR_RANGE`)。
  DRV/HEAD に LBA ビットは立てない。

hdprep の計画 (`userland/shell/hdprep_plan.c`) は同じ規則で上限を出し、既定の CHS (`CHS_DEF`) のドライブには書かない。
`ext2_format_at` は書く範囲全体をこの範囲検査で照合してから書く。

## 確かめ

`make check-hdd-stage1-host` (`test_hdd_stage1.py (旧版 6201b26bccf0)`) のケース `ata_lba28`・`ata_range`・`ata_chs`。
NP21/W の LBA の解釈と実機の I/O はホストでは見ていない。
