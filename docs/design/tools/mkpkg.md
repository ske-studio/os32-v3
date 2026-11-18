# tools/mkpkg.py — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S0 (S0-T) の契約を今のソースと照らして要約
ソース: `tools/mkpkg.py` (`build_from_yaml`・`merge_package_defs`)

パッケージ定義 (YAML) からインストール媒体の `.PKG` を作る。この設計書は登録ファイルの欠損の扱いだけを書く。

## 欠損はエラー

- 登録したファイルが無い・glob の展開が 0 件・`--defs` に渡した定義ファイルが無い、の 3 つを欠損として扱う。警告で飛ばさない。
- **全パッケージのファイルを先に解決** してから欠損をまとめて `ERROR: <理由> (package '<名前>')` で標準エラーに出し、非ゼロで終わる。このとき `.PKG` を 1 つも書かない (後ろのパッケージの欠損で、前のパッケージの `.PKG` を残さない)。
- 属性の追加ではなく全ファイルに一律。簡易 parser は変えず、展開の結果だけを見る。
- リポジトリ自身のパッケージ構成 (`build/packages.yaml`、中身は配備マニフェストのタグから作る) に 0 件の glob や欠けたファイルを残さない。残すとこの規則で媒体のビルドが落ちる。

## 検査

`tools/tests/test_mk_settings_db.py` の `MkpkgMissingFile` (欠損の 3 種と「何も書かない」) と `RealPackageDefs` (`build/packages.yaml` の構成に問題が無い)。

## 消費側の上限を作る側で守る

> 出所: TASK_VFS_FD_PATH (方針 v2 の 9) の契約を今のソースと照らして要約 (2026-11-18)

消費側 (`userland/lib/rt/pkg.c`) は長すぎるパス・多すぎる項目を切り詰めずに断る。作る側はその前に落とす。

- 格納パスは **UTF-8 のバイト長** で `PKG_GUEST_PATH_MAX` (= `PKG_MAX_PATH` 128 − NUL 1 − 前置 `/hd0` 4 = 123) 以下。cdinst が展開先に `/hd0` を前置しても `PKG_MAX_PATH` に収まる長さ。超えたら `ERROR: guest path too long …` を標準エラーに出して終了コード 1 (黙って切り詰めない)。
- 項目数 (ファイルとディレクトリの合計) は `PKG_MAX_ENTRIES` (128) まで。超えたら `ERROR: too many entries …` で終了コード 1。
- 定数は消費側の `userland/lib/rt/pkg.h` の値の写し。

検査: `make check-vfs-fd-path-host` (`tools/tests/test_vfs_fd_path.py` の段 `mkpkg`、実物の `tools/mkpkg.py` で 123 / 124 バイト、UTF-8 の 123 / 126 バイト、300 バイト、128 / 129 項目)。
