# 票 B8 — 読み取り失敗を「不存在」として扱う処理の是正 (ホスト TDD の記録)

- 票: [`docs/archive/shell/TASK_FS_TYPE.md`](../../docs/archive/shell/TASK_FS_TYPE.md) §2
- 基点: `feat/gui` の `3316fdb`
- 実行: `python3 -B tools/tests/test_b8_open.py --target`
  (`make check-b8-open-host` が同じものを回す)
- 日付: 2026-09-15

## 0. 正直に書く ([V4])

**試験は実装のあとに書いた。** `fs/ext2_inode.c` / `ext2_dir.c` / `ext2_file.c` /
`ext2_vfs.c` / `vfs.c` / `vfs_fd.c` / `hostdrvfs.c` / `hostdrv_stat_rules.inc` を
先に直し、そのあとで `tools/tests/b8_open_host.c` を書いた。
だから「先に RED を見た」とは書けない。

代わりに **変異試験で赤を取った**。直した規則を 1 つずつ元に戻した版に差し替えて
試験を回し、**18 件すべてが落ちる**ことを確かめてある (§4)。落ちなければ
「試験がその規則を見ていない」ということなので、そこは数えて正直に書く。

実際、最初に書いた版では **M9 / M10 / M18 の 3 件が GREEN のまま通った**
(= 取り逃していた)。その 3 つを捕まえるために試験を足し、締め直した経緯も §4 に残す。

## 1. 何を確かめる試験か

`vfs_path_kind()` の戻り値までしか見ない試験では、同じ形の欠陥 (B7) を
Codex 実装レビュー 往復 4 で**実際に取り逃している**。だから
**判定を消費する側 = 実物の `vfs_open()` / `vfs_open_sqlite()`** まで通す。

`tools/tests/b8_open_host.c` は 1 本の実行ファイルに 3 つの段を載せる。

| 段 | 土台 | 何を見るか |
|---|---|---|
| A | **実物の ext2** (`fs/ext2_super.c` / `ext2_inode.c` / `ext2_dir.c` / `ext2_file.c` / `ext2_fmt.c` / `ext2_vfs.c`) + **実物の `fs/vfs.c` / `fs/vfs_fd.c`**。贋物は Device API と IDE だけ。RAM 上の 8MB を実物の `ext2_format()` で作る | **間接ブロックを使う大きなディレクトリ**の読み出しを**一度だけ**落とし、`ext2_bmap` → `find_entry` → `lookup` → `stat` → `vfs_open` の全段で「未割当 = 検索終了」に化けないこと |
| B | 合成 `VfsOps` (`tools/tests/vfs_kind_host.c` と同じ作法) | `stat` と `get_file_size` の戻り値を 1 つずつ指定し、**`write_file` の呼び出し回数**で「作成へ進んでいない」ことを 0 件で押さえる |
| C | `fs/hostdrv_stat_rules.inc` の純関数 | HostDrv の `get_file_size` がディレクトリに答えないこと (`hdrv_size_result`)。ハイパーコールを叩く `fs/hostdrvfs.c` 本体はホストで組めないので、判定だけを純関数へ切り出した (票 H1 の `hdrv_stat_fill` と同じ作法) |

**一度だけ**の失敗にするのが肝。恒久的な故障なら誰が見ても異常だが、B8 が起きるのは
「一度読めなかっただけで次は読める」ときである。注入器は
「この LBA の n 回目の読み出しを落とす」(`fail_arm`) と
「armed のあいだずっと落とす」(`fail_arm_always`) の 2 つを持つ。
**仕掛けが本当に効いたか**を毎回 `g_fail_fired` で確かめているので、
「狙った経路に届いていないのに緑」にはならない。

### 段 A の足場

- `/big` に 6 文字名の詰め物を 900 件入れる。1 エントリ 16 バイト = 1 ブロック 64 件
  なので、直接ブロック 12 本 (768 件) を確実に越えて**間接ブロックを使う**。
  `lba_of_big_indirect()` が `inode.block[EXT2_IND_BLOCK] != 0` を CHECK するので、
  詰め物が足りなくなればその場で落ちる。
- 目当ての `/big/keepme` は**最後**に入れる = 間接ブロック側に載る。
- 中身が残っているかは **`vfs_open` を通さず** `ext2_read_file` で読み直す
  (`keep_intact()`)。open が壊したかどうかを open 自身に聞かない。

## 2. 追加した試験と件数

`python3 -B tools/tests/test_b8_open.py` → **261 checks, 0 failures**。

| 段 | 名前 | 中身 |
|---|---|---|
| A5 | `case_bmap_contract` | `ext2_bmap` の約束 — 未割当は `EXT2_OK` + `0`、読めなければ `EXT2_ERR_IO`。同じ経路が `find_entry` / `lookup` / `ext2_vfs_stat` / `vfs_path_kind` / `ext2_vfs_get_size` まで畳まれずに届くこと |
| A1 ×4 | `case_indirect_read_failure` | 間接ブロックの一度の失敗 × (`O_CREAT` 有無 × `O_TRUNC` 有無)。FD が出ない / `NOTFOUND` と言わない / **媒体に 1 セクタも書かない** / 中身が残る |
| A2 ×4 | `case_size_failure` | 種別は `stat` で確定しているのに**サイズ取得だけ**が一度失敗する (§2-2b、**最も重い無言のデータ消失**) × 4 通り |
| A3 | `case_sqlite_size_failure` | `vfs_open_sqlite` も同じ |
| A6 | `case_write_file_failure` | 一括書き込みの既存判定が読めないとき、**新規作成へ落ちない**。一度きりの失敗と恒久的な失敗の両方 |
| A7 | `case_write_stream_failure` | 追記書き込みが、読めない間接ブロックを「未割当」と見なして**割り当て直さない** (元のブロックの 1024 バイトが丸ごと残ることまで確認)。正常時に同じ書き込みが通る回帰つき |
| A8 | `case_other_bmap_callers` | `ext2_bmap` の**残りの呼び手** 6 系統 — `list_dir` (打ち切りを成功にしない) / `add_entry` (新ブロックを継ぎ足さない) / `delete_entry` / `read_file` `read_stream` (短いファイルに化けない) / `is_dir_empty` (`NOTEMPTY` と偽らない) / `parent_of` (rename の循環検査、**木が輪にならない**) |
| A4 | `case_normal_paths` | 正常系の回帰 — 通常ファイルは開ける / ディレクトリは `ISDIR` / 本当に不存在なら `O_CREAT` で作れる / `O_TRUNC` は切り詰まる / `O_CREAT` 有でも既存の中身は残る |
| B1 ×7 | `synth_size_failure` | 4 通り + `NOSPC` / `NOTDIR` / `INVAL`。**`write_file` の呼び出し回数が 0**、中身の長さが変わらない |
| B2 B3 | `synth_trunc_write_failure` | `O_TRUNC` / `O_CREAT` の `write_file` が失敗したら open しない |
| B4 | `synth_sqlite` | `vfs_open_sqlite` の作成抑止 + 本当に不存在なら作れる回帰 |
| B5 | `synth_normal` | 合成ドライバでの正常系 + `stat` を持たないドライバのプローブが `ISDIR` を `DIR` と読むこと |
| C1 | `stage_c` | `hdrv_size_result` — ディレクトリ / 問い合わせ失敗。**問い合わせは 1 回のまま** (種別は同じ応答の `Directory` で分かる。NP21/W は `FileStandardInformation` にこれを埋める) |

## 3. 既存の試験を 1 行直した

`tools/tests/ext2_write_io_host.c` の `ns_invalidation` が

```c
CHECK(ext2_vfs_get_size(g_ec, "/tmp/d", &sz) == VFS_OK);   /* /tmp/d はディレクトリ */
```

と書いていた。これは「記憶が名前空間に追従するか」を見るだけの行で、
**当時の `ext2_get_size_ino` がディレクトリの inode サイズをそのまま返していた挙動を
そのまま写していた**。その成功が open の受け手で「通常ファイルである」根拠に使われ、
`cat <ディレクトリ>` を通していた ③ の本体なので、期待値を `VFS_ERR_ISDIR` に直した。
理由はその場にコメントで残してある。

## 4. 変異試験 (18 件すべて RED)

直した規則を 1 つずつ元に戻して `test_b8_open.py` を回す。

| # | 戻した規則 | 落ちた件数 |
|---|---|---|
| M1 | `ext2_bmap` が I/O エラーを未割当 (0) に潰す | 47 |
| M2 | `ext2_lookup` が `find_entry` のエラーを `NOTFOUND` に畳む | 33 |
| M3 | `ext2_find_entry` が `bmap` のエラーを無視して検索終了にする | 35 |
| M4 | `ext2_vfs_*` が `resolve_path` のエラーを一律 `NOTFOUND` にする | 32 |
| **M5** | **② の肝**: open が `NOTFOUND` 限定を外して作成へ進む | **54** |
| M6 | `O_TRUNC` が `write_file` の戻り値を無視する | 2 |
| M7 | `ext2_get_size_ino` がディレクトリを断らない | 1 |
| M8 | `hdrv_size_result` がディレクトリを断らない | 3 |
| M9 | `ext2_vfs_write` が読めないときも新規作成へ落ちる | 1 |
| M10 | `ext2_write_stream` が読めないブロックを割り当て直す | 4 |
| M11 | `vfs_path_kind` のプローブが `ISDIR` を「無い」に潰す | 1 |
| M12 | `ext2_list_dir` が打ち切りを成功にする | 2 |
| M13 | `ext2_add_entry` が読めないまま新ブロックを継ぎ足す | 2 |
| M14 | `ext2_delete_entry` が `NOTFOUND` に化けさせる | 1 |
| M15 | `ext2_read_file` が短いファイルにする | 1 |
| M16 | `ext2_read_stream` が短い読み出しにする | 1 |
| M17 | `ext2_is_dir_empty` が読めなかったのを「空でない」にする | 2 |
| M18 | rename の循環検査が読めなかったのを「祖先ではない」にする | 4 |

### 最初に取り逃した 3 件

- **M9** — 一度きりの失敗だと、`ext2_create` の中の存在確認が今度は通って
  `EXT2_ERR_EXIST` になる。`rc < 0` としか見ていなかったので緑だった。
  **`rc == VFS_ERR_IO`** まで見るように締めて赤になった。
  「読めなかったことを読めなかったと言う」が規則そのものなので、これが正しい形。
- **M10** — `ext2_write_stream` を叩く試験がそもそも無かった。16KB の
  `/big/wide` を作って間接領域へ追記する A7 を足した。
- **M18** — `fail_arm_always` で落としていたので、循環検査を素通りしても後段の
  `ext2_add_entry` が止まり、被害 (木が輪になる) が見えなかった。
  d2 の先頭ブロックの**2 回目の読み出しだけ**を落とす形に変えて赤になった。

この 3 件は「① の修正が別の段で被害を先に止めてしまい、変異の影響が見えない」
という形の取り逃しだった。**被害ではなくエラーの名前まで見る**か、
**影響が出る経路を狙って落とす**かのどちらかが要る。

## 5. 触っていないもの

- `sdk/kapi.json` (KAPI は増やしていない。`make clean` からの全ビルドは不要)
- エミュレータ・実配備・`*.ini` — 一切触れていない
- `make` の各ターゲット — 走らせていない。ホスト試験の直接実行と
  `i386-elf-gcc` の単体コンパイルだけ

## 6. 実機で見ていないこと ([V4])

- **`hdrv_get_file_size` のディレクトリ拒否は実機で未確認。** 判定は純関数
  (`hdrv_size_result`) に切り出してホストで試験してあるが、ゲストから実際に
  `Np2FileStandardInfo.Directory` が返ってくるところはエミュレータ上で見ていない。
  根拠は NP21/W 側のソース — `hostdrvNT_IRP_MJ_QUERY_INFORMATION` の
  `FileStandardInformation` 枝が `GetFileAttributesEx` の
  `FILE_ATTRIBUTE_DIRECTORY` から `stdInfo.Directory` を埋めており、
  その `GetFileAttributesEx` が失敗したときは `returnData` が NULL のまま =
  問い合わせ自体がエラーになる (`std_rc != 0`) ので取りこぼしは無い
  (`~/np21w-src/src/generic/hostdrvnt.c`)。
  **問い合わせの回数は増やしていない** — 種別は元から引いていた
  `FileStandardInformation` の応答で分かるので、`hdrv_stat` のように
  `FileBasicInformation` を追加で引くことはしなかった (get_file_size は
  open のたびに走り、hsync は何千回も呼ぶ)。
- ext2 側はすべて RAM ディスク上のホスト試験。実 NHD では走らせていない。

## 7. 2 巡目 — Codex 実装レビューの P1-1〜P1-5 と free_all_blocks

- 基点: `cce2055` (1 巡目の着地)
- 出所: Codex 実装レビュー。32bit バイナリを Unicorn で実行し、追加反例込みで
  334 件中 11 件失敗を再現。PM がコードで 6 件とも確認済み
- 実行: `python3 -B tools/tests/test_b8_open.py --target` と
  `python3 -B tools/tests/test_b8_hostdrv.py --target`
  (どちらも `make check-b8-open-host` が回す)

### 7-0. 正直に書く ([V4])

- **1 巡目の修正は「検索エラーを受け取る次の段」に届いていなかった。**
  `ext2_vfs_write` の作成経路だけ直し、同じ形の `mkdir` / `ext2_create` /
  `rename` の宛先確認を見落とした。
- 試験は今回も実装のあとに書いた。赤は変異試験で取った (§7-5)。
- **作業の途中でユーザーの PC が落ちた。** 再開時に差分を全部読み直し、
  次の 2 つを「落ちる前に終わっていなかったもの」として見つけて直した:
  1. `ext2_unlink` / `ext2_rmdir` が、ブロックを返しきれなかったときも
     `ext2_free_inode` を呼んでいた (§7-3)。**自分で書いたコメント
     (「残したブロックは辿れる」) と矛盾する論理の穴**で、ファイルの破損ではない
  2. 新しい試験 `test_b8_hostdrv.py` が `build/sdk.mk` に未登録だった
  また、再開時にスクラッチ領域の補助スクリプト (変異試験など) が消えていたので
  作り直した。作り直した変異スクリプトは**コンパイルが通らないだけの変異を
  RED と数えない**よう分けてある — 落ちる前の版では free_all_blocks の変異が
  「未使用関数で -Werror」になっていただけなのを RED と誤読していた。

### 7-1. `ext2_find_entry` の呼び出し元 (全 8 か所)

「戻り値を `== EXT2_OK` / `!= EXT2_OK` の二値に潰していないか」で分けた。

| 箇所 | 関数 | 今回の前 | 扱い |
|---|---|---|---|
| `fs/ext2_dir.c:267` | `ext2_mkdir` 存在確認 | **`== EXT2_OK` だけ拒否** | **P1-1 で修正** |
| `fs/ext2_file.c:131` | `ext2_create` 存在確認 | **`== EXT2_OK` だけ拒否** | **P1-2 で修正** |
| `fs/ext2_dir.c:537` | `ext2_rename` 宛先確認 | **`== EXT2_OK` だけ置き換え** | **P1-3 で修正** |
| `fs/ext2_vfs.c:159` | `ext2_vfs_write` 既存判定 | 1 巡目で修正済 | 3 値 (OK / NOTFOUND / その他) |
| `fs/ext2_dir.c:617` | `ext2_lookup` | 1 巡目で修正済 | `return ret` |
| `fs/ext2_file.c:379` | `ext2_unlink` | 元から `if (ret) return ret` | 問題なし |
| `fs/ext2_dir.c:367` | `ext2_rmdir` | 元から `if (ret) return ret` | 問題なし |
| `fs/ext2_dir.c:520` | `ext2_rename` 移動元 | 元から `if (ret) return ret` | 問題なし |

直し方は 3 つとも同じ: **`EXT2_OK` なら EXIST (rename は置き換え分岐)、
`EXT2_ERR_NOTFOUND` のときだけ先へ進み、それ以外は何も書かずにそのまま返す。**

### 7-2. P1-4 HostDrv — OPEN の失敗を「不存在」と区別する

**直す前**: `hostdrv_create()` が失敗を一律 `-1` にし、呼び手 11 か所のうち
7 か所が `VFS_ERR_NOTFOUND`、4 か所が `VFS_ERR_IO` に畳んでいた。
stat は成功 → サイズ取得の OPEN だけ失敗 → NOTFOUND → `vfs_open` の `O_CREAT`
が `hdrv_write_file` へ → `NP2_FILE_OVERWRITE_IF` が既存ファイルを切り詰める。

**直し方**: `hostdrv_create()` が NTSTATUS を純関数
`hdrv_create_status_to_vfs()` (`fs/hostdrv_stat_rules.inc`) で VFS エラーへ写し、
**呼び手 11 か所すべてがそのまま返す**。応答が無いとき (番兵) は IO。

**NP21/W 側の根拠** (`~/np21w-src/src/generic/hostdrvnt.c` の
`hostdrvNT_IRP_MJ_CREATE`、読むだけで変更していない):

| NTSTATUS | NP21/W が返す場面 | 写し先 |
|---|---|---|
| `OBJECT_NAME_NOT_FOUND` 0xC0000034 | 名前が無い | **NOTFOUND** |
| `OBJECT_PATH_NOT_FOUND` 0xC000003A | 途中のディレクトリが無い | **NOTFOUND** |
| `OBJECT_NAME_INVALID` 0xC0000033 | 無効な文字。**NP21/W 側のコメントに「ここで NOT_FOUND を返すとワイルドカード付き copy が壊れる」とあり、わざと分けてある** | INVAL |
| `OBJECT_NAME_COLLISION` | `FILE_CREATE` で既にある | EXIST |
| `FILE_IS_A_DIRECTORY` / `NOT_A_DIRECTORY` | 種別の不一致 | ISDIR / NOTDIR |
| `DIRECTORY_NOT_EMPTY` | 削除時 | NOTEMPTY |
| `SHARING_VIOLATION` / `ACCESS_DENIED` / `MEDIA_WRITE_PROTECTED` / `TOO_MANY_OPENED_FILES` / `CANNOT_DELETE` / `INVALID_PARAMETER` | いずれも「在るが開けない」系 | **IO (既定)** |

**既定を IO にしてあることが安全性の要** — 知らない状態を「無い」と言わない。
足りなかった 6 つの状態値は `fs/hostdrvfs_proto.h` に NP21/W の
`hostdrvntdef.h` と同じ値で足した。

**同じ畳み込みの洗い出し**: `hdrv_list_dir` / `hdrv_read_file` /
`hdrv_get_file_size` / `hdrv_read_stream` / `hdrv_stat` / `hdrv_rmdir` /
`hdrv_unlink` / `hdrv_rename` (以上 NOTFOUND に畳んでいた) と
`hdrv_write_file` / `hdrv_mkdir` / `hdrv_write_stream` (IO に畳んでいた) の
**11 か所すべて**を `return rc` にした。

**受入の形**: 純関数だけの試験では足りない (1 巡目がそれで取り逃した) ので、
**実物の `fs/hostdrvfs.c` をホストで動かす**試験
`tools/tests/b8_hostdrv_host.c` を作った。`include/io.h` の `inp`/`outp` は
特権命令のインライン asm なので `tools/tests/hostdrv_hostshim/io.h` を `-I` で
先に置いて差し替え、コマンド列 `HDR9801` が揃った時点で**贋の NP21/W** が
`g_iostatus` / `g_databuf` を書く。`hdrv_get_file_size()` を**直接**呼び、
`Directory` の値ごとの戻り値 (`ISDIR`) と、OPEN の各 NTSTATUS に対する戻り値を見る。

被害の見え方にも注意が要った: **0 バイト書き込みは `IRP_MJ_WRITE` を 1 度も
出さない**ので「WRITE 回数 0」は安全の証拠にならない。中身を消すのは
**`OVERWRITE_IF` の CREATE そのもの**なので、その回数を数える。

### 7-3. P1-5 と「最後の sync」、および削除時の inode

**P1-5 の方針**: `ext2_write_stream` は `ext2_write_inode` / `ext2_sync` の
どちらかが失敗したら **`EXT2_ERR_IO` を返し、部分成功の値は用意しない**。

理由: 正の戻り値の意味は「そのバイト数がファイルの中身として読み戻せる」で、
**長さは inode にしか無い**。inode を書けなければその約束は成り立たない。
データブロックは既に媒体に載っているかもしれないが、(a) 旧サイズの内側なら
中身の更新として正しく、(b) 外側なら inode から参照されず見えない、の
どちらかで、読める中身が壊れることはない。呼び手には「確認できなかった」
とだけ伝える。**同じ offset へ同じ内容を書き直すのは安全**なので再試行できる。

**同じ形の洗い出し** (書き込み経路の最後の `ext2_sync` / `ext2_write_inode`):

| 箇所 | 扱い | 理由 |
|---|---|---|
| `ext2_write_stream` 最後の `write_inode` / `sync` | **失敗を返す** | P1-5 |
| `ext2_create` / `ext2_write` / `ext2_mkdir` / `ext2_rename` 最後の `sync` | **失敗を返す** | この FS の約束は「戻った時点でディスクが正しい」(write-through、`ext2_sync` のコメント)。書き戻せなかったのに成功と言うのは嘘になる |
| `ext2_rmdir` 最後の `sync` | **失敗を返す** | 同上 |
| `ext2_add_entry` / `ext2_delete_entry` の途中の `write_inode` (mtime 更新) | **そのまま (返さない)** | ディレクトリブロックの書き込みは既に成功しており、エントリは媒体上で追加 / 削除済み。ここで失敗を返すと呼び手が「追加できなかった」と読んでやり直し、**二重エントリを作る**。直すなら順序を変える別の設計が要る |
| `ext2_rename` 途中の親 links / ctime 更新 | **そのまま** | 同じ理由 (名前の移動は済んでいる) |
| `ext2_create` の失敗後始末の `free_all_blocks` | `(void)` で明示 | 既に失敗を返す途中で、それ以上に良い手が無い |

`ext2_sync` は `ext2_write_super_raw` がブロック 1 を read-modify-write するので、
その**読み出し**を落とせばホスト試験で失敗させられる (`[SYNC]` の段)。

**削除時の inode (再開時に見つけた穴)**: `ext2_unlink` / `ext2_rmdir` は
`delete_entry` の**あと**でブロックを返すので、返しきれなくても名前は既に
消えている — そこで「消えていない」とは言えず、**返しきれなかったことだけを
エラーで報告する**。ただし**そのときは inode を解放しない**。
`free_all_blocks` は失敗時にブロックの指し先を inode に残すので、inode の
ビットまで空きに戻すと、次の `ext2_alloc_inode` が同じ番号を配って
`ext2_create` が inode を上書きした瞬間に**残したブロックを指すものが消える**
(本当の漏れ)。inode を「links 0・dtime 付き・使用中」で残せば孤児として
辿れ、e2fsck が回収できる。

### 7-4. `ext2_free_all_blocks` の方針

**直す前**: 間接表が読めないと内側のループを飛ばし、**表ブロック自体は解放して
ポインタを 0 にしていた** → 配下のデータブロックが使用中のまま行方不明。

**方針: 「全部返せると分かってから返す」**。先に**下見** (`ext2_free_probe`) で
単一間接表・二重間接表・その内側の表を全部読み、**1 本でも読めなければ
何ひとつ解放せずに** `EXT2_ERR_IO` を返す。戻り値は `void` から `int` に変えた。

**途中まで解放して中断してはいけない理由**: 呼び手 `ext2_write` は失敗すると
**inode を書かずに戻る**ので、媒体上の inode は解放済みブロックを指したまま
になる。そのブロックが次の割り当てで別ファイルへ渡ると、2 つの inode が
同じブロックを指す — 2026-09-06 に踏んだ相互リンクと同じ壊れ方
(gotcha §4-24)。直接ブロックを先に解放する元の順序のままでは、間接表で
失敗した時点で既に直接ブロックを返してしまっているので、**下見が必須**だった。

**代償**: 間接ブロックを持つファイル (12KB 超) の切り詰め・削除で、
**間接表の読み出しが 2 倍**になる。直接ブロックだけのファイルは下見が
何も読まないので従来どおり (`test_ext2_write_io` のセクタ数の期待値は不変)。

**残る穴 ([V4])**: 下見は通ったのに本番の解放中にだけ読めなくなった場合は
途中まで解放してしまう。一度きりの失敗なら下見が先に消費するので起きないが、
下見と本番のあいだで初めて壊れた場合は防げない。完全に閉じるにはジャーナルが要り、
本票の範囲を越える。

### 7-5. 追加試験と件数、変異

**`test_b8_open.py`: 261 → 491 checks, 0 failures** (230 件増)

| 名前 | 中身 |
|---|---|
| `case_mkdir_existence_failure` [P1-1] | `vfs_mkdir("/big/keepme")` の存在確認を一度落とす → 失敗を返す・**セクタ書き込み 0**・同名エントリ 1 件のまま・中身無事。回帰で EXIST と新規作成 |
| `case_create_existence_failure` [P1-2] | `ext2_create` を直接。同上 |
| `case_rename_dest_failure` [P1-3] | `vfs_rename("/etc/plain", "/big/keepme")` の宛先確認を一度落とす → セクタ書き込み 0・宛先が二重にならない・**移動元の名前が残る**。回帰で置き換え |
| `case_write_stream_inode_failure` [P1-5] | 5 バイトに 4 バイト追記、inode 更新の読み出しだけ落とす → 4 を返さない・サイズ 5 のまま。やり直すと **戻り値 4 と媒体上のサイズ 9 が一致**し中身も一致 |
| `case_free_all_blocks_failure` | 16KB ファイルの上書き中に間接表が読めない → **ビットマップ上で**直接ブロック・間接表・その先の実データが使用中のまま (inode は書き戻されないので inode を見ても分からない)、次に作るファイルがそれらを受け取らない、中身無事 |
| `case_unlink_keeps_inode` | 削除でブロックを返しきれない → IO を返す・名前は消える・**inode もブロックも使用中のまま**・次のファイルが同じ inode 番号を受け取らない |
| `case_rmdir_keeps_inode` | 間接ブロックを持つ空ディレクトリを作り、空判定の読み出しは通して下見だけ落とす → 同上 |
| `case_trailing_sync_failure` | スーパーブロックの読み出しを落として create / write / mkdir / 追記 / rename が IO を返すこと。回帰で全部通る |

**`test_b8_hostdrv.py`: 新規 61 checks, 0 failures** — 実物の `fs/hostdrvfs.c`

| 名前 | 中身 |
|---|---|
| E1 | `hdrv_get_file_size` 直接: 通常ファイルはサイズ、**`Directory`=1 / 2 は `ISDIR`**、QUERY 失敗は IO |
| E2 | OPEN の各 NTSTATUS: NAME/PATH_NOT_FOUND だけ NOTFOUND、SHARING_VIOLATION 等は NOTFOUND でない、NAME_INVALID は INVAL、**無応答 (番兵) は IO** |
| E3 | `hdrv_read_file` / `hdrv_stat` / `hdrv_read_stream` / `hdrv_unlink` / `hdrv_rename` も畳まない。本当に無いなら従来どおり NOTFOUND |
| E4 | 切り詰めの印 (`OVERWRITE_IF` の CREATE) の対照を取り、OPEN 失敗・無応答・ディレクトリで**それが 0 回**であること |
| E5 | `hdrv_create_status_to_vfs` の対応表そのもの |

**変異 (2 巡目の修正 19 件): 19 / 19 RED** — いずれも**コンパイルは通り**、試験が振る舞いで落とした

| 変異 | 落ちた件数 |
|---|---|
| P1-1 mkdir が I/O を素通り | 7 |
| P1-2 ext2_create 同 | 6 |
| P1-3 rename 宛先確認を「無い」扱い | 9 |
| P1-5a write_stream が inode 更新失敗を捨てる | 2 |
| P1-5b write_stream が sync 失敗を捨てる | 1 |
| SYNC-a ext2_create 最後の sync を捨てる | 1 |
| SYNC-b ext2_write 同 | 1 |
| SYNC-c mkdir / rename 同 | 2 |
| FREE-a free_all_blocks が下見の結果を無視 | 7 |
| FREE-b free_all_blocks を直す前の形に | 18 |
| FREE-c ext2_write が free の失敗を無視 | 5 |
| KEEP-a unlink が返しきれなくても inode を解放 | 5 |
| KEEP-b rmdir 同 | 1 |
| KEEP-c unlink が返しきれなかったことを報告しない | 1 |
| P1-4a hostdrv_create が失敗を一律 -1 に | 7 |
| P1-4b 呼び手 11 か所が NOTFOUND に畳む | 17 |
| P1-4c 対応表の既定を NOTFOUND に | 19 |
| P1-4d 無応答 (番兵) を NOTFOUND に | 3 |
| P1-4e get_file_size が Directory を見ない | 4 |

**1 巡目の変異 18 件も再実行して 18 / 18 RED のまま** (M1 71 / M2 37 / M3 57 /
M4 36 / M5 58 / M6 2 / M7 1 / M8 3 / M9 1 / M10 4 / M11 1 / M12 2 / M13 2 /
M14 1 / M15 1 / M16 1 / M17 2 / M18 4)。件数が §4 より増えているのは試験が
増えたため。

### 7-6. 触ったファイル (2 巡目)

`fs/ext2_dir.c` `fs/ext2_file.c` `fs/ext2_inode.c` `fs/ext2_priv.h`
`fs/hostdrvfs.c` `fs/hostdrvfs_proto.h` `fs/hostdrv_stat_rules.inc`、
`build/sdk.mk`、`tools/tests/b8_open_host.c` `tools/tests/ext2_read_bound_host.c`
(スタブの型を `int` に)、新規 `tools/tests/b8_hostdrv_host.c`
`tools/tests/test_b8_hostdrv.py` `tools/tests/hostdrv_hostshim/io.h`。
`sdk/kapi.json` 不変。NP21/W のソースは読むだけ。

### 7-7. 実機で見ていないこと ([V4])

- HostDrv の NTSTATUS の写し方は、NP21/W のソースと贋エミュレータ上の
  試験で確かめただけ。**実エミュレータで `SHARING_VIOLATION` などを起こして
  見てはいない**。
- ext2 側はすべて RAM ディスク上。実 NHD では走らせていない。

## 8. 往復 3 — 解放の順序 (Codex P1-A / P1-B / P1-C)

- 基点: `7c182d3` (往復 2 の着地)
- 出所: Codex 往復 3 の P1 3 件 (すべて解放処理)。PM がコードで全件確認し、
  「窓は Codex の反例より広い」(解放から inode の書き戻しまでの区間全体) と分析
- 実行: `python3 -B tools/tests/test_b8_open.py --target` (約 3 秒)
- **§7-4 の「代償」「残る穴」は本節で置き換わった** (下見は廃止)

### 8-0. 正直に書く ([V4])

- **往復 2 で「残る穴」として自分で書いたものが、到達可能な P1 だった。**
  下見 (pre-read) は誤った順序の一か所を塞いだだけで、安全性の根拠にならない。
- 往復 3 の途中で入れた「作りかけの表を何本目まで信用するか」(`limit`) は、
  変異試験で**効いていない** (外しても何も落ちない) と分かったので外した。
  効かない理由は §8-2 の (1)(2)。
- **掃引で本票の範囲外の欠陥を 1 件見つけて直した** (§8-4、`ext2_add_entry`)。
- 最初の変異の回で 5 件が GREEN、1 件がビルド失敗 (変異側の C89 違反) だった。
  §8-6 に経緯を書き、試験を足して全件 RED にした。

### 8-1. 不変条件と順序

> **媒体上のどの参照も、解放済みのブロックを指してはならない。**

解放済みのブロックは次の `ext2_alloc_block` で別ファイルへ渡るので、それを指す
参照が媒体に残ると 2 つのファイルが同じブロックを共有する (gotcha §4-24)。

**ジャーナルは作らず、書き込み順序で閉じた。**

| 関数 | 何をどの順に書くか |
|---|---|
| `ext2_truncate_blocks(ctx, ino, inode, &leaked)` (新設、`fs/ext2_inode.c`) | 1. `block[]` 15 本を写す → 2. ポインタ・`blocks`・`size` を 0 にして **inode を先に書く** (失敗したら何も返さず、メモリ上のポインタを戻して IO) → 3. **写しを辿って**返す (`ext2_release_blocks`)。3 の失敗は `*leaked = 1` |
| `ext2_release_blocks(ctx, blocks)` (新設) | 写しの直接 → 単一間接 (表は `g_blk`) → 二重間接 (外 `g_blk` / 内 `g_dat`) の順に返す。**読めない表は配下ごと漏らし、残りは返し続ける** (参照はもう無いので途中で止める理由が無い) |
| `ext2_free_all_blocks` | **廃止**。「返してから呼び手が inode を書く」順序の関数名を残すと取り違えるため、名前ごと消した |
| 下見 (`ext2_free_probe`) | **廃止**。順序で閉じたので不要 |

### 8-2. `ext2_bmap_set` — 新しい表は「書き終えてから指させる」

後始末で表を辿るには、**メモリ上の inode から辿れる表の中身が信用できる**必要が
ある。以前の `ext2_bmap_set` は先にポインタを入れてから表を書いていたので、その
書き込みが落ちると**ゴミの入った表**を指したまま戻った (呼び手が inode を書くと、
ゴミが指す任意のブロックへの参照が媒体に載る)。

| 経路 | 直した順序 | 失敗時 |
|---|---|---|
| 新しい単一間接表 | 表を組んで書く → 成功してから `inode->block[IND]` に入れる | 表を返して IO (誰も指していない) |
| 新しい二重間接表 | 0 で書く → 成功してから指す | 同上 |
| 新しい内側の表 (ind1) | **中身 (項目込み) を書く** → 二重間接表を読み直す → 項目を入れて書く | 1・2 段目の失敗は表を返す。**3 段目の失敗は返さない** (指されたかもしれない) |
| 既存の表へ項目を足す | (従来どおり) | IO = **その項目が媒体の表に載ったかもしれない** |

戻り値の約束を明文化した: **NOSPC なら `phys_block` はどこにも載っていない
(返してよい)、IO なら載ったかもしれない (媒体上の inode から辿れる表なら返しては
いけない)**。

後始末で表を信用してよい理由 (= `limit` が効かなかった理由):
1. 写しから辿れる表の中身は必ず一度は全部書かれている (上の順序)
2. 途中で落ちた表で信用できないのはその回の 1 項目だけで、そのブロックは
   呼び手が既に返している。もう一度返しても `ext2_free_block` は「既に空き」
   として何もしない (その間に割り当ては挟まらない)

### 8-3. 呼び手ごとの扱い

#### `ext2_free_block` の呼び出し元 (全 15 か所) と `ext2_free_inode` (全 6 か所)

| 場所 | 文脈 | 参照は媒体に無いか | 失敗の扱い |
|---|---|---|---|
| `ext2_inode.c:339` | bmap_set 新しい単一間接表の書き込み失敗 | 無い (まだ指していない) | 漏れ |
| `ext2_inode.c:365` | bmap_set 新しい二重間接表の書き込み失敗 | 無い | 漏れ |
| `ext2_inode.c:386` | bmap_set 新しい ind1 の書き込み失敗 | 無い | 漏れ |
| `ext2_inode.c:393` | bmap_set 二重間接表の読み直し失敗 | 無い (まだ項目を入れていない) | 漏れ |
| `ext2_inode.c:479/488/490/507/509/511` | `ext2_release_blocks` の中 (6 か所) | 呼び手の前提 | `leaked = 1` を立てて続ける |
| `ext2_file.c:175` | create: bmap_set 失敗の `blk` | 無い (inode 未書き込み) | 漏れ (元の失敗を返す) |
| `ext2_file.c:270` | ext2_write: bmap_set 失敗の `blk` | 無い (inode は切り詰めで 0 本を指す) | 漏れ |
| `ext2_file.c:354` | write_stream: bmap_set 失敗の `new_blk` | **有るかもしれない** (既存ファイルの表) | **NOSPC のときだけ返す**。IO なら返さない (**以前は常に返していた = 相互リンク**) |
| `ext2_dir.c:180` | add_entry: bmap_set 失敗の `new_blk` | **有るかもしれない** (ディレクトリの既存の表) | **NOSPC のときだけ返す** (以前は常に返していた) |
| `ext2_dir.c:319` | mkdir: "." ".." の書き込み失敗 | 無い (inode 未書き込み) | 漏れ |
| `ext2_file.c:126` (free_inode) | create 後始末 (inode 未書き込み) | 無い | 漏れ |
| `ext2_file.c:216` (free_inode) | create の add_entry NOSPC | **切り詰めが成功したときだけ**呼ぶ | 漏れ |
| `ext2_file.c:490` (free_inode) | unlink | 切り詰め成功のときだけ | 返せなければ IO |
| `ext2_dir.c:290/320` (free_inode) | mkdir の割り当て失敗・"." 書き込み失敗 | 無い | 漏れ |
| `ext2_dir.c:422` (free_inode) | rmdir | 切り詰め成功のときだけ | 返せなければ IO |

`ext2_free_block` / `ext2_free_inode` 本体 (P1-B): 範囲外は INVAL、読み出し失敗・
書き込み失敗は IO、**空き数は書き込みが成功したときだけ動かす**、既に空きなら
何もせず OK (二重に数えない)。書き込みが途中で落ちるとビットは消えたかもしれないが、
空き数は実際より**少なく**見えるだけ (安全側)。

#### unlink / rmdir の inode 解放 — 結論と理由

**結論: 参照を外す inode の書き込みが成功したら、ブロックを返しきれなくても
inode は返す。書き込み自体が落ちたら、何も返さない (inode もブロックも孤児で残す)。**

- 往復 2 で「返しきれなければ inode を残す」にしたのは、**返しきれなかった
  ブロックを inode がまだ指していた**から (inode を返すと唯一の参照が再利用で
  上書きされ、辿れない漏れになる)。往復 3 は先に参照を外すので、inode はもう
  何も指しておらず、**残してもそのブロックへは辿れない**。残す理由が消えた。
- 書き込みが落ちたときは、媒体上の inode がブロックを指しているかもしれない
  (1KB ブロックの前半だけ書けた場合を含め、区別できない)。ブロックを返すと
  相互リンクになる。inode を残せば、ブロックはその孤児から辿れる整合した状態で
  残り、e2fsck が回収できる。

#### create の後始末 (P1-C) — `(void)` は全廃

| 失敗の位置 | 媒体上の inode | 扱い |
|---|---|---|
| ループ中 (割り当て・bmap_set・データ書き込み) | **まだ書いていない** (新しいブロックを指さない) | `ext2_create_abort_unwritten`: 写しのブロックを返し、inode を返す。どちらの失敗も漏れ |
| inode の書き込み | 載ったか**区別できない** | **何も返さない** (載っていれば、inode のビットを戻せなかったときに解放済みを指す) |
| add_entry が NOSPC | 書いた (ブロックを指す) / 名前は載っていない | `ext2_truncate_blocks` で**参照を外してから**返し、成功したときだけ inode を返す |
| add_entry がそれ以外 | 書いた / **名前が載ったか区別できない** | **何も触らない** (載っていれば完成したファイル、載っていなければ孤児。取り消すと、名前が解放済み inode を指し得る) |

#### ext2_write の失敗 (PM の ②③)

切り詰めで媒体上の inode を 0 本にしてから書くので、**以降のどこで落ちても
旧ブロックは参照されていない**。新しいブロックと表はメモリ上の inode からしか
辿れないので、後始末で全部返す。最後の inode 書き込みが落ちたときは、新しい内容が
載ったか区別できないので返さない (漏れ)。

**代償: 切り詰め後に落ちるとファイルは 0 バイトになる** (旧内容は戻らない)。
往復 2 までは旧内容が残って見えたが、それは解放済みブロックを指したまま = 相互
リンク寸前の状態だった。旧内容を残したいなら「新ブロックを全部書いてから inode
を差し替え、その後で旧ブロックを返す」順序がある (これも不変条件を守る) が、
**旧と新の両方の領域が同時に要る** (空きの少ないディスクで大きなファイルを
上書きすると NOSPC になる) ので、PM 指定の順序を採った。

**漏れは成功と言わない**: ext2_write / unlink / rmdir は、中身の操作が済んでも
返しきれなかったら IO を返す。

### 8-4. 交差リンク検査と総当たり掃引

**検査** (`media_check`、`tools/tests/b8_open_host.c`): **ext2 のコードを通さず
RAM ディスクを直に読む** (検査対象と同じ読み方をすると同じ誤りを共有して見逃す)。
ビットマップの立っている全 inode から、直接・単一間接・二重間接の全参照を辿って

| 数える値 | 意味 | 許容 |
|---|---|---|
| `freed_ref` | 辿れるのにビットマップ上は空きのブロック (**相互リンクの前段**) | **0 必須** |
| `dup_ref` | 2 か所から指されるブロック (相互リンクそのもの) | **0 必須** |
| `bad_ref` | 範囲外の番号 (ゴミの表) | **0 必須** |
| `dangling` | 空き inode を指す名前 (ディレクトリも辿る) | **0 必須** |
| `unref_inuse` | 使用中だが誰も指さない (メタデータ + **漏れ**) | 操作前との差を**漏れ件数**として報告 |

**掃引** (`sweep`): 操作を「仕掛けてから at 回目のセクタ I/O (読み書き問わず)
が落ちる」形で at = 1, 2, … と**全位置**で動かし、`once` (その 1 回だけ) と
`sticky` (そこから先すべて = 装置が途中で消えた) の両方。1KB ブロック = 2 セクタ
なので「ブロックの前半だけ書けた」も自然に出る。1 回ごとに検査し、取り消し記録
でディスクを戻し、メモリ上の状態も読み直す。**空きブロックは生きたファイルの
ブロック番号で埋めてから**走らせる (ゴミの表を信用する誤りが 0 を読んで無害に
見えないように)。

| 掃引 | 回数 | 失敗を返した回 | 漏れた回 | 最大漏れ | 不整合 | 漏れを成功と言った回 |
|---|---|---|---|---|---|---|
| ext2_write 上書き (二重間接 → 単一間接) | 2566 | 2564 | 2109 | 274 | **0** | 0 |
| ext2_write 伸長 (4B → 二重間接) | 5426 | 5424 | 2968 | 274 | **0** | 0 |
| ext2_create (二重間接まで) | 5426 | 5420 | 2962 | 274 | **0** | 0 |
| ext2_unlink (二重間接つき) | 2270 | 2248 | 1931 | 274 | **0** | 0 |
| ext2_rmdir (間接つきの空ディレクトリ) | 290 | 278 | 115 | 16 | **0** | 0 |
| write_stream 追記 (直接 → 単一間接) | 126 | 84 | 53 | 4 | **0** | (対象外) |
| write_stream 追記 (単一間接 → 二重間接) | 170 | 108 | 85 | 5 | **0** | (対象外) |
| ext2_create (ディレクトリが単一間接へ伸びる) | 182 | 180 | 33 | 2 | **0** | 0 |
| ext2_create (ディレクトリの既存の単一間接に足す) | 202 | 200 | 20 | 1 | **0** | 0 |
| ext2_mkdir | 86 | 74 | 12 | 1 | **0** | 0 |

最大漏れ 274 は、270 ブロックのファイルを返している途中で装置が消えた場合
(データ 270 + 表 4)。write_stream は途中まで書けたバイト数を返す約束なので
「漏れを成功と言った回」の判定から外した。

**掃引で見つけた本票の範囲外の欠陥 (直した)**: `ext2_add_entry` の新ブロック
経路が、ディレクトリ inode の書き込み失敗を捨てていた。この inode は**新しい
ブロックへの参照そのもの** (直接ポインタ、または新しい単一間接表へのポインタ) を
運ぶので、書けなければ名前は辿れない。**create が成功を返し、名前が消え、表と
新ブロックが漏れた** (掃引の at=79 once)。書き込み失敗を IO で返すようにした
(ブロックは返さない)。既存ブロックへ足す経路の mtime 更新は往復 2 の判断どおり
返さない (名前は既に載っているので、失敗を返すと二重に作られる)。

### 8-5. 追加・書き換えた試験

**`test_b8_open.py`: 491 → 619 checks, 0 failures**

| 名前 | 中身 |
|---|---|
| `case_reread_after_probe` [**P1-A**] | Codex の反例そのもの。16KB の上書き中に間接表の読み出しを 1 回目 / 2 回目で落とす。**媒体整合**、落ちたら IO・新しい中身 (5B) は読める・表と配下 4 本が使用中のまま・**漏れ 5 件** |
| `case_free_block_bitmap_failure` [**P1-B**] | ビットマップの読み出し失敗・書き込み失敗・**後半セクタだけ**の書き込み失敗で IO を返し空き数を動かさない。二重に返しても数えない。unlink 中にビットマップへ 1 本も書けない → IO・**媒体整合**・inode は返す・漏れ 17 件 |
| `case_rewrite_new_block_failure` [**PM ②③**] | 空打ちで新しい配置を知り、切り詰め後の新しい表 (②) / 最初のデータブロック (③) の書き込みを落とす。IO・**媒体整合**・ファイルは 0 バイトでポインタ 0・漏れ 0 |
| `case_create_cleanup_failure` [**P1-C**] | 空打ちで書き込み順を記録し、データの書き込みから落とす。(a) 1 回だけ → 漏れ 0・inode も返る (b) 後始末も全部落ちる → IO・**媒体整合**・名前なし・漏れ 1 (媒体上の inode は一度も書いていないのでそのブロックを指さない) |
| `case_create_inode_write_ambiguous` [P1-C''] | inode の書き込みが後半セクタだけ落ち (inode は実は載る)、後始末の inode 解放も落ちる。何も返さず**媒体整合**。掃引の `once` / `sticky` では作れない離れた 2 か所の組み合わせ |
| `case_unlink_release_failure` (往復 2 の `case_unlink_keeps_inode` を書き換え) | 間接表が読めない → IO・**媒体整合**・**inode は返す**・媒体上のポインタ 0・漏れ 5。参照を外す書き込み自体が落ちる → **孤児で残す** |
| `case_rmdir_release_failure` (往復 2 の `case_rmdir_keeps_inode` を書き換え) | 同じ 2 通りを rmdir で |
| `case_free_all_blocks_failure` (往復 2) | **削除** — 旧契約 (上書き失敗で旧内容が残る) を固定していたため。`case_reread_after_probe` が置き換える |
| `stage_c_sweeps` | §8-4 の 10 掃引 |
| `case_create_add_entry_nospc` (段 D、空き 1 ブロックの新しいディスク) | add_entry が NOSPC → 漏れ 0・inode 数が戻る / 後始末の解放が落ちる → 漏れ 1・inode は返す / 参照を外す書き込みが落ちる → inode とブロックを孤児で残す (漏れ 0) |

足場: 書き込み側の失敗注入 (`wfail_*`)、掃引用の注入 (`sw_*`、読み書きの種別で
絞れる)、取り消し記録 (`undo_*`)、書き込み先の記録 (`g_wlog`)。
`tools/tests/ext2_read_bound_host.c` のスタブを新しい関数形に合わせた。

### 8-6. 変異 (往復 3、23 件): **23 / 23 RED、ビルド失敗 0**

MEDIA = **媒体検査 (交差リンク検査) が落ちた**、RC = 戻り値や期待値の検査だけが落ちた。

| 変異 | 判定 | 落ちた件数 |
|---|---|---|
| **ORDER-a** truncate: 返してから inode を書く (**順序を元に戻す**) | **MEDIA** | 13 (掃引: 上書き 1105 回・unlink 1105 回・伸長 9 回が不整合) |
| **ORDER-b** ext2_write を往復 2 の形に (返してから最後に inode を書く) | **MEDIA** | 32 (上書き 1413 回・伸長 2685 回が不整合) |
| P1B-a free_block: 書き込み失敗を捨てて空き数を増やす | RC | 11 |
| P1B-b free_block: 読み出し失敗を成功と言う | RC | 5 |
| P1B-c free_block: 空き数を書き込みの前に動かす | RC | 4 |
| GUARD-a free_block: 既に空きでも空き数を増やす | RC | 1 |
| BMAPSET-a 新しい単一間接表を書く前に指させる | MEDIA | 3 |
| BMAPSET-b 新しい ind1 を書く前に指させる | MEDIA | 3 |
| P1C-a create (未書き込み) の後始末でブロックを返さない | RC | 2 |
| P1C-b create add_entry NOSPC: 参照を外せなくても inode を返す | RC | 2 |
| P1C-c create add_entry: NOSPC 以外でも取り消す | MEDIA | 3 |
| P1C-d create inode 書き込み失敗: 区別できないのに返す | MEDIA | 6 |
| WRITE-b ext2_write 最後の inode 書き込み失敗で返す | MEDIA | 1 |
| LEAK-a / LEAK-b / LEAK-c write / unlink / rmdir が漏れを成功と言う | RC | 3 / 3 / 2 |
| KEEP-a unlink: 返しきれなければ inode を残す (往復 2 の形) | RC | 2 |
| KEEP-b / KEEP-c unlink / rmdir: 参照を外せなくても inode を返す | RC | 1 / 2 |
| STREAM-a write_stream: bmap_set IO でも new_blk を返す (元の形) | MEDIA | 2 |
| ADDENT-a add_entry: bmap_set IO でも new_blk を返す (元の形) | MEDIA | 1 |
| ADDENT-b add_entry 新ブロック: dir inode 書き込み失敗を捨てる (掃引で発見) | RC | 1 |
| MKDIR-a mkdir: inode 書き込み失敗を捨てて名前を付ける (元の形) | RC | 1 |

**最初の回の経緯**: `limit` を無視する変異 2 件 (LIMIT-a / WRITE-a) が GREEN →
§8-2 の理由で効いていないと判断して `limit` を外した。P1C-d / KEEP-c / ADDENT-a が
GREEN → それぞれ `case_create_inode_write_ambiguous`・rmdir の「参照を外す書き込み
が落ちる」段・「既存の単一間接へ足す」掃引を足して RED にした。P1C-a は変異側の
C89 違反でビルド失敗 → 変異を直して RED。

**往復 1 の 18 件と往復 2 の 13 件 (FREE-* / KEEP-* 6 件は往復 3 で置き換え) も
再実行して全件 RED**。M1 と SYNC-a / SYNC-b は変更後の文面に合わせて変異の文字列を
直した (M1 72 件、SYNC-a 1、SYNC-b 1)。

### 8-7. 残る制限 ([V4])

- **漏れは残る。** OS32 にはゲスト内の fsck が無いので、I/O 失敗を繰り返すと
  空き容量は減り得る。ホスト側の `e2fsck` で回収できる (漏れは「使用中だが誰も
  指さない」なので、e2fsck の block bitmap differences として直る)。
- **切り詰め後の失敗でファイルは 0 バイトになる** (§8-3)。
- **rename の add_entry が失敗したとき、名前が載ったか区別できない**
  (ディレクトリブロックの書き込みが途中で落ちる場合)。載っていれば rename は失敗を
  返したまま 2 つの名前が同じ inode (links 1) を指し、後で片方を unlink すると
  もう片方が解放済み inode を指す。**往復 3 では直していない** (解放処理ではなく
  rename の原子性の問題。別票候補)。
- 掃引は `once` と `sticky` だけ。離れた 2 か所の失敗の組み合わせは
  `case_create_inode_write_ambiguous` の 1 通りしか見ていない。
- ext2 側はすべて RAM ディスク上。実 NHD では走らせていない。

### 8-8. 触ったファイル (往復 3)

`fs/ext2_inode.c` `fs/ext2_file.c` `fs/ext2_dir.c` `fs/ext2_priv.h`、
`tools/tests/b8_open_host.c` `tools/tests/ext2_read_bound_host.c`
`tools/tests/b8_tdd.md`。`sdk/kapi.json` 不変。

## 9. 往復 4 — 名前と inode の順序 (レビュー X1 / X2 / X3)

- 基点: `bf957e4` (往復 3 の着地)
- 出所: Fable 5.1 サブエージェントのレビュー (Request changes)。反例を実行済みで、
  PM も `bf957e4` で再現。再現器は `rv_host.c` (本試験の写し + 反例)
- 評価: **ブロックの参照については「書き込み順序で閉じる」は成立**。inode は 128B で
  セクタを跨がないので、inode の部分書き込みは「届いた / 届かない」の二値になる。
  **成立していなかったのは同じ不変条件の inode 側 = 「解放済み inode を指す名前」**
- 実行: `python3 -B tools/tests/test_b8_open.py --target` (約 5 秒)

### 9-0. 正直に書く ([V4])

- 往復 3 で表に入れた「書き終えてから繋ぐ」を、**表に載せるディレクトリブロック
  (X1) に適用していなかった**。
- 往復 3 の掃引が X1 / X2 を見逃したのは、**ゴミの模様**と**配置**に依存していた
  から。X1: 番号の模様をディレクトリとして読むと使用中の inode 番号に当たり
  dangling にならなかった。X2: 掃引で足すエントリは 1 セクタ目に収まっていた。
- rename の原子性は往復 3 で「範囲外 (別票候補)」と書いたが、**到達可能**だった
  (sticky 故障 1 回で足りる)。
- 旧コードの `fs/ext2_dir.c` (`bf957e4`) に往復 4 の試験を当てると
  **892 checks 中 62 failures** (X1・X2・X3・rmdir・mkdir・rename の各掃引が不整合)。
- **X2 の「0 書き」を外す変異は媒体検査では落ちない** (§9-6)。追加の二段書きが
  独立に X2 を防ぐため。0 書きは多重の備えで、直接の検査 (スラックの inode 番号が
  0 か) だけが落とす。

### 9-1. X1: `ext2_add_entry` の新ブロック — 中身 → size → 繋ぐ

**直す前**: `ext2_bmap_set(new_blk)` (既存の単一間接表ならここで媒体に載る) →
新ブロックの中身を書く。中身の書き込みが落ちると、前の持ち主のバイト列が
ディレクトリとして見える (`find_entry` / `list_dir` は size ではなく bmap で走査)。
レビュー実測: ゴミの「inode=956 (未使用)、名前 q」が見え、次の `victim` が inode 956
を受け取って q と別名になり、`unlink(q)` が victim を壊した。

| 段 | 書くもの | その段で落ちたときの媒体 |
|---|---|---|
| 1 | 新ブロックの中身 (`g_aux`。`bmap_set` が `g_aux` を潰すので**先に書き終える**、§4-24) | どこからも辿れない。new_blk を返す |
| 2 | ディレクトリの size を `(bi+1)*1KB` まで伸ばして inode を書く | まだ繋いでいない。new_blk を返す。size が届いていれば**末尾の穴** (次の追加が同じ位置を埋め、`max` で伸ばすので size はずれ続けない) |
| 3 | `ext2_bmap_set` (既存の間接表ならここで媒体に載る) | NOSPC: 載っていない、返す。IO: 載ったかもしれない、**返さない** (載っていれば中身は正しく、size の内側) |
| 4 | inode (直接ポインタ / 新しい単一間接表へのポインタはここで載る) | 載ったか区別できない。返さない (漏れ) |

size を繋ぐ前に伸ばすのは、**「size より先に繋がったブロック」(= 検査 (b) の
不整合) を作らない**ため。逆向き (size の内側の末尾が繋がっていない) は許容する。

### 9-2. X2: 消した名前の復活 — 0 書きと二段書き

**直す前**:
- `ext2_delete_entry` の併合は前のエントリの rec_len を伸ばすだけで、**消した
  エントリのバイト列 (inode 番号) をスラックに残した**。
- `ext2_add_entry` の分割は、前のエントリの rec_len を縮める書き込みと新エントリの
  書き込みを 1 回の `ext2_write_block` で行い、**2 つが別セクタに載り得た**。
  `ext2_write_block` はセクタ 0 → 1 の順なので、後半だけ落ちると rec_len は
  縮んだまま新エントリの位置にスラックの古いバイト列が残り、**消した名前が復活**。
  その inode 番号が別ファイル F に再利用されていれば F を別名で指し、unlink が
  F の inode とブロックを返した (F の名前は残る)。

**直し方 (2 つ、どちらも入れた)**:

1. **併合でも消したエントリの inode 番号を 0 にする** (`ext2_delete_entry`)。
   I/O は増えない。rec_len と inode 番号が別セクタに載っても、どちらか一方が
   届けばエントリは見えなくなる。
2. **見せる書き込みを 1 セクタのフィールド 1 つに寄せる** (`ext2_add_entry`)。
   - 分割: 見せる瞬間 = 前のエントリの **rec_len を縮める**こと
   - 空きエントリの再利用: 見せる瞬間 = **inode 番号を入れる**こと
   - 新エントリの中身 (分割なら inode 番号込み、再利用なら inode 番号 0 のまま) と
     見せるフィールドが**別セクタに載るときだけ**、先に中身だけを書く
     (まだスラック / inode 0 なので見えない)。その後で見せるフィールドを書く。
   - 同じセクタに収まるときは従来どおり 1 回 (I/O を増やさない)。
   - 判定は `ext2_same_sector()`、セクタ幅は `EXT2_SECTOR_SIZE` (`fs/ext2_priv.h`、
     `ext2_write_block` の書き方と結び付けたコメント付き)。

**2 を入れた理由**: 1 だけでは**既存の NHD に残っている旧コードの削除の跡**
(スラックに inode 番号が残ったまま) を守れない。hsync は削除と作成を繰り返すので、
使っているディスクにはこの跡がある。`[X2-legacy]` と掃引の「旧コードの跡」で
この状態を媒体へ直に書いて作り、2 だけで防げることを見ている。

| 段 (分割で別セクタのとき) | 落ちたときの媒体 |
|---|---|
| 1. 中身だけ (rec_len は元のまま) | 新エントリはスラックの中 = 見えない |
| 2. rec_len を縮める (1 セクタ) | 届いた: 名前 1 つ増えて完成 / 届かない: 見えない |

### 9-3. X3: rename — links_count を先に上げ、最後に下げる

不変条件: **どの inode も、それを指す名前の数 ≤ links_count**。

**直す前**: add_entry (新しい名前) → delete_entry (古い名前) の間 links_count を
上げていなかったので、その間で装置が消えると 2 つの名前が links 1 の inode を
指したまま失敗を返した。復旧後に片方を unlink すると links が 0 になって inode と
ブロックが返り、**もう片方が解放済み inode を指す**。レビュー実測: sticky 故障
35 位置中 10 位置で 2 名、10/10 で dangling。また「古い名前を消せなければ新しい
名前を消して巻き戻す」は、古い名前の削除が実は届いていた場合に**名前を 0 にする**。

| 段 | 書くもの | その段で落ちたときの媒体 |
|---|---|---|
| 1 | 移す inode の links_count +1 (ctime も) | 名前 1、links は元か +1 (多い側 = 安全) |
| 2 | (ディレクトリを別の親へ) 新しい親の links_count +1 | 同上。新しい親の links が多いかもしれない |
| 3 | 新しい名前 (`ext2_add_entry`) | 名前 1 か 2、links は +1 済み。**NOSPC なら名前は載っていない**ので 1・2 を戻す (戻せなくても多い側) |
| 4 | 古い名前を消す (`ext2_delete_entry`) | 名前 2 か 1、links +1 = 整合。**巻き戻さない** (往復 3 までの巻き戻しは外した) |
| 5 | (ディレクトリを別の親へ) ".." を新しい親へ、古い親の links_count −1 | ".." が古い親を指したままなら古い親の links は下げていない |
| 6 | 移す inode の links_count −1 | links が 1 多い (孤児側。e2fsck が直す) |

**代償**: 失敗したとき名前が 2 つ残ることがある (ハードリンクとして整合)。
ディレクトリの rename が 4 で落ちると、ディレクトリに名前が 2 つ残る
(links は合っているが、ext2 はディレクトリのハードリンクを想定しない。e2fsck が
直す)。1 回の rename で inode の書き込みが 2 回増える (1 と 6)。

**同じ形の洗い出し (直した)**:
- `ext2_mkdir`: 新しいディレクトリの ".." が載る**前に**親の links_count を上げる
  ように並べ替えた (以前は add_entry の後で上げていた = その間は親を指す名前が
  links より多い)。
- `ext2_rmdir`: 親の links_count を下げるのは、**参照を外す inode の書き込みが
  成功したときだけ** (落ちると孤児のディレクトリブロックの ".." が親を指したまま)。
- `ext2_mkdir` の add_entry が NOSPC (レビュー非 blocker): `ext2_create` と揃え、
  参照を外して inode とブロックを返し、親の links も戻す。NOSPC 以外 (I/O) は
  名前が載ったか区別できないので従来どおり孤児で残す。
- `ext2_unlink` (名前を消してから links −1)、`ext2_create` (links 1 の inode を
  書いてから名前) は元から正しい順序。

### 9-4. `media_check` に足した検査

| 値 | 意味 | 扱い |
|---|---|---|
| `links_short` (a) | その inode を指す名前の数 (ディレクトリの全ブロックの全エントリ、"." ".." 含む) **> links_count** | **不整合** |
| `links_surplus` (a) | 名前の数 < links_count (孤児側) | 許容、掃引で件数を報告 |
| `dir_overrun` (b) | ディレクトリで **size より先のブロックが繋がっている** | **不整合** |
| `dir_hole` (b) | size の内側の末尾が繋がっていない | 許容、件数を報告 |

(c) **ディレクトリ風のゴミの模様** (`scribble_free_blocks_dirlike`): 空きブロックを
「先頭に未使用 inode 番号の有効なエントリ "q"、残りは空エントリ」で埋める。
**段 C の全掃引を、番号の模様とディレクトリ風の模様の 2 回ずつ**回す。

**配置** (`make_slack_dir`): ".", ".." の後に 244 文字の A, B, C (rec 252) を並べて
C を 2 セクタ目の先頭 (528) に置き、C を消して、その inode 番号を別ファイル F が
再利用する。次に同じ長さの D を足すと、B の rec_len (1 セクタ目) と D の中身
(2 セクタ目) が別セクタに載る。旧コードの削除の跡 (inode 番号が残る) は媒体へ
直に書いて作る。

### 9-5. 追加した試験と件数

**`test_b8_open.py`: 619 → 892 checks, 0 failures** (約 5 秒)

段 C の掃引 (2 模様 × 16 操作、**計 34,848 回**。模様ごとの回数は同じ):

| 掃引 | 回数 | 失敗を返した回 | 漏れた回 | 孤児 (links 余り) の回 | 末尾の穴の回 | 不整合 | 漏れを成功と言った回 |
|---|---|---|---|---|---|---|---|
| ext2_write 上書き | 2566 | 2564 | 2109 | 0 | 0 | **0** | 0 |
| ext2_write 伸長 | 5426 | 5424 | 2968 | 0 | 0 | **0** | 0 |
| ext2_create (二重間接まで) | 5426 | 5420 | 2962 | 10 | 0 | **0** | 0 |
| ext2_unlink (二重間接つき) | 2270 | 2248 | 1931 | 12 | 0 | **0** | 0 |
| ext2_rmdir | 290 | 278 | 115 | 94 | 0 | **0** | 0 |
| write_stream (直接 → 単一間接) | 126 | 84 | 53 | 0 | 0 | **0** | (対象外) |
| write_stream (単一間接 → 二重間接) | 170 | 108 | 85 | 0 | 0 | **0** | (対象外) |
| ext2_create (ディレクトリが単一間接へ伸びる) | 190 | 188 | 35 | 82 | 20 | **0** | 0 |
| ext2_create (既存の単一間接に足す) | 210 | 208 | 26 | 86 | 6 | **0** | 0 |
| ext2_mkdir | 86 | 80 | 18 | 24 | 0 | **0** | 0 |
| **新** ext2_create (スラックに消した跡 / 2 セクタ目) | 66 | 60 | 0 | 14 | 0 | **0** | 0 |
| **新** ext2_create (旧コードの跡 / 2 セクタ目) | 66 | 60 | 0 | 14 | 0 | **0** | 0 |
| **新** ext2_rename (同じディレクトリ) | 82 | 72 | 0 | 28 | 0 | **0** | 0 |
| **新** ext2_rename (別のディレクトリへ) | 82 | 72 | 0 | 28 | 0 | **0** | 0 |
| **新** ext2_rename (既存ファイルを置き換える) | 242 | 220 | 83 | 42 | 0 | **0** | 0 |
| **新** ext2_rename (ディレクトリを別の親へ) | 126 | 116 | 0 | 80 | 0 | **0** | 0 |

段 E (レビューの反例を期待値つきで取り込み、1 件ごとに新しいディスク):

| 名前 | 中身 |
|---|---|
| `case_x1_add_entry_content_before_link` [X1] | レビューと同じ仕掛け (次の inode を phantom、ディレクトリ風のゴミ、新ブロックの 1 セクタ目を落とす)。IO・**媒体整合**・q は見えない・新ブロックは返った・victim を作って unlink(q) は NOTFOUND・victim は生きている |
| 同 [X1-link] | 既存の単一間接表へ繋ぐ書き込み (2 セクタ目) を落とす。同じ判定 |
| `case_x2_slack_resurrection` [X2] | レビューと同じ配置。2 セクタ目だけ落とす → IO・**媒体整合**・C は NOTFOUND・unlink(C) は NOTFOUND・F は生きている・やり直せば D を足せる。**スラックの inode 番号が 0** であることを直に見る |
| 同 [X2-commit] | 見せる書き込み (rec_len、2 回目の書き込みの 1 セクタ目) を落とす |
| 同 [X2-legacy] | 旧コードの跡 (スラックに inode 番号が残る) を直に書いてから 2 セクタ目を落とす。二段書きだけで防げること |
| `case_x3_rename_two_names` [X3] | レビューと同じ sticky 全位置 (41 位置)。**毎回の媒体整合**、2 名が残った 10 回すべてで片方を unlink しても**媒体整合**・残った名前の inode は生きている |
| `case_x4_mkdir_add_entry_io` [X4] | mkdir の add_entry を I/O で落とす → IO・**媒体整合**・漏れ 0・孤児 (links 余り) が 1 増える |
| 段 D `[X4']` | 空き 1 ブロックで mkdir → NOSPC・**媒体整合**・漏れ 0・inode 数が戻る・親の links が戻る |

レビューの再現器 (`rv_host.c`、パスだけ自分の worktree に向けたもの) も
**105 checks, 0 failures** (直す前 105 / 2、X3 は 2 名 10 回・unlink 後 dangling 0)。

### 9-6. 変異 (往復 4、10 件): **10 / 10 RED、ビルド失敗 0**

| 変異 | 判定 | 落ちた件数 |
|---|---|---|
| X1-order add_entry 新ブロック: 繋いでから中身を書く (元の順序) | **MEDIA** | 15 (「既存の単一間接に足す」掃引が 2 模様とも 14 回不整合) |
| X1-size add_entry 新ブロック: size を繋いだ後で伸ばす | **MEDIA** | 4 (同掃引 10 回、`dir_overrun`) |
| X2-zero 併合時に inode 番号を 0 にしない | RC | 2 (スラックの inode 番号を直に見る CHECK だけ) |
| X2-order 中身と見せる書き込みを 1 回にまとめる (元の形) | **MEDIA** | 10 (「旧コードの跡」掃引・`[X2-legacy]`) |
| X2-both 0 書きと二段書きの両方を外す | **MEDIA** | 19 |
| X3-links rename: links_count を上げ下げしない | **MEDIA** | 20 (rename 4 掃引すべて) |
| X3-parent rename (別の親へ): 新しい親の links を ".." の後で上げる | **MEDIA** | 2 |
| MKDIR-parent mkdir: 親の links を名前の後で上げる (元の順序) | **MEDIA** | 5 |
| MKDIR-nospc mkdir: NOSPC でも何も返さない (往復 3 の形) | RC | 2 |
| RMDIR-parent rmdir: 参照を外せなくても親の links を下げる (元の形) | **MEDIA** | 3 |

**X2-zero が媒体検査で落ちない理由**: 復活には「スラックに生きた inode 番号が
ある」ことと「rec_len だけが縮む部分書き込み」の両方が要る。二段書きが後者を
無くしたので、0 書きを外しても媒体の状態は変わらない (旧コードの跡でも整合する
ことを `[X2-legacy]` が示す)。**0 書きは多重の備え** — 二段書きの判定を誤る変更が
入ったとき、新しく消した名前だけは守る。両方を外すと媒体検査が落ちる (X2-both)。

**往復 1〜3 の変異も再実行して全件 RED**: 往復 1 18/18、往復 2 13/13、往復 3 23/23
(MKDIR-a は `ext2_mkdir` の空行が変わったので文字列を直した。往復 3 では RC だった
が、今回は links の照合で **MEDIA** になった)。

### 9-7. 残る制限 ([V4])

- **`ext2_write` が漏れのとき、中身は正しく書けているのに IO を返す** (往復 3)。
  呼び手が `O_TRUNC` で再試行すると、切り詰めと解放を繰り返して漏れを再発する
  ループになり得る。**直していない** (レビュー非 blocker、記録のみ)。
- **`ext2_release_blocks` は三重間接 (`block[14]`) を返さない**。三重間接は
  書き込み側も未対応 (`ext2_bmap_set` は NOSPC を返す) なので既知。記録のみ。
- 失敗した rename は名前を 2 つ残すことがある (§9-3 の代償)。
- 末尾の穴 (`dir_hole`) と links の余り (`links_surplus`) は許容している。どちらも
  e2fsck が直す。OS32 にはゲスト内の fsck が無い。
- 往復 3 までの制限 (漏れが残る、切り詰め後の失敗で 0 バイト、掃引は once と
  sticky だけ) はそのまま。
- ext2 側はすべて RAM ディスク上。実 NHD では走らせていない。

### 9-8. 触ったファイル (往復 4)

`fs/ext2_dir.c` (`ext2_add_entry` / `ext2_delete_entry` / `ext2_mkdir` /
`ext2_rmdir` / `ext2_rename`)、`fs/ext2_priv.h` (`EXT2_SECTOR_SIZE`)、
`tools/tests/b8_open_host.c`、`tools/tests/b8_tdd.md`。
`docs/archive/shell/TASK_FS_TYPE.md` §2-6 は PM が直すので触っていない。
`sdk/kapi.json` 不変。

## 10. 往復 5 — ディレクトリの 2 名状態 (blocker) とユーザー決裁 1 / 2

- 基点: `feat/gui` の `8409f51` (往復 4 の着地 `7116029` + 往復 5 レビューの記録)。
  作業開始時の未コミット差分は `7116029` と同一だったので mixed reset した (書きかけは無かった)
- 出所: Fable 5.1 の往復 5 レビュー (Request changes、blocker 1 件) —
  `docs/archive/shell/TASK_FS_TYPE.md` §2-7。レビュアーの再現器は 205 checks / 4 failures
- ユーザー決裁 (2026-09-15):
  1. **ディレクトリの rename は旧名を先に消す** (失敗すると名前 0 個の孤児。e2fsck が回収)
  2. **メタデータの I/O エラーで以後の書き込みを止める** (Linux ext2 の `errors=remount-ro` 相当)
- 実行: `python3 -B tools/tests/test_b8_open.py --target` (e2fsck の抜き取り込み、約 10 秒)

### 10-0. 正直に書く ([V4])

- **往復 4 の `media_check` は、ディレクトリの 2 名 (レビューの 02) と輪 (04) を整合と判定していた。**
  e2fsck 流の links 算術 (名前の数 ≤ links) だけでは「ディレクトリに名前が 2 つ」を数えられず、
  OS32 の `rmdir` が links を見ないことと突き合わせていなかった。
- 往復 4 の記録 §9-3 に「ディレクトリの rename が 4 で落ちると名前が 2 つ残る (links は合って
  いる)。e2fsck が直す」と**代償として書いていた**。OS32 自身の `rmdir` がその状態を dangling に
  変えることを見落としていた。
- **§9-1 の「末尾の穴は次の追加が同じ位置を埋める」は誤り**だった。次の名前が既存ブロックの
  スラックに収まると穴は残る (`[R5-6]` の 01 で `dir_hole=1` のまま)。`fs/ext2_dir.c` のコメントを
  直した。§9-1 の本文は往復 4 の記録として残し、ここで訂正する。
- 往復 5 の最初の実行で**試験全体が止まった**。e2fsck と話す `int $0x80` のインライン asm に
  `"memory"` の clobber を付けておらず、GCC が `read` の前後でバッファが変わらないと見なして
  応答待ちが古いバイトを見続けた。直した (`h_sys3`)。

### 10-1. ディレクトリ rename の順序 (決裁 1)

`fs/ext2_dir.c` の `ext2_rename_dir` (新設)。ファイルの rename は往復 4 の順序のまま
(links を先に上げ、新名 → 旧名、最後に下げる。ファイルのハードリンクは正当)。

守る不変条件:
- (a) どの inode も、それを指す名前 ("." ".." を含む) の数 ≤ links_count
  — **rename の各段が書き終えた時点の媒体について**成り立つ。**後続の操作に対して
  ずっと成り立つわけではない** (往復 6 で訂正、下の「孤児の ".."」)
- (b) ディレクトリ inode を指す "." ".." 以外の名前は 1 つ以下
- (c) ".." を新しい親へ向ける前に新しい親の links を上げ、旧親の links は ".." が離れた後で下げる
- ~~孤児の ".." が旧親を指したまま残るのは許容 (e2fsck が直す)~~ **往復 6 で訂正**:
  孤児の ".." は旧親の links に数えられたまま残る。そのままなら漏れ側 (e2fsck が直す) だが、
  **旧親が rmdir で解放され、その番号を mkdir が再利用すると** 孤児の ".." が解放済み
  inode (dangling) か、無関係な生きたディレクトリ (links 不足) を指す — 往復 6 レビューの反例
  (別の親 118 回中 28 回、e2fsck も 8 像すべて不整合)。**これを `ext2_rmdir` のガード
  (空なのに links_count > 2 なら inode を返さない、§11-1) で防ぐ。**この性質は決裁 1 の孤児に
  限らず、往復 3/4 の mkdir / rmdir の途中でできる孤児にも元からあった

**D 自身の links_count は動かさない** (名前は 1 → 0 → 1 で、少ない側にしか振れない)。
往復 4 は D の links を +1 / −1 していたので、1 回の rename で inode の書き込みが 2 回減った。

| 段 | 書くもの (別の親へ移すとき。同じ親なら 2〜4 は無い) | その段で落ちたときの媒体 |
|---|---|---|
| 0 | (読むだけ) 新しい親の links が `EXT2_LINK_MAX` 未満か | 何も書いていない |
| 1 | 旧名を消す (`ext2_delete_entry`) | 名前 1 (届かなかった) か 0 (孤児)。links・".." は元のまま |
| 2 | 新しい親の links +1 | 孤児。新しい親の links は元か +1 (多い側) |
| 3 | D の ".." を新しい親へ (1 セクタの 4B = 二値) | 孤児。".." は旧親か新親。どちらも links に数えられている (旧親はまだ下げていない、新親は上げ済み) |
| 4 | 旧親の links −1 | 孤児。旧親の links は元か −1 (元 = 多い側) |
| 5 | 新名を載せる (`ext2_add_entry`、見せる書き込みは 1 フィールド) | 名前 0 (孤児) か 1 (完了) |
| 6 | D の ctime | 名前は完成。~~失敗はエラー状態を立てるが OK を返す~~ **往復 6 で訂正: IO を返す** (§11-3)。往復 5 の実装は段 5 がブロックを割り当てた回だけ sync が IO を返していて、この表と食い違っていた |

**NOSPC の巻き戻し** (段 5 が NOSPC = 新名はどこにも載っていない): 旧親の links +1 → ".." を
旧親へ → 新しい親の links −1 → 旧名を載せ直す。どこで落ちても孤児で止まり、(a)(b)(c) は保たれる。
旧名を消した隙間には同じ長さの名前がちょうど入るので、載せ直しは NOSPC にならない。
**「満杯」で孤児を作らない** — 決裁 1 の孤児は I/O 失敗の代償であって、NOSPC はエラー状態にも
しない (`[R5-NOSPC]`)。

**やり直し経路 (`dst_ino == ino`) の意味が変わった**: 往復 4 までは「ハードリンク同士」として
ディレクトリでも OK を返し、2 名を残していた。往復 5 の rename はディレクトリに 2 名を作らない
ので、ここへ来るのは**往復 4 以前のコードが書いた媒体か、別の破損だけ**。".." や親の links が
どこまで進んでいたか分からないので完了させようとせず、**メタデータの不整合としてエラー状態に
して IO を返す** (`[R5-LEGACY]`)。ファイルは従来どおり OK (POSIX どおり何もしない)。

**置き換えを伴う rename (ファイル) は原子的でない** (レビュー R5-4、往復 4 以前からの挙動):
1 回故障の全 77 位置中 24 位置で「宛先が消えて移動元が残る」。媒体は全件整合。記録のみ。

### 10-2. エラー状態 (決裁 2)

| 項目 | 実装 |
|---|---|
| どこで立てるか | **`ext2_read_block` / `ext2_write_block` の失敗**で `ext2_fs_error(ctx)` (`fs/ext2_super.c`)。この 2 つは**メタデータ専用**にし、データブロック (ファイルの中身) は新設の `ext2_read_data_block` / `ext2_write_data_block` を使う (`ext2_read_file` / `ext2_read_stream` / `ext2_create` / `ext2_write` / `ext2_write_stream` の 8 か所)。**既定をメタデータ側**に置いたのは、メタデータの呼び手を 1 か所見落としただけでエラー状態が立たなくなる逆向きを避けるため |
| 媒体に何を書くか | スーパーブロックの**先頭セクタ (base+2) だけ**を読み直し、`s_state` に `EXT2_ERROR_FS` を OR して書く。**1 マウントにつき 1 度**。失敗しても何もしない (メモリ上の状態は先に立てる = 再入しない)。空き数などメモリ上の値は書かない (操作の途中でビットマップと合っている保証が無い)。**専用の 512B バッファ**を使う — 共有バッファを使うと、操作の途中 (二重間接表を `g_blk` に載せたまま内側の表の読み取りが落ちた瞬間など) に呼び手の表を潰す (§4-24 と同じ壊れ方) |
| 何を拒否するか | 入口 `ext2_check_writable` で `EXT2_ERR_ROFS`: `ext2_create` / `ext2_write` / `ext2_write_stream` / `ext2_unlink` / `ext2_mkdir` / `ext2_rmdir` / `ext2_rename` / `ext2_vfs_set_mtime`。truncate は独立した入口を持たず、`ext2_write` / `ext2_unlink` / `ext2_rmdir` の中でだけ走るのでこれで塞がる。VFS 経由の `vfs_open(O_CREAT / O_TRUNC)` も `write_file` が断るので FD は出ない。**読み取り系は通す** |
| 拒否しないもの | **内部の段** (`ext2_add_entry` / `ext2_delete_entry` / `ext2_alloc_*` / `ext2_free_*` / `ext2_truncate_blocks` 等)。操作の途中でエラーが出たとき、その操作の後始末 (往復 3・4 で掃引済み) は従来どおり走らせる。Linux の remount-ro も進行中の操作のバッファ書き込みは止めない |
| `ext2_sync` | エラー状態では**書かない**。書き戻すもの (`meta_dirty`) が残っていれば `EXT2_ERR_IO`、無ければ OK。数のずれは e2fsck が直す (漏れ側) |
| ROFS と IO の使い分け | **ROFS は「入口で断った = 何もしていない」だけ**。操作が走った後の失敗は IO。最初は sync が ROFS を返していて、漏れを IO で返す約束 (往復 3) を sync の ROFS が上書きした (`[P1-A]` が落ちた) ので分けた |
| マウント時 | `s_state` に `EXT2_ERROR_FS` があれば `kprintf(0x0E, "[EXT2] warning: mounting fs with errors, running e2fsck is recommended")` を出し、**読み書きでマウント**する (Linux と同じ、決裁の条件)。印は消さない (e2fsck だけが消す)。`ctx->mounted_with_errors` に残す。エラー状態 (`ctx->fs_error`) はマウントごとに 0 から |
| エラー状態に入ったとき | `kprintf(0x0C, "[EXT2] I/O error on metadata: writes disabled until remount (run e2fsck)")` を 1 度。既存の `[E2W]` と同じ作法 |
| `ext2_fmt.c` | `s_errors` を `CONTINUE (1)` → **`RO (2)`**。OS32 自身は値に関わらず止めるので振る舞いと食い違っていた。ホストの Linux がこの像をマウントしたときも、エラー後に書き続けなくなる。`s_state` / `s_errors` のオフセットは `fs/ext2.h` の定数にした ([C4]) |
| エラーコード | **`OS32_ERR_ROFS = -15`** を `sdk/include/os32/os32_kapi_shared.h` の末尾に追加 (既存の番号は動かしていない)。`VFS_ERR_ROFS` (`fs/vfs.h`)、`EXT2_ERR_ROFS = -11` (`fs/ext2.h`)、`ext2_to_vfs_err` で写す。**番号の追加だけで KAPI の構造体・スロットは変えていない** (`sdk/kapi.json` 不変、`tools/check_kapi_version.py` は `KAPI バージョン一致: v52 (4 箇所)` を返す)。KAPI_SPEC §3-2 のエラー番号の予約を更新 (ネットワークの開始点を -16 へ) |

**データブロックの書き込み失敗をエラー状態にしない理由**: 参照 (inode のポインタ・間接表) を
書くのはメタデータの段なので、データブロックの書き込みが落ちても**どの参照も正しいブロックを
指したまま**で、ブロックの中身が古いか半分新しいだけ。以後の操作が構造を壊す原因にならない。
Linux ext2 もデータの I/O エラーでは `ext2_error` を呼ばない (`-EIO` を返すだけ)。

**`ext2_write_stream` の小さな是正**: データブロックの読み取り / 書き込みが落ちたとき、1 バイトも
書けていなければ 0 ではなく IO を返す (往復 1 で bmap の失敗にだけ入れた `io_err` を、コメントに
書いてあった意図どおりデータの失敗にも広げた)。`[R5-ES]` の (b) で見つけた。

**`delete_entry` / `add_entry` 既存ブロック経路の mtime 書き込み失敗** (レビュー非 blocker):
`ext2_write_inode` → `ext2_write_block` の失敗なので**自動的にエラー状態に入る**。戻り値は
従来どおり OK (名前は既に媒体に載っているので、失敗と言うと呼び手が二重に作ろうとする)。
以後の書き込みはエラー状態が断るので、二重作成の心配もこのセッションでは無い。

**links_count の上限** (レビュー非 blocker、安かったので入れた): `EXT2_LINK_MAX = 32000`
(Linux ext2 と同じ値)。`ext2_mkdir` (親) とディレクトリ rename の段 0 (新しい親)、ファイル rename の
段 1 (移す inode) で、上限なら何も書かずに `EXT2_ERR_MLINK` → `OS32_ERR_FULL` (既存の番号)。

### 10-3. `media_check` に足した検査

| 値 | 意味 | 扱い |
|---|---|---|
| `dir_multi` | ディレクトリ inode を指す "." ".." 以外の名前が 2 つ以上 | **不整合** |
| `dotdot_bad` | 名前で辿れる (= 名前の親の鎖が根に届く) ディレクトリの ".." が、そのディレクトリを名前で持つ親を指していない。孤児の枝は対象外 | **不整合** |
| `dir_loop` | 名前の親を辿ると輪に入るディレクトリの数 (根に届かず、親の無いディレクトリでも止まらない) | **不整合** |
| `bad_dir` | 壊れた `rec_len`: 8 未満 / 4 の倍数でない / ブロックを越える / 名前が入らない / 鎖がブロック末尾でちょうど閉じない。**以前は黙って `break` していた** | **不整合** |

2 名のうち**どちらの親を「名前の親」として記録するかは inode の走査順で決まる**ので、
2 名の媒体では `dotdot_bad` が 0 にも 1 にもなる。2 名そのものは `dir_multi` が必ず数える。

### 10-4. e2fsck の抜き取り

**やり方**: 試験バイナリ (`-nostdlib`) が RAM ディスクの ext2 部分 (8MB) を `argv[1]` の
ディレクトリへ書き、`@@E2FSCK <像> <media_ok> <ラベル>` を出して**標準入力の 1 行を待つ**。
`test_b8_open.py` がその行で `/usr/sbin/e2fsck -fn` を当て、出力を `tools/tests/b8_e2fsck.py` で
分類し、**`media_ok` と食い違ったら失敗**に数えてから 1 行返す。像は毎回同じパスへ上書きするので
ディスクには 1 枚しか残らない。e2fsck が無い環境では `E2FSCK SKIP` を出して媒体検査だけで通す。
**分類できない行は不整合として扱う**。

**抜き取る位置**:
- 段 C の各掃引: 番号の模様ではフィボナッチ位置 (1, 2, 3, 5, 8, 13, … 4181)、両模様で
  「最初の漏れ / 孤児 / 穴 / 不整合」の回 (once と sticky それぞれ)
- 段 E の全ケース (失敗の直後と、後続操作の後)。X3 はフィボナッチ位置と 2 名が残った最初の 3 回
- 段 F: `[R5-1]` / `[R5-1s]` の失敗直後と後続操作の後 (フィボナッチ位置 + 最初の孤児)、
  `[R5-ES]` / `[R5-1b]` / `[R5-6]` の全像、`[R5-NOSPC]`、`[R5-LEGACY]` (**不整合を期待する像**)

**結果**: **537 像**: 異常なし 289 / 許容のみ 246 / 不整合 2。不整合の 2 像はどちらも**不整合を期待して作った像** (`[R5-LEGACY]` の 2 名、`[R5-BADDIR]` の壊れた rec_len) で、`media_check` も不整合と判定した。**`media_check` との食い違い 0**

### 10-5. e2fsck の出力の分類 (許容の理由)

| 分類 | 件数 | e2fsck の行 (例) | **なぜ漏れ側か** |
|---|---|---|---|
| 使用中なのに未参照 (block の漏れ) | 124 | `Block bitmap differences:  -(282--555)` | ビットマップが使用中なのにどの inode からも辿れない。往復 3 の「参照を先に外し、解放は後」の途中で落ちた分。**`-` の差分だけ** (e2fsck が空きへ戻す向き = 配られない)。`+` (参照されているのに空き = 次の割り当てで交差リンク) は不整合側 |
| 空き数 / ディレクトリ数の修正 | 518 | `Free blocks count wrong for group #0 (7308, counted=7560).` | superblock / GD の数だけのずれ。割り当ては必ずビットマップを読んで空きを探すので、数のずれで二重に配られることはない。エラー状態の sync はこの数を書かない |
| 使用中なのに未参照 (inode の漏れ) | 50 | `Inode bitmap differences:  -1006` | inode 版の漏れ (create の後始末の途中で落ちた分)。`-` だけ |
| 未接続 inode (孤児) | 65 | `Unattached inode 1006` | 名前 0 個で links ≥ 1。OS32 の操作は名前からしか辿らないので到達できない。e2fsck が lost+found へ |
| 未接続ディレクトリ (孤児) | 28 | `Unconnected directory inode 920 (was in /sw)` | **決裁 1 の孤児** (ディレクトリ rename)、rmdir / mkdir の途中で落ちた分。同上 |
| 孤児ディレクトリの '..' | 28 | `'..' in ... (920) is /sw (915), should be <The NULL inode> (0).` | 孤児の ".." が旧親を指したまま (決裁 1 の条件で許容)。**同じ出力で未接続と報告された inode のときだけ**許容し、名前で辿れるディレクトリの ".." の誤りは不整合 |
| ref count が多い (孤児 / links 余り) | 55 | `Inode 920 ref count is 2, should be 1.` | links_count > 名前の数。片方の名前を消しても links は 0 にならず、生きた inode を返さない向き。**逆 (少ない) は不整合** |
| ディレクトリの i_size が大きい (末尾の穴) | 9 | `Inode 921, i_size is 13312, should be 12288.` | size だけ先に伸びて繋いでいない (往復 4 の X1 段 2)。参照は増えていない。逆向き (size より先にブロックがある = `dir_overrun`) は不整合 |
| i_blocks の修正 | 2 | `Inode 915, i_blocks is 28, should be 30.` | ブロック数の統計だけ (往復 5 レビューの非 blocker)。参照そのものは正しい |

件数は 537 像を通しての**その行の出現回数**。分類器 (`tools/tests/b8_e2fsck.py`) は ほかに
`contains a file system with errors, check forced` (エラーの印) / ファイルの i_size の修正 /
ビットマップ末尾の詰め物 / 解放済み inode の dtime が 0 / lost+found が無い を許容として知っているが、
今回の抜き取りには出なかった (`-f` を付けるので印の行は出ない)。

`-n` の e2fsck は**壊れたディレクトリブロックを直せないので打ち切る** (`Salvage? no` → `e2fsck: aborted`、
終了コード 12)。不整合の診断が出ている打ち切りは判定に使い、診断の無い終了コード 8 以上だけを
道具の失敗として数える (`[R5-BADDIR]` で最初は道具の失敗に落ちた)。

**不整合** (1 件でもあれば `media_ok=1` と食い違う): 削除済み inode を指すエントリ /
ディレクトリへのリンク (2 名) / `'..'` の誤り (孤児以外) / multiply-claimed blocks /
参照されているのに空き (ビットマップ差分の `+`) / 範囲外のブロック番号 / ディレクトリブロックの
破損 / ディレクトリの i_size より先にブロックがある / ref count が少ない / 輪 / 分類できない行。
今回の実行で出たのは `[R5-LEGACY]` の**期待どおりの 1 像**だけ (ディレクトリへのリンク +
`'..'` の誤り。`media_check` も `dir_multi=1` で不整合と判定し、一致)。

### 10-6. 追加した試験と件数

**`test_b8_open.py`: 892 → 1208 checks, 0 failures** (e2fsck 537 像、`media_check` との食い違い 0)

| 名前 | 中身 |
|---|---|
| `[R5-ES]` `case_r5_error_state` | フォーマットの値 (`VALID`、`s_errors=RO`)。(a) データブロックの読み取り失敗・(b) 書き込み失敗ではエラー状態にならない。(c) ディレクトリブロックの読み取り失敗でエラー状態・通知 1 回・**媒体に印 (s_state のセクタ 1 回だけ書く)**。読み取り系 (stat / read / ls / open O_RDONLY) は通り、書き込み系 10 種 (`vfs_write` / `write_stream` / `ext2_create` / `vfs_mkdir` / `vfs_rmdir` / `vfs_rm` / `vfs_rename` / `vfs_set_mtime` / `open(O_CREAT)` / `open(O_TRUNC)`) が**すべて `-15` で 1 セクタも書かない**。(d) 新しい ctx で再マウント: 警告 1 回・読み書きでマウント・印は残る。(e) inode 表の書き込み失敗でもエラー状態・エラー状態の sync は書かない |
| `[R5-LINK]` `case_r5_link_max` | 親の links が上限なら `mkdir` / ディレクトリ rename が `FULL` で何も書かない |
| `[R5-1]` / `[R5-1s]` `case_r5_dir_rename_followups` | **レビューの R5-1 を再マウント込みで取り込んだもの**。ディレクトリ rename (別の親 / 同じ親、子ディレクトリつき) を once / sticky の全位置で落とし、落ちた回は毎回 (i) エラー状態であること (ii) このセッションの `mkdir` が ROFS であることを見る。**新しい ctx で再マウント**して媒体検査、2 名の有無を数え、後続操作 — 祖先を D の配下へ rename (輪) / 子と D を全部の名前で rmdir / mkdir 2 回 (返した inode を受け取り得る) / create / unlink — を当ててもう一度媒体検査。別の親: 118 回 (2 名 **0**、孤児 52、移動済み 28、未移動 38)、同じ親: 70 回 (2 名 **0**、孤児 16、28、26)。**不整合 0 (失敗直後 / 後続操作の後とも)、輪 0、エラー状態の立ち忘れ 0、再マウント後の拒否 0** |
| `[R5-1b]` `case_r5_loop_attempt` | レビューの輪の反例。旧名の削除の書き込みを 1 回落とす → IO・エラー状態・同じセッションの輪の rename は ROFS。再マウント後: 旧名は残り新名は無い、D の名前は 1 つなので /etc を D の配下へ移すのは**正当に通り輪にならない**、逆向きは INVAL |
| `[R5-NOSPC]` `case_r5_dir_rename_nospc` | 新しい親が満杯・空き 0 で別の親へディレクトリ rename → NOSPC、エラー状態にしない、旧名・".."・両方の親の links が元どおり、孤児を作らない |
| `[R5-6]` `case_r5_e2fsck_images` | レビューの 00〜04 を往復 5 のコードで作り直す: 00 base / 01 末尾の穴 (短い名前を足しても `dir_hole=1` のまま) / 02 旧名の削除の失敗 (2 名にならない) / 02b 新名の書き込みの失敗 (**孤児のディレクトリ**) / 03 rmdir → mkdir (孤児の inode は配られない) / 04 輪の試みは INVAL。全像を e2fsck へ |
| `[R5-LEGACY]` `case_r5_legacy_two_names` | 往復 4 以前のコードが残した 2 名を媒体に作る → `dir_multi=1`・**e2fsck も不整合** (期待どおり一致)。このセッションでメタデータの I/O エラー → rmdir は ROFS で**媒体に dangling が出ない**。やり直し rename は IO + エラー状態で 1 セクタも書かない。**記録のみ**: エラー状態の無い新しいマウントで rmdir を当てると dangling=1 (§10-9) |

既存の試験の直し:
- 段 A / E の各 case の頭と、失敗注入の直後に `fault_done()` (= 再マウント。電源を入れ直したのと
  同じで sync しない) を入れた。同じディスクで続きを試していたので、エラー状態が後続の書き込みを
  断るようになったため。`[P1-B]` はメモリ上の空き数を見る間 (入口の拒否を持たない内部関数だけ)
  は再マウントせず、FS 経由の段の前で 1 回だけにした
- 「1 セクタも書かない」の 6 か所は、エラー状態の印を書く s_state のセクタを除いて数える
  (`wr_non_state()`)
- 段 C の掃引表は往復 4 と同じ (不整合 0)。回数は 2 模様で計 **34,840** (往復 4 は 34,848):
  mkdir が +4 (上限の確認で親を読む)、ディレクトリを別の親へが −8 (D の links を書かなくなった)

### 10-7. 変異 (往復 5)

`scratchpad/mutate_r5.py` (1 件ずつ当てて `test_b8_open.py` を e2fsck 込みで回す)。
判定は **BUILD** (ビルド失敗) / **MEDIA** (媒体検査に由来する CHECK が落ちた) / **E2FSCK** (e2fsck と
`media_check` の食い違い) / **RC** (それ以外の CHECK)。**14 / 14 RED、ビルド失敗 0。**

| 変異 | 判定 | 落ちた件数 | 何が落としたか |
|---|---|---|---|
| R5-ORDER ディレクトリ rename を往復 4 の順序 (新名 → 旧名) に戻す | **MEDIA** | 36 | 段 C の「ディレクトリを別の親へ」掃引 (2 模様)、`[R5-1]` / `[R5-1s]` の 2 名・失敗直後と後続操作の後の不整合・輪 |
| R5-NOERR エラー状態を立てない | **MEDIA** | 39 | `[R5-ES]` の拒否一式、`[R5-LEGACY]` の **dangling=1 と D の inode の解放** (このセッションの rmdir が通る) |
| R5-PASS エラー状態でも書き込みを通す | **MEDIA** | 29 | 同上 |
| R5-DOTDOT 別の親へ移すとき ".." を書き換えない | **MEDIA** | 11 | 掃引と `[R5-1]` (旧親の links を下げるので `links_short`、後続操作の輪) |
| R5-DOTDOT+MC 上に加えて media_check の ".." 検査も外す | **MEDIA** | 11 | 同上。`links_short` が先に落とすので e2fsck の出番が無く、**e2fsck の裏付けを示す変異になっていなかった** → 下の DOTDOT2 を足した |
| R5-DOTDOT2 ".." を書かず、旧親の links も下げない (".." だけが誤り) | **MEDIA** | 10 | `dotdot_bad`、後続操作の輪 (循環検査は ".." を辿るので素通りする) |
| R5-DOTDOT2+MC 上に加えて media_check の ".." 検査も外す | **MEDIA + E2FSCK** | 7 + 食い違い 2 | **e2fsck だけが ".." の誤りを見つけた像が 2 枚**。media 側は後続操作でできた輪 (`dir_loop`) |
| R5-ORDER+MC 順序を戻し、media_check の dir_multi / dotdot / 輪 も外す | **MEDIA + E2FSCK** | 28 + 食い違い 9 | **e2fsck だけが 2 名を見つけた像が 9 枚** (ディレクトリへのリンク) |
| R5-DATAERR データブロックの読み取り失敗でもエラー状態にする | RC | 8 | `[R5-ES]` (a)(b) |
| R5-RETRY 2 名のディレクトリの rename やり直しを OK で返す (往復 4 の形) | **MEDIA** | 5 | `[R5-LEGACY]`: エラー状態にならず、続く rmdir が dangling を作る |
| R5-NOSPC ディレクトリ rename の NOSPC で旧名へ戻さない | RC | 5 | `[R5-NOSPC]` (旧名が無い、links 余り、".." が戻らない) |
| R5-LINKMAX mkdir の links_count 上限を外す | RC | 2 | `[R5-LINK]` |
| R5-SYNCROFS エラー状態の sync が ROFS を返す | RC | 2 | `[P1-A]` ほか (漏れを IO で返す約束を sync が上書き) |
| R5-BADDIR media_check の rec_len 検査を外す | RC (検査自身) | 1 | `[R5-BADDIR]` の `bad_dir >= 1`。**最初は GREEN** (往復 5 のコードは壊れたブロックを作らないので検査を通る像が無かった) → 媒体を直に壊す `[R5-BADDIR]` を足して RED。壊した像でも全体の判定は `dotdot_bad` でなお不整合になるので、e2fsck との食い違いは出ない |

**往復 1〜4 の変異は今回は再実行していない** ([V4])。変異の道具 (往復 1〜4 の `mutate*.py`) は
PC が落ちたときに scratchpad ごと失われた。往復 1〜4 の試験 (892 件) は全件通したままである。

### 10-8. レビュアーの再現器

レビュアーの再現器 (`scratchpad/r5/`) の `#include` を自分の worktree へ差し替えて実行した
(`scratchpad/r5mine/`): **185 checks, 10 failures**。落ちた 10 件はすべて説明できる:

| 件数 | 落ちた CHECK | 理由 |
|---|---|---|
| 3 | `[R5-1]` ×3 の `both > 0` | 「2 名が起きること」という**前提**。往復 5 では `two-name-runs=0`。blocker の判定そのもの `dangling-after-rmdir` は **3 本とも 0** |
| 2 | `[R5-1b]` の 2 名の前提 (`find(etc, "rmd2") == OK`、`d_old == d_new`) | 同上。続く `rename(/etc → /sw/rmd/etc2)` は D の名前が 1 つなので正当に通り、輪の経路 `/sw/rmd/etc2/rmd2/...` は NOTFOUND (輪は無い)。媒体も整合 |
| 3 | `[R5-5]` の unlink ×2 と `raw_inode_used(b) == 0` | 失敗を注入した**同じセッション**でやり直しと unlink を続けているので、**決裁 2 のエラー状態が ROFS で断った** (`retry rc=-11`)。再マウントを挟む形は本試験の `[R5-1]` 等が見ている |
| 2 | `[R5-6]` の `ext2_create("after")` と `ext2_sync` | 同じ理由 (エラー状態のまま後続の書き込み)。関数が早期に戻るので 02〜04 の像は作られない — 本試験の `[R5-6]` が再マウント込みで作り直している |

R5-2 (末尾の穴からの回復) / R5-3 (X2 の配置) / R5-4 (置き換え rename の原子性) は全件通った
(R5-4: 77 位置中 24 位置で宛先が消えて移動元が残る、うち不整合 0 — 往復 4 以前と同じ)。

### 10-9. 残る制限 ([V4])

- **往復 4 以前のコードが媒体に残した 2 名のディレクトリは、エラー状態の無いマウントで rmdir すると
  今も dangling になる** (`[R5-LEGACY]` の記録: rc=0 dangling=1)。決裁 1 は OS32 が**新たに**この
  状態を作らないことを、決裁 2 はその状態でメタデータの I/O エラーが出た**セッション**を守るだけ。
  `7116029` で書いた媒体を使ったなら e2fsck が要る。rmdir が「空のディレクトリなのに links > 2」を
  見て断る検査は安く入るが、決裁 (ii)(iii) の範囲を越えるので入れていない (PM 判断)。
- **エラー状態はメモリ上だけ**。再起動すると読み書きでマウントされる (決裁どおり)。媒体の印は
  警告を出すだけで、ゲスト内に fsck は無い。
- 内部の段 (`ext2_add_entry` 等) は入口の拒否を持たないので、ホストの試験から直接呼べば
  エラー状態でも書ける。OS32 の中でこれらを入口として呼ぶ経路は無い。
- 置き換えを伴うファイルの rename は原子的でない (§10-1)。
- e2fsck の分類表は今回の抜き取りで**実際に出た行**から作った。出ていない種類の行は
  「分類できない = 不整合」に落ちるので、見逃しではなく失敗として表に出る。
- ext2 側はすべて RAM ディスク上。実 NHD では走らせていない。

### 10-10. 触ったファイル (往復 5)

カーネル (FS): `fs/ext2.h` (`EXT2_ERR_ROFS` / `EXT2_ERR_MLINK` / s_state・s_errors の定数 / `EXT2_LINK_MAX`)、
`fs/ext2_ctx.h` (`fs_error` / `mounted_with_errors`)、`fs/ext2_priv.h` (データ用 I/O・`ext2_fs_error`・
`ext2_check_writable` の宣言)、`fs/ext2_super.c` (メタデータ / データの I/O、エラー状態、マウント時の警告、
エラー状態の sync)、`fs/ext2_file.c` (データ I/O の置き換え 8 か所、入口の拒否 4 か所、write_stream の `io_err`)、
`fs/ext2_dir.c` (`ext2_rename_dir` 新設、rename のやり直し経路、mkdir / rmdir / rename の入口の拒否、
links の上限、末尾の穴のコメント)、`fs/ext2_vfs.c` (ROFS / MLINK の写像、set_mtime の入口の拒否)、
`fs/ext2_fmt.c` (`s_errors = RO`、定数化)、`fs/vfs.h` (`VFS_ERR_ROFS` / `VFS_ERR_FULL`)

SDK / 文書: `sdk/include/os32/os32_kapi_shared.h` (`OS32_ERR_ROFS = -15` を末尾に追加)、
`docs/KAPI_SPEC.md` (§3-2 のエラー番号の予約)。**`sdk/kapi.json` 不変**

試験: `tools/tests/b8_open_host.c`、`tools/tests/test_b8_open.py`、`tools/tests/b8_e2fsck.py` (新規)、
`tools/tests/ext2_read_bound_host.c` / `tools/tests/vfs_mount_dev_host.c` (新しい関数の贋物)、
`tools/tests/b8_tdd.md`。`build/sdk.mk` は変えていない (`check-b8-open-host` が e2fsck 込みで回る)

`docs/archive/shell/TASK_FS_TYPE.md` §2-7 と CLAUDE.md の gotcha は PM が直すので触っていない。

## 11. 往復 6 — rmdir のガード (ユーザー決裁)、複数グループの割り当て、非 blocker

- 基点: `feat/gui` の `e838f22` (往復 5 の着地)。作業開始時の worktree は `8409f51` の上に
  未コミット差分があり、`e838f22` との差は `tools/tests/b8_e2fsck.py` (未追跡、中身は同一の
  blob) と PM が足した `CLAUDE.md` の 1 行だけだったので mixed reset し、`CLAUDE.md` を
  `e838f22` に合わせた (書きかけは無かった)
- 出所: Fable 5.1 の往復 6 レビュー (**Approve、blocker なし**、非 blocker 8 件)。
  うち 1 件にユーザー決裁 (空なのに links_count > 2 の rmdir を断る)
- 実行: `python3 -B tools/tests/test_b8_open.py --target` (約 11 秒)

### 11-0. 正直に書く ([V4])

- **往復 5 の §10-1 は不変条件を言い過ぎていた。**「(a) 名前の数 ≤ links_count が保たれる」
  「孤児の ".." が旧親を指したまま残るのは許容」は、rename の各段が書き終えた媒体についてしか
  言えない。孤児の親が rmdir で解放・再利用されると崩れる (§10-1 をその場で訂正した)。
  往復 5 の `[R5-1]` の後続操作に「旧親の rmdir -> 同じ名前で mkdir」が無かったので見えなかった
- **往復 5 の §10-1 の段 6 の行 (「OK を返す」) は実装と食い違っていた** (§11-3)
- **§10-0 の「busy loop」は言い方が不正確だった。**実測すると CPU を回し続けるのではなく、
  子が stdin の `read` で眠り、Python が子の stdout で待つ**相互の待ち合い**だった (§11-5)
- 試験を書いたのは実装の後。赤は変異で取った (§11-7)

### 11-1. rmdir のガード (ユーザー決裁)

`fs/ext2_dir.c` の `ext2_rmdir`: `ext2_is_dir_empty` が真で、inode を読んだ後、**何か書く前に**
`links_count > EXT2_EMPTY_DIR_LINKS (2)` なら inode を返さずに断る。

| 項目 | 決めたこと |
|---|---|
| 条件 | 空 (名前が "." ".." だけ) なのに links_count > 2。**孤児の ".." がまだ数えられている**か、**links が多い側に振れている** (親の links を先に上げた mkdir が途中で落ちた、など)。整合した媒体では起きない |
| エラーコード | **`EXT2_ERR_NOTEMPTY` → `OS32_ERR_NOTEMPTY` (-7)**。新しい番号は足していない。名前では見えないが、まだ子 (孤児) がこのディレクトリを親として参照している、という意味で最も近い。IO は「メタデータの I/O エラー」に使っている (エラー状態と対) ので使わない。ROFS は「入口でエラー状態が断った」専用 |
| エラー状態 | **入れない** (決裁どおり)。I/O エラーではなく漏れ側の無害な不整合で、以後の書き込みを止めるほどではない |
| 通知 | `kprintf(0x0E, "[EXT2] rmdir refused: empty directory inode %d still has links_count %d (orphaned subdirectory), run e2fsck\n")` を断るたびに 1 行。色 0x0E はマウント時の警告と同じ (「エラー状態」の 0x0C より弱い) |
| 置き場 | 空の判定 (`is_dir_empty`) の**後**。前に置くと、子のある普通のディレクトリ (links > 2) で通知が出てしまう |
| 定数 | `EXT2_EMPTY_DIR_LINKS = 2` を `fs/ext2.h` に ([C4]) |

**健全なディレクトリを誤って断らない証拠** (3 通り):

1. **組み立てた場合** `[R6-GUARD]`: (a) 作ってすぐ消す (links 2)、(b) 子を 2 つ作ってから全部消す
   (links 4 → 2)、(c) 子ディレクトリを別の親へ rename で移し終えた旧親 — 3 つとも `rmdir` が OK で、
   **ガードの通知は 0 回**。(b) の途中の「名前のある子がいる」NOTEMPTY でも通知は出ない
2. **全位置の掃引で媒体と突き合わせた場合**: 後続操作の rmdir の直前に `media_check` し、
   「parent を ".." で指す孤児の数」(`orphan_dotdot_refs`) と「parent の links が名前の数より多いか」
   を**媒体から独立に**数える。ガードの判定を `GuardTally` に次の 4 つで記録:
   `refused_orphan` (孤児がいて断った) / `refused_surplus_only` (孤児はいないが多い側で断った =
   決裁どおりの安全側) / **`wrong_refuse` (孤児なし・links = 名前の数 なのに断った)** /
   **`orphan_left` (孤児がいるのに通した)**。後の 2 つを 0 で押さえる
3. **変異** `R6-GUARD-STRICT` (条件を `>= 2` にする): 健全なディレクトリの rmdir が 32 件落ちる
   (試験が健全側を本当に見ていることの裏付け)

| 試験 | 旧親 (孤児の親) の rmdir | 通した (うち健全) | 断った (孤児 / 多い側だけ) | **健全なのに断った** | **孤児がいるのに通した** | 後続後の不整合 |
|---|---|---|---|---|---|---|
| `[R5-1]` 別の親 118 回 | `/sw` | 78 (78) | 40 (28 / 12) | **0** | **0** | **0** |
| `[R5-1s]` 同じ親 70 回 | `/sw` | 54 (54) | 16 (16 / 0) | **0** | **0** | **0** |
| 段 C2 mkdir 90 回 ×2 模様 | `/iso1/par` | 38 (38) | 24 (10 / 14) | **0** | **0** | **0** |
| 段 C2 rmdir 290 回 ×2 模様 | `/iso2/par` | 92 (92) | 96 (14 / 82) | **0** | **0** | **0** |

**レビュアーの再現器** (`scratchpad/b/mk.py`、`W` を自分の worktree へ差し替え `scratchpad/b6mine/`):
レビュアー側 (`e838f22` 相当) は別の親 118 回中 **28 回が後続後に不整合**、e2fsck も 8 像が不整合
(1208 checks, 1 failure)。自分の worktree では **不整合 0**、`r6-sw-removed=78` (40 回はガードが断った)、
e2fsck の不整合は期待どおりの 2 像だけ・食い違い 0 (1208 checks, 0 failures)。

**往復 3/4 の mkdir / rmdir の孤児** (段 C2、新設): 段 C の `/sw` には他の試験用のファイルが並んでいて
空にならないので、孤児の親が**それ以外に何も持たない**木 (`/iso1/par` / `/iso2/par/rmd`) を別の
ディスクに作り、`sweep()` に**再起動 (再マウント) → 後続操作 → もう一度媒体検査 + e2fsck** の口
(`g_sw_follow`) を足した。後続操作は「親を rmdir → 同じ名前で mkdir」。段 C の配置と回数は変えていない。

### 11-2. `ext2_alloc_block` / `ext2_alloc_inode` がビットマップの読み取り失敗で次のグループへ進んでいた

**直し方**: ビットマップの**読み取り失敗・書き込み失敗は、その場で `EXT2_ERR_IO`** (次のグループへ
進まない)。空きが無いときは `EXT2_ERR_NOSPC`、未マウントは `EXT2_ERR_NOMOUNT`。以前は失敗を全部 `-1`
(= `EXT2_ERR_IO` と同じ値) で返し、呼び手は一律 NOSPC に読み替えていた。

**呼び手** (10 か所) は割り当ての戻り値を**そのまま返す**: `ext2_create` (inode / ブロック)、
`ext2_write` (ブロック)、`ext2_mkdir` (inode / ブロック)、`ext2_add_entry` の新ブロック、
`ext2_bmap_set` の表 3 か所、`ext2_write_stream` (NOSPC 以外なら `io_err` を立てて、1 バイトも
書けていなければ 0 ではなく IO)。

`ret == EXT2_ERR_NOSPC` を見て後始末を分けている箇所 (rename の NOSPC の巻き戻し、create / mkdir の
NOSPC の後始末、`bmap_set` の NOSPC なら `new_blk` を返す、ファイル rename の links 戻し) は、
割り当ての IO が来ても**すべて漏れ・孤児側に倒れる** (IO =「載ったかもしれない」として返さない)。
`bmap_set` の約束のコメントに「表を割り当てるビットマップの I/O もここに入る」と書き足した。

**複数グループの試験** `[R6-GROUPS]`: `ext2_format` は `EXT2_BLOCKS_PER_GROUP_MAX = 8192` 固定で
グループの大きさを選べないので、**ディスクを大きくして**複数グループを作った。既定の 8MB はそのまま
(既存の掃引の回数・配置を変えない) で、この case だけ `g_fs_sectors = DISK_GROUPS_FS_SECTORS`
(36000 セクタ = 18000 ブロック = **3 グループ** 8192 / 8192 / 1615)。フォーマット直後の像は
e2fsck で異常なし。

| # | 何をするか | 見るもの |
|---|---|---|
| 1 | グループ 0 のブロックビットマップを読めなくして create | **IO**、エラー状態、**グループ 1〜2 のビットマップが 1 バイトも変わらない**、再マウント後に**名前が無い** (完了していない)、媒体検査 + e2fsck |
| 2 | グループ 0 の inode ビットマップ + create | 同上 |
| 3 | グループ 0 の inode ビットマップ + mkdir | 同上 + e2fsck |
| 4 | グループ 0 のブロックビットマップ + 新しいブロックが要る追記 | **IO** (0 でも短い書き込みでもない)、サイズは元のまま |
| 5 | 対照: グループ 0 を**満杯**にする (メモリ上の空き数 0) | 従来どおり**次のグループから**割り当てる (ブロック・inode とも) |
| 6 | NOSPC と IO の区別 | 全グループ満杯なら `NOSPC` でエラー状態にしない / 読めなければ `IO` |

変異 `R6-ALLOC-CONT` / `R6-ALLOCI-CONT` (往復 5 の `continue` に戻す) で、グループ 1 の
ビットマップが変わり、名前ができ (= 完了しているのに IO)、`alloc_*` が IO を返さなくなって
それぞれ 5 件落ちる。

### 11-3. `ext2_rename_dir` 段 6 のコメントと挙動

往復 5 の段 6 は「名前は完成しているので OK を返す」とコメントしていたが、実際は `ext2_sync` の結果を
返しており、エラー状態の sync は `meta_dirty` のときだけ IO を返すので、**段 5 がブロックを割り当てた
回だけ IO、割り当てなかった回は OK** に分かれていた。

**挙動を変えた: ctime を読めない / 書けないなら常に IO。**往復 3 で決めた「漏れや未永続を成功と
言わない」契約に揃える側。呼び手がやり直しても二重には作れない: このセッションはエラー状態が入口で
ROFS を返し、再マウント後は旧名がもう無いので NOTFOUND になる (`[R6-STAGE6]` で両方を確かめた)。
ファイルの rename の段 6 (links −1) は元から IO を返していたので、これで揃った。

試験 `[R6-STAGE6]` / `[R6-STAGE6g]`: 空打ちで D の inode ブロックへの書き込み回数を数え、最後の 1 回
(= 段 6) だけを落とす。段 5 が割り当てない回 (`/etc` へ移す) と、**割り当てる回** (満杯のディレクトリへ、
スラック (最大 244B) に入らない 250 文字の名前で移す) の両方で **IO**・エラー状態・同じセッションの
やり直しは ROFS・再マウント後は新名だけがあり媒体は整合・やり直しは NOTFOUND。
変異 `R6-STAGE6` (sync の結果に戻す) で、割り当てない回が OK を返して 1 件落ちる。

### 11-4. `ext2_rmdir` が inode を残したときも `used_dirs--` していた

inode を手放せた (links 0 を書けた = `ext2_truncate_blocks` が成功した) ときだけ減らす (`dropped`)。
孤児として残したディレクトリはまだディレクトリとして在る。

注: 孤児が残るのは I/O エラーのときだけで、そのときはエラー状態に入り sync が GD を書かないので、
**この誤りは媒体には出ていなかった** (メモリ上の数だけ)。`[R6-USEDDIRS]` はメモリ上の数で見る
(孤児を残した rmdir の後で数が変わらない / 対照として、消せた rmdir で 1 減って媒体にも載る)。
変異 `R6-USEDDIRS` で 1 件落ちる。

### 11-5. e2fsck の受け渡しの見張りと §10-0 の結論

**見張り** (`tools/tests/test_b8_open.py` の `run_with_e2fsck`):
- 子の出力を別スレッドで `queue` へ流し、主スレッドは**時間の上限つき**で待つ
- 止まったら子を kill し、**「E2FSCK STALL: <理由> — 最後に e2fsck へ渡した像: <ラベル>
  (経過、像の枚数)」**と止まる直前の子の出力 15 行を出して**失敗**させる

| 上限 | 既定 | 環境変数 |
|---|---|---|
| 子が 1 行も出さない | 180 秒 | `B8_STALL_SEC` |
| 次の像が来ない | 600 秒 | `B8_IMAGE_SEC` |
| 全体 | 1500 秒 (make の 2400 秒より前) | `B8_TOTAL_SEC` |
| e2fsck 1 回 | 120 秒 | `B8_E2FSCK_SEC` |
| 出力が閉じてから終了まで | 60 秒 | `B8_EXIT_SEC` |

実測は全体約 11 秒、子の出力行の間隔は最大でも 1 秒未満 (e2fsck を除く) なので、閾値には 100 倍以上の
余裕がある。ほかに: 目印 `@@E2FSCK ` を**行のどこにあっても**拾う / 行の形が壊れていたら失敗に数える /
e2fsck の stdin を `/dev/null` にする / 子への応答の `BrokenPipeError` を拾う。

**§10-0 の止まり方を実際に起こした** (`scratchpad/r6/hang/hang.py`、旧 runner = `e838f22` の
`test_b8_open.py`、新 runner = 往復 6):

| 起こしたもの | 旧 runner | 新 runner |
|---|---|---|
| H1: `h_sys3` の `"memory"` clobber を外す (往復 5 初回の形) | **止まる** (90 秒の外側 timeout で切った) | **30 秒で STALL として失敗**、像のラベル (`SWEEP ext2_write 上書き ... at=1 once`) を出す |
| H2: 目印の直前に改行の無い出力を置く (目印が行頭から外れる) | **止まる** (90 秒で切った)。旧 runner は行頭しか見ないので目印と気づかず応答を返さない | 目印を拾って最後まで通る (654 像、食い違い 0) |

**止まっているときの子の状態** (H1 を新 runner で動かし、15 秒後に採った): `STAT=S`、`%CPU=0.1`、
`WCHAN=anon_pipe_read`、utime + stime = 3 tick。**busy loop ではない。**clobber が無いと GCC は
応答待ちの `ack[0]` を読み直さず、`"ok\n"` を読み終えても次の `read` へ進んで stdin で眠る。Python は
子の次の出力を待っているので、**互いに待ち合って止まる** (CPU を使わない)。§10-0 の「古いバイトを
見続ける busy loop」は言い方が不正確だった。

**結論**:
- `e838f22` の試験バイナリには clobber があり、目印の前に改行の無い出力を置く経路も無い
  (通常の実行が毎回最後まで通る) ので、**H1 / H2 のどちらも PM の `make check` の 2400 秒の説明には
  ならない。原因は特定できていない** ([V4])
- `/tmp` はこの環境では tmpfs なので、像の書き出し (1 回 8〜18MB × 約 650 回) がディスクで詰まる説も
  当たらない
- 見張りを入れたので、同じことが起きれば**どの像の後で止まったか**を出して 3 分以内に失敗する。
  e2fsck の stdin を `/dev/null` にしたのは、端末を継いだ e2fsck が背景のジョブとして止まる余地を
  消すため (起きた証拠は無い、予防)

### 11-6. 記録のみ (直していない)

- **非 blocker 5**: エラー状態の sync は空き数を書かないので、失敗した操作の後始末で返した分だけ
  媒体の `free_blocks` が実際より少なく残り得る。数が 0 に見えたグループは `alloc_block` が飛ばすので
  e2fsck まで配られない (1 グループなら FS 全体が満杯に見える)。交差側ではない (漏れ側)
- **非 blocker 7**: `sdk/rust/os32api/src/gui/proto.rs` は `OS32_ERR_AGAIN` まで手で写しているが
  `OS32_ERR_ROFS` (-15) は無い。`check_gui_proto.py` の照合対象は `os32_gui_shared.h` なので範囲外、
  Rust 側は生の負値として扱うので実害なし (PM 確認済み)
- **非 blocker 8**: `ext2_rename` の `dst_ino == ino` (ディレクトリ) は I/O エラー無しに媒体へ
  `EXT2_ERROR_FS` を書く。VFS が "." ".." を畳むので壊れた媒体でしか到達しないが、ドライバを直接呼ぶ
  ホスト試験では意図しない印が付く
- **ガードの代償** (往復 6 で増えた制限): links が**多い側に振れているだけ**の空ディレクトリも断るので、
  例えば rmdir の最後の段 (親の links −1) が I/O で落ちた後の親は、**ホストで e2fsck を当てるまで
  OS32 から rmdir できない**。段 C2 の rmdir 掃引では 290 回中 82 回がこの形。決裁の「または links が
  多い側に振れている」をそのまま実装した結果で、データは失われない

### 11-7. 追加した試験と件数、変異

**`test_b8_open.py`: 1208 → 1461 checks, 0 failures** (e2fsck **654 像**: 異常なし 350 / 許容のみ
302 / 不整合 2、**`media_check` との食い違い 0**。不整合の 2 像は往復 5 と同じ期待どおりの
`[R5-LEGACY]` と `[R5-BADDIR]`。新しい分類の行は出ていない)

| 名前 | 中身 |
|---|---|
| `[R5-1]` / `[R5-1s]` (拡張) | 後続操作に「新しい親に後続で作ったものを片付け → **旧親を rmdir → 同じ名前で mkdir**」を足し、ガードの判定を `GuardTally` で媒体と突き合わせる (§11-1 の表) |
| 段 C2 (新設) | mkdir / rmdir の掃引 × 2 模様で、失敗 → 再起動 → **孤児の親を rmdir → 同じ名前で mkdir** → 媒体検査 + e2fsck。`sweep()` に後続操作の口を足した |
| `[R6-GUARD]` | 健全 3 通り (通知 0)、本物の孤児 (`delete_entry` で名前だけ消す) の親は NOTEMPTY・**書き込み 0 セクタ・エラー状態にしない・通知 1 行・inode は残る**・VFS からも NOTEMPTY・その後の書き込みは通る・mkdir しても媒体は整合 (e2fsck 2 像)。多い側だけ (links 3) も断り、2 に戻せば消せる |
| `[R6-STAGE6]` / `[R6-STAGE6g]` | §11-3 |
| `[R6-USEDDIRS]` | §11-4 |
| `[R6-GROUPS]` | §11-2 (3 グループ、6 通り、e2fsck 4 像) |

**変異 (往復 6)**: `scratchpad/r6/mutate_r6.py`。判定は BUILD / MEDIA / E2FSCK / STALL / RC。
**9 / 9 RED、ビルド失敗 0** (1 件目の `R6-USEDDIRS` は `if (1)` にして `dropped` が未使用になり
`-Werror` でビルドが落ちた — 変異の書き方の誤り。`if (dropped || 1)` に直して再実行し RC で落ちた)。

| 変異 | 判定 | 落ちた件数 | 何が落としたか |
|---|---|---|---|
| R6-GUARD ガードを外す | **MEDIA** | 28 | `[R5-1]` / `[R5-1s]` の後続後の不整合 (`bad_after`)・孤児の親を通した (`orphan_left`)、段 C2 4 掃引の後続後の不整合、`[R6-GUARD]` |
| R6-GUARD+MC ガードを外し、`media_ok` から `links_short` / `dangling` も外す | **MEDIA + E2FSCK** | 21 + **食い違い 11** | **e2fsck だけが不整合と見た像が 11 枚** (`[R5-1]` 5 / `[R5-1s]` 2 / 段 C2 rmdir 4 / `[R6-GUARD]` 1 のうちの抜き取り分)。決め手の行は **`Inode 915 ref count is 2, should be 3.`** (= ref count が少ない: 解放された親の番号を mkdir が再利用し、孤児の `..` がその新しいディレクトリを指して数えられる)。同じ出力に `'..' in ... (916) is /sw (915), should be <The NULL inode>` と `Unconnected directory inode 916` も出る。MEDIA 側はガードの判定の突き合わせ (`orphan_left`) |
| R6-GUARD-STRICT ガードを `>= 2` に (健全なものも断る) | MEDIA | 32 | 健全なディレクトリの rmdir 一式 (`[R6-GUARD]` (a)〜(c)、往復 3 の BONUS-4 / 4b、`[R6-USEDDIRS]` の対照、`wrong_refuse`・`healthy_removed`) |
| R6-ALLOC-CONT alloc_block を `continue` に戻す | RC | 5 | `[R6-GROUPS]` (1)(4)(6): 別グループのビットマップが変わる・名前ができる・IO を返さない |
| R6-ALLOCI-CONT alloc_inode を `continue` に戻す | RC | 5 | `[R6-GROUPS]` (2)(3)(6) |
| R6-ALLOC-NOSPCIO create / mkdir が割り当ての IO を NOSPC に読み替える | RC | 2 | `[R6-GROUPS]` (2)(3) |
| R6-STAGE6 段 6 を sync の結果に戻す | RC | 1 | `[R6-STAGE6]` (割り当てない回が OK を返す) |
| R6-USEDDIRS 残したときも `used_dirs--` | RC | 1 | `[R6-USEDDIRS]` |
| R6-WATCH `h_sys3` の `"memory"` clobber を外す (見張りそのもの) | **STALL** | — | 30 秒で `E2FSCK STALL: 子が 30 秒間 1 行も出力しない — 最後に e2fsck へ渡した像: SWEEP ext2_write 上書き ... at=1 once` |

割り当ての変異が MEDIA ではなく RC なのは想定どおり: 往復 5 の `continue` でも媒体は整合し、壊れるのは
「完了しているのに IO」という**戻り値と媒体の食い違い**だから。

**往復 1〜5 の変異は今回は再実行していない** ([V4])。往復 1〜5 の試験は全件通したまま。

### 11-8. 触ったファイル (往復 6)

カーネル (FS): `fs/ext2.h` (`EXT2_EMPTY_DIR_LINKS`)、`fs/ext2_dir.c` (rmdir のガード・`used_dirs`、
rename_dir 段 6 とそのコメント、不変条件のコメントの訂正、割り当ての戻り値の伝播 3 か所、`kprintf.h`)、
`fs/ext2_inode.c` (`ext2_alloc_block` / `ext2_alloc_inode` の戻り値、`bmap_set` の伝播 3 か所と約束の
コメント)、`fs/ext2_file.c` (割り当ての戻り値の伝播 4 か所)。**`sdk/kapi.json`・エラー番号は不変**

試験: `tools/tests/b8_open_host.c` (段 G・段 C2・`[R5-1]` の拡張・`sweep()` の後続操作の口・
複数グループのディスク・ガードの突き合わせ)、`tools/tests/test_b8_open.py` (見張り)、`tools/tests/b8_tdd.md`
(§10-1 の訂正と本節)。`build/sdk.mk` は変えていない

## 12. 段 H — 独立した 2 か所の失敗 (二重故障、2026-09-16)

[`docs/archive/shell/TASK_FS_TYPE.md`](../../docs/archive/shell/TASK_FS_TYPE.md) §2-6 が
「範囲外の未解消」として残していた 3 つのうちの最後の 1 つ。失敗注入の網羅は
「N 番目を 1 回だけ (once)」「N 番目以降を全部 (sticky)」の 2 形式で、独立した
2 か所の失敗の組み合わせは専用試験 1 件 (`[P1-C'']` `case_create_inode_write_ambiguous`) だけだった。

### 12-1. 何を足したか

- 注入に `sw_arm_pair(i, j)` を足した。「**i 番目と j 番目の I/O だけ**が落ちる」(i < j、sticky なし)。
  位置は実際に走った I/O の順番なので、1 つ目で経路が変わった後の j もその走行の中の j 番目になる。
  既存の `sw_arm` は `at2 = 0` のままなので、once / sticky の掃引の挙動は 1 ビットも変わらない。
- 段 H (`stage_h_pair_sweeps`) を足した。自前のディスクに `/p` `/q` を作り、代表的な書き込み経路を
  i < j の全組み合わせで掃く。判定は段 C と同じ `media_check` と本物の `e2fsck -fn` の抜き取り
  (両方が落ちた回のうち、i と j がどちらもフィボナッチ位置のもの + 各分類の初回)。
- 経路ごとに**空打ちで I/O 数 N を数え**、`N > B8_PAIR_MAXN` (既定 200) の経路は理由つきで飛ばす。
  組み合わせは N^2/2 で増えるので、二重間接まで届く段 C の経路 (N は数千) は既定では入らない。
  広く掃くときは `B8_PAIR_MAXN=3000 python3 -B tools/tests/test_b8_open.py` のように上げる
  (数分〜かかる)。上限は実行時に段 H の見出しへ出る。

対象の 11 経路 (それぞれ「番号の模様」と「ディレクトリ風の模様」の 2 回):
`ext2_create` (1 ブロック / 13 ブロック = 単一間接)、`ext2_write` 伸長 (4B → 13 ブロック)、
`ext2_write` 縮小 (13 ブロック → 4B = truncate)、`ext2_unlink` (1 ブロック / 13 ブロック)、
`ext2_rename` (ファイル 同一ディレクトリ / 別ディレクトリ / **ディレクトリ**を別の親へ)、
`ext2_mkdir`、`ext2_rmdir`。

### 12-2. 結果 (2026-09-16、`d191fd5` + 段 H)

```
== 段 H: 独立した 2 か所の失敗 (i 番目と j 番目の I/O だけが落ちる) ==
        上限 N<=200 (B8_PAIR_MAXN)
  [PAIR] ext2_create (1 ブロック) / 番号の模様
         N=36 runs=133 both-fired=98 error-runs=133 leak-runs=22 max-leak=1 orphan-runs=18 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_create (13 ブロック = 単一間接) / 番号の模様
         N=114 runs=2931 both-fired=2818 error-runs=2931 leak-runs=1930 max-leak=14 orphan-runs=18 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_write 伸長 (4B -> 13 ブロック) / 番号の模様
         N=114 runs=2947 both-fired=2834 error-runs=2947 leak-runs=2167 max-leak=15 orphan-runs=0 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_write 縮小 (13 ブロック -> 4B = truncate) / 番号の模様
         N=90 runs=2403 both-fired=2314 error-runs=2403 leak-runs=2124 max-leak=3 orphan-runs=0 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_unlink (1 ブロック) / 番号の模様
         N=36 runs=169 both-fired=134 error-runs=136 leak-runs=33 max-leak=1 orphan-runs=31 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_unlink (13 ブロック = 単一間接) / 番号の模様
         N=90 runs=2315 both-fired=2226 error-runs=2282 leak-runs=1945 max-leak=3 orphan-runs=31 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_rename (ファイル / 同じディレクトリ) / 番号の模様
         N=40 runs=205 both-fired=166 error-runs=165 leak-runs=0 max-leak=0 orphan-runs=85 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_rename (ファイル / 別のディレクトリへ) / 番号の模様
         N=40 runs=205 both-fired=166 error-runs=165 leak-runs=0 max-leak=0 orphan-runs=85 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_rename (ディレクトリ / 別の親へ) / 番号の模様
         N=58 runs=331 both-fired=274 error-runs=291 leak-runs=0 max-leak=0 orphan-runs=164 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_mkdir / 番号の模様
         N=44 runs=209 both-fired=166 error-runs=209 leak-runs=40 max-leak=1 orphan-runs=60 hole-runs=0 inconsistent=0 unreported-leak=0
  [PAIR] ext2_rmdir (空のディレクトリ) / 番号の模様
         N=46 runs=271 both-fired=226 error-runs=271 leak-runs=58 max-leak=1 orphan-runs=106 hole-runs=0 inconsistent=0 unreported-leak=0
```

ディレクトリ風の模様も同じ数字 (11 経路 × 2 模様 = 22 掃引)。合計

| | |
|---|---|
| 走行 | 24,238 (両方が落ちた回 22,844) |
| **不整合 (`media_check`)** | **0** (22 掃引すべて) |
| 報告されない漏れ | **0** (漏れたのに成功を返した回は無い) |
| 漏れた回 / 最大の漏れ | 16,638 回 / 15 ブロック (§2-6 の「意図した交換」) |
| 孤児の出た回 / 末尾の穴 | 1,196 / 0 |
| `e2fsck -fn` の抜き取り | 段 H で 98 枚増えて全体 752 枚、**食い違い 0**、不整合 2 枚 (どちらも従来からの「期待して作った不整合」の像) |
| 検査数 | 1461 → 1551 checks, 0 failures |
| 実行時間 | 11.0 秒 → **13.7 秒** (段 H が +2.7 秒。`make check` 全体 約 90 秒に対する増分) |

**不整合は 1 件も見つからなかった。** 許容したのは §2-6 の「漏れ」(使用中だが未参照のブロック /
inode) と孤児・末尾の穴だけで、これは `e2fsck` 側でも「Block bitmap differences」「Unattached inode」
「ref count が多い」の分類に入り、`media_check` と食い違わなかった。

走行数が N^2/2 より少ないのは、1 つ目の失敗でメタデータの I/O エラー状態に入ると以後の書き込みが
断られて走行が短くなり、j 番目の I/O がそもそも存在しない組み合わせが多いため (j は「その走行で
実際に届いた位置」までしか掃かない)。`both-fired` がその内訳。

### 12-3. この節が言わないこと ([V4])

- **変異試験をしていない。** 段 H が実際に二重故障だけの不整合を捕まえられるかは、
  意図的に壊した ext2 で確かめていない (票の指示で FS のコードには触らないため)。
  段 H の生きていることは `both-fired` の回数と e2fsck の抜き取り枚数 (+98) でしか示していない。
- 既定の上限 N ≤ 200 なので、**二重間接まで届く経路の二重故障は掃いていない** (段 C の once / sticky は掃いている)。
- 3 か所以上の同時故障は対象外。

## 13. 段 X3 — 通常ファイル同士の置き換え rename (票 H2 §2-2)

対象票: [`../../docs/archive/shell/TASK_H2.md`](../../docs/archive/shell/TASK_H2.md) §2-2 / §2-2-1 /
§2-2-2 / §2-2-3 と §4-1 の **X3 / X3b / X3c / X3d**。基点 `feat/gui` = `15c5edf`。

### 13-1. 何を足したか

1. **注入側に「媒体へ届いた後に失敗する」模様を足した** (往復 3 所見 2)。
   これまでの注入はセクタを写す**前**に `-1` を返していたので、
   `drivers/ide.c` が状態レジスタを転送の**後**に見る現実 —
   「書き込みが失敗した = 媒体は未変更」が成り立たない —
   を 1 度も作れていなかった。`wfail_arm_landed()` と `g_sw_land` がその模様。
   さらに `g_wfail_then_rfail` で「書き込みが落ちた後の読み直しも落とす」= 公開の
   3 値の「不明」を作れるようにした。
2. **掃引に追加の不変条件を掛けられるようにした** (`g_sw_extra`)。
   置き換え rename では
   **「宛先の名前がどの試行でも存在し、旧 inode D か新 inode S を指す」**
   を、ext2 のコードを通さず媒体から直に確かめる (`raw_find_name`)。
   段 C の once / sticky と段 H の二重故障の両方に掛けてある。
3. 段 H に `ext2_rename (既存ファイルを置き換える)` を足した (既存 11 経路の
   数字は変えていない)。
4. 新しい段 X3 で、公開処理の 3 値・名前解決メモ・`ext2_unlink` の戻り値を
   狙って確かめる。
5. **X4** (`case_x3_links2`): 置き換えた後の旧 inode D の始末を分けて見る。
   `links 1 -> 0` は**解放される**、`links 2 -> 1` は**残す** (もう一つの名前が
   まだ D を指しているので、解放したら生きている名前が解放済み inode を指す)。
   残した側は旧内容がその名前から読めることまで確かめる。

### 13-2. RED (置き換え経路と戻り値の伝播を外した版)

`fs/ext2_dir.c` の分岐を `if (0)` にして**従来の unlink + add 経路**へ戻し、
`fs/ext2_file.c` の `ext2_unlink` を「`ext2_write_inode` / `ext2_sync` の戻り値を
捨てて `EXT2_OK`」に戻した状態。

```
          [X3] ext2_rename (既存ファイルを置き換える) runs=242 dst->new=56 dst->old=38 dst-missing=148 dst-alien=0
  FAIL line 2680: X3: the destination name exists after every injected failure
          [X3] ext2_rename (既存ファイルを置き換える / 二重故障) runs=761 dst->new=436 dst->old=57 dst-missing=268 dst-alien=0
  FAIL line 2680: X3: the destination name exists after every injected failure
== 段 X3: 通常ファイル同士の置き換え rename (票 H2 §2-2) ==
  [X3b] 前半セクタ / 媒体に届く前に失敗 -> 未公開
  FAIL line 5093: raw_find_name(dir, "dst", &seen) == 1
  FAIL line 5108: seen == d_ino
  [X3b] 後半セクタ / 媒体に届く前に失敗 -> 未公開
  FAIL line 5093: raw_find_name(dir, "dst", &seen) == 1
  [X3b] 前半セクタ / 媒体に届いた後に失敗 -> 公開済み
  FAIL line 5097: rc == EXT2_OK
  FAIL line 5098: seen == s_ino
  FAIL line 5099: raw_find_name(dir, "src", (u32 *)0) == 0
  FAIL line 5101: raw_inode_used(d_ino) == 0
  (後半セクタも同じ 5 件)
  [X3b] 前半セクタ / 書き込みも読み直しも失敗 -> 不明
  FAIL line 5093: raw_find_name(dir, "dst", &seen) == 1
  FAIL line 5110: seen == d_ino || seen == s_ino
  [X3d] ext2_unlink: links の書き込みだけ失敗 -> OK を返さない
  FAIL line 5249: X3d: ext2_unlink never reports OK when links stay behind

1724 checks, 19 failures
```

**旧コードが何をしていたか**: `ext2_rename` は宛先が通常ファイルなら
`ext2_unlink(new)` で**宛先の名前を先に消して**いた。掃引 242 回のうち
**148 回で宛先の名前が消えたまま**残る (`dst-missing=148`、二重故障では 268)。
これが H2 が塞ぎにいった穴そのもの。

`ext2_unlink` は複数リンク分岐の `ext2_write_inode` と末尾の `ext2_sync` の
戻り値を捨てて `EXT2_OK` を返していたので、「名前は消えたが `links_count` は
2 のまま」でも成功に見えた (X3d)。

### 13-3. RED (X3c の否定側 — 段 0 の `ext2_ns_touch` を外した版)

```
  [X3c] 段 2 の後に古い D を返さない (ext2_ns_touch)
  FAIL line 5165: st.st_ino == s_ino
  FAIL line 5166: st.st_size == X3_NEW_LEN
  FAIL line 5171: vfs_get_size(fd) == X3_NEW_LEN
  FAIL line 5177: sz == X3_NEW_LEN

1724 checks, 4 failures
```

段 2 は `delete_entry` / `add_entry` を通らないので、既存の無効化に相乗りできない。
段 3 が落ちた回 (この試験は `/a` のブロックへの書き込みを落として作る) は
`ext2_delete_entry` の `ns_touch` も走らないため、**`ext2_resolve_path` の記憶が
解放済みの D を返し続ける**。`stat` / `vfs_open` / `get_size` の 3 経路で見ている。

### 13-4. GREEN

```
  [SWEEP] ext2_rename (既存ファイルを置き換える) / 番号の模様
          runs=206 error-runs=204 leak-runs=83 max-leak=12 orphan-runs=39 hole-runs=0 inconsistent=0 unreported-leak=0
          [X3] ext2_rename (既存ファイルを置き換える) runs=206 dst->new=166 dst->old=40 dst-missing=0 dst-alien=0
  [SWEEP] ext2_rename (既存ファイルを置き換える) / ディレクトリ風の模様
          runs=206 error-runs=204 leak-runs=83 max-leak=12 orphan-runs=39 hole-runs=0 inconsistent=0 unreported-leak=0
          [X3] ext2_rename (既存ファイルを置き換える) runs=206 dst->new=166 dst->old=40 dst-missing=0 dst-alien=0
  [PAIR] ext2_rename (既存ファイルを置き換える) / 番号の模様
         N=58 runs=436 both-fired=379 error-runs=436 leak-runs=69 max-leak=1 orphan-runs=222 hole-runs=0 inconsistent=0 unreported-leak=0
          [X3] ext2_rename (既存ファイルを置き換える / 二重故障) runs=436 dst->new=375 dst->old=61 dst-missing=0 dst-alien=0
  (ディレクトリ風の模様も同じ数字)

== 段 X3: 通常ファイル同士の置き換え rename (票 H2 §2-2) ==
  [X3b] 前半セクタ / 媒体に届く前に失敗 -> 未公開
  [X3b] 後半セクタ / 媒体に届く前に失敗 -> 未公開
  [X3b] 前半セクタ / 媒体に届いた後に失敗 -> 公開済み
  [X3b] 後半セクタ / 媒体に届いた後に失敗 -> 公開済み
  [X3b] 前半セクタ / 書き込みも読み直しも失敗 -> 不明
  [X3b] 後半セクタ / 書き込みも読み直しも失敗 -> 不明
  [X3c] 段 2 の後に古い D を返さない (ext2_ns_touch)
  [X3d] ext2_unlink: links の書き込みだけ失敗 -> OK を返さない
          runs=7 links-not-fixed=2 wrongly-ok=0
  [X4] 置き換え: 旧 inode の links 1 -> 0 は解放、2 -> 1 は残す

1778 checks, 0 failures
E2FSCK samples=761 clean=399 allowed-only=360 inconsistent=2 mismatch=0
```

| | |
|---|---|
| **宛先の名前が消えた回** | **0** (掃引 206 x 2 模様 + 二重故障 436 x 2 模様 = 1284 回) |
| **宛先が旧でも新でもない inode を指した回** | **0** |
| 旧 / 新のどちらにも実際に転ぶ | 掃引 166 / 40、二重故障 375 / 61 |
| 不整合 (`media_check`) | **0** (全掃引) |
| 報告されない漏れ | **0** |
| `e2fsck -fn` の抜き取り | 761 枚、**食い違い 0**。不整合 2 枚はどちらも従来からの「期待して作った不整合」の像 |
| 検査数 | 1551 → **1778 checks, 0 failures** |

`e2fsck` の不整合 2 枚は `F R5-LEGACY two names` と `F R5-BADDIR corrupted rec_len`
(どちらも意図して壊した像) のまま。置き換え rename が新しく作った不整合は無い。

### 13-5. この節が言わないこと ([V4])

- **ゲスト (NP21/W) でも実 NHD でも 1 度も動かしていない。** 装置と
  エミュレータ内部のキャッシュ、電源断からの復旧 (票 §4-2 の 6) は未確認。
- 段 X3 の X3b は**狙いを定めた 6 通り**で、公開セクタへの故障を全位置で
  掃いてはいない (全位置の掃引は段 C / 段 H が `g_sw_extra` の不変条件つきで
  受け持つ)。
- X3d は「名前は消えたが `links_count` が 2 のまま」の形が起きた 2 回を見ている。
  `links_count` を**直す**ことはしていない (推測で書き換えない、§2-2-4)。
- 段 H の上限は既定 `N <= 200` のまま。**二重間接まで届く経路の二重故障は
  掃いていない**。
