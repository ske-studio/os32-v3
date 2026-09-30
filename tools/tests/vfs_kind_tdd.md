# B6 / B7 — `vfs_path_kind()` と、それを消費する `vfs_open()` (ホスト試験の記録)

- 票: [`docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md`](../../docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md)
  (票 H1) / Codex 実装レビュー 往復 3 の **B6**、往復 4 の **B7**。退行が入ったのは `d574704`
- 上位の記録: [`h1_tdd.md`](h1_tdd.md) — H1 全体の RED→GREEN はそちら
- 実行: `python3 -B tools/tests/test_vfs_kind.py [--target]`
  (`make check-vfs-kind-host` が同じものを `--target` 付きで回す)
- 対象: `fs/vfs.c` / `fs/vfs_fd.c` (実物を `#include`)
- 仕様: [`docs/06_filesystem.md`](../../docs/06_filesystem.md) §6-1 (VFS エラーと種別)

## 0. 正直に書く ([V4])

**この記録は試験と実装の後に書いた (2026-09-15)。** 試験は B6 / B7 を直した
往復 3・4 の中で書かれていて、そのときの RED→GREEN は [`h1_tdd.md`](h1_tdd.md)
にある。ここはそれを `docs/TESTS.md` から名前で引けるようにするための記録で、
**新しく RED を踏み直してはいない**。

自動の変異 (`--mutate`) は**無い**。§3 の表は試験を読んで書いたもので、
実際に壊して走らせた記録ではない。

## 1. 何を確かめる試験か

`tools/tests/vfs_kind_host.c` が実物の `fs/vfs.c` をそのまま `#include` し、
境界 (kstring / kmalloc) だけを同義の C で置く。FS ドライバは合成した `VfsOps` で、
`stat` / `list_dir` / `get_file_size` の戻り値を**1 つずつ**指定できる。

**直す前の反例** (B6):

```
stat が IO で失敗
  -> 「stat 未対応」とみなして下のプローブへ落ちる
  -> list_dir が FULL (1000 件超)
  -> get_file_size は **ディレクトリでも成功する**
  -> VFS_KIND_FILE
結果 `cd` が NOTDIR になり、sys_open のディレクトリ拒否をすり抜ける。
```

往復 4 の **B7** は「`vfs_path_kind` の戻り値までしか見ていなかった」ことが
検出漏れの原因だったので、**実物の `fs/vfs_fd.c` も同じ翻訳単位に取り込み、
`vfs_open()` まで通す**。種別の判定は、それを**消費する側**まで試験する。

## 2. GREEN (現状)

```
HOST GNU89 -Werror COMPILE PASS (real fs/vfs.c)
35 checks, 0 failures
EXIT vfs_kind_host=0
TARGET i386-elf -Werror COMPILE PASS (fs/vfs.c)
TARGET i386-elf -Werror COMPILE PASS (fs/vfs_fd.c)
```

| 節 | 見ているもの |
|---|---|
| `stat` を持つドライバ | DIR / FILE / `NOTFOUND` をそのまま返す。ext2 / FAT / ISO9660 / HostDrv は 4 つとも `stat` を持つので、既存の判定が動かないことの担保 |
| `stat` の失敗 | `IO` は `VFS_ERR_IO` のまま返す。**`get_file_size` を呼ばない**し、**プローブへも落ちない** (`drv_size_calls == 0` / `drv_list_calls == 0`) |
| `stat` 非対応ドライバ | プローブ経路。DIR なら `get_file_size` を呼ばない、FILE なら 1 回だけ呼ぶ |
| プローブの失敗 | `list_dir` が `FULL` / `IO` なら**そのエラーを返す**。`get_file_size` へ進まない (= ディレクトリをファイルに化けさせない) |
| マウント境界 | マウント点そのものは DIR、未マウントは `VFS_ERR_NOMOUNT` |
| `vfs_open` (B7) | 種別が不明なら **FD を返さない**。`cat /etc` を通さない。`O_CREAT` でも種別が不明なら開かず**作りもしない** (`drv_write_calls == 0`) |
| `vfs_open` の従来どおり | 通常ファイルは開ける / DIR は `VFS_ERR_ISDIR` / 不存在 + `O_CREAT` は作れる (`NOTFOUND` は続行) / 不存在で `O_CREAT` 無しは `NOTFOUND` |

`--target` は同じソースが `i386-elf-gcc -Werror` でも通ること ([C1]) を別に見る。

## 3. 壊すとどれが落ちるか

| 壊し方 | 落ちる検査 |
|---|---|
| `stat` の失敗を「`stat` 未対応」と同じ扱いにする (B6 の退行) | 「`get_file_size` を呼ばない」「`stat` を持つドライバではプローブへ落ちない」 |
| `list_dir` の `FULL` / `IO` を無視して `get_file_size` のプローブへ進む | 「`OS32_ERR_FULL` を返す」「`get_file_size` を呼ばない」— ディレクトリが FILE に化ける |
| `vfs_open` が `vfs_path_kind` の負値を `NOTFOUND` に畳む (B7) | 「そのエラーを返す (ISDIR にも NOTFOUND にもしない)」 |
| `vfs_open` が種別不明でも `O_CREAT` を進める | 「`O_CREAT` でも種別が不明なら開かない」「作成もしない」— **既存ディレクトリの上にファイルを作りかける** |
| `NOTFOUND` まで「開かない」側に寄せる | 「不存在 + `O_CREAT` は作れる (`NOTFOUND` は続行する)」が失敗 |

## 4. この試験が言わないこと

- ドライバは合成した `VfsOps` で、**実物の ext2 / FAT / ISO9660 / HostDrv は
  動かしていない**。ext2 側の実物経路は [`b8_tdd.md`](b8_tdd.md)、
  HostDrv の列挙は [`hostdrv_list_tdd.md`](hostdrv_list_tdd.md) が見る。
- マウント表・FD 表の容量や世代番号は対象外 ([`vfs_fd_sqlite_tdd.md`](vfs_fd_sqlite_tdd.md))。
- `(dev_type << 8) | unit` の mount エンコード (POLICY_DEBUG §4-30) は
  [`vfs_mount_dev_tdd.md`](vfs_mount_dev_tdd.md) が見る。
