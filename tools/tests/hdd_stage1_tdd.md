# HDD の一時置き場 (段 1) — RED→GREEN の記録

- 票: [docs/archive/realhw_v21/TASK_HDD_INSTALL.md](../../docs/archive/realhw_v21/TASK_HDD_INSTALL.md) 段 1 / §1-v3 (N3〜N7、R3-1)
- 実行: `python3 -B tools/tests/test_hdd_stage1.py --target --mutate` (`make check-hdd-stage1-host`)
- ハーネス: `tools/tests/hdd_stage1_host.c` (純粋関数、ホスト 64 ビット + libc)、
  `tools/tests/ext2_part_host.c` (ext2 の区画探索と format_at、ILP32 -nostdlib)

## 1. 対象と見るもの

| 対象 | ケース | 見るもの |
|---|---|---|
| `drivers/pc98pt.c` | `pt_offsets` / `pt_817` / `pt_1663` / `pt_reject` | 標準配置 (+8/+9/+10-11) と `PC98PartEntry` (fatfs と共有) の一致、8/17 (NHD の 1632〜) と 16/63 (2016〜) のバイト列、境界に無い区画・16 ビット超のシリンダ (F3)・幾何 0・終わり <= 開始・ディスクの外・FAT の項目・**旧配置のバイト列を別の場所として読まない** |
| `drivers/ide_addr.c` | `ata_lba28` / `ata_range` / `ata_chs` | word 49 bit9 → LBA28 (DRV/HEAD bit6、LBA[27:24])、総数と 2^28 の上限 (`lba=268435456` が LBA 0 に化けない)、`lba + count` の桁あふれ、word 53 bit0 の現在の幾何を既定より優先 (F13)、既定 0 → 8/17・シリンダ 16 ビット、ヘッド 17 以上は使わない |
| `fs/ext2_layout.c` | `layout` | Codex の 16,652 セクタ (最終グループ 133 < 必要 135 → 1 グループ 8,193 ブロック)、32/33 グループ境界 (262,145 / 262,146 ブロック)、g=2 (非スパース) と g=3 (スパース) の境目を 1 ブロックずつ掃いて「落とした大きさは素朴な配置が本当に足りない」「落とさなかった大きさは足りる」の両方が出る、128〜524,288 セクタの全長で最終グループが収まり長さを超えない |
| `userland/shell/hdprep_plan.c` | `hdprep_geom` / `hdprep_disk` / `hdprep_mounts` / `hdprep_plan` | 断る条件の全部 (ATA 無し・BIOS 幾何なし / 無効・BX≠512・LBA も現在の CHS も無い・LBA 1 の項目 (どの添字でも、OS32 自身でも)・LBA 0 の 55AA・ルートの hd0・大きさ 8〜256 の外)、マウント中は umount が要る、16/63 で開始 2016・256MiB をシリンダへ切り下げ、BIOS の CX と IDENTIFY の総数の小さい方で頭打ち、計画した区画が共有部の書き手と固定点をそのまま通る |
| `fs/ext2_super.c` / `ext2_fmt.c` | `find_bios` / `find_fail` / `format_clamp` / `format_at_16652` / `format_at_refuse` / `mount_bounds` | 区画表の CHS を **BIOS 幾何**で LBA にする (IDENTIFY 8/17 なら 272 に化ける表が 2016 を指す)、空 / 読めない / 幾何なし / ディスクの外 / 旧配置 / FAT だけ → 失敗し **format は 1 セクタも書かずマウントもしない**、`ext2_format` は区画で頭打ち、`ext2_format_at` は範囲の外を書かず区画表も触らない、長さ 0・LBA 0〜17・ディスクの外・桁あふれ・総数 0 は 1 セクタも書かずに断る、FS が区画より大きいとマウントしない、ブロック I/O は区画の外 (×2 の桁あふれを含む) をデバイスに出さない |
| format_at の像 | e2fsck | 16,652 / 20,160 / 36,000 / 62,496 セクタを `e2fsck -fn` → clean |
| `tools/pc98pt.py` | 突き合わせ | C の `pc98pt_make_os32` と Python の `make_os32` が 8 組 (エラーを含む) で 1 バイトも違わない |
| `tools/nhd_deploy.py` | migrate-pt | 旧配置 (cdinst が書いた形) + mkfs.ext2 の NHD を作って変換 → 開始 LBA 1632 不変・長さ不変・項目 = `make_os32`・ローダが LBA 2〜・FS のバイト列不変・**e2fsck -fn clean**、2 回目は standard で何も書かない、断る 6 通り (項目 2 つ・sid 違い・ext2 無し・FS > 区画・ディスクの外・ローダ 8193B) は NHD が 1 バイトも変わらない、`update_partition_table` (init の書き手) が標準配置を書く |

## 2. RED

- 新しい純粋関数 (`pc98pt.c` / `ide_addr.c` / `ext2_layout.c` / `hdprep_plan.c`) はこの票で作った。
  **実装と試験は同じ作業の中で書いた (試験先行ではない)**。試験が実装の誤りを捕まえることは
  §3 の変異 (各判定を 1 つずつ外す・旧動作に戻す) で確かめた。
- 旧動作は**変異で再現して RED を確かめた** (関数の形が変わったので HEAD のソースを
  そのまま新しいハーネスに載せることはできない):
  - 区画表を旧配置 (+6) で読む・見つからないとき LBA 1088 にフォールバック・区画表の CHS を
    IDENTIFY の幾何で LBA にする (F4)・範囲検査なし (F12)・最終グループを落とさない (F12)・
    LBA28 の上限なし・現在の幾何を無視 (F13) — どれも RED (§3)。
- 既存の ext2 の RAM ディスク試験 5 本 (`ext2_write_io` / `b8_open` / `ext2_empty_name` /
  `vfs_fd_path` / `fatfs_stat`) は 1088 のフォールバックに寄りかかっていたので、変更直後に
  リンク失敗・マウント失敗で RED になった。LBA 1 に本物の共有部で区画表を置く足場
  `tools/tests/hdd_pt_fake.h` に乗せて GREEN (ディスクは FS をシリンダへ切り上げた分だけ広げ、
  FS の大きさ・配置は変えていない)。

## 3. GREEN と変異

### 3-1. 最初の記録の訂正 (実装レビュー往復 1、Opus M1)

最初の版は `MUTATIONS 34/34 RED` と書いたが、**実態は違った**。C の変異を当てる写し
(`MIRROR`) に `drivers/ide.h` が無く、`ide_addr.h` の `#include "ide.h"` が解決できずに
**C の変異 29 本は全部コンパイル失敗**で、それを RED に数えていた。つまり C 側は何も
確かめていなかった。直したこと:

- `MIRROR` に `drivers/ide.h` を足し、写しに無いヘッダは本物の `drivers/` / `fs/` から引く。
- **ビルドが通らない変異は RED ではなく ERROR** に数える。Python 側も、試験の失敗 (終了 1) だけを
  RED にし、読み込みの失敗・例外 (終了 3) は ERROR にする。合格は「全部 RED・ERROR 0・
  SURVIVED 0・NOT_APPLIED 0」。
- `-Werror` の未使用警告だけで落ちていた 5 本 (行を消して引数が未使用になる) は、
  `if (0 && …)` の形でコンパイルが通るように書き直した。
- 正しく数えると生き残りが出たので、試験を足した: 「format_at がディスクの外まで書く」には
  ATA の上限 > IDENTIFY の総数のケース、「標準配置の表を旧配置と誤認する」には旧として読んでも
  ディスクの内側に収まる大きさのケース、「IDENTIFY の総数を見ない」には総数 < BIOS 幾何の容量のケース。

### 3-2. 現在 (往復 1 の修正後、2026-09-24)

```
SUMMARY 25/25 PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
MUTATIONS 47/47 RED (ERROR 0, SURVIVED 0, NOT_APPLIED 0)
```

変異は C 37 本 + Python 10 本 (`tools/tests/test_hdd_stage1.py` の `C_MUTATIONS` /
`PY_MUTATIONS`)。往復 1 で足した対象: hdprep の総数 0 (C3)・ATA の上限 (C4)、format_at の
`ide_range_ok` (C4)、ext2_format の BIOS 幾何の要求 (m3) と 32 グループの頭打ち (m2)、
旧配置の検出 (M2)、migrate-pt の検査の順序 (C1: ローダ・カーネル・来歴)・1KiB 以外の
ブロック長 (C2)、旧配置の NHD への配備の門 (M2)。`do_migrate_pt` は全体を回し
(マウント・コピー・push は贋物)、断るときに**コピーも push も呼ばれず NHD が 1 バイトも
変わらない**ことを見る。写し (一時ディレクトリ) の上で変異させるので `check-par` で並列に回せる。

### 3-3. ラリー 2 の修正後 (2026-09-24)

```
SUMMARY 25/25 PASS
TARGET i386-elf GNU89 -Werror COMPILE PASS
MUTATIONS 52/52 RED (ERROR 0, SURVIVED 0, NOT_APPLIED 0); CONTROLS 9/9 SURVIVED (期待どおり)
```

- 変異は C 38 本 + Python 14 本。足したもの: ext2_format の頭打ち後の範囲を ATA の上限と照合しない
  (Codex 3)、自動で移行できない旧配置を通す (Codex 1)、OS32 の項目が無い / 壊れた NHD へ配る、
  取り込みの後の門を掛けない (Codex 2)、マウント済みの NHD を門に通さない。
- **対照 (恒等変異)** — 変異させるファイル 9 本それぞれに意味を変えない書き換え (末尾に注釈) を当て、
  **SURVIVED になること**を `--mutate` の中で確かめる (Opus a)。対照が RED / ERROR なら変異の土台が
  壊れているので失敗にする (ラリー 1 で見つかった「写しに ide.h が無く全部コンパイル失敗」の型を
  ここで捕まえる)。

### 3-4. ラリー 3 の修正後 (2026-09-24)

```
SUMMARY 25/25 PASS
MUTATIONS 60/60 RED (ERROR 0, SURVIVED 0, NOT_APPLIED 0); CONTROLS 9/9 SURVIVED (期待どおり)
```

(Codex の修正の時点では 56/56。Opus ラリー 3 の minor 2 件で下の 4 本を足して 60/60。C 38 本 + Python 22 本)

C 38 本 + Python 18 本。`legacy_pt_guard` が通すのは NHD のヘッダが無いファイルとファイルが無いときだけに
した (Codex ラリー 3)。足した試験: 旧配置と決まった後の移行の調べが OSError / RuntimeError で落ちる
(断り、「移行できない理由」に例外を出す)、ヘッダの読み取りが OSError、権限で開けない、切り詰められた NHD。
足した変異 4 本: 読めない・例外を通す (旧動作)、読み取りの OSError を「NHD でない」扱い、移行の調べの例外を
外へ投げる (理由に出さない)、切り詰めを「NHD でない」扱い。

Opus ラリー 3: push (`do_deploy`) は NHD として読めない像 (0 バイト・壊れたヘッダ) を送らない
(`legacy_pt_guard(push=True)`、版に関係なく。mount / sync は警告して通す)。ローカルの NHD の読み取りの
OSError は版に関係なく、deploy・sync 系のマウント・migrate-pt の検査のどれでも断る (migrate-pt は例外で
落ちずに MigrateError)。試験は NHD のパスだけ EIO を返す `open` の贋物で回す。足した変異 4 本: push の門の
not_nhd を通す、do_deploy が push の門を使わない、版が古いと読まずに通す、migrate-pt の検査が OSError で
例外のまま落ちる。`test_deploy_protect.py` の来歴の試験の像は、本物の NHD の形 (標準配置の OS32 区画) にした。

### 3-5. Codex 確認の minor (2026-09-24)

```
SUMMARY 25/25 PASS
MUTATIONS 61/61 RED (ERROR 0, SURVIVED 0, NOT_APPLIED 0); CONTROLS 9/9 SURVIVED (期待どおり)
```

旧配置と決まった後の移行の調べで起きた**読み取りの失敗 (OSError)** は、版の分岐 (v63 以下 / 版が取れない
ときは旧配置を通す) より前に断る。読めた旧配置を v63 以下で通す仕様は変えていない。試験: v63・push・
版が取れないツリーのそれぞれで、移行の調べが OSError → 断る (読めた旧配置は v63 で通す)。変異 1 本:
読み取りの失敗の判定を版の分岐の後に回す (C 38 本 + Python 23 本)。

## 4. 見ていないこと

- 実機・NP21/W での I/O (LBA28 のレジスタが実際に正しい物理セクタを指すか、`hdprep` の対話)。
  NP21/W の `ideio.c` の LBA の解釈 (`sn | cy << 8 | hd << 24`) はソースで確かめただけ。
- BIOS (INT 1Bh AH=84h) が 8GB ディスクをどの幾何で見せるか (受入 H3)。
- `cmd_hdprep.c` (KAPI を呼ぶ側) はホストでは回していない — 判定と計画だけが試験の対象。
