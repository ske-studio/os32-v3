# vfs_excl TDD 記録 — 排他的作成 `O_EXCL` (票 H2 §2-1、KAPI v53)

対象票: [`docs/archive/shell/TASK_H2.md`](../../docs/archive/shell/TASK_H2.md) §2-1 / §4-1 の **X1 / X2**
試験: `tools/tests/vfs_excl_host.c` (実物の `fs/vfs.c` + `fs/vfs_fd.c` を `#include`)
実行: `python3 -B tools/tests/test_vfs_excl.py [--target] [--mutate]` / `make check-vfs-excl-host`

基点: `feat/gui` = `15c5edf`。

## 何を見ているか

| ID | 反例 | 期待 |
|---|---|---|
| X1 | `O_CREAT | O_EXCL` で既存ファイル / 既存ディレクトリ / 判定不能 (I/O 失敗) | EXIST / **EXIST** (ISDIR ではない) / その負値。**作らない・切り詰めない** |
| X2 | `O_EXCL` 単独、`create_excl` を持たない FS | INVAL / NOSYS。**非対応の判定が種別検査より先** (`get_file_size` / `list_dir` を 1 度も呼ばない) |

FS ドライバは合成 `VfsOps`。`create_excl` を**持つもの** (ext2 相当) と**持たないもの**
(HostDrv / FAT / ISO9660 相当) の 2 つを用意し、`create_excl` / `get_file_size` /
`write_file` の戻り値と**呼び出し回数**を 1 つずつ指定する。「作っていない」は
`write_file` の回数 0 で押さえる。

## RED (実装前)

`sdk/kapi.json` / `os32_kapi_shared.h` / `fs/vfs.h` に **定数 `KAPI_O_EXCL` と
`VfsOps.create_excl` の口だけ**を入れ、`fs/vfs_fd.c` の分岐と ext2 の実装は入れない状態。

```
KAPI FLAG PASS (KAPI_O_EXCL=0x400, v53, スロット 214 本のまま)
HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c + fs/vfs_fd.c)
=== 票 H2 §2-1: 排他的作成 O_EXCL (KAPI v53) ===
== X1: O_CREAT|O_EXCL は既存を EXIST、判定不能はその負値 ==
  FAIL 既存ファイル -> VFS_ERR_EXIST
  ok   既存ファイル: 作らない・切り詰めない
  FAIL create_excl は 1 度だけ呼ばれる
  FAIL 既存ディレクトリ -> ISDIR ではなく EXIST
  ok   既存ディレクトリ: 1 バイトも書かない
  FAIL 判定不能 (IO) -> その負値をそのまま返す
  ok   判定不能: 作らない
  FAIL 書き込み禁止 (ROFS) もそのまま返す
  FAIL 空き不足 (NOSPC) もそのまま返す
  ok   作れたら FD が出る
  FAIL 作りたての長さは 0
  ok   **write_file 経由の作成は通らない** (create_excl だけが作る)
  FAIL get_file_size を通らない (作ったばかり)
  ok   O_TRUNC 併用でも作れる
  FAIL O_TRUNC 併用でも余計な書き込みをしない
== X2: O_EXCL 単独は INVAL、非対応 FS は NOSYS ==
  FAIL O_EXCL 単独 (O_CREAT 無し) -> VFS_ERR_INVAL
  ok   O_EXCL 単独: create_excl を呼ばない
  ok   O_EXCL 単独: 1 バイトも書かない
  FAIL O_RDONLY|O_EXCL も INVAL
  FAIL 非対応 FS -> VFS_ERR_NOSYS
  ok   非対応 FS: 作らない
  FAIL 非対応 FS: ディレクトリでも ISDIR ではなく NOSYS
  ok   非対応 FS: 種別検査 (get_file_size / list_dir) まで進まない
  FAIL 非対応 FS: stat が I/O で落ちても NOSYS が先に返る
  ok   非対応 FS: サイズ取得まで進まない
  ok   マウント外は NOMOUNT (経路の解決が先)
== 回帰: O_EXCL 無しの open は従来どおり ==
  (7 件すべて ok)

33 checks, 14 failures
EXIT vfs_excl_host=1
```

**旧コードが何をしていたか**: `vfs_open_internal` は知らないビットを黙って無視する。
`O_CREAT | O_EXCL` は「`O_CREAT` だけ」として通り、既存ファイルは**開けてしまう**
(EXIST にならない)。`O_EXCL` 単独も `O_RDONLY` として素通り。非対応 FS でも
`vfs_path_kind` → `get_file_size` まで走ってしまう。

`KAPI_O_EXCL` を定義する前 (`15c5edf` そのまま) は、試験の入口の
`check_flag_constant()` が先に落ちる:

```
KAPI_O_EXCL が 0x0400 ではない: None
```

## GREEN (実装後)

```
KAPI FLAG PASS (KAPI_O_EXCL=0x400, v53, スロット 214 本のまま)
HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c + fs/vfs_fd.c)
=== 票 H2 §2-1: 排他的作成 O_EXCL (KAPI v53) ===
  (33 件すべて ok)

33 checks, 0 failures
EXIT vfs_excl_host=0
TARGET i386-elf -Werror COMPILE PASS (fs/vfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/vfs_fd.c)
TARGET i386-elf -Werror COMPILE PASS (fs/ext2_vfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/ext2_dir.c)
TARGET i386-elf -Werror COMPILE PASS (fs/ext2_file.c)
TARGET i386-elf -Werror COMPILE PASS (fs/ext2_super.c)
```

## 変異試験 (`--mutate`、否定側)

規則を崩した版で試験が**確かに落ちる**ことを見る。落ちなければ試験が規則を見ていない。

```
MUTATE nosys_after_kind       RED (期待どおり落ちた)
MUTATE excl_error_is_absent   RED (期待どおり落ちた)
MUTATE excl_alone_ok          RED (期待どおり落ちた)
```

- `nosys_after_kind` … 非対応 FS の判定を種別検査の**後ろ**へ動かした版
  (票 §6 往復 2 の指摘そのもの)。
- `excl_error_is_absent` … `create_excl` の「判定できない負値」を `VFS_OK` に
  読み替えて作ってしまう版 (票 B8 の否定側)。
- `excl_alone_ok` … `O_EXCL` 単独を `O_CREAT` 付きとして通す版。

## 確かめていないこと

- **ゲスト (NP21/W) では 1 度も動かしていない。** 実 IDE / 実 NHD の上での挙動、
  および `make all` / `make check` は PM の受入 (票 §4-2) で確認する。
- `create_excl` の排他性そのもの (VFS が非再入でゲストが協調型であること) は
  **設計上の前提**で、試験では作っていない。ホスト側が同時に書ける FS では
  成り立たないため、HostDrv には実装していない。
