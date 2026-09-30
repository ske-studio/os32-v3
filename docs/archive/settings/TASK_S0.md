# S0 — 設定レジストリの基盤 3 票 (S0-K / S0-D / S0-T)

> 発行: PM (2026-09-13) / 状態: **受入完了 (2026-09-13)**

設計 第 4 版で実装着手 (ユーザー決裁 2026-09-13: 4 往復目は回さず実装へ。Codex は実装レビューで再確認)**。決裁: [S0_PLAN_2026-09-13.md](S0_PLAN_2026-09-13.md) §3 (2026-09-13、4 点承認 + 初期値はビルド毎に生成しインストーラに添付)。
契約の正典は [S0_FOUNDATION.md](S0_FOUNDATION.md) §2 (非破壊 / transaction) と §4 (ABI 追加案)、§5 (D0)。本票はそれを「いま実装する範囲」に切り、着地条件を決める。
規約: [C1] C89、[C2] kstr*、[ABI1〜3] kapi.json SSoT / 末尾追記 / 版上げ + `make clean`、[V4]。コーダーは worktree で実装 + ホスト TDD のみ (配備・コミット・エミュレータ・make は禁止)。

## 0. 3 票の関係

| 票 | 範囲 | 依存 | 触るファイル |
|---|---|---|---|
| **S0-K** | KAPI **v50** (7 本、末尾追記)、`shm_write_row` の境界検査、exec 回収順序 (DB → FD) | — | `sdk/kapi.json` + 生成物、`kapi/kapi_db.c/.h`、`exec/exec.c` (回収順の 1 か所)、`kernel/kselftest.c`、`userland/tests/db_v50_test.c` (ゲスト試験)、`docs/KAPI_SPEC.md`、`tools/tests/` (deploy.yaml / sdk.mk は PM) |
| **S0-D** | 通常配備の settings 保護 (D0) | — | `tools/nhd_deploy.py`、`tools/hostdrv_deploy.py`、`tools/prune_stale.py`、新規 `tools/deploy_protect.py`、**`userland/system/hsync.c`** (ゲスト側)、`tools/tests/` |
| **S0-T** | 初期値の正典 tsv、生成ツール、ビルド統合、インストーラ添付 | — | `assets/settings/defaults.tsv`、`tools/mk_settings_db.py`、**`tools/mkpkg.py`** (欠損をエラーに)、`build/assets.mk`、`build/image.mk`、`build/core_packages.yaml`、`tools/tests/` (deploy.yaml は PM) |

3 票のコード・試験ファイルは重ならない。**共有ファイルは PM が着地時に直列に編集する**: `userland/deploy.yaml` (S0-K の試験プログラム登録、S0-T の tsv 登録)、`build/sdk.mk` (3 票の `make check` 登録)、`tools/emu_agent/agent.py` (許可ターゲット)。コーダーは登録すべき行を報告するだけで、これらを編集しない。S2 (`libos32cfg` + `cfg`) は S0-K の v50 と S0-T の tsv に依存する。

## 1. S0-K — KAPI v50 と回収順序

### 1a. ABI (kapi.json の `api` 末尾に **この順で** 追記。現行末尾 = slot 200 `sys_yield` (0x328)。data_fields は 0x348 / 0x34C へ移る)

| slot | offset | name | args | ret | 規則 |
|---|---|---|---|---|---|
| 201 | 0x32C | `db_open_existing` | `const char *path, int writable` | handle ≥ 0 / -1 | `writable` 0 = `SQLITE_OPEN_READONLY`、1 = `SQLITE_OPEN_READWRITE`、他は拒否。**CREATE / URI を付けない** (`sqlite3_open_v2`、vfs は既定の `os32`)。空 path、`:memory:`、`file:` 接頭、`OS32_MAX_PATH` 超は拒否。RO では変更 PRAGMA を実行しない。RW は `journal_mode=DELETE` を確認し、不成立なら close して失敗。**open の前**に (1) `vfs_stat` で本体が存在し size > 0 であること、(2) `<path>-journal` が存在**しない**ことを検査し、どちらかに反すれば **SQLite を呼ばず**に失敗する (副作用ゼロ: SQLite の `hasHotJournal` は RO でも 0 ページの DB に付随する journal を削除するため、往復 2 の 1)。診断: 欠損 = `SQLITE_CANTOPEN`、0 バイト = `SQLITE_NOTADB`、journal あり = `SQLITE_BUSY_RECOVERY` (RO / RW とも。回復は S3 の明示操作)。失敗の拡張コードは **owner 別の「直前 open 失敗」欄**に保存 (`db_error_code(-1)` で読める) |
| 202 | 0x330 | `db_prepare_only` | `int handle, const char *sql` | 0 / -1 | SQL は NUL 込み 1024B 以内 (超過は**切り捨てず拒否**)。単一の非空 statement のみ (末尾の空白 / コメントは可、次の statement があれば拒否 = `sqlite3_prepare_v2` の `pzTail` を検査)。**step しない**。同じ handle の旧 stmt は finalize して置換 |
| 203 | 0x334 | `db_bind_int` | `int handle, int index, int value` | 0 / -1 | prepare 後・最初の step 前だけ。index は 1-based、範囲外は拒否 |
| 204 | 0x338 | `db_bind_text` | `int handle, int index, const char *text, int length` | 0 / -1 | `length` 0〜255、負・超過は拒否。カーネルへ**検証付きコピー**してから `SQLITE_TRANSIENT` で bind。0B でも非 NULL の空値。NULL ポインタは拒否 (NULL は `db_bind_null`) |
| 205 | 0x33C | `db_bind_blob` | `int handle, int index, const void *data, int length` | 0 / -1 | `length` 0〜4096。同上 |
| 206 | 0x340 | `db_bind_null` | `int handle, int index` | 0 / -1 | |
| 207 | 0x344 | `db_error_code` | `int handle` | code | slot の **「最後の失敗」コード** (SQLite 拡張 result code) を返す。**データ操作** (open_existing / prepare_only / bind_* / step / exec) は成功で 0 に、失敗でそのコードに更新する。**finalize / close は成功しても更新しない** (失敗したときだけ自分のコードで更新) — 「後片付けが原因診断を消さない」(FOUNDATION §4、往復 3 の 2)。close 後も slot が再利用されるまで `db_error_code(handle)` はその値を返す (再利用後は新しい接続の値になる — handle に世代が無いので旧利用者を区別しない、FOUNDATION の「世代付き ABI は導入しない」と整合)。引数不正は `SQLITE_MISUSE`。`handle = -1` は呼び手 owner の直前 open 失敗診断。取得しても消えない。F1 の隔離 (close 失敗 slot の再利用禁止) はこの欄とは別に維持。既存 step / exec の自動 finalize より前に拡張コードを保存する |

- `db_error_code` は **診断専用の lookup** を使う (`slot_get` は解放済み / 隔離 slot を拒否するので使わない)。閉じた handle は slot 再利用まで最後の失敗を返し、再利用後は新接続の値、範囲外 / 一度も開いていない slot は `SQLITE_MISUSE`。
- 新 handle には既存の `db_step` (ROW=1 / DONE=0 / 失敗=-1、DONE で自動 finalize) / `db_finalize` / `db_close` をそのまま使う。反復は再 prepare (reset API は足さない)。
- ポインタ引数の検証 (往復 1 の B1、往復 2 の 2〜4): 既存の `argptr` マスク + `ring3_ptr_ok` は**先頭番地だけ**を見る (先頭が許可帯外なら wrap に入る前に kill — これは既存挙動で変えない)。v50 の wrap は先頭が通った後に自前で範囲を検査する: `kapi/kapi_db.c` の `db_user_range_ok(const void *p, u32 len)` = `p != NULL`、`p + len` が overflow しない、**呼び手が CPL=3 由来 (`ring3_in_syscall`) のときだけ** `[p, p+len)` の全ページが `ring3_ptr_ok` の許可帯 **かつ呼び手のアドレス空間 (syscall 中も CR3 はアプリの PD のまま。**PTE は見ない** — 2026-09-13 の実機 K2 で 2 通りの実装が同じ誤判定をした。PD もアプリ PT も pgalloc から出て `PGALLOC_BASE` はアプリ帯そのものなので、syscall 中 (CR3 = アプリ PD) に物理 = 仮想で表を辿ると per-app 物理へ張り替わったアプリ自身のデータを読む。許可帯の中の非 present ページは**既存のフォールトガードが呼び手を kill する** 扱いにする、往復 3 の 1) で present + USER** (許可帯内でも guard / 未マップ (sbrk 上限〜guard) は非 present なので、ここで弾かないとカーネルのコピーで #PF → 呼び手が kill される)。CPL=0 の呼び手 (常駐シェル / gshell = シェル帯 0x300000、ディスパッチャを通らない直呼び) は帯検査を**しない** (NULL / 長さ / 容量の検査だけ)。`path` / `sql` は上限バイトまでの範囲内で NUL を探す (NUL が見つかるまでの各ページを同じ規則で検査、上限内に NUL が無ければ拒否)。検証後にカーネル側の **1 本の静的スクラッチ** (sql 1024B、path 256B (NUL 込み)、text 256B (255 + NUL)、blob 4096B) へコピーし、bind は `SQLITE_TRANSIENT` (SQLite が写す) — 次の呼び出しで上書きされてよい。SQLite に呼び手のポインタを保持させない。受入 K2 の不正ケースは**先頭が許可帯内**のものに限る: 許可帯末尾 -1 から 2B、負の `length`、NUL 無しの path — いずれも -1 が返り試験プログラムは落ちない。**guard / 未マップページをまたぐ範囲は -1 ではなく kill** (2026-09-13 の改定) なので K2 からは外した。`p + length` の overflow は許可帯と 4096B では構成できないので**ホスト側の helper 試験**で検査する。先頭が許可帯外 (`0xFFFFFFFE` 等) は既存どおり kill なので K2 に含めない。
- 既存 10 本の順序・型・挙動 (prepare が先頭行まで進む、exec が先頭 statement だけ) は**不変**。
- 版: `KAPI_VERSION` 49 → **50**。`docs/KAPI_SPEC.md` §3-2 に v50 の行、関数表を更新。`build/app.conf` の要求版は S2 で上げる (S0-K では上げない)。

### 1b. `shm_write_row` の境界検査 (F5 の前倒し)

- header + 全列 descriptor + payload の合計が 16KB を超えるとき、**範囲外書き込みも部分 ROW も返さず**失敗 (`-1`、`db_error_code` に `SQLITE_TOOBIG` 相当を保持)。既存の SHM レイアウト・型番号は不変。列数・列幅の上限は現行の定数を管理元 ([C4]) から引く。

### 1c. exec 回収順序 (F2 の順序修正のみ)

- `exec_reclaim_owned(id)` で **`db_cleanup_owned(id)` を `vfs_close_owned(id)` より先**に呼ぶ (SQLite が rollback / close で journal を書ける間に FD が生きている)。`shm_free_owned` / `con_sink_owner_exit` / `launch_owner_exit` の相対順は変えない (先頭に DB を持ってくるだけ)。
- FD lease の完全統合 (F2c/F2d) は後回し。**close 失敗 slot の再利用禁止 (F1) は維持**。

### 1d. ホスト TDD (tools/tests、`make check` に登録)

- 実 SQLite + 実 `os32_sqlite_vfs.c` + RAM backend (既存 `sqlite_groups_host.c` / `vfs_fd_sqlite_host.c` の作法) で: RO で missing が**作られない**、RO で書き込み系 SQL が失敗、RW+no-create で missing が失敗、`journal_mode` 不成立で失敗、prepare-only が step しない (SELECT の先頭行が進まない / DML が実行されない)、複数 statement 拒否、1024B 超拒否、bind の 1-based / 範囲外 / 0B / NULL / 4096B roundtrip / 4097B 拒否、text 256B 拒否、`db_error_code` の保持 (close で上書きしない、-1 の open 失敗)、SHM 境界超過で範囲外書き込み 0、回収順序 (mock で DB cleanup が FD close より先、rollback 時に FD 生存)。
- kselftest に 1〜2 項 (v50 の表の件数、境界検査)。
- `check_kapi_version.py` / `check_constraints.py` / i386-elf-gcc 単体コンパイル。

## 2. S0-D — 通常配備の settings 保護 (D0)

保護対象名 (`PROTECTED_BASENAMES`、大文字小文字を**区別しない**): `settings.db`、`settings.db-journal`、`settings.db-wal`、`settings.db-shm`、`settings.db.bak`。保護対象ディレクトリ: ゲストの `/etc`。

- 新規 `tools/deploy_protect.py`:
  - `resolve_dest(root, guest_path, host_src) -> str`: **実際の最終コピー先**を確定する (ディレクトリ指定 `guest: /etc/` は basename を補う、`..` / 連続 `/` / `./` を畳む = ゲストの正規化名)。root (マウントした NHD のツリー / HostDrv のルート) の外へ出る結果は例外 (拒否)。
  - `is_protected(root, dest) -> bool` は **2 段** (往復 2 の 6): (1) **名前規則 — realpath の前に**、正規化したゲスト名の親が `/etc` で basename が保護対象名 (大文字小文字無視) なら真 (dangling symlink や `etc → conf` の別名で実体が欠損していても守る = 「欠損は欠損のまま」)。(2) **実体規則** — `dest` の各要素を OS の `realpath` で解決し、root 内でなければ拒否、解決先の `st_dev/st_ino` が `<root>/etc/settings.db*` の**存在する**いずれかと一致すれば真 (hardlink / symlink の別名対策)。`stat` の失敗は **ENOENT (確定した不存在) だけ「保護対象ではない」**とし (新規の通常ファイル、例えば初回の `/etc/settings.tsv` を配備できる)、それ以外 (EACCES / EIO / ELOOP / 解決失敗) は保護側 = 配備を失敗させる (往復 3 の 3)。`<root>/etc` 自体が symlink または別マウント (realpath が字句と異なる) なら**配備全体を拒否** (bind mount の別名はツールで検出できないので運用で禁止し、この検査で最低限止める)。
  - 判定は「作成 / 切り詰め / 削除 / rename の**直前**」に、確定した最終パスで行う。HostDrv の内容比較 (同一なら copy しない) より**前**に判定し、保護対象は比較もせずに `protected:` を出す。
- 適用点 (**すべて**、B3、往復 3 の 4): `tools/nhd_deploy.py` の `do_sync` (cp、:670)、`do_sync_from_hostdrv` (:748、**そのファイルコピー前の mkdir (:733) にも**: 保護対象名のディレクトリ (`etc/settings.db/` の残骸) は作らない)、`do_copy` / `do_copy_all` (:254)、`do_rm` (:310)、`remove_partial` (:585); `tools/hostdrv_deploy.py` の `do_sync` (copy2 と mkdir)、`do_clean` (:218〜223 — `rmtree(root/etc)` をやめ、**エントリごとに削除して保護対象とその祖先ディレクトリは残す**); `tools/prune_stale.py` の `prune_hostdrv` (:107) と `prune_nhd` (:124)。`hsync` のディレクトリ作成 (`hsync.c:168`) も同じ名前規則で保護対象名のディレクトリを作らない。除外は明示ログ `protected: <path> (skipped)`。`nhd_deploy.py copy` / `copy-all` / `rm` の CLI からも保護対象は書けない (ホストのツールで settings.db を書く道を**作らない**)。
- **ゲスト側の `hsync`** (`userland/system/hsync.c`、HostDrv → NHD の再帰コピー、`O_CREAT|O_TRUNC`、往復 2 の 7): コピー先の文字列を**ゲスト内で字句正規化** (`./` `..` 連続 `/`、`-f` の subdir 連結 `hsync.c:254` を含む) してから名前規則 (親 `/etc` + 保護対象名、大文字小文字無視) で判定し、さらに `/etc/settings.db*` が存在すれば `sys_stat` の inode と比較して同一実体 (NHD 上の hardlink) も**コピーしない** (ログに skipped)。HostDrv に古い `etc/settings.db` が残っていても本 DB を切り詰めない。この 1 ファイルは S0-D の C 側の範囲。
- **NHD イメージ全体の配備** (B4、往復 2 の 8): `do_deploy` は local → remote の全体コピーなので、ファイル単位の除外では稼働後の設定 (remote) が古い local で戻る。→ **stamp** (`<local>.pulled`、JSON): `pull` (明示 / `ensure_local_nhd` の自動 pull とも、**コピー成功後にだけ**書く。失敗した pull は stamp を消す) が `remote_path`、`remote_size`、`remote_mtime`、**`remote_sha256`** (pull 時の remote の内容 hash)、`local_path` を記録する。`deploy` は (1) `OS32_NHD_LOCAL` が stamp の `local_path` と一致、(2) remote の path / size が一致、(3) remote を**再ハッシュ**して `remote_sha256` と一致 (mtime を保った別内容の置換も検出) のすべてを満たすときだけ全体を書き、どれかが崩れれば失敗 (`--force` でだけ通す)。local 側は deploy 自身が sync で書き換えるので local の hash は記録しない。
- **失敗の伝播** (B5、往復 2 の 9): `do_sync` / `do_sync_from_hostdrv` の `sync` 戻り値 (:687 / :763)、`do_copy` / `do_copy_all` の cp 失敗 (:257、continue して成功を返している)、`do_rm` の rm / sync 失敗、`do_umount` の sync 失敗 (:178)、`prune_stale.py` の rm / sync 失敗 (:124) を**すべて**非ゼロにし、`main` (:905) が各サブコマンドの戻り値を終了コードにする。後続 (NHD deploy) へ進めない。保護対象の**除外**は失敗ではない。
- 通常 deploy はDB や journal を作成・上書き・削除・rename しない。`NO_PRUNE` に依存しない。`/etc` を prune 対象に広げない。`do_init` / `do_format` は明示の全消去操作として通常更新から分離し、本票では触らない (呼ぶ側の承認 [D2] に委ねる)。`do_clean` (HostDrv) は上の適用点どおり**保護対象を残す**。
- 例外: 明示 install (FDD / CD の `install.bin` / `cdinst.bin` がゲスト内で行うコピー) と `cfg init` (S2、ゲスト内) はツールの対象外。
- ホスト TDD (B12): temp dir + mock で (1) **保護対象の source を意図的に置く** fixture (HostDrv / manifest に `etc/settings.db` と `-journal` を置き、宛先に別内容の DB がある / 無い) で、前後の存在一覧と hash が不変、`protected:` が出る; (2) tsv だけの通常配備では `protected:` が出ず tsv が更新される; (3) `guest: /etc/` のディレクトリ指定、symlink / hardlink の別名、大文字名、`..`、sync-from-hostdrv、copy / copy-all / rm CLI、prune の各経路; (4) 失敗注入 (cp / sync / rm) で非ゼロ、保護対象への write 系呼び出し 0; (5) stamp: local あり + stamp 無し / `local_path` 不一致 / remote hash 不一致で deploy が失敗、`--force` で通る; local 無し → 自動 pull が stamp を書いて deploy が通る; pull 失敗で stamp が残らない。`sudo` / `mount` / 実配備は遮断。`make check` に登録 (PM)。

## 3. S0-T — 初期値の正典 (tsv)、生成ツール、ビルド統合、インストーラ添付

決裁との整合 (B6): **生成 DB は媒体 (FDD / CD) だけが持つ**。既存システムへの seed は生成 DB ではなく **tsv を通常配備** (`assets/settings/defaults.tsv` → `/etc/settings.tsv`、通常配備で上書きされてよい) し、`cfg init` (S2) が `/etc/settings.db` が**無いときだけ** tsv から生成する (明示コマンド、自動生成しない)。`libos32cfg` は tsv の最小 reader を持つ (ホストの生成ツールと同じ規則)。

- `assets/settings/defaults.tsv` (B7): UTF-8、行 = `scope\tkey\ttype\tvalue` の**厳密 4 列** (タブで split、行全体を strip しない — 末尾の空欄 = 空 text を保つ)、`#` 始まりと空行はコメント。規則: `scope` は `system` / `gshell` / `user` / `app:<name>` (63B 以内)、`key` は `[a-z0-9_]+(/[a-z0-9_]+)*` (63B 以内)、`type` は `int` / `text` / `blob`、`int` は 10 進で **int32 の範囲** (`-2147483648`〜`2147483647`、超過は拒否)、`text` は UTF-8 妥当・埋め込み NUL 無し・255B 以内、`blob` は hex (偶数桁、空白なし、大文字小文字可、4096B = 8192 文字以内; C 側 reader (S2) はこの行長を扱う)。`scope` にも埋め込み NUL 無し、`app:<name>` の name は `[a-z0-9_]+`。改行は LF (CR は拒否)、`int` の字句は `-?[0-9]+`。重複 (scope, key) は拒否。初版の中身: `gshell\tdesktop/color\tint\t1`、`gshell\tdesktop/wallpaper\ttext\t` (空)、`gshell\ttaskbar/clock_24h\tint\t1`。
- `tools/mk_settings_db.py --tsv defaults.tsv --out out.db [--epoch N]` (B8): Python `sqlite3` で DESIGN §3 のスキーマ、`schema_version = 1`、`page_size = 1024`、`journal_mode = DELETE`、`user_version = 1`。**決定性は内容だけで決める**: `meta.created` は `--epoch` (省略時は `SOURCE_DATE_EPOCH`、それも無ければ **0**) の値。mtime は使わない。同じ tsv + 同じ epoch → 同じバイト列 (試験は「mtime の違う同内容の 2 ファイル」で確認)。上の規則の違反は非ゼロ終了。
- ビルド統合 (B9): `build/assets.mk` の `$(BUILD_OUT)/settings.db` は **`FORCE` 依存** (ビルド毎に必ず生成、ユーザー決裁)。`all` / `assets-all` に加え、**`images/os32_boot.d88` と `packages` (`build/image.mk`) から直接依存**させる。`packages` の依存は現行 `programs` だけなので、core パッケージが要求する既存入力 (`$(BUILD_OUT)/vmkernel.lz4`、`unicode_bin`、boot 成果物) も **`packages` の依存に結ぶ** (clean 後の単独 `make iso` / `make -j` でも欠損エラーにならない、往復 3 の 5)。`clean-assets` / `clean` で消す。`tools/mkpkg.py` は**すべての**登録ファイルの欠損を warning で飛ばさずエラー (非ゼロ) にする (属性の追加ではなく一律。現行の簡易 parser を変えない)。`make -j` の競合は依存で解決する (生成ターゲット 1 本に集約)。
- インストーラ添付: `build/image.mk` の FDD イメージに `/etc/settings.db=$(BUILD_OUT)/settings.db`、CD は `build/core_packages.yaml` の core パッケージに `/etc/settings.db`。
- **既知の前提不整合** (B10): 現行の `install.bin` は `/kernel.bin` を必須とし (`userland/system/install.c:330`)、FDD イメージは `/VMKRNL.LZ4` を収録するので、**FDD からの新規インストール自体が今は `/etc` コピーまで到達しない**。これは本票より前からの不整合で、修正 (install を lz4 カーネル + `/boot` レイアウトに合わせる) は **S3 (リカバリ / インストーラ更新) の票**で扱う。したがって本票の T1 は「媒体に入っている」までで、「新規インストールで seed される」は S3 の受入に送る (票に明記、成功と言わない)。
- `userland/deploy.yaml` に `assets/settings/defaults.tsv → /etc/settings.tsv` (tags core) を PM が登録 (YAML は `$(BUILD_OUT)` を展開しないので、DB ではなく tsv を載せる)。
- ホスト TDD: tsv → db の決定性 (mtime が違う同内容 2 ファイル + 同じ epoch で同一 hash、epoch 違いで異なる)、スキーマ / `user_version` / `page_size`、int32 超過・不正 UTF-8・NUL・key 規則違反・重複・列数違反の拒否、末尾空欄の text が空で入る、`sqlite3` で読み戻して行数一致、mkpkg の欠損エラー。`make check` に登録 (PM)。

## 4. 受入 (ゲスト、S0 の段階では最小)

| ID | 試験 | 合格条件 |
|---|---|---|
| K1 | `make clean` + `make clean-external` → `all` → `external` → `check`、配備 (停止 → pull → バックアップ → deploy-nhd → `make deploy`)、kselftest | `ver` API v50、kselftest +N / 0、regress 6 本 (`tools/emu_agent/tasks/regress.txt` の 6 件) |
| K2 | CPL=3 の小さな試験プログラム (`userland/tests/db_v50_test.c`、S0-K に含める。deploy.yaml 登録は PM) | RO で missing を開いても `/etc/nosuch.db` が**作られない**、prepare-only + bind + step で 1 行取れる、4096B blob roundtrip、`db_error_code(-1)` に open 失敗のコードが残る、**不正範囲** (許可帯末尾 -1 から 2B、負の length、overflow、NUL 無し path) が拒否され試験プログラムが落ちない |
| D1 | (a) HostDrv に**意図的に** `etc/settings.db` (別内容) を置き `make deploy` → ゲストで `hsync -f etc` (同サイズでも上書き経路を踏む) → NP21/W 停止 → `pull` → `make deploy-nhd` の順 | ログに `protected:` が出て、ゲストの `/etc/settings.db` の有無と内容 (hash) が前後で不変; (b) tsv だけの通常配備で `protected:` が出ず `/etc/settings.tsv` が更新される; (c1) local あり + stamp を消して `deploy-nhd` → 失敗; (c2) local を消して `deploy-nhd` → 自動 pull が stamp を書いて成功 |
| T1 | FDD / CD の媒体 | `images/os32_boot.d88` と core パッケージに `/etc/settings.db` が入る (mount / mkpkg の一覧で確認)、`sqlite3` で読める、2 回ビルドして同一 hash。**新規インストールでの seed は S3 の受入** (B10) |

## 5. レビューで見てほしい点

1. §1a の 7 本が S0_FOUNDATION §4 の契約と 1 対 1 か (RO/no-create、prepare-only、bind の上限、error_code の保持規則)。
2. §1c の順序変更で FEP (`kernel/ime_dict.c` の直接接続、protect 付き FD) や既存の `db_cleanup_all` 経路が壊れないか。
3. §2 の保護がすべてのコピー経路 (manifest / glob / tag / sync-from-hostdrv / prune) を覆うか、迂回 (`..`、別名、HostDrv 残骸) が残らないか。
4. §3 の方式 (生成 DB は媒体だけ、既存システムには tsv を通常配備し `cfg init` が明示生成) が決裁と DESIGN に一致するか。
5. 共有ファイル (deploy.yaml / sdk.mk / agent.py) を PM が直列編集する前提で、3 票のコード側が並走できるか。
6. 往復 2 の 10 件の反映: RO/RW open 前の stat + journal 検査 (副作用ゼロ)、present+USER の PTE 検査と CPL=0 の除外、K2 の範囲、診断は直前操作、名前規則を realpath の前に、hsync の正規化と inode、stamp の来歴 (remote hash + local_path)、失敗伝播の全経路、D1 の fixture 分離。

## 6. Codex 設計レビューの記録 (2026-09-13、`codex exec -s read-only`、網羅指示)

| 往復 | 判定 | blocker |
|---|---|---|
| 1 (`8597957`) | Request changes | 12 件: 範囲検証、D0 の最終パス / 別名 / 未保護経路 / 全体配備 / 失敗伝播、seed DB の通常配備が決裁と不一致、tsv 規則、決定性、Make 依存、FDD インストーラの既存不整合、共有ファイル、D1 fixture |
| 2 (`0212b9a`) | Request changes | 10 件: RO でも 0 ページ DB の journal を消す、許可帯内の非 present ページ、先頭が帯外は kill、CPL=0 の呼び手、診断の意味、欠損保護は名前規則を先に、hsync の正規化、stamp の来歴、CLI の失敗伝播、D1 と自動 pull |
| 3 (`cffa137`) | Request changes | 5 件: PTE 検査は呼び手の PD、finalize / close は診断を消さない、stat の ENOENT は不存在、mkdir と rmtree、packages の既存入力依存 |
| 4 版 | 決裁: 実装へ | 5 件を反映。ユーザー決裁 (2026-09-13) で設計の 4 往復目は回さず、K / D / T を同時発注 |

## 8. 実装メモ (D、2026-09-13)

- 新規 `tools/deploy_protect.py` = 2 段判定 (名前規則を realpath の前に → 実体規則の `st_dev`/`st_ino`)、`resolve_dest` / `is_protected` / `check_root_etc` / `protect_log` / `check_dest`。`stat` の失敗は **ENOENT だけ不存在**、他は `ProtectError` で配備全体を失敗させる。
- 適用点は票 §2 の列挙どおり全部: `nhd_deploy` の `do_sync` (mkdir 含む) / `do_sync_from_hostdrv` (mkdir 含む) / `do_mkdirs` / `do_copy` / `do_copy_all` / `do_rm` / `remove_partial`、`hostdrv_deploy` の `do_sync` (copy2 と mkdir、内容比較より前) / `do_clean` (rmtree をやめエントリごとに削除)、`prune_stale` の `prune_hostdrv` / `prune_nhd`。除外は `protected: <path> (skipped)`。
- stamp = `<local>.pulled` (JSON: remote_path / size / mtime / **sha256** / local_path)。`pull` と `ensure_local_nhd` の自動 pull が**コピー成功後にだけ**書き、失敗した pull は消す。`deploy` は 3 条件 (local_path 一致 / remote の path・size 一致 / remote 再ハッシュ一致) が揃うときだけ全体を書き、`--force` でだけ通す。**書いた直後に来歴を取り直す** (remote==local になるため。ゲストが走れば hash が変わるので検出目的は保たれる)。
- 失敗伝播: `run_sync()` を通した `sync`、`cp` / `rm` の戻り値、`do_umount` の sync をすべて非ゼロにし、`main` は各サブコマンドの戻り値を終了コードにする (`sys.exit(0 if main() is not False else 1)`)。保護対象の除外は失敗ではない。
- ゲスト側は `userland/system/hsync_protect.inc` (純関数: 字句正規化 + 名前規則) を `hsync.c` が include し、`dst_protected()` が名前規則 → `/etc/settings.db*` との inode 比較の順で見る。ディレクトリ作成経路と `-f` の subdir 連結も同じ判定を通る。併せて `dst_dir` が `""` のときに `dst_path[-1]` を読んでいた既存の境界バグを直した。
- ホスト TDD: `tools/tests/test_deploy_protect.py` (53 件、RED 35 失敗 → GREEN 全通過)、`tools/tests/test_hsync_protect.py` + `hsync_protect_host.c` (28 checks)。記録は `tools/tests/s0_tdd.md` 節 D。配備・エミュレータ・`make` は未実行、D1 受入は未了 ([V4])。

### 8a. 実装レビュー 往復 1 の修正 (D、2026-09-13)

- **最終パスの確定** (B1 / B2 / B9): `resolve_dest` が**実ディレクトリを見て** basename を補い (`cp` / `copy2` は宛先が既存ディレクトリなら中へ書く)、`cp` にはファイルパスだけを渡す。`mkdir -p` / `makedirs` は新設 `mkdir_chain` が root から 1 段ずつ判定して作る (保護対象名の祖先は `ProtectedPath`、除外なので CLI は成功 = B7)。`/bin/..` は `/` として正当に扱う。
- **前提検査と削除経路** (B3 / B4 / B6): `check_root_etc` を `is_protected` の**入口で必ず 1 回**通し、`<root>/etc` が symlink / 別マウント / 通常ファイルなら配備全体を拒否 (clean も)。`_clean_tree` は top-down で「判定 → symlink → ディレクトリ → ファイル」の順に見て保護ディレクトリへ降りない (symlink 自体の削除は `is_protected_symlink`)。`mkdir` の rc、`os.walk(onerror=)`、`rmdir` の失敗をすべて非ゼロにし、prune の件数は実際に消した数にした。
- **来歴と hsync** (B8 / B5): `pull` / `ensure_local_nhd` は**入口で** stamp を消し、`do_mount()` まで全部成功した最後にだけ書く。`hsync` は `/etc` を `sys_ls` で列挙し、大文字小文字を無視して一致する実在名を全部 stat する (`/etc/SETTINGS.DB` + hardlink の取りこぼしを塞ぐ)。`OS32_ERR_NOTFOUND` 以外の stat 失敗は同期を中止する。試験は `tools/tests/s0_tdd.md` §D.6 (86 件、RED 29 失敗 → GREEN 全通過)。

### 8b. 実装レビュー 往復 2 の修正 (D、2026-09-13)

- **最終パスと祖先** (1 / 2): `resolve_dest` の basename 補完は**1 回まで**で、補完後がまだディレクトリなら `ProtectError` (`<root>/bin/settings.db -> ../etc` のような二重補完を塞ぐ)。新設 `protected_ancestor` を `check_dest` が **stat より先に**当て、保護祖先を含む宛先は「書かずに成功除外」。ゲスト側も `hsp_ancestor_protected` で同じ規則 (`/etc/settings.db/sub`)。
- **入口・走査・来歴** (6〜9): 各サブコマンドの入口で `check_root_etc` を 1 回 (対象 0 件の `sync --tag` / `prune --delete` でも止まる)。`os.walk` は親の `dirnames` から保護対象を in-place で外して降りない (読めない `etc/settings.db/` で CLI が落ちない)。stamp は tmp → fsync → rename の原子的書き込みで、失敗すれば tmp も既存も消して非ゼロ。`.pulled` が非オブジェクトなら「壊れている」を返して `--force` に届かせる。
- **hsync** (3 / 4 / 5): 保護対象一覧が `HS_MAX_PROT` (16) を越えたら同期を拒否。`sys_read` の負値 / `sys_ls` / `sys_mkdir` / `sys_stat` / `vfs_sync` の失敗と、`MAX_FILES` / `MAX_DEPTH` の打ち切りを全部エラーに数え、`main` を `int` にして終了コードへ載せた。無検査の `str_cpy` / `str_cat` を撤去し、連結は容量付き (`str_ncpy` / `str_ncat`) だけ — 溢れたら判定より前に「path too long」で失敗。試験は `tools/tests/s0_tdd.md` §D.7 (103 件、RED 12 失敗 → GREEN 全通過)。

### 8c. 実装レビュー 往復 3 の修正 (D、2026-09-13、symlink 拒否は**ユーザー決裁済み**)

- **方針変更**: symlink の迷路を 1 件ずつ塞ぐのをやめ、新設 `check_tree(root)` が配備ツリー (HostDrv ルート / マウントした NHD ツリー) を `os.walk(followlinks=False)` で走査し、**symlink が 1 つでもあれば配備全体を拒否**する (ただし**保護対象名のディレクトリの内部**と**ルート直下の `lost+found`** には入らない — どちらも配備が 1 バイトも書かない場所、§8d / §8f)。各サブコマンドの入口 (`guard_root` / prune / clean) で 1 回だけ通す。`is_protected_symlink` (symlink の削除許可) は撤回し、`_clean_tree` は symlink を見たら中止。これで D1 (解決後の祖先) と D2 (中間 symlink の削除) が到達不能になる — **symlink 無しで成立する変種は無い** (symlink が無ければ realpath == 字句パスなので `protected_ancestor` が既に覆い、ディレクトリへの hardlink は作れない)。念のため prune の削除判定にも `protected_ancestor` を足した。
- **個別** (D3〜D5): `cp` / `rm` / `mkdir` / `ls` の operand は必ず `--` の後に置き、source は `os.path.abspath` で絶対化 (`--target-directory=…` という名前のファイルをオプションに解釈させない)。補完後の宛先が**保護対象名のディレクトリ**なら再補完せず「成功除外」(保護対象でないディレクトリは従来どおり拒否)。prune の候補収集は `os.lstat` で行い、ENOENT 以外の `OSError` は非ゼロ (`isdir` / `isfile` が EACCES を False に丸めて「0 件で掃除済み」になっていた)。
- **hsync** (D6): 名前が `NAME_CAP` (64) に収まらない項目は**切り詰めずに取り込まない**で件数を数え、`sync_directory` がエラーにする (別名のファイルを作って成功と出ていた)。判定は純関数 `hsp_name_fits` に切り出してホスト試験に載せた。静的配列の幅は変えていない。試験は `tools/tests/s0_tdd.md` §D.8 (121 件、RED 18 失敗 → GREEN 全通過)。**hsync 本体の回帰証拠は純関数までである** ([V4])。

### 8d. 追加往復の修正 (D、2026-09-13)

- 補完後の宛先がディレクトリのとき、**`protected_ancestor` を名前規則より先に**見て、どちらかで守られていれば成功除外にする (`<root>/etc/settings.db/settings.db/` のように親が `/etc` でない形でも守られている)。`do_clean` は root を `os.lstat` で見て ENOENT だけ「無い = 成功」、他の `OSError` と非ディレクトリは失敗 (`os.path.isdir` が親の EACCES を False に丸めて「存在しません」で成功していた)。
- 進捗の件数と全体の成否を分けた: `do_copy` は失敗があれば `FAILED (n copied, m failed)` で非ゼロ、`do_pull` の「完了!」は mount と来歴まで通った最後にだけ出す。non-blocker で `sync-from-hostdrv` は source の HostDrv ツリーも `check_tree` に通し、`check_tree` は保護対象名のディレクトリに降りない (配備が中へ書かない場所の読めない残骸で全体を止めない)。試験は `tools/tests/s0_tdd.md` §D.9 (134 件、RED 8 失敗 → GREEN 全通過)。

### 8e. 最終往復の修正 (D、2026-09-13)

- `hsync` の最終行を `g_errors` で分け、失敗があれば `FAILED: n copied, ...` (赤) を出す (件数は不変)。ラベルは純関数 `hsp_final_label(errors)` に切り出し、`hsync_protect_host.c` で 2 値を固定した。終了コードは従来どおり `g_errors ? 1 : 0`。

### 8f. 実配備 1 回目の差し戻し (D、2026-09-13)

- `mkfs.ext2` が必ず作るルート直下の `lost+found` (root 所有 mode 700) で `check_tree` が「ツリーを辿れない」= 失敗になり、`make deploy-nhd` が止まった (配備ツールは非 root の Python で走査し、実コピーだけ sudo cp)。配備対象でも保護対象でもないので、新設 `protect.skip_root_entries` / `protect.walk_root` が **ルート直下の `lost+found` (実ディレクトリのときだけ)** を走査から外す。`check_tree` / `do_sync_from_hostdrv` / `_clean_tree` を同じ helper に通した。symlink や通常ファイルなら外さず従来どおり判定 (symlink は拒否)、**それ以外の読めないディレクトリは契約どおり失敗**。試験は `tools/tests/s0_tdd.md` §D.11 (142 件、RED 5 失敗 → GREEN 全通過)。

## 7. 実装メモ (K、2026-09-13)

- v50 の 7 本は `sdk/kapi.json` 末尾に slot 201〜207 (0x32C〜0x344)、data_fields は 0x348 / 0x34C へ移動。生成物は generator 出力のまま (手編集なし)。
- ポインタ検証は `exec/exec.c` に `ring3_user_range_ok` を新設し (`ring3_ptr_ok` を非 static 化、宣言は `exec.h`)、PTE は新設 `paging_current_pte_flags(virt)` で **いま CR3 に載っている表**を MMU と同じ順に辿る (当初は `AppSlot.as` の控えを引いていたが実配置とずれた — 実機 K2 で訂正)。`kapi_db.c` は `db_user_range_ok` から 1 本だけ呼ぶ (帯判定を二重に書かない)。
- 上限は `os32_kapi_shared.h` の `DB_SQL_MAX_BYTES` / `DB_BIND_TEXT_MAX` / `DB_BIND_BLOB_MAX` が管理元 ([C4])。スクラッチは sql 1024 / path 256 / text 256 / blob 4096 の静的 1 本ずつ。
- 「close 後は slot **再利用まで**最後の失敗を返す」を実装。**再利用後は新しい接続の値** (成功 open 直後なら 0) になる — handle に世代が無いので「再利用後 MISUSE」は判別できない。一度も開いていない slot と範囲外 handle は `SQLITE_MISUSE`。
- `exec_reclaim_owned` は DB を先頭へ (他の相対順は不変)。回収順の差はホストで観測済み: 後始末がバックエンドに届いた回数が 新 21 / 旧 2。戻り値はどちらも成功なので**回数で**見る (VFS の I/O 失敗握り潰しは票 F3a のまま)。
- ホスト TDD は `tools/tests/kapi_db_v50_host.c` + `test_kapi_db_v50.py` (9 件、実 SQLite + 実 VFS + RAM backend)。RED→GREEN は `tools/tests/s0_tdd.md`。既存 5 本回帰済み。
- **実装レビュー 往復 1 の blocker 6 件を修正** (2026-09-13): SHM の事前検査と writer の境界条件を一致 (ちょうど収まる TEXT/BLOB は書く)、stat は NOTFOUND だけを不存在とし他は `SQLITE_IOERR`、`PRAGMA journal_mode` の照会失敗 (拡張コード) と非 DELETE (`CANTOPEN`) を分離、stmt 無しの `db_step` の DONE でも診断を 0 に、`<path>-journal` が `VFS_MAX_PATH` に収まらない path は open 前に `CANTOPEN`、末尾の空白を SQLite のトークナイザと同じ 5 文字に。
- non-blocker も同時に: K2 の PTE ケースを「許可帯内の未マップページ (`sbrk_heap_limit`)」へ、`SQLITE_TRANSIENT` の試験をホストと K2 に、`PDE.PS` を `paging_current_pte_flags` で明示拒否、`s0_tdd.md` §K の模型と列数上限の記述を実態へ。
- 反例 7 ケースを `test_kapi_db_v50.py` に常設 (計 16 ケース)。6 件すべて実装を 1 件ずつ戻して RED を採取済み (`s0_tdd.md` §K 2b)。
- **実装レビュー 往復 2 の blocker 4 件を修正** (2026-09-13): 末尾判定を自前の表から **SQLite への再 prepare** に替え (`sql_tail_check`、`\v` / UTF-8 BOM / 未閉じ `/*` の 3 つが自前では写せない)、`db_prepare_only` は**入口で必ず**旧 stmt を finalize してから引数検証し、列値の**実体化の失敗** (accessor が NULL / `SQLITE_NOMEM`) を欠落 ROW にせず ERROR にし、path は先に `vfs_resolve_path` で**絶対名へ解決**してから容量検査 / stat / open を行う (cwd を勘定に入れる。相対名を SQLite に渡さない)。
- 反例 4 ケース (`sql_tail_sqlite` / `prepare_replaces` / `materialize_fail` / `resolve_len`) を常設し (計 19 ケース)、4 件すべて実装を戻して RED を採取 (`s0_tdd.md` §K 2c)。往復 1 の `sql_tail` は判定を SQLite に委ねたので `sql_tail_sqlite` に統合した。
- kselftest のビット 3 は「末尾判定」(接続が要るのでブート時に踏めない) から「journal 名が `VFS_MAX_PATH` に収まるか」へ差し替え。`MEMORY_BUDGET.md` の K の数値は **K1 の clean build 後に再測**が要る (注記済み)。
- **実装レビュー 往復 3 の blocker 1 件を修正** (2026-09-13): `vfs_resolve_path` は作業領域で **切り詰めてから** 正規化するので、溢れた入力は「短い別の絶対名」に化ける (cwd `/tmp` + `"./"×122 + "a/../b.db"` → `/tmp/b`)。resolve の **前** に `kstrlen(cwd) + 1 + kstrlen(path) + 1 <= VFS_MAX_PATH` (絶対名は cwd 抜き) を数えて `SQLITE_CANTOPEN` で断る。`fs/vfs.c` は変更していない。
- 反例 `resolve_truncate` を常設 (計 20 ケース、RED 採取済み)。ホストの `vfs_resolve_path` 模型を fs/vfs.c と同じ順序 (連結 → 切り詰め → 正規化) に書き直した。non-blocker も同時に: B3 の TEXT 側と「長さ 0 + NOMEM」の枝、`journal_mode` の「照会成功・非 DELETE」、`MEMORY_BUDGET` の static 計上、`KAPI_SPEC` 概要の版 / エントリ数、`s0_tdd` の「リンク失敗は RED ではない」。
- **最終往復の blocker 1 件を修正** (2026-09-13): `db_resolve_fits` はバイト長だけを見ていたが、`fs/vfs.c` は深さ 32 を超えた成分を捨て、その後ろの `..` が**保持済みの成分**を消す (`("/a"×32) + "/x/.." + ("/.."×31) + "/b.db"` → `/b.db`)。**成分数**の検査 (`db_path_depth`、空と `.` は数えず `..` は 1、相対名は cwd の分も加算) を足して超過は `SQLITE_CANTOPEN`。反例 `resolve_depth` を常設 (計 21 ケース、RED 採取済み)、resolver は変更なし。
- **実機 K2 の不合格 (2026-09-13)**: 印字された `open failure code = 21` (`SQLITE_MISUSE`) を消去法で追うと、RO open の時点で MISUSE を出せるのは **path のポインタ検証** (`kapi_db.c:881` → `db_user_str_copy` → `ring3_user_range_ok`) だけ。`vfs_stat` 由来 (CANTOPEN/IOERR/NOTADB/BUSY_RECOVERY)・`db_resolve_fits` (CANTOPEN)・`sqlite3_open_v2`・`db_journal_mode_check` (RW 専用)・owner 範囲外は、同じ試験の assertion 3 (`sys_stat(missing) != 0`) と owner の池 (0〜5 < 6) で除外できる。根拠は `s0_tdd.md` §K 2f。
- `paging_addrspace_pte_flags` は **シロ** (実 `kernel/paging.c` を使う `paging_bounds_host.c` に exec と同じ順序の項を足して確認)。**穴はホスト試験の側**で、それまで全ケースが `host_cpl3 = 0` で走っていたため検証経路が 1 度も踏まれていなかった → 新ケース `cpl3_paths` で塞いだ (計 22 ケース)。
- 次の 1 回で確定させる計器を入れた: `ring3_range_reject_count / _last / _addr / _page / _heap_top` (カーネルシンボル)、`kapi_db.c` の診断文を「ポインタ検証の拒否」と「引数の拒否」で分離、`db_v50_test.c` が失敗のたびに `db_error_code(-1)` と `db_last_error()`・土台の size・RO open の可否を出す。**根本原因の修正はまだ入っていない** ([V4])。
- **実機 K2 の原因と修正 (2026-09-13)**: `paging_addrspace_pte_flags` は PDE が指す PT を辿らず `struct addrspace` の**控え** (`app_pt_phys[]` / `page_tables[]`) から PT を選んでいた。これは exec の実配置とは別勘定で、外れた先が master の PT だとアプリ帯は `clear_app_band` + per-app 物理化の後なので present も USER も無い = 実機で見えた「.rodata が非 present」。**MMU と同じ辿り方をする `paging_current_pte_flags(virt)`** (CR3 → PDE → PS 拒否 → PDE が指す PT) に置き換え、`paging_addrspace_pte_flags` は削除、`ring3_user_range_ok` から `AppSlot.as` への依存も外した。
- ホスト試験は exec の実配置を写した fixture を `host_cr3` に載せて歩く形に訂正 (`paging_bounds_host.c`)。決め手は「PDE が指す PT だけ差し替えると答が変わる」項 — 控えから選ぶ実装はここで落ちる (RED 採取済み)。`db_v50_test.c` の `last_error=invalid handle` は、slot の状態を返す `db_last_error()` ではなく SHM の `error_offset` を読む `shm_error()` に直した。
- **実機 K2 の決着 (2026-09-13)**: CR3 から辿る版でも症状は同じだった。理由は「控えかどうか」ではなく **アプリの PD の下で表を歩いたこと**。カーネルはページテーブルを物理 = 仮想で読むが、PD もアプリ PT も pgalloc から出て `PGALLOC_BASE` は 0x400000 = `MEM_APP_BAND_BASE` (アプリ帯そのもの)。`clear_app_band` + per-app 物理化の後、アプリの PD ではその仮想番地が別物へ張り替わっているので、PT のつもりでアプリ自身のデータを読み、#PF も起きないまま「非 present」と答える (`fault_kill_count = 0` と整合)。
- **方針 B を採用**: PTE 検査を外し、範囲検査は NULL / overflow / **帯 (`ring3_ptr_ok`)** だけにした。帯の中の非 present ページは既存のフォールトガードが呼び手を kill する (kprintf の `%s` など他の KAPI と同じ)。**§1a と K2 の「guard をまたぐ範囲が -1」は「kill される」に改める** — K2 からはその項を外した (同じプロセスでは踏めない)。`paging_current_pte_flags` は削除し、`kernel/paging.h` に「走っているアプリの PD を syscall 中に歩いてはならない」を警告として残した。計器はそのまま (方針 B が効いていれば `ring3_range_reject_count` は 0)。
- **前回の訂正 ([V4])**: 「`paging_addrspace_pte_flags` はシロ」(往復 2f) は誤り、「控えから選んでいたのが原因」(2g) は半分だけ正しい。どちらもホスト模型が「物理 = 仮想」の罠を再現していなかったため。`paging_bounds_host.c` にその罠を固定する番人を置いた。
- **未実施**: `make` 全般・配備・エミュレータ・ゲスト試験 (K1 / K2)。`build/app.conf` / `userland/deploy.yaml` / `build/sdk.mk` は PM 登録待ち (登録行は報告に記載)。

## 9. 実装メモ (T、2026-09-13)

- `assets/settings/defaults.tsv` (3 行 + 書式コメント)、`tools/mk_settings_db.py`、試験 `tools/tests/test_mk_settings_db.py` (42 件)、記録 `tools/tests/s0_tdd.md` §T。
- 空行 / コメントの判定は **行頭だけ** (`line == ""` か `#` 始まり) で、行全体を strip しない。S2 の C 側 reader が同じ規則を素直に書けるようにした。
- `meta.created` は epoch を UTC の ISO 8601 (`1970-01-01T00:00:00Z`) にした文字列。決定性は「内容 + epoch」だけで決まり、mtime は読まない。挿入は (scope, key) 順に固定し、最後に `VACUUM` + `PRAGMA user_version=1` で自由ページを落とす。実物は 3KB / freelist 0。
- `build/assets.mk`: `SETTINGS_DB = $(BUILD_OUT)/settings.db` を FORCE 依存で生成。通常配備の対象ではないので `ASSETS_DEPLOYED` には入れず、`ASSETS_ALL` (→ `clean-assets` / `clean`) と `all:` への追加依存、媒体ターゲットから引く。`FORCE` は `build/programs.mk` の既存を使う。
- `build/image.mk`: FDD に `/etc/settings.db=`、`packages` の依存を `programs boot $(BUILD_OUT)/vmkernel.lz4 unicode_bin $(BUILD_OUT)/settings.db assets/fep.db` に。`assets/fep.db` は userland 層の NORMAL が要求する既存入力で、mkpkg を厳格化した以上これも結ぶ必要がある。
- `tools/mkpkg.py`: 欠損は一律エラー。全パッケージのファイルを**先に**解決して欠損を集め、1 つも `.PKG` を書かずに非ゼロ終了する (途中まで書いた媒体を残さない)。
- 副作用の注意: これまで欠損は warning だったので、`deploy.yaml` ではなく `package_defs.yaml` 側に「作られていない登録ファイル」があると `make iso` が**落ちるようになる**。現行の登録 38 件はすべて `programs` / `boot` / assets の成果物で、`make -n packages` で mkpkg より前に生成されることを確認済み。
- 未実施: ゲスト受入 T1 (媒体を mount して一覧、`sqlite3` で読む)、`make` 実行。B10 の FDD インストーラ不整合は S3。
- **レビュー往復 1 の B10 [P2] 反映** (2026-09-13): 「一律エラー」に残っていた 2 つの例外を塞いだ — 存在しない `--defs` を黙って無視していたのと、glob の展開が 0 件でも成功にしていたのをどちらもエラーにした (簡易 parser は変えず、展開結果だけを見る)。反例 (正常なパッケージ + 0 件 glob のパッケージ) で `.PKG` を 1 つも書かないことを試験に固定し、代表 3 規則 (int32 超過 / 重複キー / CR) は実装を一時的に緩めて反例 RED を採り直した (`s0_tdd.md` §T は「反例 RED」と「回帰試験」を分けて記録)。試験は 43 件。
- **レビュー往復 2 の B [P2] 反映** (2026-09-13): 先頭ゼロ付きの `int` (字句規則も int32 範囲も満たす) が Python 3.14 の桁数制限 (4300 桁) で `int()` の `ValueError` になっていた。符号の後の `0` を**変換の前に**畳み、畳んだ桁数が 10 を超えたら範囲外として拒否する (`sys.set_int_max_str_digits` に頼らない)。試験は 45 件。
- その副作用: リポジトリ自身の登録に 0 件の glob が 3 つあり (`assets/images/*.vbz` / `*.vdp` — ディレクトリ自体が無い、`assets/manga/*.mgx` — 実体は大文字の `.MGX` だけ)、そのままでは `make iso` が落ちる。`userland/package_defs.yaml` の APPEND から該当 3 行をコメント化した (素材を戻すとき復活させる旨を併記)。同じ食い違いをビルドではなく試験で先に捕まえる `RealPackageDefs` を追加。**この 1 ファイルは他レーンの持ち物なので、PM の判断で差し戻してよい** (その場合は 3 行を消すか素材を置くかのどちらかが要る)。

## 10. Codex 実装レビューの記録 (2026-09-13)

| レーン | 往復 1 | 内容 |
|---|---|---|
| D (`2c35f7c`) | Request changes | 9 件: cp / copy2 のディレクトリ宛て補完で最終ファイルが判定と違う、再帰 mkdir が保護対象の祖先を作る、clean の symlink と保護ディレクトリ内部、`check_root_etc` を入口で必ず呼ぶ / ENOTDIR、hsync の大文字名列挙と stat 失敗、mkdir / walk / rmdir の失敗が成功扱い、保護対象ディレクトリの除外が CLI 失敗、pull 失敗の全経路で stamp を消す、`/bin/..` の正当な正規化 |
| T (`6360618`) | Request changes | 1 件: mkpkg の `--defs` 不在と 0 件 glob の例外 → `2adcb2e` で修正済み (package_defs.yaml の実体の無い 3 登録をコメント化) |
| K (`08b4879`) | Request changes | 6 件: SHM 境界ちょうどの部分 ROW、journal の stat 失敗を不存在扱い、RW open の失敗が一律 CANTOPEN、stmt 無しの step で診断が 0 に戻らない、255B path の journal 誤認、`\f` の末尾空白。non-blocker: 再利用後の診断は新接続の値 (票を実装に合わせた)、K2 の PTE 検査ケースの強化、TRANSIENT 試験、PDE_PS、記録の訂正 |
ゲート (`08b4879`、テスター): `make clean` + `clean-external` → `all` (74s) → `external` → `check` (42s) すべて exit=0。vmkernel.lz4 469,605 B、settings.db 3,072 B、db_v50_test.bin 6,319 B。
| D (`10bc6ae`) / T (`2adcb2e`) | 往復 2: Request changes | D 9 件: 補完後のパスが symlink でディレクトリを指すと cp が再補完、最終パスの祖先保護の欠けと除外の失敗扱い、hsync の保護一覧 16 件超、hsync の read / ls / mkdir / sync 失敗と void main、hsync のパス連結の容量、stamp 書き込み失敗、`.pulled` が非オブジェクト、0 件操作で `check_root_etc` が呼ばれない、os.walk が保護ディレクトリの中を先に読む。T 1 件: 先頭ゼロ 4301 桁の int を Python の桁数制限で拒否 |
| K (`77d61b3`) | 往復 2: Request changes | 4 件: 末尾トークン判定が SQLite と不一致 (`\v` の続き、BOM、`/*` 末尾) → pzTail を再 prepare して SQLite に判定させる、入力検証で拒否した prepare が旧 stmt を bind 可能なまま残す → 入口で必ず finalize、列値の実体化 NOMEM で欠落 ROW を成功扱い → ERROR、journal 名の容量検査に cwd が含まれない (相対 path) → 解決後の絶対名で検査。T (`f2ac51c`) は往復 2 の 1 件を修正済み |
| K (`769e1fc`) | 往復 3 (最終): Request changes | 1 件: `vfs_resolve_path` が cwd 連結後に切り詰めてから正規化するため、`./`×122 + `a/../b.db` で `/tmp/b` (別 DB) を RW で開ける → resolve 前に長さ超過を検知して CANTOPEN。non-blocker: B3 の `len == 0` + NOMEM と TEXT 側、journal_mode の非 DELETE 拒否試験、static `probe` 264B の計上、KAPI_SPEC 概要の版更新、再 prepare は末尾 PRAGMA の副作用を残す (契約外)。**3 往復を使い切ったのでユーザー判断** (修正は準備中) |
| K (`c24f058`) / D (`a383619`) | 最終往復 (ユーザー承認、K+D 合同): Request changes | K 1 件 (P1): パスの深さ 32 成分超で `vfs_resolve_path` が成分を捨て、その後の `..` が別 DB (`/b.db`) に到達 → 入口で成分数も検査して CANTOPEN。D 1 件 (P2): hsync が失敗時も `Done:` ラベル → 成否でラベルを分ける。non-blocker: K の試験自身の範囲外読み、s0_tdd の旧記述。修正は準備中、**着地とその後 (再往復か配備か) はユーザー判断**。ゲート (`a383619`): clean + clean-external → all → external → check すべて exit=0、vmkernel.lz4 470,181 B |
| K (`a70df4f`) / D (`68d08c7`) | 追加往復 (ユーザー承認): **Approve** | K・D とも追加 blocker なし。non-blocker: ホスト試験の保証範囲 (K のポインタ判定は模型、hsync は純関数中心)、D1 / K1 / K2 は受入で。ゲート (`a70df4f`): `make all` / `external` / `check` exit=0、vmkernel.lz4 470,305 B。**配備へ** |

## 11. 実機受入の記録 (PM / テスター、2026-09-13)

配備 1 回目 (feat/gui `4554a10`、vmkernel 470,305 B): NP21/W 停止 → `nhd-pull` (stamp `os32.nhd.pulled` 261 B) → バックアップ `os32.nhd.bak-s0-20260913-140849` → `os32-cycle deploy` → **`make deploy-nhd` が D0 の前提検査で失敗**: `配備ツリーを辿れない: [Errno 13] Permission denied: '/tmp/os32/lost+found'` (ext2 標準の root 所有 700 のディレクトリを非 root の `os.walk` が読めない)。NHD は未変更、HostDrv の `make deploy` は exit=0。→ D へ: ルート直下の `lost+found` (ディレクトリ、名前一致) だけ走査から外す。
配備 3 回目 (feat/gui `18682d4`、K2 の切り分け計器入り、vmkernel 470,718 B): `db_v50_test` は `6/8 passed` で **RO / RW とも既存 DB の open が 21 (MISUSE)**、`last_error=invalid handle`。計器: `ring3_range_reject_count` 4、`_last` **5 = 非 present**、`_addr` 0x50140e、`_page` 0x501000 (アプリ自身の .rodata)、`_heap_top` 0x7bf000、`fault_kill_count` 0 → **`paging_addrspace_pte_flags(AppSlot.as, page)` が実際に有効な PD/PT を見ていない** (走っているコードの隣のページを非 present と判定)。→ K へ: 検査は CR3 に載っている PD を歩く形に。

配備 4 回目 (feat/gui `8162a2c`、方針 B = PTE 検査撤去、vmkernel 470,756 B): 順序は **NP21/W 停止 (PM、taskkill) → `nhd-pull` (テスター、stamp 15:14) → `os32-cycle deploy` (テスター、ゲストの vmkernel サイズ一致) → `make deploy` (HostDrv) → kselftest 87 / 0**。先に走らせた `nhd-pull` は NP21/W が NHD をロックしていて `Permission denied` で失敗し stamp が消えた (仕様どおり) — **pull は停止後に行う**。
- **K2 合格**: `db_v50_test` = `PASS 41/41` (`open failure code = 14` = CANTOPEN、`/etc/nosuch.db` は作られず `/etc` は filetypes / system.cfg / settings.tsv のまま、fixture `/tmp/db_v50.db` 6144 B)。計器: `ring3_range_reject_count` **2** (`_last` = 4 BAND、`_addr` 0x7fffff、`_page` 0x800000 = K2 の**意図した**「許可帯末尾 -1 から 2B」の拒否 2 回)、`fault_kill_count` 0。
- **K1 再確認**: regress 6 / 6 (`s0k4reg`)、kselftest 87 / 0。
- **S0 の受入は D1 (a / b / c1 / c2)、T1、K1、K2 のすべてが合格**。§1 の「guard をまたぐ範囲が -1」は「kill される」に改めたので、その項は K2 から外れている (別プログラムでの確認は S0 の範囲外、票 §7 の「残る懸念」)。
