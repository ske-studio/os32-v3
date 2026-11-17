# tools/mk_settings_db.py — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S0 (S0-T) の契約を今のソースと照らして要約
ソース: `tools/mk_settings_db.py` (`parse_tsv`・`write_db`・`resolve_epoch`)・`assets/settings/defaults.tsv`・`build/assets.mk`・`build/image.mk`・`build/packages.yaml`

設定レジストリの初期値の正典 (tsv) から、インストール媒体が持つ `settings.db` を作るホストの道具。設定レジストリの全体の約束は `docs/design/userland/lib/cfg/libos32cfg.md`。

## 初期値の正典と置き場

- 正典は `assets/settings/defaults.tsv` (人が書く側)。
- 生成した DB は **媒体 (FDD・CD) だけ** が持つ。`build/packages.yaml` の MINIMAL パッケージの `files:` が `build/out/settings.db` を `/etc/settings.db` として載せ、FDD の像もそこから作る。
- 既存のシステムには tsv を `/etc/settings.tsv` として通常配備する (`userland/deploy.yaml`)。DB を作るのはゲストの `cfg init` (tsv が在って DB が無いときだけ) で、通常配備は DB を作らない。

## tsv の規則 (`parse_tsv`)

ゲストの C の reader (`userland/lib/cfg/cfg_tsv.c`) と同じ規則で読む。

- UTF-8、改行は LF だけ。ファイルのどこかに CR があれば拒否。
- 1 行 = `scope<TAB>key<TAB>type<TAB>value` の厳密 4 列。行を strip しない (末尾の空欄は **空の text** で、NULL ではない)。空行と `#` で始まる行は無視 (行頭だけ。行の中の `#` は値の一部)。
- scope: `system`・`gshell`・`user`・`app:<name>` (`name` は `[a-z0-9_]+`)、空でなく 63 byte 以内、NUL なし。
- key: `[a-z0-9_]+(/[a-z0-9_]+)*`、空でなく 63 byte 以内、NUL なし。
- type: `int`・`text`・`blob`。
  - int: 字句は `-?[0-9]+`、int32 の範囲。先頭の 0 は有効で、変換の **前** に畳み、畳んだ桁が 10 を超えたら範囲外 (Python の桁数制限で例外にしない)。
  - text: UTF-8 として妥当、NUL なし、255 byte 以内 (空でよい)。
  - blob: hex (偶数桁、空白なし、大文字小文字可)、8192 文字 (= 4096 byte) 以内。
- (scope, key) の重複は拒否。
- 違反は `mk_settings_db: <path>:<line>: <理由>` を標準エラーに出して非ゼロで終わり、DB を作らない。境界の値 (int32 の両端、63 byte の key、4096 byte の blob、多 byte 文字を含む 255 byte の text) は受ける。

## 出力 (`write_db`)

- schema: `meta(schema_version INTEGER NOT NULL, created TEXT)` と `settings(scope TEXT NOT NULL, key TEXT NOT NULL, type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB, PRIMARY KEY(scope,key)) WITHOUT ROWID`。type の番号は int 0・text 1・blob 2。
- `page_size` 1024、`journal_mode` DELETE、`meta.schema_version` と `user_version` は 1 (`--schema-version` は試験の fixture 専用)。
- **決定性**: 同じ tsv と同じ epoch なら同じバイト列。`meta.created` は `--epoch` → `SOURCE_DATE_EPOCH` → 0 の順で決めた時刻 (UTC の ISO 形式)。入力の mtime は読まない。行は (scope, key) の順で入れ、VACUUM で自由ページを落とす (`freelist_count` 0、`integrity_check` ok)。
- 出力先に古い DB・`-journal`・`-wal`・`-shm` があれば先に消し、空の journal を残さない。

## ビルドへの結線

- `build/assets.mk` の `$(BUILD_OUT)/settings.db` は tsv と本ツールと `FORCE` に依存し、ビルドのたびに作り直す。`all`・`assets-all` が依存し、`clean` で消える。
- `build/image.mk` の FDD の像 (`images/os32_boot.d88`・`images/os32_boot144.img`、`FDD_IMAGE_DEPS`) と `packages` がこの DB に直接依存する。`packages` は core パッケージが要る既存の入力 (programs・boot・`vmkernel.lz4`・`unicode_bin` など) にも依存し、clean の後の単独の `make iso`・`make -j` でも欠けない。
- パッケージの欠損は mkpkg がエラーにする (`docs/design/tools/mkpkg.md`)。

## 検査

`tools-host` の中の `tools/tests/test_mk_settings_db.py` (ホストだけ): 決定性 (`Determinism`)・schema と PRAGMA (`Schema`)・規則違反の拒否と境界値の受理 (`Rejects`)・mkpkg の欠損 (`MkpkgMissingFile`)・リポジトリのパッケージ構成 (`RealPackageDefs`)・実物の tsv (`RealDefaults`)・ビルドの結線 (`BuildWiring`)。
