# ext2_empty_name TDD 記録 — 長さ 0 の名前を ext2 に載せない

対象票: [`docs/archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md`](../../docs/archive/kernel_v21/TASK_EXT2_EMPTY_NAME.md)
試験: `tools/tests/ext2_empty_name_host.c` (実物の `fs/ext2_*.c` + `fs/vfs.c` + `fs/vfs_fd.c` + `userland/lib/rt/pkg.c` を `#include`)
実行: `python3 -B tools/tests/test_ext2_empty_name.py [--target] [--mutants] [case]` / `make check-ext2-empty-name-host`

基点: `feat/gui` = `97c2eab`。

## 何を見ているか

| 段 | 反例 | 期待 |
|---|---|---|
| vfs | `/hd0` に載せた ext2 へ `mkdir /hd0`・`/hd0/`・`/hd0//`・cwd=/hd0 で `""` | **EXIST**、1 セクタも書かない。`/hd0/.`・`/hd0/tmp/..`・`.` は票 TASK_VFS_FD_PATH 以降 **INVAL** (最終要素の `.` / `..`) |
| vfs | `mkdir /hd0/a/`・`/hd0//b`・`/hd0/a//c/` | 作れる (末尾 `/` と `//` は VFS が畳む) |
| vfs | マウント点への write / open(O_CREAT) / open(O_EXCL) / rename (元・先) / rmdir / unlink | 負値、書かない |
| root | `/` に載せた ext2 で `mkdir /`・`//`・`""`、write / rmdir / unlink `/` | 同上 (`/.`・`/..` は票 TASK_VFS_FD_PATH 以降 INVAL) |
| ext2 | `ext2_mkdir` / `create` / `add_entry` / `rename` / `rmdir` / `unlink` に `""`・`.`・`..`・`x/y`・256 文字 | **INVAL**、書かない。255 文字は作れる |
| ext2 | `ext2_vfs_mkdir("/")` / `("")` | EXIST。`"/d/"` と write `"/"` `"/d/"`、create_excl `"/d/"`、rename 先 `"/d/"` は負値 |
| synth | 合成ドライバを `/syn` に載せて同じ操作 | ドライバまで**1 回も届かない**。`mkdir /syn/a/` は `"/a"` で届く |
| legacy | 名前の無い項目を仕込んだルート (実 NHD と同じ形) | `find_entry("")` は NOTFOUND、`"/"` への write はそのディレクトリ inode を変えない |
| cdinst | cdinst の mkdir 12 本 + 実物の `mkpkg.py` の PKG 2 本 (無圧縮 / LZSS) を実物の `pkg.c` で展開 | 中身が載る、名前の無い項目が無い |

どの段 (legacy 以外) も最後に全ディレクトリの**生の項目**を歩き (`ext2_list_dir` は name_len 0 を読み飛ばすので
使わない)、像を書き出して本物の `e2fsck -fn` に当てる。**終了コード 0 以外は失敗**。

## RED (修正前、`97c2eab` の fs/)

```
case vfs
  FAIL: vfs_mkdir("/hd0") rc=0 (期待 EXIST)、書き込みあり
  FAIL: vfs_rmdir("/hd0") rc=0  ← 作った名前の無いディレクトリを消していた
case root
  FAIL: vfs_mkdir("/") rc=0、vfs_rmdir("/") rc=0
case ext2
  FAIL: ext2_mkdir("")/(".")/("..") → rc=0 / -6 / -6、"x/y" rc=0、256 文字 rc=0 (name_len が 0 に回り込む)
  FAIL: ext2_rmdir(".") rc=0 など 30 件余り
  e2fsck -fn ext2.img: rc=12  (First entry 'z' ... should be '.' / illegal characters 'x/y' / directory corrupted)
case cdinst
  (scan) empty name: dir ino 2 -> ino 23
  e2fsck -fn cdinst.img: rc=12
    | Directory inode 2, block #0, offset 132: directory corrupted     ← 実 NHD と同じ inode 番号・同じ offset
```

vfs / root の像が clean だったのは、同じ段の `rmdir` が名前の無いディレクトリを消していたから (試験の後半が
前半の傷を隠していた)。書き込みの有無と戻り値の両方を見るので RED になっている。

## GREEN

```
case vfs     e2fsck -fn vfs.img: clean
case root    e2fsck -fn root.img: clean
case ext2    e2fsck -fn ext2.img: clean
case cdinst  e2fsck -fn cdinst.img: clean
case legacy  (仕込んだ 1 件だけ検出)
case synth
checks 200 failures 0
TARGET i386-elf -Werror compile PASS (fs/ext2_dir.c, fs/ext2_file.c, fs/ext2_vfs.c, fs/vfs.c)
```

## 変異 (`--mutants`、fs/ の写しを変異させるので実物は書き換えない)

14 本すべて killed: vfs_mkdir / vfs_write / vfs_rmdir のマウント点検査を外す、ext2_mkdir / create / add_entry /
rename / rmdir / unlink の `ext2_name_check` を外す、長さ上限・`..`・`/` の各検査を外す、ext2_vfs_mkdir の
ルート = EXIST を外す、find_entry の長さ 0 の早期 NOTFOUND を外す。
`vfs_mkdir` の検査は ext2 側の `ext2_vfs_mkdir` と冗長なので、ext2 だけでは生き残った — synth 段 (ext2 以外の
ドライバ) を足して殺した。

## 試験していないこと

- NP21/W / 実機の cdinst そのもの (cdinst.c 本体は IDE を直に叩くのでホストで組めない。付け替えは同じ写しを使う)。
- FAT / ISO9660 / HostDrv の各ドライバが空の名前をどう扱うか (VFS がマウント点を渡さなくなったことだけを synth で見る)。
- `userland/cmds/tar.c` の `mkdir_parents` / `fdinst` (同じ VFS を通るので同じ修正が効くはずだが、実物は回していない)。
