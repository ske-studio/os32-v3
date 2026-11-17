# tools/deploy_protect.py — 設計

> 状態: **現行 (2026-11-18)** — 出所: S0_FOUNDATION・TASK_S0 (S0-D) の契約を今のソースと照らして要約
ソース: `tools/deploy_protect.py`

通常の配備が `/etc/settings.db` とその仲間を作らない・書かない・消さないための共通の判定。配備ツール (`tools/nhd_deploy.py`・`tools/hostdrv_deploy.py`・`tools/prune_stale.py`) は、作成・切り詰め・削除・rename の **直前** に、確定した最終パスでこれを呼ぶ。判定と印字だけで、実配備・sudo・mount はしない。
DB は **ゲストが書く** 物でホストのビルド成果物ではない (設定レジストリの全体の約束は `docs/design/userland/lib/cfg/libos32cfg.md`)。

## 守る物 (`PROTECTED_BASENAMES`・`PROTECTED_DIR`)

ゲストの `/etc` 直下の次の名前 (大文字小文字を区別しない)。

- `settings.db`・`settings.db-journal`
- `settings.db-wal`・`settings.db-shm` (WAL を使う意味ではなく安全側)
- 復旧 (`install --recover-settings`・`--revert-settings`) が作る `settings.db.bak`・`.bak-journal`・`.failed`・`.failed-journal`・`.new`・`.new-journal`・`.recover-state`

保護は除外であって失敗ではない。除外した物は `protected: <path> (skipped)` を標準出力に 1 行出し (`protect_log`)、配備の成否と件数には数えない。既存は内容も有無もそのまま、欠けている物は欠けたまま。`NO_PRUNE` に頼らない。

## 判定の 2 段 (`is_protected`)

1. **名前の規則** (`name_is_protected`): 字句で正規化したゲスト名の親が `/etc` で、basename が守る名前なら真。realpath より前に当てるので、実体が無くても (dangling symlink・欠損) 守る。
2. **実体の規則**: realpath で解決し、root の外なら `ProtectError`。解決後のゲスト名にも名前の規則を当て、さらに `st_dev`・`st_ino` が `<root>/etc` に **在る** 守る物のどれかと同じなら真 (hardlink の別名)。

stat の失敗は ENOENT (確定した不存在) だけを「守る物ではない」とし (初回の `/etc/settings.tsv` を配備できる)、ほかの失敗 (EACCES・EIO・ELOOP・解決の失敗) は `ProtectError` = 配備を失敗にする。判定できないまま書かない。

## 最終パスの確定 (`resolve_dest`・`normalize_guest_path`)

- ゲスト名の `.`・連続 `/` を畳み、`..` は 1 段戻す。root より上へ出たら `ProtectError`。root へ戻り切る (`/bin/..`) のは正当な `/`。
- `guest: /etc/` のようなディレクトリ指定は host_src の basename を補う。宛先が既存のディレクトリなら cp・copy2 は中へ書くので、実ディレクトリを見て basename を補う。補うのは 1 回まで。補った先がまだディレクトリなら、守る物 (自身が守る名前か、祖先に守る物) なら「書かずに成功除外」、そうでなければ `ProtectError`。
- 呼び手は戻り値 (ファイル名) だけを cp・copy2 に渡し、ディレクトリを渡さない。cp・rm・mkdir・ls の operand は `--` の後に置き、source は絶対パスにする (オプションに読ませない)。

## 祖先とディレクトリ作成 (`protected_ancestor`・`mkdir_chain`・`check_dest`)

- `check_dest` は、前提の検査 → 最終パスの確定 → **祖先** → 最終パス自身、の順。祖先を stat より先に見る (`/etc/settings.db` がディレクトリなら中へ書かない。通常ファイルなら `/etc/settings.db/sub/f` の stat の ENOTDIR を「判定できない失敗」にしない)。祖先が守る物なら成功除外。
- `mkdir -p`・`os.makedirs` の代わりに `mkdir_chain` で root から 1 段ずつ判定し、守る物に当たったら `ProtectedPath` (除外 = 成功) を投げる。守る名前のディレクトリ (`etc/settings.db/` の残骸) を作らない。
- `ProtectedPath` は `ProtectError` の子。呼び手は「除外 (成功)」と「判定できない (失敗)」を分ける。

## 配備全体を断る前提 (`check_root_etc`・`check_tree`)

- `check_root_etc`: `<root>/etc` が symlink・通常ファイル・字句と違う実体・root と別のマウントなら `ProtectError`。名前の規則だけで答えが出る経路でも、判定のたびに先に通す。bind mount の別名はツールで検出できないので運用で禁止する。
- `check_tree`: 配備ツリーに symlink が 1 つでもあれば配備全体を断る (OS32 の ext2 に symlink を作る手段は無く、HostDrv は Windows のフォルダなので、symlink は未対応の配置)。重いのでサブコマンドの入口で 1 回。走査から外すのは、守る名前のディレクトリの中 (配備は中へ書かない) と、ルート直下の実ディレクトリの `lost+found` (`SKIP_ROOT_DIRS`、`skip_root_entries`・`walk_root`) だけ。`lost+found` が symlink・通常ファイルなら外さない。ほかの読めないディレクトリは失敗。
- 外した場所の中の symlink は検査していない。「ツリー全体に symlink が無い」とは言えない (配備がそこに書かないので最終パスの意味には効かない)。

## 呼び手 (配備ツール) の約束

- 適用点: `nhd_deploy.py` の manifest の sync (file・glob・tag)・`sync-from-hostdrv` (source の HostDrv ツリーにも `check_tree`)・`copy`・`copy-all`・`rm`・書きかけの後始末、`hostdrv_deploy.py` の sync と clean、`prune_stale.py` の HostDrv・NHD の掃除。HostDrv の内容比較より **前** に判定し、守る物は比較のためにも開かない。
- clean は `/etc` を木ごと消さず、項目ごとに上から判定して、守る物とその祖先を残す。prune は `/etc` を対象に広げない。HostDrv のルートの stat は ENOENT だけ「無い = 何もしない (成功)」で、ほかは失敗。
- CLI の `copy`・`copy-all`・`rm` からも守る物は書けない・消せない。
- 失敗を成功にしない: cp・rm・mkdir・sync・umount・`os.walk` の失敗はサブコマンドの非ゼロの終了コードになり、後続 (NHD の全体の配備) へ進まない。進捗と成否を分け、失敗があれば `FAILED (n copied, m failed)` を出す。
- 対象が 0 件のサブコマンド (`sync --tag`・`prune --delete`) でも入口で `check_root_etc` を通す。

## NHD の全体の配備と来歴 (`tools/nhd_deploy.py` の `write_pull_stamp`・`verify_pull_stamp`)

ファイル単位の除外では、古いローカルの NHD で remote を丸ごと上書きする `deploy` が稼働後の設定を戻してしまう。そこで pull の来歴 `<local>.pulled` (JSON: `remote_path`・`remote_size`・`remote_mtime`・`remote_sha256`・`local_path`) を使う。

- 来歴は pull (明示・自動とも) が mount まで **全部成功した最後** にだけ書く。一時ファイルへ書いて fsync → rename。書けなければ一時ファイルと既存の来歴を消して pull を失敗にする。pull の入口で古い来歴を消す。
- `deploy` は、来歴が在って JSON のオブジェクトで、`local_path`・`remote_path`・remote の大きさが一致し、remote を **再ハッシュ** した値が一致し、pull の後にゲストを起動していない (`guest_started`) ときだけ全体を書く。どれかが崩れたら失敗で、`--force` でだけ通す。壊れた来歴 (`[]`・`null`) でも `--force` の判定まで進む。

## ゲスト側 (`userland/system/hsync.c`)

HostDrv → NHD の再帰コピーも同じ名前の表で守る。字句の判定は `userland/system/hsync_protect.inc` (`docs/design/userland/system/hsync_protect.md`)。

## 例外

明示のインストール (`install.bin`・`cdinst.bin`) と `cfg init` はゲストの中の操作で、この判定の対象外。`do_init`・`do_format` は明示の全消去で、通常の更新の代わりにしない。

## 検査

`settings-protect-host` の前半 (`tools/tests/test_deploy_protect.py`)。temp dir と mock だけで走り、`subprocess.run` を差し替えて `mount`・`umount`・`losetup`・`mkfs.ext2`・`e2fsck`・`taskkill.exe` が呼ばれたら落ち、cp・rm・mkdir の宛先が temp の外でも落ちる。import の前に `NP21W_DIR`・`HOSTDRV_DIR`・`OS32_NHD_LOCAL` を temp へ向ける (`.env` を読ませない)。
