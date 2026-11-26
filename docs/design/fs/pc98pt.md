# sys/fs/pc98pt.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 1 (§1-v3 N3・N4) の契約を今のソースと照らして要約
ソース: `sys/fs/pc98pt.c`・`sys/fs/pc98pt.h` (`PC98PartEntry`・`pc98pt_get`・`pc98pt_put`・`pc98pt_entry_range`・`pc98pt_find_os32`・`pc98pt_os32_is_legacy`・`pc98pt_make_os32`)・ホストの写し `tools/pc98pt.py` (`make_os32`・`find_os32`)

PC-98 の区画表 (LBA 1、32B × 16 項目) の読み書きを 1 か所に集めた純粋関数。読み手はカーネルの ext2
(`ext2_find_partition`)・FAT (`sys/fs/fat/fatfs_vfs.c`)・HDD ローダ (`boot/boot_main.c`)、書き手は hdprep・インストーラ
(`userland/system/inst_disk.c`)・`tools/nhd_deploy.py`。型は C の素の型だけで、ローダでも組める。

## 配置 (PC-98 標準配置)

| オフセット | 中身 |
|---|---|
| +0 / +1 | mid / sid。OS32 の区画は mid 0x80 (起動可能)・sid 0xE2 |
| +4 / +5 / +6-7 | IPL のセクタ / ヘッド / シリンダ |
| +8 / +9 / +10-11 | 開始のセクタ / ヘッド / シリンダ |
| +12 / +13 / +14-15 | 終了のセクタ / ヘッド / シリンダ |
| +16〜31 | 名前 16B (OS32 は `OS32` + 空白) |

媒体はこの構造体を重ねて読まず、`pc98pt_get` / `pc98pt_put` でバイト列から取り出す。構造体はオフセットの正典を見せる
ためのもので、`offsetof` と `PC98PT_OFF_*` の一致を試験が見る。空の項目 = mid と sid が両方 0 (`pc98pt_entry_empty`)。

## 範囲の読み方 (`pc98pt_entry_range`)

- CHS は**区画表を書いた幾何** (BIOS 幾何) で LBA にする。セクタは 0 始まり。
- 区画はシリンダ単位: 開始 = 開始 CHS の LBA、終わり (排他) = (終了シリンダ + 1) × heads × spt。終了のヘッド・セクタは読まない。
- 断る (`PC98PT_ERR_*`): 幾何のヘッドかセクタが 0 か 255 超 (`GEOM`)、開始のヘッド ≥ heads・セクタ ≥ spt・シリンダが
  16 ビット超、終わり ≤ 開始、`disk_total` ≠ 0 で終わりがディスクの外 (`RANGE`)。`disk_total` = 0 はディスク側の上限を
  見ない (総数を知らないローダ)。
- `pc98pt_find_os32`: sid 0xE2 の**最初の**項目の範囲。無ければ `NOTFOUND`、範囲が壊れていれば `RANGE`。

## 旧配置 (`pc98pt_os32_is_legacy`)

2026-09-23 までの OS32 は開始を +6/+7/+8-9、終了シリンダを +12-13 に書いていた。旧配置を**読む互換はしない**。
sid 0xE2 の最初の項目が標準配置では読めず、旧配置では範囲として成り立つときだけ 1 を返し、カーネルは「移行が要る」と
案内する (`make nhd-migrate-pt`、`docs/08_build.md` §8-4 の「v64 の区画表の移行」)。標準配置で読める表を旧配置として
誤認しない。

## 書き手 (`pc98pt_make_os32`)

[start, start + len) を覆う OS32 の項目を作る。start と len はシリンダ (heads × spt) の倍数 (違えば `ALIGN`)、
開始・終了シリンダは 16 ビット以内 (超えれば `CYL`)、len = 0 は `RANGE`。中身: mid 0x80、sid 0xE2、IPL = 開始シリンダの
ヘッド 0 セクタ 0、開始 = (scyl, 0, 0)、終了 = (ecyl, heads - 1, spt - 1)、名前 `OS32` の後ろは空白。
Python の `make_os32` は同じ入力 (誤りを含む) で 1 バイトも違わない。

8/17 なら NHD の 1632〜、16/63 なら 2016〜 が標準の開始 (計画は `userland/shell/hdprep_plan.c`)。

## 確かめ

`make check-hdd-stage1-host` (`test_hdd_stage1.py (旧版 6201b26bccf0)`) のケース `pt_offsets`・`pt_817`・`pt_1663`・`pt_reject` と、
C と Python の突き合わせ。
