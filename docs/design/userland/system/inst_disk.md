# userland/system/inst_disk.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 2 (9〜11、§1-v3 N4・N8) の契約を今のソースと照らして要約
容量とパスの判定は `userland/system/inst_disk.c`。PC-98 の媒体判定・起動資材の大きさ・IPL 補正は `sys/platform/pc98/install_disk.c`、
CHS 計画と区画表の生成は hdprep と共用の `sys/platform/pc98/install_plan.c` にある。区画表は `sys/fs/pc98pt.c`、ext2 は
`sys/fs/ext2/ext2_layout.c` の計算を使う。

## モード (`inst_classify(lba0, lba1, heads, spt, disk_total, expect_start, &mode)`)

幾何は BIOS 幾何、`expect_start` は計画の開始 (`hdprep_plan` の start)。

| 表の中身 | 結果 |
|---|---|
| 項目 0 で LBA 0 に 55AA 無し | `INST_MODE_EMPTY` |
| 項目 0 で 55AA あり | `HDPREP_E_MBR_SIG` |
| 項目 2 つ以上 (mid だけの項目も数える) | `INST_E_MULTI` (-41) |
| 1 つで sid ≠ 0xE2、または名前が `OS32` (後ろは空白か NUL) でない (`OS32X`・`os32` も違う) | `INST_E_FOREIGN` (-40) |
| OS32 の項目が標準配置で読め、開始 = 期待値 (項目が表のどこにあっても) | `INST_MODE_RECREATE` |
| 同上で開始 ≠ 期待値 (1 シリンダ前後も) | `INST_E_START` (-42) |
| 旧配置でだけ読め、旧配置の開始 = 期待値 (8/17 のシリンダ 12 = LBA 1632) | `INST_MODE_RECREATE_OLD` |
| 旧配置の開始 ≠ 期待値 (16/63 ではシリンダ 12 = LBA 12,096 で期待 2016 と違う) | `INST_E_START` |
| どちらの配置でも読めない・ディスクの外 | `INST_E_BROKEN` (-43) |

8/17 の標準配置の表を 16/63 で読むと開始 12,096 になり断る。

## 媒体の大きさ (`inst_check_boot_files`)

IPL 1〜512B (`INST_E_IPL_SIZE`)、ローダ 1〜8192B (`INST_LOADER_MAX`、`INST_E_LOADER_SIZE`)、vmkernel.lz4 1〜508KiB
(`INST_KERNEL_MAX`、`INST_E_KERNEL_SIZE`)。定数は `boot/boot_defs.h` の `MAX_IMAGE_SIZE`・`tools/nhd_deploy.py` の
`LOADER_MAX_SECTORS` × 512・`boot/boot_hdd.asm` が読むローダのセクタ数と一致させる (試験 `consts`)。

## 容量 (`inst_check_space(part_sectors, need, &room)`)

- 空き = `ext2_layout_plan(part_sectors, 32)` の総ブロック - 全グループのメタデータ - 1 - ルート 1。inode の空き = 総数 - 予約 10。
  この空きは実物の `ext2_format_at` の像の `dumpe2fs` と一致する。
- 要る量 = ファイルごとのデータ + 間接ブロック (`inst_file_blocks`、13 ブロック目から一重、二重間接まで) + ディレクトリ
  1 ブロック + 項目 16 件に 1 ブロック + 1 + 余白 256 ブロック。inode は ファイル + ディレクトリ + 余白 128 (自動で作る親の分)。
- 1 ファイルが ext2 の上限 (1KiB ブロックで二重間接まで、67,383,296B) を超える → `INST_E_FILE_SIZE`、配置が成り立たない →
  `INST_E_LAYOUT`、ブロック不足 → `INST_E_SPACE`、inode 不足 → `INST_E_INODES`。失敗でも `room` は 0 で埋めてから返す。

## パス (`inst_check_path`)

展開するパス (前置 `/hd0` を付ける前) は、先頭が `/`、どの要素も空・`.`・`..` でない (`//x`・`/x/`・`/.` も断る)、
要素数 + 1 が 32 (`VFS_MAX_PATH_DEPTH`) 以下。外れたら `INST_E_PATH` / `INST_E_DEPTH`。

## IPL と区画表

- `inst_patch_ipl`: IPL の [8] = ヘッド、[9] = セクタ (BIOS 幾何)、末尾 55AA。IDENTIFY の幾何は書かない。
- `inst_build_pt`: 512B を全部 0 にし、項目 0 にだけ OS32 の区画 (`pc98pt_make_os32`、標準配置)。共有部で読み戻すと計画と
  同じ範囲になる (8/17 = 1632 + 407,864、16/63 = 2016 + 524,160、8/17 で 256MiB を 136 で切り下げると 524,280)。

## 確かめ

`make check-hdd-stage2-host` (`test_hdd_stage2.py (旧版 f26dff44ab09)`、`hdd_stage2_host.c (旧版 89440456122b)`) のケース `classify817`・
`classify1663`・`bootfiles`・`blocks`・`space`・`iplpt`・`room`・`consts`・`paths`。
