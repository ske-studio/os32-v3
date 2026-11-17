# userland/system/hsync_protect.inc — hsync が学習 DB を上書きしない判定 (FEP の部分)

> 状態: **現行 (2026-11-18)** — 出所: DICT_META_DESIGN・TASK_DICT_META (要約して移した)
ソース: `userland/system/hsync_protect.inc` (`hsp_set_fep_user_path`・`hsp_is_protected_basename`)・`userland/system/hsync.c` (`register_fep_user_path`・`scan_protected_entities`・`--fep-off`)

この文書の本体は FEP の学習 DB の保護。settings.db の保護 (名前の表 `hsp_protected_names` と字句の判定) は末尾の節「settings.db の保護」。
学習 DB の置き場と形は [ime_user_store](../../kernel/ime/ime_user_store.md) にある。

## 何をするか

hsync は HostDrv の中身を NHD へ写す。HostDrv に古い学習 DB が残っていても、ゲストが書いた学習 DB を上書きしない。
守るのは `/etc` 直下の、次の名前で始まる物 (大文字小文字を区別しない、後ろに何が付いても — ジャーナル・一時の配置名を含む)。

- 既定の `fep_user.db` (設定を変えた後も守る)。
- settings で指定した学習 DB の名前 (`hsp_set_fep_user_path` で登録した物)。

## 登録 (`register_fep_user_path`)

1. 同期の前に 1 回、**写し先** の `<root>/etc/settings.db` を読む (KAPI の `db_open_existing`・`db_prepare_only` …)。
2. DB・表・行が無ければ既定の名前を登録する。
3. 値は `typeof(tval)='text'` で型を確かめて読む (`CAST` は製品の SQLite で使えない)。
4. `hsp_set_fep_user_path` は `/` で始まるパスを字句で正規化し、`/etc/` の直下 (大文字小文字を区別しない) で、`-journal` を足して 256 byte に収まる物だけ受ける。だめなら `OS32_ERR_INVAL` で、前の登録を残す。
5. どの段で失敗しても (開けない・SQL・値の形)、段の名前と rc を警告 1 行に出し、**`/etc` 直下の `.db` を含む名前を全部守って** 同期を続ける (`hsp_fep_all_db`)。

旧いカスタムのパスを探して守り続ける仕組みは無い。置き場を変えるときは、学習 DB を先に移してから設定を変える。

## 実在の名前の収集と上限

`scan_protected_entities` は `/etc` を列挙し、保護の名前に当たる実在の名前を大文字小文字を無視して全部集め、inode で別名 (hardlink) も守る。
集める枠 `HS_MAX_PROT` は 24。settings の 13 名、既定の学習 DB の本体・ジャーナル・一時名、別のカスタム名の同じ 3 名を足しても余る。
25 件目が出たら同期を断る (黙って捨てない)。

## 使用中の辞書と `--fep-off`

- VFS は開いている SQLite DB (使用中の `/db/fep*.db`) とそのジャーナルの置き換えを `BUSY` で断る。hsync はそれを理由つきで表示する。
- `hsync --fep-off` は同期の間だけ FEP の辞書を閉じ (KAPI `ime_dict_suspend(1)`)、終わったら開き直す。`-n` (試しの実行) では閉じない。
- 学習 DB は deploy・packages の配布物に入れない。

## 検査

- `hsync-fep-protect` (`tools/tests/test_hsync_fep_protect.py`、本体 `tools/tests/hsync_fep_protect_host.c`): 字句の規則 (大文字小文字・正規化・祖先・カスタム名・ジャーナル)、実在の別名、24 枠の境目 (libc なし)。
- `hsync-fep-host` (`tools/tests/test_hsync_fep.py`): `--fep-off` での停止と再開を hsync の main から。
- 設定が読めないときに `/etc` の DB を全部守る場合は `tools/tests/fdrecov_cases.inc` にもある。

## settings.db の保護

> 出所: S0_FOUNDATION・TASK_S0 (S0-D) の契約を今のソースと照らして要約 (2026-11-18)

ホストの配備の判定 (`tools/deploy_protect.py`、`docs/design/tools/deploy_protect.md`) と同じ名前の表を、ゲストの hsync も持つ。HostDrv に古い `etc/settings.db` が残っていても、ゲストが書いた DB を切り詰めない。

- 名前の表 `hsp_protected_names`: `/etc` 直下の `settings.db`・`-journal`・`-wal`・`-shm`・`.bak`・`.bak-journal`・`.failed`・`.failed-journal`・`.new`・`.new-journal`・`.recover-state`・`.master.new`・`.master.new-journal` の 13 名 (復旧が扱う 11 名 + `-wal`・`-shm`)。大文字小文字を区別しない完全一致 (FEP の名前は前方一致で別)。
- 字句の正規化 (`hsp_normalize`): `.`・`..`・連続 `/` を畳む。相対・末尾 `/` (mkdir の経路)・`hsync -f etc` の subdir の連結も、正規化した後の文字列で判定する。`\` を含む・root を越える `..`・バッファや深さの上限を越える入力は失敗。
- 判定 (`hsp_path_classify`): 1 = 自身か **祖先** が守る物 (`hsp_ancestor_protected`、`/etc/settings.db/sub` も守る)、0 = 対象外、-1 = 判定できない (正規化の失敗)。-1 も書かないが、「守った」とは数えずエラーに数える (何も同期していないのに errors 0 で終わらせない)。
- 実体の規則 (hsync.c の `scan_protected_entities`): `/etc` を列挙して守る名前に当たる実在の名前を大文字小文字を無視して全部集め、stat の inode で別名 (hardlink) もコピーしない。stat が NOTFOUND 以外で失敗したら同期を中止。集める枠 `HS_MAX_PROT` を越えたら同期を断る (黙って捨てない)。
- 名前の長さ (`hsp_name_fits`): 列挙の名前が `NAME_CAP` に収まらなければ切り詰めて写さず (別名のファイルを作らない)、エラーに数える。`MAX_FILES`・`MAX_DEPTH` での打ち切りも件数つきでエラー。パスの連結は容量を検査する。
- 失敗の伝播: read の負値・`sys_ls`・mkdir・`vfs_sync` の失敗はエラーに数え、`main` の終了コードにする。最終行の頭 (`hsp_final_label`) はエラーがあれば `FAILED:`、無ければ `Done:` で、件数はそのまま出す。

### 検査

`settings-protect-host` の後半 (`tools/tests/test_hsync_protect.py`、本体 `tools/tests/hsync_protect_host.c`): 実物の `hsync_protect.inc` をホストで `-Werror` で組み、正規化・名前の規則・通してよい物・`-f` の連結・長いパス・名前の長さ・最終行のラベルを確かめる。同じソースが i386-elf-gcc と PROGRAM_FLAGS でも通ることと、hsync.c の inode の比較が在ることも見る。列挙・打ち切り・I/O 失敗の分岐はホストでは走らせていない。
