# sys/fs/ext2/ext2_fmt.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 1 (§1-v3 N5・N7) の契約を今のソースと照らして要約
ソース: `sys/fs/ext2/ext2_fmt.c` (`ext2_format`・`ext2_format_at`・`ext2_format_range`)・`sys/fs/ext2/ext2.h` (`EXT2_FMT_MIN_LBA`)・配置 `sys/fs/ext2/ext2_layout.c`

ext2 を作る 2 つの入口。どちらも書く前に範囲を全部確かめ、外れたら **1 セクタも書かない**。大きさは
`ext2_layout_plan` の固定点 (最終グループに全メタデータが入る長さへ切り下げ、`docs/design/fs/ext2/ext2_layout.md`)。

## `ext2_format_at(drive, start_lba, length)` (KAPI、hdprep・インストーラ用)

区画表を**読まずに**、与えられた範囲 [start_lba, start_lba + length) だけに作る。区画表も触らない。
次のどれかなら `EXT2_ERR_INVAL` で断る (ドライブが無い・幾何が取れないなら `EXT2_ERR_IO`):

- length = 0
- start_lba < `EXT2_FMT_MIN_LBA` (18。LBA 0 = IPL、1 = 区画表、2〜17 = ローダ 8KiB)
- IDENTIFY の総数が 0、start_lba ≥ 総数、length > 総数 - start_lba (足し算の桁あふれもここで断る)
- 範囲全体が ATA の指定の方式で指せない (`platform_ide_range_ok` → `ide_addr_range_ok`、LBA28 の上限・現在の CHS の容量)

## `ext2_format(drive, total_sectors)` (旧来の入口)

区画表の OS32 区画 (`ext2_find_partition`) の先頭に作る。区画が無ければその誤り (`EXT2_ERR_NOPART` など) を返し、
LBA を仮定して書き始めない。区画の位置を BIOS 幾何で決められず IDENTIFY の幾何で読んだだけのときは書かない
(`EXT2_ERR_INVAL`、BIOS と違えば別の場所を指している)。total_sectors は 32 グループ (`EXT2L_MAX_SECTORS(EXT2_MAX_GROUPS)`)
で頭打ち (断らない) し、さらに区画の長さで頭打ちにする。

## 共通部 (`ext2_format_range`)

固定点で決まった大きさを、入口に関係なく**この 1 か所で**ATA の範囲と照合してから書き始める。書き込みの文脈は
`part_len` を持つので、ブロック I/O の層でも区画の外は断る (×2 の桁あふれを含む)。

インストーラ・hdprep の容量の見積もり (`inst_check_space`) は同じ固定点を使い、作った像の `dumpe2fs` の空きと一致する。

## 確かめ

`make check-hdd-stage1-host` (`tools/tests/test_hdd_stage1.py`) のケース `format_clamp`・`format_at_16652`・`format_at_refuse`
(`tools/tests/ext2_part_host.c`) と、作った像の `e2fsck -fn` clean。
