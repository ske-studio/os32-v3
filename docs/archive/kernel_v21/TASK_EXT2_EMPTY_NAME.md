# TASK_EXT2_EMPTY_NAME — NP21/W の NHD のルートに「名前の無いディレクトリ項目」があり、Linux の ext2 が読めない

> 状態: **受入完了 (2026-09-24)** — 修正 fc5ce67 は実装レビュー両者 Approve で着地、NP21/W の CD 新規インストールで作った HDD をホストで e2fsck → clean (73fd0d03)。v2.1 に同梱。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (Claude Code `claude-opus-5-5`、2026-09-23 夜) / それまでの状態: **修正済み・レビュー待ち** (コーダー Opus 5.5、worktree `wt/ext2-empty`、ホスト試験のみ。エミュレータ・実機では未確認)。FS (カーネル層) の欠陥なので POLICY_DEV §1 に沿って優先。
> **明日の実機の CD インストール (TASK_HDD_INSTALL) と同じ経路 (`cdinst`) で作られた NHD で起きている**。

## 事実 (PM、読み取り専用で確認)

- `make nhd-pull` した NP21/W の NHD (`os32.nhd`、最終更新 2026-09-23 17:06) を Linux で ro マウントすると、ルートの各エントリの stat が I/O エラー → `make deploy-kernel` が前提検査で停止 (NHD には書いていない)。
- `e2fsck -n -f`: `Directory inode 2, block #0, offset 132: directory corrupted`。
- ルートのブロック 260 の offset 0x84: **inode 23、rec_len 8、name_len 0、type 2 (ディレクトリ)** — 名前の無い項目。続く 0x8C に inode 43 `db` (rec_len 884)。inode 23 は空のディレクトリ (`.` と `..` だけ)。
- 作成順 (inode 番号) は cdinst の mkdir の並び (`userland/system/cdinst.c:505〜520`: sys 11, boot 12, bin 13, sbin 14, usr 15, usr/bin 16, usr/man 17, etc 18, data 19, home 20, home/user 21, tmp 22) と一致し、**その直後に inode 23 (名前なし)**、後で `db` 43。日時はすべて `30-Mar-2025 15:43` (OS32 のフォーマッタの固定値)。= **この NHD は NP21/W 上の cdinst (CD インストール) で作られ**、cdinst の mkdir の後の段 (パッケージ展開) で**空の最終要素 (末尾が `/` のパスなど) の mkdir** が名前の無いディレクトリを作ったと推定 (未確認)。
- OS32 自身の ext2 はこの項目を読み飛ばすので NP21/W 上では気づかれなかった。Linux (e2fsck) は name_len = 0 を壊れと見なす。
- 2026-09-14 のバックアップ (`build/nhd/os32.nhd.bak-n1-20260914-101620`) は別の配置で、e2fsck は正常。

## 調べること

1. ext2 の mkdir / 項目追加が**空の名前 (長さ 0) を断るか** (`fs/ext2_dir.c`、VFS のパス分解で末尾 `/` や `//` がどう渡るか)。断らないならカーネルの欠陥。
2. cdinst / mkpkg のパッケージ展開で、ディレクトリ項目のパスが `/` で終わるものが渡るか (`tools/mkpkg.py` の出力、`rt/pkg.h`)。
3. 修正後、cdinst → e2fsck -n がきれいに通ることを受入に (ホストで NHD を作って検査できる)。

## 調査結果 (コーダー、2026-09-23)

### 根本原因 (確定)

1. `userland/system/cdinst.c` `install_package_hd()` は PKG のパスへ `/hd0` を前置し、`userland/lib/rt/pkg.c`
   `pkg_extract()` がファイルごとに `ensure_parent_dirs()` を呼ぶ。これはパスの**各 `/` の位置で**切って
   `sys_mkdir` するので、`/hd0/boot/vmkernel.lz4` なら最初に **`sys_mkdir("/hd0")`** になる。
2. `fs/vfs.c` `vfs_mkdir()` → `vfs_resolve_path` は `/hd0` のまま → `vfs_route` がマウント `/hd0` の相対パス **`"/"`** を返す。
3. `fs/ext2_vfs.c` `ext2_vfs_mkdir()` → `ext2_split_path("/")` が親 `"/"` + 名前 **`""`**。
4. `fs/ext2_dir.c` `ext2_mkdir(root, "")` は名前を検査しない。`ext2_find_entry("")` は name_len 0 の項目が無いので NOTFOUND
   → inode・ブロックを割り当てて `ext2_add_entry` が **name_len 0** の項目を書いた。2 回目以降の `mkdir("/hd0")` は
   作った項目自身に一致して EXIST なので 1 個だけ。
5. 並びの一致: cdinst の mkdir 12 本 (inode 11〜22) → MINIMAL.PKG の DIR 項目 (全部 EXIST) → 最初のファイル
   `/boot/vmkernel.lz4` の `ensure_parent_dirs` で **inode 23** (名前なし) → vmkernel.lz4 自身が **inode 24**
   (写しの `ncheck 24` = `/boot/vmkernel.lz4`)。ホストで cdinst と同じ並びを実物の ext2 + pkg.c で再現した像は、
   実 NHD と**同じ `Directory inode 2, block #0, offset 132: directory corrupted`** で e2fsck に落ちる。

PKG 側は無実: `packages/*.PKG` の全項目は先頭 `/`・末尾 `/` 無し・`//` 無し (mkpkg.py の自動ディレクトリも同じ)。
末尾 `/` や `//` は `vfs_resolve_path` が畳むので、**VFS 経由では「マウント点そのもの」だけ**が空の名前になる
(`mkdir /`、マウント点での `mkdir .` / `mkdir ""`、`tar -x` の `mkdir_parents` も同じ形)。VFS を通らない
ext2 直呼び (試験・将来の呼び手) では `"/a/"` の末尾も空の名前になり得た。255 を超える名前は name_len (u8) が
回り込むが、`OS32_MAX_PATH` = 256 のため VFS 経由では届かない。

### 修正

- `fs/vfs.c`: 相対パスがマウント点そのもの (`"/"`) なら FS へ渡さない。`vfs_mkdir` = **EXIST** (POSIX の
  `mkdir("/")` と同じ。`pkg.c` / `tar.c` の mkdir -p 型は戻り値を見ない / EXIST を在ると読む)、`vfs_rmdir` /
  `vfs_rename` = INVAL、`vfs_write` / `vfs_rm` = ISDIR。
- `fs/ext2_dir.c` `ext2_name_check()`: 空・`.`・`..`・255 超・`/` 入りを EXT2_ERR_INVAL。`ext2_mkdir` /
  `ext2_create` / `ext2_rename` (新旧) / `ext2_rmdir` / `ext2_unlink` の入口 (inode 割り当ての前) と
  `ext2_add_entry` (最後の砦) で呼ぶ。`ext2_find_entry_loc` / `ext2_delete_entry` は長さ 0 と 255 超を探さず
  NOTFOUND — 既存 NHD の名前の無い項目を `""` で掴まない (掴むと `vfs_write("/hd0")` がディレクトリ inode 23 に
  ファイルの中身を書く)。
- `fs/ext2_vfs.c` `ext2_vfs_mkdir()`: パスが `""` / `"/"` / `"//"` なら EXIST。
- **末尾 `/` は取り除く側** (断らない): `mkdir /a/b/` は POSIX で有効で、`vfs_resolve_path` が既に畳んでいる。
  呼び手 (cdinst / install / hsync / シェル mkdir / tar / pkg) はどれも末尾 `/` を付けないか、付けても正規化の
  後なので、断る理由が無い。ext2 直呼びに末尾 `/` が来たら (VFS を通っていない) INVAL で断る。
- `tools/mkpkg.py`: ゲストパスの形を検査 (先頭 `/`、空・`.`・`..` の要素なし、`/` だけは不可)。先頭 `/` の無い
  パスは cdinst で `/hd0bin/…` に化ける。現行の定義は全部通る (`make all` で 6 本とも生成)。
- `userland/lib/rt/pkg.c` は変えていない (`mkdir("/hd0")` が EXIST を返すようになるだけで正しい)。

### 試験

`make check-ext2-empty-name-host` (`tools/tests/test_ext2_empty_name.py --target --mutants`、記録
`tools/tests/ext2_empty_name_tdd.md`)。cdinst 相当の像 (実物の mkpkg.py の PKG 2 本) を含む 4 枚の像が
`e2fsck -fn` で clean。

### 既存の NHD

写しで `e2fsck -fy` を試した: offset 132 (と 1016) を salvage、inode 23 (空のディレクトリ) は
`/lost+found/#23` へ接続 (`lost+found` は e2fsck が新しく作る。OS32 のフォーマッタは作らない)、UUID が生成され、
状態は clean。**`db` (inode 43、`fep.db` 5.8MB) を含む他の項目は残る**。直した後の OS32 は `#23` を普通の空
ディレクトリとして扱う (rmdir で消せる)。なお写しの s_state は元から「not clean with errors」だった (原因未確認)。

## 実装レビュー (2026-09-24)

fc5ce67 は Codex / Opus のラリー 1 で**両者 Approve** (blocker なし)。修正前からの欠陥 (FD のパス再解決でディレクトリを上書き、長いパスの切り詰め) は別票 [`TASK_VFS_FD_PATH.md`](TASK_VFS_FD_PATH.md)。既存 NHD の名前の無い項目は修正後のカーネルでも見えず消せない → ホストの `e2fsck -fy` で直す (NHD の書き換えは [D2])。`check-ext2-empty-name-host` は e2fsck が無いと SKIP で通るので、受入ではログの `E2FSCK` 行を見る。

## NP21/W での受入 (2026-09-24、ユーザー指示「新規インストールによる修復をテスト」)

- 手順: NHD を空に (`tools/mk_blank_nhd.py`、ユーザー承認、バックアップ不要) → FD (最新) で起動 → CD の `cdinst` (Normal、Debug/Append なし) → 停止 → ホストで `e2fsck -fn` → HDD 起動。
- **結果**: `e2fsck -fn` **clean** (112 files)、ルートに名前の無い項目なし。入った `/boot/vmkernel.lz4` と `/sys/shell.bin` は今のビルドとバイト一致。HDD 起動で `ver` = API v62、**kselftest 206/206 (fail 0)**、`kbdstat` `cmd=16`。
- **NP21/W の観察 (記録)**:
  1. CD ドライブは ini の `HDD3FILE` ではなく**別の設定から** `os32_install.iso` を掴んでいた (1 回目のインストールは古い ISO = v61 の中身が入った)。`HDD3FILE` に別の ISO を入れた試験用 ini (`np21w-trial-cdinst.ini`) では、**インストール後の HDD から BIOS が起動しなかった** (「システムディスクをセットしてください」)。元の試験用 ini では起動する。
  2. 空の NHD で最初の冷起動では `hd0` が検出されないことがあった (リセットで検出)。実機で同じ現象が出るかは未確認。
  3. インストール直後に WSL から Windows 側の NHD を直接読んだ (`tail`) ため、NP21/W が NHD を開けなかった (ユーザーの指摘)。**Windows 側の NHD は直接読まず、`make nhd-pull` の写しを使う**。
