# userland/lib/rt/pkg.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_VFS_FD_PATH (方針 v2 の 9) の契約を今のソースと照らして要約
ソース: `userland/lib/rt/pkg.c` (`pkg_parse`・`pkg_first_overflow`・`pkg_extract`)・`userland/lib/rt/pkg.h` (`PKG_MAX_PATH`・`PKG_MAX_ENTRIES`・`PKG_ERR_*`)

インストール媒体の `.PKG` (ヘッダ 32 B → ファイルテーブル → データ部) を読んで展開する。作る側は `tools/mkpkg.py`。
原則: **溢れる・切れた・食い違う表は切り詰めずに断る。** 長いパスを途中まで読んで読み位置がずれたり、超過した項目を黙って捨てたりすると、後のファイルが前を上書きしたり欠けたまま成功したりする。

## `pkg_parse` — 表の読み込み

| 入力 | 結果 |
|---|---|
| 開けない・ヘッダ 32 B が読めない | `PKG_ERR_IO` |
| マジックが `PKG1` でない | `PKG_ERR_MAGIC` |
| 項目のパス長が `PKG_MAX_PATH` (128、NUL 込み) 以上 = 127 バイトを超える | パスを読まずに `PKG_ERR_TOOLONG` |
| 128 項目 (`PKG_MAX_ENTRIES`) の後に終端 (長さ 0) 以外が来る | `PKG_ERR_CORRUPT` (ヘッダの項目数が 128 と言っていても) |
| 表が途中で切れている (長さ・パス・サイズ + 種別の 5 B のどれかが読み切れない) | `PKG_ERR_CORRUPT` |
| ヘッダの項目数と表の項目数が違う | `PKG_ERR_CORRUPT` |

127 バイトのパスと 128 項目はちょうど通る。

## `pkg_first_overflow` — 展開先の前置で溢れる項目

`prefix_len + パスの長さ + 1 > PKG_MAX_PATH` となる最初の項目の番号を返す (無ければ -1)。cdinst は前置 `/hd0` (4 バイト) で呼び、溢れる項目があれば **展開の前に** 断る (1 セクタも書かない)。格納パス 123 バイトまでは展開でき、124〜127 バイトは断られる。

## `pkg_extract` — 展開

- 種別がファイルの項目だけを書く。置き場を `O_WRONLY | O_CREAT | O_TRUNC` で開けない (置き場がディレクトリなど) ・書き切れないときは `PKG_ERR_IO` で止める。失敗を飲み込んで `PKG_OK` を返さない。無圧縮・LZSS のどちらの経路も同じ。

検査: `make check-vfs-fd-path-host` (`tools/tests/test_vfs_fd_path.py` の段 `pkg`、実物の `pkg.c` を ext2 の像の上で)。像は `e2fsck -fn` に当てる。
