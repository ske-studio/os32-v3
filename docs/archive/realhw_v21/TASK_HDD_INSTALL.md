# TASK_HDD_INSTALL — 実機 Ra266 の 8GB IDE へ置き場を作り、CD からインストールして HDD 起動する

> 状態: **受入完了 (2026-09-25)** — 段 0/1/2 着地 (28a81fb 他)、ERASE (fe8305d8)。実機 Ra266 で H3 と CD インストール → HDD 起動 (H5 の前半) が合格 (末尾「実機の結果」)。H5 の後半 (ホストの e2fsck) はユーザー決定で行わない (2026-09-25)。H4 (hdprep 単体の手順) の記録は無い。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-23) / それまでの状態: **方針確定 (2026-09-23)** — Codex ラリー 3 で N1〜N8 は計画上すべて閉、残った R3-1 (区画表を書く前のマウント確認が成り立たない) は**ユーザー決裁 B**: 「`ext2_format_at` → 区画表を書いて読み戻す → 通常のマウントで確認、失敗なら『未完了』と表示して止める」。実装は段 0 → 段 1 → 段 2 の順。**実機はしばらく使えない (ユーザー、2026-09-23)** ので H3〜H5 は後回し。ラリー 2 は Fable が月間上限で不参加 (Fable のラリー 1 所見は Codex が同意/不同意を判定済み)。実装はキーボード修正 (wt/kbd-rty) の着地後。
> ユーザー指示 (2026-09-23): 「キーボードが直ってから HDD インストールへの移行を早める。FDD のみで一時的に置ける場所が無いのは不便。CD イメージからインストールする」。
> 正典の親: [PLAN.md](../../tasks/realhw/PLAN.md) §2 (8GB は未知の領域、先頭に小さく切る) / §3 (FD 起動 + CD から入れる)。

> **2026-09-23 夜の注意**: NP21/W 上の cdinst で作った NHD のルートに名前の無い項目ができていた → [`../kernel_v21/TASK_EXT2_EMPTY_NAME.md`](../kernel_v21/TASK_EXT2_EMPTY_NAME.md)。**実機で CD インストールする前に直す**。

## 0. 事実 (PM がコードと実機で確認、2026-09-23)

| # | 事実 | 根拠 |
|---|---|---|
| F1 | 実機の HDD は IDENTIFY で **C=16382 H=16 S=63、16,514,063 セクタ** (約 8.4GB)。`[ide] drive0 identify=0`、既存の FAT は無い (`[fatfs] mount failed pdrv=1 err=13`) | 起動画面の写真 (CI ビルド 527255b) |
| F2 | 2 つのインストーラ (`userland/system/cdinst.c`、FD 用 `install.c`) は **8 ヘッド × 17 セクタの固定幾何**で区画表 (LBA 1) と IPL (`ipl[8]/[9]`) を書く。ブート予約は 12 シリンダ = **LBA 1632** から (`HDD_PARTITION_LBA`)。区画は**ディスク全体** | cdinst.c:30〜37, 111〜131, 216〜217 / install.c:28〜30, 251〜255 |
| F3 | 区画の終了シリンダは `total/136 - 1` を **16 ビット**で書く。F1 のディスクでは 121,426 → **桁あふれ**。8/17 で表せる上限は 65,536 × 136 = 約 4.5GB | 同上 |
| F4 | **カーネルは区画の開始 LBA を IDENTIFY の幾何で計算する** (`ext2_find_partition`: `(start_c × info.heads + start_h) × info.sectors + start_s`)。実機では 12 × 16 × 63 = **12,096**、インストーラが書いた実体は **1,632** → **マウントもフォーマットも別の場所を見る**。NP21/W の NHD は IDENTIFY も 8/17 なので一致して見えていた | fs/ext2_super.c:300〜339、fs/ext2_fmt.c:73 (`fmt_ctx.base_lba = ext2_find_partition()`) |
| F5 | IPL (`boot/boot_hdd.asm`) は IPL 内の `geo_heads/geo_spt` (インストーラが 8/17 を書く) で LBA→CHS を計算し **BIOS INT 1Bh** に渡す。**BIOS が 8GB ディスクをどの幾何で見せるかは未測定** (資料の壁: 8/17 = 4.25GB、16/63 = 31.4GB。Bible 2-9-1)。幾何は **INT 1Bh AH=84h (新センス)** で BX=セクタ長 / CX=シリンダ / DH=ヘッド / DL=セクタ として得られる (Bible 2-9 §3 SENSE [HDD]) | boot_hdd.asm:36, 70〜100 |
| F6 | `loader_hdd.asm` は IPL から DA / heads / SPT を `7F00h` のパラメータ域で受け取る。FD のローダ (`loader_fat_new.asm`) は HDD の幾何を問い合わせていない | loader_hdd.asm:24〜49、loader_fat_new.asm |
| F7 | ext2 の一括フォーマットの実績は 200MB の NHD まで (1KB ブロック、1 グループ 8192 ブロック)。シェルの `format [0-3] [sects]` は `ext2_format()` を呼ぶだけで区画表を書かない | fs/ext2_fmt.c、userland/shell/cmd_sys.c:112〜127 |
| F8 | カーネルの ATA I/O は IDENTIFY の**既定**幾何で LBA→CHS (`ide.c:356〜367`、`drivers/dev.c:254-265` にも同じ変換)。**正しいのはドライブの現在の変換と一致するときだけ** (F13) | drivers/ide.c、dev.c |
| F10 | **区画表のバイト配置が PC-98 標準と 2 バイトずれている** (ラリー 1 で両者が指摘)。標準 (FreeBSD `diskpc98.h`、OS32 の `fs/fatfs_vfs.c:380` `PC98PartEntry`) は +4/5/6-7 IPL CHS、**+8 開始セクタ / +9 開始ヘッド / +10-11 開始シリンダ**、+12/13/14-15 終了。OS32 の書き手 (cdinst / install / `tools/nhd_deploy.py:689-723`) と読み手 (`ext2_find_partition`、`boot/boot_main.c:16-43`) は +6/+7/+8-9 を開始、+10〜13 を終了に使う独自配置で、**互いに揃っているので NP21/W では出なかった** | Codex B1 / Fable B2 |
| F11 | **ext2 は最大 32 グループ = 256MiB** (`fs/ext2_ctx.h:18` `EXT2_MAX_GROUPS 32`、format は `NOSPC`、mount は `IO`)。「200MB の実績」は設計上限の内側だっただけ | 両者 |
| F12 | formatter は**最終グループがメタデータより小さくても断らず区画外へ書く** (例: 200MiB を 8/17 のシリンダに切り上げると最終グループ 15 ブロック < 必要 249)。`ext2_find_partition` は読めない・見つからないとき **LBA 1088 を返し**、format はその位置から書き始める。`dev_blk_write_lba` は範囲検査をしない | Codex B3/B5、Fable 非 blocker 9 |
| F13 | カーネルの ATA CHS は IDENTIFY の**既定**幾何 (word 1/3/6)。BIOS が INITIALIZE DEVICE PARAMETERS で**現在の変換** (word 53 bit0 → word 54-58) を変えていれば、既定での CHS は別の物理セクタを指す。`lba_supported` (word 49 bit9) は読むだけで未使用 | Fable B3 |
| F14 | 低位メモリに「ブート情報の固定域」は**無い** (カーネルは `boot_drive` をスタック引数で受ける)。0x1000〜 はフォントキャッシュが上書きする。HDD ローダは INT 1Bh 後の CF を見ない (`loader_hdd.asm:158`) | Fable B4 / Codex B9 |
| F15 | 訂正: `install.c` は IPL に **IDENTIFY の幾何**を書く (区画表は 8/17) = 実機では混在 (F2 の誤り)。F3 の書かれる値は 55,889 (121,425 の下位 16 ビット)。F4 の後段: format も同じ `ext2_find_partition` を使うので、FS は 1632 ではなく 12,096 に作られようとする (その前にグループ上限で失敗) | 両者 |
| F9 | 実機で CD ドライブが検出されているかは未確認 (セカンダリは `bank1 probe=0x50`、`drive2/3 identify=-2` = ATAPI なら ATA IDENTIFY は断るので整合)。`dev` / `ls /cd0` をシリアルで依頼中 | 起動画面 |

## 1. 方針 v2 (ラリー 1 の合意を反映)

**原則**: (1) 幾何は 2 種類 — ATA 直叩きは**ドライブの現在の変換** (F13)、区画表と IPL の CHS は **BIOS 幾何**。(2) 区画表は **PC-98 標準配置** (F10) に直す。(3) **ディスクへの最初の書き込みの前に、成立条件を全部検査**し、1 つでも欠けたら 1 バイトも書かない。

### 段 0 — 実機の計測 (書き込み無し。段 1 の前に必ず)

1. FD ローダ (`loader_fat_new.asm`) が **INT 1Bh AH=84h** を DA=80h/81h に呼び、CF・AH・BX・CX・DH・DL を記録。置き場は**新設のブート情報域** (候補 0x8A000〜0x8BFFF、`include/memmap.h` に定義、`tools/gen_memmap.py` の表へ)。形式は magic + version + 反転チェック語 + drive ごとの valid + 取得元。カーネルは `kernel_main` の**フォント初期化より前**に写して保存し、以後その値だけを使う。magic が合わなければ「BIOS 幾何なし」。
2. カーネルは IDENTIFY の word 1/3/6 (既定)、word 53 と 54-58 (現在)、word 49 bit9 (LBA)、word 60-61 (総数) を表示。
3. 出力 `[hdd] bios da=80 cf=0 len=512 C/H/S=… / ata def=… cur=… lba=1 total=…` を実機で記録 (受入 H3)。**段 1 の設計値はこの結果で確定する** (8/17 か 16/63 か、既定 = 現在か)。

#### 段 0 の実装メモ (2026-09-23、wt/hdd-stage0)

- **番地**: `MEM_BOOTINFO_BASE` = 0x7E00、予約 256B (0x7E00〜0x7EFF、`include/memmap.h`)。使うのは先頭 0x30B。
  地図 (`docs/02_memory.md` §2-1) ではフォントキャッシュの内側の帯として載る。NASM 側の写しは `boot/bootinfo.inc`
  (番地は `gen_memmap.py --check`、オフセットは `make check-bootinfo-host` が照合)。
- **形式** (`include/bootinfo.h`、リトルエンディアン):

  | オフセット | 大きさ | 中身 |
  |---|---|---|
  | +0x00 | u32 | magic `0x49544F42` ('BOTI') |
  | +0x04 | u16 | version = 1 |
  | +0x06 | u8 | 取得元 1 = FD ローダ / 2 = HDD ローダ |
  | +0x07 | u8 | ドライブ数 = 2 |
  | +0x08 / +0x18 | 16B × 2 | ドライブ記録 (DA 80h → [0]、81h → [1]): +0 DA, +1 valid, +2 CF, +3 AH, +4 BX (u16), +6 CX (u16), +8 DH, +9 DL, +10 queried, +11〜15 0 |
  | +0x28 | u16 | ドライブ記録 (+0x08〜+0x27) のバイト和 |
  | +0x2A | u16 | 0 |
  | +0x2C | u32 | 反転チェック語 = ~(magic ^ version) = `0xB6ABB0BC`。**最後に書く** |

  valid の規則 (ローダ・カーネル共通): CF=0、BX=512、CX≠0、DH≠0、DL≠0。カーネルはさらに queried=1、DA ∈ {80h, 81h}、
  ローダの valid=1 を要求する。
- **ローダ**: 手続きは `boot/bootinfo_rm.inc` の 1 つ (`bi_clear` → `bi_sense` → `bi_seal`) を両方が `%include`。
  FD ローダは 80h と 81h、HDD ローダは IPL から受けた DA だけを問い合わせる。HDD ローダは AH=84h の DH/DL が
  IPL の heads/SPT と違えば `HDD geom mismatch: IPL H/S=hh/ss BIOS(84h) H/S=hh/ss` (6 行目) を出して止まる。
  AH=84h が使えない答えなら比べられないので `BIOS sense (84h) unusable: geometry not checked` (7 行目) を出して進む。
  読みの INT 1Bh は CF を 0x7F10 (CF)・0x7F11 (AH) に残し、CF=1 なら `HDD read error (INT 1Bh CF=1) AH=xx LBA=xxxxxxxx`
  で止まる (F14)。`ext2_mini` は `MAX_IMAGE_SIZE` 超を切り詰めずにエラーにし、ローダは
  `vmkernel.lz4 too large (> 508KiB)` で止まる (N8)。
- **カーネル**: `kernel_main` の最初の文で `bootinfo_capture()` (写して検証し、低位の magic を 0 に戻す)。
  `bootinfo_hdd_geom(da, &cyl, &heads, &spt, &seclen)` が保存値を返す。IDE の初期化の直後に `bootinfo_report()`:

  ```
  [hdd] bios da=80 cf=0 ah=00 len=512 C/H/S=16382/16/63 src=fd
  [hdd] bios da=81 cf=1 ah=60 len=0 C/H/S=0/0/0 src=fd (unusable)
  [hdd] ata0 def=16382/16/63 cur=16382/16/63(valid) lba=1 total=16514063
  ```

  情報域が無効なら `[hdd] bios geom: none (magic xxxxxxxx err=-N)`。HDD ローダが問い合わせなかった方は出さない。
  ATA の行は `ataN` (N = IDE ドライブ番号 0/1)。IDENTIFY の word 1/3/6・49・53・54-56・60-61 は `ide_get_geom()`
  (`IdeGeom`) に持つ。KAPI の `IdeInfo` (96B) は変えていない。I/O の CHS 変換も変えていない (LBA28 は段 1)。
  (上の数値は例。実機の値は受入 H3 で記録する)

### 段 1 — 一時置き場 (FD 起動のまま)

#### 段 1 の実装メモ (2026-09-24、wt/hdd-stage1、KAPI v64)

- **区画表の共有部**: `drivers/pc98pt.h` / `pc98pt.c` (純粋関数、型は C の素の型でローダでも組める)。
  標準配置 (+8/+9/+10-11 開始、+12/+13/+14-15 終了)、区画はシリンダ単位 (終わり = (終了シリンダ + 1) ×
  heads × spt、終了ヘッド・セクタは読まない)。OS32 の区画 = sid 0xE2 の最初の項目。読み手は
  `fs/ext2_super.c` (`ext2_find_partition`)・`boot/boot_main.c` (+ `boot_debug.c`)・`fs/fatfs_vfs.c`
  (`PC98PartEntry` を共有、`pc98pt_get` で読む)・`userland/shell/cmd_hdprep.c`。ホスト側は
  `tools/pc98pt.py` (C と 1 バイトずつ突き合わせる)。cdinst / install の書き手は段 2 で標準配置に移した
  (下の「段 2 の実装メモ」)。
- **`ext2_find_partition(drive, &start, &len)`**: 失敗 (`EXT2_ERR_IO` / `EXT2_ERR_NOPART` = -13) を返す。
  1088 のフォールバックは廃止。CHS → LBA は `bootinfo_part_geom` (DA 80h/81h の BIOS 幾何、無ければ
  IDENTIFY の既定)、終わりは IDENTIFY の総数の内側。マウントは FS が区画より大きければ断り、
  `Ext2Ctx.part_len` でブロック I/O が区画の外を断る。`ext2_format` は区画の長さで頭打ち。
- **ATA**: `drivers/ide_addr.c` が方式を決める (word 49 bit9 → LBA28、word 53 bit0 → 現在の CHS、
  どちらも無ければ既定の CHS)。範囲外は `IDE_ERR_RANGE` (-4)。`drivers/dev.c` の hd0-3 は LBA の
  API (`ide_read_sectors`) へ委譲し、CHS の変換はここに無くなった。`dev_blk_*_lba` は `lba + count` の
  桁あふれを断る。
- **KAPI v64** (slot 230〜233): `ext2_format_at` / `dev_mount_count` / `sys_umount_checked` /
  `hdd_geom_info` (表示と断る条件に BIOS 幾何と ATA の申告が要るので 4 本目を足した)。
- **固定点**: `fs/ext2_layout.c`。最終グループの必要量 = sparse の SB + GDT + bitmap 2 + inode 表
  (+ group 0 のルート 1)。lost+found は formatter が作らないので数えない。
- **hdprep**: `userland/shell/cmd_hdprep.c` + `hdprep_plan.c` (純粋)。開始 = 1632 以上の最初の BIOS
  シリンダ境界、長さ = 指定 (既定 256MiB) をシリンダへ切り下げ、上限は BIOS の CX × シリンダと IDENTIFY の
  総数の小さい方。`yes` は打鍵で読む (rshell の `/api/cmd` からは答えられない)。
- **migrate-pt**: `tools/nhd_deploy.py migrate-pt` / `make nhd-migrate-pt` (08_build.md §8-4)。
- **実装レビュー往復 1 (Codex C1〜C4 / Opus M1・M2・m2〜m5) で足したもの**:
  migrate-pt は全部の検査 (ローダ ≤ 8KiB・カーネル ≤ 508KiB・表・1KiB ブロックの ext2・push の来歴) を
  **最初の書き込みの前**に行う (`migrate_preflight`)。`hdprep` は IDENTIFY の総数 0 を計画の段階で断り、
  上限を BIOS 幾何・総数・ATA の方式 (LBA28 の 2^28 / 現在の CHS の容量) の最小にする。`ext2_format_at` は
  範囲全体を `ide_range_ok` で照合する。`ext2_format` は 32 グループへ頭打ち (断らない) にし、区画の位置を
  BIOS 幾何で決められないときは書かない。マウントはどちらの幾何を使ったかを出し、旧配置の表を見つけたら
  「migrate-pt が要る」と出す。`fatfs_vfs.c` の区画走査も `bootinfo_part_geom` を使う。ホストの
  `deploy` / `sync-from-hostdrv` / `sync` は旧配置の NHD に v64 以降のカーネルを配らない (`legacy_pt_guard`)。
- **ラリー 2 (Codex 1〜3 / Opus a・c)**: `legacy_pt_guard` は「旧配置か」(`classify_pt_layout`、カーネルと
  同じく sid 0xE2 の最初の項目) と「自動で移行できるか」(`plan_migrate_pt`) を分け、旧配置・OS32 項目なし・
  壊れた項目はどれも断る。通すのは NHD のヘッダが無いファイル (警告) とファイルが無いときだけで、読めない・切り詰め・移行の可否の調べの例外 (OSError を含む) は断る (ラリー 3)。 push (`do_deploy`) はヘッダの無い・0 バイトの NHD も断る (Opus ラリー 3)。sync / sync-from-hostdrv の
  マウントは `ensure_mounted_for_kernel` で、取り込みの後・losetup の前に門を通す。ext2 の書き込み範囲は
  入口に関係なく `ext2_format_range` の入口で `ide_range_ok` と照合する。変異試験に恒等の対照を足した。
- **組み合わせの危険** (08_build.md §8-4): 旧配置の NHD + v64 のカーネル (HostDrv + `hsync boot` で起きる) は
  `/` がマウントできない。v63 以前のカーネル + 標準配置の表は、OS32 項目 (シリンダ 12) を **LBA 12** と読み、
  `format 0` がローダと ext2 を壊す。
- 試験: `make check-hdd-stage1-host` (`tools/tests/test_hdd_stage1.py`、記録 `tools/tests/hdd_stage1_tdd.md`)。
  既存の ext2 の RAM ディスク試験 5 本は LBA 1 に区画表を置く足場 `tools/tests/hdd_pt_fake.h` に乗せた。



4. **区画表の読み書きを 1 つの共有部に集約**し、標準配置で読み書きする: `ext2_find_partition`、`boot/boot_main.c`、cdinst、install、`tools/nhd_deploy.py`、fatfs の読み手と同じ struct を使う。**同じコミット**で揃える。既存 NHD は `make deploy-kernel` の区画表書き直しで移行する (H2 で確認)。旧配置を読む互換はしない (実機に OS32 の旧配置の区画は存在しない)。
5. **区画の探索は失敗を返す**: `ext2_find_partition` の 1088 フォールバックを廃止し、(start, length) を返す。format は検証済みの (start, length) だけを受け、その範囲外に書かない。
6. **大きさは既存の上限内**: 段 1 の既定は **256MiB 以下** (ext2 32 グループ)。最終グループがメタデータ (bitmap 2 + inode 表) を収められない長さは切り下げる。2GB への拡張 (`EXT2_MAX_GROUPS` を上げる、format の進捗表示) は**別票**。
7. **区画作成 `hdprep`** (シェル組込み、hd0 = DA 80h 専用、他は断る): 書く前に表示と検査 — 対象ドライブ・BIOS/ATA 幾何・書く LBA 範囲・LBA 0/1 の生の中身。**断る条件**: BIOS 幾何なし / BX≠512 / ATA の既定≠現在 (F13、ide.c を現在の変換か LBA28 に直すまで) / LBA 1 に空でない区画項目 or LBA 0 に 55AA (`--force` でも既存区画があれば断る。今回は空のディスク専用) / `/hd0` がマウント中 (先に umount し戻り値を見る)。実行は**ユーザーの [D2] 承認 + `yes` 入力**。書く前に予定域の末尾シリンダへ 1 セクタ write→readback の探り、区画表を書いたら読み戻して比較、format 後にマウント → 書き → 読み戻し。区画表は**最後に**書く (format 失敗で区画表だけ残らない)。
8. 起動時の `/hd0` マウントは段 1 の区画で働く (FD 起動時)。

### 段 2 — インストーラ

9. cdinst / install を段 1 の共有部 (区画表・幾何・検査) に乗せる。IPL の `[8]/[9]` には段 0 の BIOS 幾何を書く。区画開始 = LBA 1632 以上の最初の BIOS シリンダ境界 (8/17 なら 1632、16/63 なら 2016)。
10. 既存区画の再利用は**今回しない** (区画表を 2 回書く現行経路、`/sys` の mkdir 失敗、上書きの扱いが未整理 — Codex B7)。インストールは段 1 の区画を**作り直す** (その旨を確認画面で表示)。一時置き場のデータは失われることを明記。
11. 事前検査: パッケージの必須内容・ローダ 8192B 以下・展開先の容量。追加パッケージ・sync の失敗で「完了」と言わない。

#### 段 2 の実装メモ (2026-09-24、wt/hdd-stage2、KAPI 変更なし)

- **共有部**: `userland/system/inst_disk.c` (純粋: モードの判定・媒体の大きさ・容量の見積もり・IPL と区画表の
  組み立て) と `inst_hdd.c` (KAPI で hd0 を検査して書く手順)。cdinst と install の両方がこれを繋ぐ
  (`build/programs.mk` の `INST_OBJ`)。幾何と計画は hdprep と同じ `hdprep_plan.c`、区画表は `pc98pt.c`、
  ext2 の配置は `fs/ext2_layout.c` をそのまま組む (写さない)。
- **計画**: `hdprep_plan(g, 256)` — 開始 = 1632 以上の最初の BIOS シリンダ境界、長さ = 256MiB を
  シリンダへ切り下げ (上限は BIOS 幾何・IDENTIFY の総数・ATA の方式の最小)。区画表と IPL の [8]/[9] は
  BIOS 幾何 (旧 install が IPL に書いた IDENTIFY の幾何は使わない、F15)。
- **モード** (`inst_classify`): 区画項目 0 で LBA 0 に 55AA 無し = 空。項目がちょうど 1 つで sid 0xE2・名前
  "OS32" (後ろは空白か NUL)・開始 = 計画の開始なら**再作成**。**判断**: 旧配置の OS32 の 1 項目も、旧配置で
  読んだ開始が計画の開始と同じとき (= 8/17 の NHD、シリンダ 12 = LBA 1632) は再作成の対象にした (PM の推奨、
  これまでの CD / FD インストールで作った NHD を入れ直せる)。16/63 ではシリンダ 12 は LBA 12,096 で
  期待値 2016 と違うので断る (実機に旧配置は無い)。それ以外 (未知の区画・2 項目以上・開始違い・
  どちらの配置でも読めない項目・空の表で 55AA) は断る。再作成では確認画面に「一時置き場 (/hd0) の
  ファイルは全部消える」と出す。
- **順序** (R3-1): 全検査 (媒体 → hd0 の幾何・モード・マウント → 大きさ・容量) → 表示 → 承認 (y) →
  `inst_hdd_release` (hd0 がマウント中なら `sys_umount_checked("/hd0")`、その後 `dev_mount_count(0)` が 0 で
  なければ断る — ここまでは 1 セクタも書かない) → `ext2_format_at` → 区画表 (項目 0 だけ、他は 0) →
  読み戻し比較 → `sys_mount("/hd0")` → ローダ (LBA 2〜、512 B ずつ書いて読み戻し) → IPL (LBA 0) →
  ディレクトリ → 展開 → sync。書いた後の失敗は `INCOMPLETE:` と出し、「完了」は出さない。
  ローダと IPL をマウントの確認の**後**に書くので、どこで止まっても次の実行は通る (format の失敗なら
  空のディスク、区画表の後なら再作成)。
- **事前検査**: cdinst は選んだ型のパッケージを全部 `pkg_parse` し、前置 (/hd0) の溢れ・MINIMAL の
  `/boot/vmkernel.lz4` と `/sys/shell.bin`・BOOT.PKG の boot_hdd.bin / loader_hdd.bin (無圧縮)・大きさ・容量を
  書く前に見る (以前は展開の途中で溢れに気付いた)。install は FD を書く前に一度列挙して数える (列挙の
  失敗もここで分かる)。容量は `ext2_layout_plan` から空き (ブロック・inode) を出し、ファイルごとの
  データ + 間接ブロック、ディレクトリ 1 ブロック、項目 16 件に 1 ブロック、余白 256 ブロックと比べる。
  空きの計算は実物の `ext2_format_at` の像の dumpe2fs と一致する (試験)。
- **ext2_mini の上限** (N8) と **容量表示の 32 ビットの溢れ** (`ide.c` の `size_mb`) は段 0 で直っていた
  (431822e)。この段では ext2_mini に試験を付けた。
- **使わなくなった経路**: cdinst / install は `ext2_format` (区画表を読んで位置を決める)・`ide_write_sectors`・
  `sys_umount` (void) を呼ばない。HDD の自動検出 (`dev_get_info` の hd*) もやめ、hd0 = DA 80h だけを扱う。
- 試験: `make check-hdd-stage2-host` (`tools/tests/test_hdd_stage2.py`、記録 `tools/tests/hdd_stage2_tdd.md`)、
  `make check-install-fresh-host` (install.c の段 2 のケース 6 本)。
- **実装レビュー往復 1 (Codex P1-1〜3・P2-4〜6 / Fable minor) で足したもの**:
  - cdinst は書く前に各 PKG の**データ部**を表と突き合わせる (項目の大きさの和 = orig_size、無圧縮なら
    comp_size = orig_size、PKG の長さ = データ部の先頭 + comp_size)。必須 (vmkernel.lz4・shell.bin) は空でも断る。
  - パスは要素ごとに見る (`inst_check_path`): 絶対パス・空 / `.` / `..` の要素なし・`/hd0` と合わせて
    32 要素 (VFS_MAX_PATH_DEPTH) 以内。事前検査と展開の直前の 2 か所。
  - install は FD の `/sys/shell.bin` も必須にし、4 本とも「通常のファイル・空でない」を見る (cdinst と同じ規則)。
  - 1 ファイルの上限 (ext2 1KiB ブロックで二重間接まで、67,383,296 B) を超える項目は容量の検査で断る。
    自動で作る親ディレクトリの分として inode に 128 の余白を持つ。
  - hd0 を外すのは `/hd0` に hd0 が 1 つだけマウントされているときだけ。別の prefix にもあれば**承認の前に**断る。
  - **INCOMPLETE の後は再起動してから入れ直す** (同じ起動のまま再実行すると ext2 の fs_error などで通らない
    ことがある)。区画表の読み戻しが違った場合と、次の実行が区画表を断った場合は、**ゲストからは直せない**
    旨とホスト側の手当てを出す: NP21/W は NP21/W を止めて `make nhd-init` (`tools/nhd_deploy.py init`、
    NHD を作り直す)、実機は OS32 の外の道具で LBA 1 を消す。
- **NP21/W で見つかった落ち (628c61f、2026-09-24)**: cdinst が hd0 の検査の `vfs_devname("/")` の返り値を読んで
  CPL=3 の fault kill。`fs/vfs.c` の `vfs_devname` はカーネル帯のマウント表の `dev_name` をそのまま返し、
  そこに USER ビットは無い。常駐シェル (CPL=0) の hdprep では出ず、ホスト試験の贋物は利用者の文字列を返すので
  見えなかった。`sys_getcwd` と同じ手で直した: `sdk/kapi.json` の target を `vfs_devname_user` (exec/exec.c、
  トランポリンページの写しを返す) に差し替え。スロット・引数・戻り型・版は不変 (KAPI_SPEC.md に注記)。
  `sh.bin` の hdprep / filer も同じ経路で直る。あわせて `build/app.conf` の cdinst / install の版を 64 にした
  (v63 以前のカーネルで予約スロットを呼ばない)。`path_get_drive` / `path_get_cwd` も target が素のままで、
  同じ種類の潜在不具合の疑いがある (インストーラは呼ばない。未確認・未修正)。
  → 往復 2 で直した (下)。
- **NP21/W の確認 (c76da0b、PM、2026-09-24)**: 空の NHD → CD から Normal → HDD 起動 → kselftest 212/0 →
  ホストで `e2fsck -fn` clean。
- **実装レビュー往復 2 (Codex P1-1・P1-2 / Fable minor) で足したもの**:
  - cdinst の事前検査は項目の型を見る: ファイル・ディレクトリ以外の型は断る (`pkg_extract` は黙って飛ばす
    ので、型 2 の `/sys/shell.bin` が必須として通って展開されなかった)。展開の直前にも同じ検査。
  - 必須 (vmkernel.lz4・shell.bin) は**展開順にたどった最終の大きさ**で判定する (MINIMAL → GUI → NORMAL →
    DEBUG、PKG の中は項目の順。後の同じパスの項目が O_TRUNC で置き換える — NORMAL の大きさ 0 の shell や
    同じ PKG の中の重複が「完了」になっていた)。展開の後にも `/hd0` の実物をその大きさと突き合わせ、違えば
    INCOMPLETE。install (FD) は FAT の名前が一意で後からの上書きは無いが、写した `/hd0/sys/shell.bin` を
    同じく突き合わせる。
  - `path_get_drive` / `path_get_cwd` も `vfs_devname` と同じ形で直した (`exec/exec.c` の `*_user`、target の
    差し替え、版は据え置き)。kselftest `test_tramp_user_str` に `vfs_devname("/")` と `path_get_*` の返り番地が
    トランポリンの写しであることの 2 項を足した (**kselftest は 212 → 214 項**)。str-return guard は
    `const char *` を返す KAPI の全部と userland/ の全 C ソースを見る。
  - 他の OS の区画 (未知の sid・2 項目以上・空の表で 55AA) には「このディスクは対象外」と出し、表を消せとは
    言わない。ホスト側の手当て (`make nhd-init`) は OS32 の項目が中途半端なとき (開始違い・壊れ) と区画表の
    読み戻しが違ったときだけ出す。

#### 段 2 の追補: 明示の消去 `ERASE` (N4 の例外、2026-09-25、wt/cdinst-wipe、KAPI 変更なし)

実機 Ra266 の 8GB は前の OS (MS-DOS / Windows と思われる) の区画表が残っていて、cdinst が
`Refused: hd0 has a partition that OS32 did not create (code -40)` で止まった (幾何と ATA の検査は通過)。
ユーザーは中身を捨ててよいので強制的に消したい。N4 の安全側の既定 (断る) は変えず、**明示の打鍵でだけ**
消す道を共通部 `inst_hdd.c` に足した (cdinst・install の両方が同じ規則)。

**順序 (Codex レビュー往復 1 の後の PM 決定)**: 全検査 → 確認画面と `y/N` → `ERASE` の打鍵 → 消去 → format → …。
最初の実装は hd0 の検査の途中で消していたので、媒体の大きさや容量の不足が分かっているのに区画表だけを失う
事態があり得た。今は表が使えないディスクでも**消した後の空のディスクとみなして**大きさ・容量・マウントの
検査と確認画面まで進み、`y` の後に `ERASE` を求め、そこで初めて消す。`ERASE` 以外なら 1 セクタも書かない
(umount の前に終える)。保証の範囲 (Codex 往復 2): **`y` と `ERASE` の行を受ける前は何も書かない**、
**umount とマウント数の検査が通る前は消去も format も始めない**。umount (`fs/vfs.c` `vfs_umount`) は外す前に
`ops->sync()` を呼び、ext2 の dirty なメタデータ (`fs/ext2_super.c` のスーパーブロック・グループ記述子) を
書き出し得るので、消去が hd0 への最初の書き込みとは言い切れない — インストーラ**自身の**最初の書き込み。

- **いつ聞くか**: `inst_classify` が FOREIGN (-40)・MULTI (-41)・`HDPREP_E_MBR_SIG`・BROKEN (-43)・
  START (-42) を返したときだけ (`InstTarget.erase_needed`、理由は `erase_code`)。空のディスク・再作成では
  聞かない。幾何・ATA の範囲・計画の検査は今までのものをそのまま先に通す (16/63、総数 16514063 なら開始 2016)。
- **検査の表示**: `hd0 cannot be used as it is: <理由> (code N)` に続けて、他の OS の区画 (FOREIGN・MULTI・55AA)
  なら「hd0 holds another system's partitions or boot code … the only way onto this disk is to ERASE its whole
  partition table (asked after the confirmation below)」、中途半端な OS32 の項目 (開始違い・壊れ) なら今までの
  ホスト側の手当て (`make nhd-init`)。その後に `Current contents of hd0:` の要約 — LBA 0 の先頭 4 バイトと
  55AA の有無、LBA 1 の項目の数、使っている各項目の mid・sid・名前 (表示できない字は `.`)・開始と終了のシリンダ
  (C/H/S も生のまま)・BIOS 幾何で読んだ LBA の範囲 (読めなければ `not a valid range`)。写真で何が入っていたか
  分かるようにする。最後に「Nothing is erased unless you answer y at the confirmation below and then type ERASE」。
- **マウントの検査**: ルートが hd0、または `/hd0` 以外にマウントされていれば、確認画面より前に今までどおり
  断る。`/hd0` にだけマウントされていれば確認画面に `will be unmounted first` と出し、`ERASE` を受けた**後**に
  `sys_umount_checked("/hd0")` で外し、`dev_mount_count(0)` が 0 でなければ消さずに断る。`ERASE` の入力を待つ
  間に新しくマウントされた・`/hd0` の相手が替わった場合もここで分かる。断りの表示は umount を呼んだかで分ける:
  umount の前 (`/hd0 no longer holds hd0`、外す予定の無いマウントが増えた) は `Nothing was written`、umount の
  後 (`umount /hd0 failed`、外したのに `still mounted`) は `Nothing was erased or formatted. (Unmounting may
  have flushed hd0's file system data, as any umount does.)` (Codex 往復 2 の P2: 失敗した umount も sync の
  途中で書いた可能性がある)。通常のインストール (ERASE でない) の umount の断りも同じ表示。
- **確認画面**: `Target: hd0 …, empty disk: create the OS32 area` の後に「hd0 holds another system's partitions
  or a table OS32 cannot use (<理由>, code N). After y, type ERASE to erase the partition table (LBA 0 and 1)
  and install onto hd0 as an empty disk. EVERYTHING ON hd0 WILL BE LOST. Anything but ERASE writes nothing.」。
- **打鍵**: `y` の後に `Erase hd0's partition table? Type ERASE:` に 1 行。`ERASE` (大文字 5 文字、完全一致) +
  Enter (CR・LF・CRLF) だけが通る。空行・小文字・前後の空白・`y`・BS や **NUL** などの制御文字を含む行・ESC・
  15 文字を超える行は `Not erased. Nothing was written.` で終える (1 セクタも書かない)。
- **`y` の行末 (Codex 往復 2 の P2)**: `y/N` は 1 字で決まるので、端末が `y` と一緒に送る CR / LF / CRLF や
  人が `y` の後に押した Enter が残り、それを ERASE の空行と読むと「`y` + Enter → `ERASE` + Enter」が必ず
  取り消しになっていた (シリアルの行送信)。規則: **`y` の後の最初の行末 1 つは `y` の行末として映さずに読み
  捨てる** (`ih_read_line` の `skip_eol`、最初の 1 字だけ)。「もう届いている分を読み捨て、後から届く分は
  ERASE の行の先頭の空行として 1 回だけ無視する」のと結果が同じで、到着の時刻に左右されない一つの規則に
  した。行末以外 (字・NUL・ESC) が先なら何も捨てない (NUL は入力のまま)。2 つめの行末は空行 = 取り消し
  (`y` の後に Enter を押していない人は Enter を 2 回押して取り消す: 安全側)。
- **`[0-3]` の選択の行末 (Codex 往復 3 の P2)**: cdinst の選択も 1 字で決まるので、「`1` + Enter」の Enter が
  `Continue? [y/N]` の答え (= 取り消し) になり、行送信では ERASE に届かなかった。同じ規則で、Continue の最初の
  1 字が行末なら選択の行末として 1 回だけ捨てる (`inst_hdd_getkey_after_key`)。2 つめの行末は取り消し、
  NUL・ESC・`n` などは捨てずに答え (取り消し)。install には選択が無いので `y/N` は今までどおり。
- **鍵の読み方 (Codex P1・P2)**: `kbd_trygetchar` / `serial_trygetchar` は「入力なし」を -1、受けた NUL を 0 で
  返す。最初の実装は 0 を読み捨てていたので、シリアルから `ERA<NUL>SE<CR>` が届くと消えた。共通の
  `inst_hdd_getkey` は 0 以上をすべて入力として渡し、NUL を含む行は不一致にする。CR の直後の LF は 1 つの
  行末の一部として捨てる (もう届いていれば行読みが、後から届けば次の読みが) ので、CRLF の Enter が次の
  問いに持ち越されない。cdinst の `[0-3]` と `y/N`、install の `y/N` も同じ読み手を使う。
- **消す**: `ERASE` の後、マウントを外して LBA 0 → LBA 1 を 0 の 512 B で書き、それぞれ読み戻して全部 0 を
  確かめる (`LBA 0 and 1 of hd0 erased and verified (all zero)`)。以後は空のディスクとして format → 区画表 → …。
- **消した後に止まったとき**: format が失敗すると `INCOMPLETE: ext2_format_at failed` と「hd0's partition table
  was ERASED (LBA 0 and 1 are zero). Run the installer again: it installs onto hd0 as an empty disk.」を出す。
  区画表を書いた後の失敗 (マウント・展開・sync) は今までどおり「再起動して入れ直せば OS32 の区域を作り直す」
  だけを出す (次の実行は再作成モードになるので「空のディスク」とは言わない)。消す書き込みか読み戻しが
  失敗したら `INCOMPLETE: erasing LBA 0 and 1 of hd0 failed` と「もう一度実行して y と ERASE を打つ」を出す。
  `y/N` で断る・大きさや容量で断るのは消す**前**なので、`Nothing was written` のまま。
- **中途半端な OS32 の項目** (開始違い・壊れ) と区画表の読み戻しの違いの案内は「次の実行で y の後に ERASE を
  打つ」を先に出し、ホスト側の手当て (`make nhd-init`) も残す。
- 試験: `tools/tests/test_hdd_stage2.py` の cdinst `erase` / `erase_fail` / `erase_mount` / `keys` と install の
  同名 3 本。贋の鍵は**長さ付きのバイト列** (NUL も 1 バイト)、尽きたら -1、kbd と serial を交互に、鍵の間に
  「入力なし」を挟み、鍵を渡す贋物の側で write・format・umount が 0 回であることを見る (消すのが打鍵より
  前なら贋物が落ちる)。FOREIGN・MULTI・55AA・BROKEN・START × 8/17・16/63 × ERASE でない 17 通り (NUL 入り
  5 通りを含む) で 1 セクタも書かず、CR・LF・CRLF の 3 通りで通り、`N`・大きさ・容量は消す前に止まり、
  ERASE の入力中にマウントされる・`/hd0` の相手が替わると消さず、読み戻しは先頭側と 511 バイト目の両方、
  LBA ごとの write / read の失敗で後続の書き込みが止まる。`y` の行末: `y\r\n`・`y\r`・`y\n`・`y` の後の
  ERASE + CR・LF・CRLF で消去まで進み、`y` の行末の後に Enter だけ (7 通り) は取り消し — kbd / serial ×
  鍵の間の「入力なし」0〜2 (もう届いている / 後から届く)。umount の贋物は `inj_umount_sync` で「外す前の
  sync が書く」を数え、umount の後の断りが `Nothing was written` と言わないこと、umount の前の断りは
  言うことを見る。cdinst の選択: `1\r\n`・`1\r`・`1\n`・`1` × `y\r\n`・`y\n`・`y` × ERASE + CR・LF・CRLF で
  消去まで、選択の行末の後に Enter だけ・`n`・ESC・NUL (12 通り) は取り消し、空のディスクは `1\r\ny\r\n` で
  入る — kbd / serial × 「入力なし」0〜2。変異は同じ `--mutate` (128 本)。

### 段 3 — CD インストール → HDD 起動

12. HDD ローダは INT 1Bh ごとに CF を検査し、失敗を画面に出して止まる (F14)。
13. hd0 (DA 80h) 起動のみ。hd1 は断る。

## 1-v3. ラリー 2 (N1〜N8) による改訂 — §1 の該当項をこの節で置き換える

- **N1 ブート情報域の置き場** (段 0-1 を置換): **0x7E00〜0x7EFF (256B)**。根拠: 実モードのスタックは 0x7C00 から下へ、ローダは 0x8000〜、`loader_hdd` の受け渡しは 0x7F00〜0x7F0F、圧縮イメージは 0x10000〜0x8EFFF (`boot_defs.h:45-49`)、PM の ESP は 0x9FFFC、カーネル本体は 0x100000。フォントキャッシュ (0x1000〜) は `kernel_main` のフォント初期化で上書きするので、**`kernel_main` の最初 (フォント・ヒープより前) で写す**。実装者はローダ 2 本のバッファ・スタックとの非重複を `.map` / ソースで確かめて報告する。`include/memmap.h` に定義し `tools/gen_memmap.py` の表へ。
- **N2 HDD 起動の生成経路** (段 0-1 に追加): **FD ローダと HDD ローダ (`loader_hdd.asm` の実モード部) の両方**が、起動のたびに情報域を**まず無効 (magic=0) で初期化**してから AH=84h を呼び、成功時だけ valid を立てる。残留した前回の値を受け入れる経路は無い。HDD ローダは、IPL から受けた heads/SPT と AH=84h の値が食い違えば画面に出して**止まる** (IPL に焼いた値の陳腐化の検出)。
- **N3 NHD の移行** (段 1-4 を置換): `tools/nhd_deploy.py` に**移行の 1 操作** (`migrate-pt`) を足す — NP21/W 停止中 ([D1]) に、旧配置の区画表を読み、**同じ開始 LBA・長さ**を標準配置で書き直し、第二段ローダ (LBA 2〜) とカーネルを**同時に**配備する。`make deploy-kernel` だけでは移行しない旨を 08_build.md に書く。H2 は `migrate-pt` → 起動 → マウント → 既存ファイルの md5。
- **N4 モードの分離** (段 1-7 / 段 2-10 を置換): `hdprep` = **空のディスク専用** (区画項目が 1 つでもあれば断る)。インストーラ = **再作成モード**: 区画表の項目が**ちょうど 1 つ**で、それが OS32 が作ったもの (sys_id = ext2 用の値かつ名前 `OS32`、開始 = 期待値) のときだけ、承認後に作り直す。それ以外 (未知の区画、2 つ以上) は断る。**例外 (2026-09-25、ユーザー指示)**: 断る表のときだけ要約を出し、全検査と `y` の後に `ERASE` の打鍵を受けたら LBA 0/1 を 0 にして空のディスクとして入れる (段 2 の追補「明示の消去 `ERASE`」)。既定は断るまま。
- **R3-1 (ユーザー決裁 B)**: 下の N5 の手順の順序を「全検査 → `ext2_format_at` → **区画表を書く → 読み戻し比較 → 通常マウントで確認**」に改める。確認失敗は「未完了」と表示して止める (空のディスクが相手なので失うデータは無い)。
- **N5 format の新しい入口**: KAPI を**追記** `ext2_format_at(drive, start_lba, length)` (既存 `ext2_format` は変えない、[ABI2])。区画表を読まずに与えられた範囲だけに書き、範囲がディスク総数を超えれば断る。hdprep / インストーラは「全検査 → `ext2_format_at` → マウント確認 → **区画表を最後に**書く → 読み戻し比較」。KAPI 版はキーボード修正の v62 の次 (v63)。
- **N6 使用中の検査**: KAPI を**追記** `dev_mount_count(drive)` (その物理デバイスがどの prefix でマウントされているか・root かを数える) と `sys_umount_checked(prefix)` (sync の失敗を含めて int を返す。既存 `sys_umount` の void は変えない)。hdprep / インストーラは hd0 のマウントが 1 つでもあれば umount_checked し、失敗・root なら断る。
- **N7 最終グループ** (段 1-6 を置換): 最終グループの必要量 = **sparse の SB・GDT (予約 GDT 含む) + bitmap 2 + inode 表** (group 0 はさらに root / lost+found の初期ブロック)。切り下げ後にグループ数・inode 数を**再計算して再判定**し、成立する長さになるまで繰り返す (固定点)。判定関数はホスト試験 (H1) の対象。
- **N8 圧縮イメージの上限**: インストーラの事前検査に `vmkernel.lz4 ≤ MAX_IMAGE_SIZE (508KiB)`。`boot/ext2_mini.c` は上限を超えるファイルを**切り詰めずにエラー**にし、ローダはそれを画面に出して止まる。
- **ATA 側の変換** (Codex の非 blocker「word 53 bit0 が無効な場合」と F13): **ATA I/O は word 49 bit9 (LBA 対応) なら LBA28 で行う** (`ide.c` と `dev.c` の両方の CHS 変換を LBA28 に切り替え)。LBA 非対応なら word 53 bit0 = 1 のときの現在の幾何、どちらも無ければ hdprep / インストーラは書き込みを断る。NP21/W も LBA に対応している (`np21w-src/src/cbus/ideio.c`)。これで「既定≠現在」の問題はドライブ側から消え、残るのは BIOS 幾何 (区画表・IPL) だけになる。
- **F1 の「FAT が無い」**: 断定しない。空判定は段 1 の hdprep が LBA 0/1 の生バイトで行う。
- **受入の追加** (H1/H2/H4): PT 読み取り失敗、情報域の無効 (magic 無し・前回の残留)、別 prefix でのマウント中、再作成モードで未知の区画を断る、format / sync / 追加パッケージの失敗を完了扱いにしない、H4 の「末尾」は実際の割り当て LBA を `dd` で確認。容量表示の 32 ビット溢れ (`ide.c:219`) は総セクタ数から計算し直す。

## 2. ラリー 1 の論点の処理

| 論点 | 出所 | 処理 |
|---|---|---|
| 区画表の配置 | 両者 | 段 1-4 で標準へ。旧配置の互換はしない |
| 256MiB 上限 | 両者 | 段 1 は上限内、拡張は別票 (Fable 案 b = Codex 案) |
| 最終グループ・1088 フォールバック・範囲検査 | Codex | 段 1-5/6 |
| 現在の変換 (word 53-58) | Fable | 段 0-2 で測り、既定≠現在なら書き込みを断る |
| ブート情報域の置き場・偽陽性 | 両者 | 段 0-1 |
| 既存データ・使用中媒体・承認 | 両者 | 段 1-7 (空のディスク専用) |
| 再利用 | Codex | 段 2-10 でしない |
| drive / DA / mount の対応 | 両者 | hd0 = 80h 専用 |
| ローダの CF / 事前検査 / 完了条件 | 両者 | 段 2-11、段 3-12 |
| IPL に幾何を焼く方式の陳腐化 | Fable 非 blocker 4 | 記録のみ (IPL 自身が AH=84h を呼ぶ案は 512B 制約と合わせて段 3 で再検討) |

## 3. 受入 (案 v2)

| ID | 内容 | 場 |
|---|---|---|
| H1 | ホスト試験: 共有部の区画表 (標準配置、`PC98PartEntry` と一致) の書き→読み、8/17 と 16/63、32/33 グループ境界、最終グループ切り下げ、16 ビットシリンダ超過・幾何なし・範囲外の拒否、IPL の値 = 区画計算の幾何 | ホスト |
| H2 | NP21/W: `deploy-kernel` 後の既存 NHD が新配置で起動・マウント、kselftest。FD 起動 (HDD 無し) で `[hdd] bios da=80 cf=0 ah=0f len=0 C/H/S=0/0/0 (unusable)` → 幾何なし → 従来どおり (**NP21/W は未接続でも CF=0 で 0 を返す**、`np21w-src/src/bios/sxsibios.c` の `sasibios_sense`。実機は未確認) | NP21/W |
| H3 | 実機: 段 0 の `[hdd]` 行を記録 | 実機 |
| H4 | 実機: 承認後 `hdprep` → 再起動 → `/hd0` マウント → 書き → 再起動後 md5 一致 (区画の前半と末尾の両方のファイル) | 実機 |
| H5 | 実機: CD インストール → HDD 起動 → kselftest。**インストール後の HDD をホストで `e2fsck -fn` して clean** (TASK_EXT2_EMPTY_NAME の教訓: OS32 で読めることは正しい ext2 の証拠にならない) | 実機 |

## 4. しないこと

8GB 全体・2GB 区画 (別票)、複数区画・既存区画との共存・再利用、hd1 起動、4KB ブロック、LBA 拡張 BIOS。

## 実機の結果 (2026-09-25、PC-9821Ra266、CI e146022)

- **H3**: `[hdd] bios da=80 … C/H/S=16382/16/63`、`ata0 … total=16514063 (8063 MB) io=1` (09-24 に記録済み)。
- **cdinst (Full)**: 初回は `Refused: hd0 has a partition that OS32 did not create (code -40). Nothing was written.` — 以前の OS の区画表が残っていた (中身は不要なディスク)。ユーザーがノート側で先頭のセクタを消してから再実行し、**Installation Complete**。所要は 10 分を超えた (CD の読みが 1 セクタずつ + read_stream ごとのパス解決 → wt/cd-fast で改善中)。
- **HDD 起動 (H5 の前半)**: シリアルで `ver` = `API: v65`、`Commit: e146022`、`Image CRC: 8b9f14c9 (447445 bytes, HDD loader)` (CI の vmkernel.lz4 と大きさ一致)。`/sys/lib/libos32gui.shlib`、`/boot/vmkernel.lz4` 447445 B、`hd0: block 16514063 sects`。
- **未実施**: インストール後の HDD をホストで `e2fsck -fn` (H5 の後半、実機の HDD をホストにつなぐ手段が要る)。kselftest の件数は写真待ち。
- **決めたこと**: 他 OS の区画が残るディスク向けに、打鍵 `ERASE` で LBA 0/1 を消してから入れる道を別票で用意 (wt/cdinst-wipe、N4 の例外)。
