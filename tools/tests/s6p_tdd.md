# S6-P (ext2 の小さな書き込みが極端に遅い) — ホスト TDD の記録

票: S6-P / 観測は [`docs/archive/settings/TASK_S6.md`](../../docs/archive/settings/TASK_S6.md) 「PM 受入記録」
診断: [`docs/archive/settings/TASK_S6P.md`](../../docs/archive/settings/TASK_S6P.md)
実装 (段 B, ext2): `fs/ext2_ctx.h` / `fs/ext2_super.c` / `fs/ext2_inode.c` / `fs/ext2_dir.c` /
      `fs/ext2_vfs.c` / `fs/ext2_priv.h` / `fs/ext2_fmt.c`
実装 (段 A, 呼び出し側): `lib/microtar/microtar.c` の `write_null_bytes()`
      (`lib/microtar/README.OS32` 改変点 4) / `userland/cmds/tar.c` ([C1] 修正)
試験: `tools/tests/ext2_write_io_host.c` + `test_ext2_write_io.py`
      (`make check-vfs-mount-dev-host` に登録、`build/sdk.mk`)
      / `tools/tests/test_tar_cmd.py` (`check-tools-host`)

## 様式

`con_sink_tdd.md` / `ext2_read_bound_host.c` と同じで、**模型ではなく実物**を見る:

1. `ext2_write_io_host.c` が `fs/ext2_super.c` / `ext2_inode.c` / `ext2_dir.c` /
   `ext2_file.c` / `ext2_fmt.c` / `ext2_vfs.c` を 1 行も写さずに `#include` する。
2. 差し替えるのは境界だけ — Device API (`dev_find` / `dev_blk_read_lba` /
   `dev_blk_write_lba`)、`ide_drive_present` / `ide_get_info`、kstring / kmalloc /
   kprintf / `vfs_register_fs`。
3. ディスクは RAM 上の 8MB (`base_lba` はパーティションテーブルが空なので
   `ext2_find_partition()` のフォールバック 1088)。**実物の `ext2_format()`** で
   作るので、ブロック配置も実機と同じ経路で決まる。
4. 数えるのは **512B セクタ単位の I/O 回数** = `ide_read_sector_chs` /
   `ide_write_sector_chs` の呼び出し回数。ext2 の 1KB ブロック 1 本は
   `ext2_read_block` / `ext2_write_block` が必ずセクタ 2 本に割る。
4b. 書庫を作る側 (段 A) も実物 — `lib/microtar/microtar.c` をそのまま
   `#include` し、`mtar_t` の read/write/seek に `ext2_vfs_*_stream` を挿す
   (`userland/cmds/tar.c` の `mt_read` / `mt_write` / `mt_seek` と同じ形)。
   32bit の glibc ヘッダが無い環境なので、microtar が include する
   `<stdio.h>` / `<stdlib.h>` / `<string.h>` は
   `tools/tests/mtar_freestanding/` の最小シムで埋め、`sprintf` ("%o" /
   "%06o" だけ) と `strcpy` / `strcmp` / `strlen` の実体は試験側が持つ。
   **回数は模型ではなく実測**なので、padding が 1 バイト書きに戻れば
   P5 が落ちる。
5. `u32` は `unsigned long` なので、ホストも **ILP32 で組む**
   (`-m32`)。この環境には 32bit の glibc が無いので、`con_sink_host.c` と同じく
   `-nostdlib` + Linux の `int 0x80` で write/exit する。
   同じソースが `i386-elf-gcc -Werror` でも通ることを別に見る ([C1])。

## 再生する呼び出し列

`tar c /tmp/e8.tar /etc/system.cfg` (15B のファイル 1 本 → 書庫 2048B) が
`lib/microtar/microtar.c` 経由で出す `sys_write` をそのまま並べる:

| microtar | 直し前の sys_write | 直し後 (512B 単位) |
|---|---|---|
| `mtar_write_file_header` → `twrite(512)` | 512B × 1 | 512B × 1 |
| `mtar_write_data(15)` → `twrite(15)` | 15B × 1 | 15B × 1 |
| 同上 → `write_null_bytes(497)` | **1B × 497** | 497B × 1 |
| `mtar_finalize` → `write_null_bytes(1024)` | **1B × 1024** | 512B × 2 |
| 合計 | **1523 回** | **5 回** |

`write_null_bytes()` (microtar.c:111) は `for (i = 0; i < n; i++) twrite(tar, &nul, 1);`
— 1 バイトずつ書く。後方 `lseek` は create 経路には出ない (`mtar_seek(last_header)` は
read 側)。`lseek` 自体は `fs/vfs_fd.c` の `vfs_seek` が `VfsFile.offset` を
書き換えるだけなので **0 セクタ** (ケース P4 で表明)。

## ケース

| ID | 何を見るか |
|---|---|
| P1 | 新しいブロックを起こす 512B 書き込み 1 回の I/O 回数 |
| P2 | 既存ブロックへの 512B 書き込み 1 回 |
| P3 | 1 バイト追記 1 回 (microtar の padding と同じ形) |
| P4 | 後方 lseek は 0 セクタ / 戻った先への 512B 書き直し |
| P5 | **実物の microtar** で `tar c` を走らせ、`sys_write` の回数・最小の書き込み・合計セクタを数える |
| P6 | 読み側 (512B 読み 1 回) |
| P7 | write-through の契約 — 別インスタンスでマウントし直して中身・サイズ・空き数が一致 |
| P8 | 経路の記憶が名前空間に追従する (unlink → 再作成 / rename / rmdir → 同名ファイル / 2 インスタンス同時) |
| P9 | 確保を伴う書き込みでは sync が必ず出る (alloc > no-alloc) |
| P10 | メタデータ (空き数・`used_dirs`・GD) が **毎操作のあと**ディスクと一致する |

## RED (修正前、基点 `4fd5fb4`)

```
  P1 write(512B @0, new block): rd=18 wr=10 total=28 sectors
  P2 write(512B @0, existing) : rd=18 wr=8  total=26 sectors
  P3 write(1B @527, existing) : rd=18 wr=8  total=26 sectors
  P4 lseek(back)              : rd=0  wr=0  total=0  sectors
  P4 rewrite header(512B @0)  : rd=18 wr=8  total=26 sectors
  P5 tar c (real microtar): sys_write=1523 calls, smallest=1B, bytes=2048, total=39602 sectors
  P6 read(512B @512)          : rd=12 wr=0  total=12 sectors
  P9 alloc=28 vs no-alloc=26 sectors
```

### 26 セクタ (= 13 ブロック I/O) の内訳 — 1 回の `sys_write` あたり

`fs/vfs_fd.c` の `vfs_write_fd` は **パス文字列**を渡すので、毎回ここから始まる:

| 何 | ブロック読 | ブロック書 |
|---|---|---|
| `ext2_resolve_path` → `ext2_lookup("/tmp/e8.tar")` — `find_entry(root,"tmp")` (inode + dir ブロック) + `find_entry(tmp,"e8.tar")` (同) | 4 | 0 |
| `ext2_write_stream`: `ext2_read_inode` | 1 | 0 |
| 部分ブロックの read-modify-write (`ext2_read_block` / `ext2_write_block`) | 1 | 1 |
| `ext2_write_inode` (read-modify-write) | 1 | 1 |
| `ext2_sync` → `ext2_write_super_raw` (ブロック 1) | 1 | 1 |
| `ext2_sync` → `ext2_write_gd_raw` (ブロック 2) | 1 | 1 |
| 合計 | **9** | **4** |

9 + 4 = 13 ブロック × 2 セクタ = **26 セクタ**。実測と一致。
15 秒 / 39602 セクタ ≈ 0.38ms/セクタ = NP21/W の PIO 1 セクタの実測値と整合する。

## GREEN 1 (段 B — ext2 だけ直した時点)

```
  P1 write(512B @0, new block): rd=18 wr=10 total=28 sectors   (変化なし — 確保があるので sync が要る)
  P2 write(512B @0, existing) : rd=6  wr=4  total=10 sectors   (26 → 10)
  P3 write(1B @527, existing) : rd=6  wr=4  total=10 sectors   (26 → 10)
  P4 lseek(back)              : rd=0  wr=0  total=0  sectors
  P4 rewrite header(512B @0)  : rd=6  wr=4  total=10 sectors   (26 → 10)
  P5 tar c (real microtar): sys_write=1523 calls, smallest=1B, bytes=2048, total=15258 sectors
  P6 read(512B @512)          : rd=4  wr=0  total=4  sectors   (12 → 4)
  P9 alloc=28 vs no-alloc=10 sectors
  P10 metadata always reaches the disk
SUMMARY 10/10 PASS
```

残る 10 セクタ (5 ブロック I/O) の内訳: inode 読み / データブロック読み /
データブロック書き / inode 読み直し / inode 書き。

ext2 だけで 2.6 倍。ただし **1523 回の 1 バイト書きは 1523 回のまま**なので、
2KB の書庫に 15258 セクタ ≈ 5.8 秒。本丸は呼び出し側だった。

## GREEN 2 (段 A — `write_null_bytes()` を 512B 単位にした後)

`lib/microtar/microtar.c` の `write_null_bytes()` をブロック単位に
(README.OS32 の改変点 4)。同じ P5 を実物の microtar で走らせた実測:

```
  P5 tar c (real microtar): sys_write=5 calls, smallest=15B, bytes=2048, total=78 sectors
SUMMARY 10/10 PASS
```

| | RED | GREEN 1 (ext2) | GREEN 2 (+ microtar) |
|---|---|---|---|
| `tar c` の `sys_write` 回数 | 1523 | 1523 | **5** |
| いちばん小さい書き込み | 1B | 1B | **15B** |
| セクタ I/O 合計 | 39602 | 15258 | **78** |
| 0.38ms/セクタ での見込み | 15.0 秒 | 5.8 秒 | **0.03 秒** |

5 回の内訳は `512 / 15 / 497 / 512 / 512` (ヘッダ / 本体 / padding / 終端 2 本)。
書庫の中身は `mtar_read_header()` で読み直して名前・サイズ・型・本体・終端の
ゼロを確認している (P5 の `archive_verify`)。

P5 は `writes <= 8` と `minsz >= 15` を表明するので、padding が 1 バイト書きに
戻ったらこの試験が落ちる。

## 直した 2 点 (契約は変えていない)

### (a) `ext2_sync()` は SB / GD が動いていなければ何もしない

`Ext2Ctx.meta_dirty`。空きブロック数・空き inode 数・`used_dirs` を動かす
4 か所 (`ext2_alloc_block` / `ext2_free_block` / `ext2_alloc_inode` /
`ext2_free_inode`) と `ext2_mkdir` / `ext2_rmdir` が `ext2_meta_touch()` を呼ぶ。
`ext2_mount()` は読み込み直後に 0 に戻す。

write-through は保たれる — **書き戻すものが無いときに出していた同じ中身の
read-modify-write 4 ブロック (8 セクタ) を出さないだけ**。確保を伴う書き込み
(P1 / P9) では今までどおり出る。付け忘れは P10 が落とす
(毎操作のあと別インスタンスでマウントし直して GD 全部を突き合わせる)。

注: 明示 `sync` (`ext2_vfs_sync`) も同じ規則になる。ext2 の外から同じ
パーティションへ生セクタを書いた場合 (KAPI の `dev_blk_write_lba`) は、
以前なら `sync` がメモリ上の古い写しを上から書いていた。今は書かない —
どちらにせよメモリ側の写しは信用できないので、上書きしないほうが安全。

### (b) 解決済み経路の記憶 (パス → inode)

`Ext2Ctx.memo[EXT2_PATH_MEMO_N=4]` + `ns_gen`。`ext2_vfs.c` の
`ext2_resolve_path()` だけが使う。`ext2_add_entry` / `ext2_delete_entry` が
成功したら `ext2_ns_touch()` で世代が進み、**記憶は全部まとめて無効**になる。
パスが別の inode を指すようになる操作 (create / unlink / rename / mkdir /
rmdir) は必ずそのどちらかを通る。

- `OS32_MAX_PATH` に収まらない経路は覚えない (`kstrncpy` の切り詰めで
  別の経路に化けて当たるのを避ける)。
- 世代が一周 (u32) したら記憶を全部捨ててから 1 に戻す。
- 記憶は `Ext2Ctx` の中なので `umount` (kfree) で必ず消える。
- P8 が番犬: unlink → 同名で再作成 / rename で同名が別の中身 / rmdir →
  同名ファイル / 2 インスタンス同時マウント。

### (c) 付随: `ext2_format()` の一時コンテキストを static に

`sizeof(Ext2Ctx)` は 732 → **1788 バイト** (記憶が 1056 バイト)。
`fs/ext2_fmt.c` はこれをスタックに積んでいたので、そのままだと
`ext2_format()` のフレームが 824 → 1880 バイトに膨らむ (カーネルスタックは
16KB)。`static` にして **88 バイト**にした — 変更前より小さい。
シングルタスクなのでフォーマットが同時に 2 本走ることはない。
マウント中の `Ext2Ctx` は今までどおり kmalloc (1 インスタンス 1788 バイト)。

## 手を出さなかったもの (記録)

| 案 | 効き | やらなかった理由 |
|---|---|---|
| `ext2_write_stream` の `ext2_write_inode` が inode テーブルブロックを読み直すのを省く (直前の `ext2_read_inode` が同じブロックを `ext2_g_blk` に置いている) | 10 → 8 セクタ (-20%) | 「`ext2_g_blk` が今どのブロックを持っているか」を追う仕掛けが要る。共有バッファの取り違えは過去に 2 度ファイルを壊している (gotcha §4-24 / §4-32)。効きの割に危ない |
| inode が 1 バイトも変わっていなければ `ext2_write_inode` ごと省く | 上書き専用 | `ext2_current_time()` が定数なので、サイズが伸びない上書きなら効く。tar の create はサイズが毎回伸びるので効かない |
| ブロックキャッシュ層の新設 | 大 | write-through の契約が変わる。票の範囲外 |

## 併せて直したもの

`userland/cmds/tar.c` の `collect_cb()` が `(void)ctx;` のあとに `int len;` を
置いていて [C1] (宣言はブロック先頭) に反していた。実機のビルドフラグには
`-Wdeclaration-after-statement` が無いので通っていたが、`test_tar_cmd.py` に
足したクロスコンパイル段 (`-Werror` 付き) で落ちたので直した。

## 試験の登録

- `build/sdk.mk` の `check-vfs-mount-dev-host` に
  `python3 -B tools/tests/test_ext2_write_io.py` を 1 行足した (既存の ext2 試験群)。
- `test_tar_cmd.py` は `check-tools-host` 側に PM が登録済み。ここには
  `i386-elf-gcc -Werror` のクロスコンパイル段 (`build_target`) を足した —
  vendor の `microtar.c` が実機フラグで 1 行も直さずに通ることの表明。
