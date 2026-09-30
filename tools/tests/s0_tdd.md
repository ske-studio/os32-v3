# S0 — 設定レジストリ基盤のホスト TDD 記録

票: [`docs/archive/settings/TASK_S0.md`](../../docs/archive/settings/TASK_S0.md)、
契約の正典は [`docs/archive/settings/S0_FOUNDATION.md`](../../docs/archive/settings/S0_FOUNDATION.md)。

3 票 (S0-K / S0-D / S0-T) が同じファイルに書くので、節を票ごとに分ける。
**自分の節だけ**を書き、他の節には触れないこと。

- `## K.` — KAPI v50 と回収順序 (S0-K)
- `## D.` — 通常配備の settings 保護 (S0-D)
- `## T.` — 初期値 tsv と生成ツール (S0-T)

---

## D. 通常配備の settings 保護 (D0) — 2026-09-13

### D.1 何を止めたいか

`/etc/settings.db` は**ゲストが書く**もので、ホストのビルド成果物ではない。
ところが配備は 4 系統あり、どれか 1 つでも掴むとユーザーの設定が消える。

| 系統 | 入口 | 消し方 |
|---|---|---|
| マニフェスト配備 | `nhd_deploy.do_sync` / `hostdrv_deploy.do_sync` | `cp` / `copy2` が `O_TRUNC` 相当で上書き |
| HostDrv 丸写し | `nhd_deploy.do_sync_from_hostdrv`、ゲストの `hsync` | HostDrv に残った古い `etc/settings.db` が本体を切り詰める |
| 掃除 | `prune_stale`、`hostdrv_deploy.do_clean` | `rmtree(root/etc)` は判定の余地なく木ごと落とす |
| イメージ全体 | `nhd_deploy.do_deploy` | 古い local NHD で remote を丸ごと上書き = 稼働中に書かれた設定が消える |

ファイル単位の除外だけでは 4 番目が残る。そこで pull の**来歴** (`<local>.pulled`) を
足し、`deploy` は来歴が崩れていないときだけ全体を書く。

### D.2 試験の範囲と遮断

- `tools/tests/test_deploy_protect.py` — temp dir + mock。`subprocess.run` を
  `FakeRun` に差し替え、`mount` / `umount` / `losetup` / `mkfs.ext2` / `e2fsck` /
  `taskkill.exe` が呼ばれたら **AssertionError で試験を落とす**。`cp` / `rm` /
  `mkdir` / `sync` だけを temp dir の中で模擬する。import 時に `.env` を読ませない
  よう、`NP21W_DIR` / `HOSTDRV_DIR` / `OS32_NHD_LOCAL` を temp へ向けてから import する。
- `tools/tests/test_hsync_protect.py` + `hsync_protect_host.c` — ゲスト側の字句判定
  (`userland/system/hsync_protect.inc`) を**実物のソースのまま** ホストで走らせ、
  同じソースが `i386-elf-gcc` + PROGRAM_FLAGS でも通ることを別に見る ([C1])。
- エミュレータ・実配備・`make` には一切触れていない ([V4])。

### D.3 RED → GREEN

RED は実装前の `tools/nhd_deploy.py` / `hostdrv_deploy.py` / `prune_stale.py`
(feat/gui `775fc4a` の版) に対して、新しい試験だけを当てて取った。
`tools/deploy_protect.py` は新規モジュールなので RED 時も置いてある
(だから「判定そのもの」の 13 件は RED でも通る = 差分は**適用点**にある)。

```
RED  : Ran 53 tests — FAILED (failures=26, errors=9)     18 passed
GREEN: Ran 53 tests — OK
```

RED で落ちた代表 (票 §2 の項目との対応):

| 試験 | RED の症状 | 票 |
|---|---|---|
| `NhdSync.test_protected_source_is_skipped_and_db_unchanged` | manifest の `etc/settings.db` が別内容で上書きされた | (1) |
| `NhdSync.test_missing_db_stays_missing` | 欠損していた DB を通常配備が**作った** | (1) |
| `NhdSync.test_directory_style_guest_path` / `test_dotdot_detour_is_blocked` | `guest: /etc/` と `..` 経由で素通り | (3) |
| `SyncFromHostdrv.test_stale_hostdrv_db_does_not_truncate_nhd_db` | HostDrv の残骸が NHD の本体を潰した | (3) |
| `SyncFromHostdrv.test_protected_directory_in_hostdrv_is_not_created` | `etc/settings.db-wal/` を mkdir した | (3)、往復 3 の 4 |
| `NhdCli.test_copy_*` / `test_rm_cannot_remove_protected` | CLI から DB を書ける・消せる | (3) |
| `HostdrvSync.test_clean_keeps_protected_and_its_ancestors` | `rmtree` が `/etc` ごと落とした | (3)、往復 3 の 4 |
| `HostdrvSync.test_protected_is_judged_before_content_comparison` | 保護対象を内容比較のために開いていた | (1) |
| `Prune.test_prune_*_keeps_protected` | 掃除が保護対象を消した | (3) |
| `NhdSync.test_sync_failure_is_not_success` ほか | `sync` / `rm` / `cp` の失敗が成功として返った | (4) |
| `MainExit.*` | `copy` / `rm` / `umount` の失敗が exit 0 のまま後続へ進んだ | (4) |
| `Stamp.*` (7 件) | 来歴が無い / local が別物 / remote が書き換わっていても全体上書きした | (5) |

「判定そのもの」(`ProtectJudgement`、13 件) は RED でも GREEN でも通る。名前規則を
realpath の**前**に置くこと (dangling symlink / 欠損でも守る)、`stat` の失敗は
**ENOENT だけ不存在**として扱い他は `ProtectError` で配備を止めること、
`<root>/etc` が symlink / 別マウントなら**配備全体を拒否**すること、hardlink の
別名を `st_dev`/`st_ino` で拾うことを、この 13 件が固定している (往復 2 の 6、
往復 3 の 3)。

### D.4 hsync (ゲスト側)

```
HOST GNU89 -Werror COMPILE PASS
  28 checks / 0 failures        (正規化 7、名前規則 11、通してよいもの 6、-f 連結 4)
TARGET i386-elf GNU89 COMPILE PASS   (hsync.c を PROGRAM_FLAGS で)
```

`./` / `..` / 連続 `/` / 相対 / 末尾 `/` (mkdir 経路) / 大文字 / `hsync -f etc` の
subdir 連結の各形で `/etc/settings.db*` に届くことを確かめてある。正規化に失敗する
入力 (root を越える `..`) は**保護側**へ倒す。実体規則 (同一 inode の hardlink を
コピーしない) は `sys_stat` が要るので `hsync.c` 側にあり、ここでは見ていない
(ゲスト受入 D1 で見る)。

### D.5 この試験が言えないこと ([V4])

- 実機 / エミュレータでの D1 受入 (HostDrv に意図的に `etc/settings.db` を置いて
  `make deploy` → `hsync -f etc` → 停止 → `pull` → `make deploy-nhd`) は**未実施**。
  コーダーは配備・エミュレータ・`make` を実行しない。
- `hsync` の inode 比較 (実体規則) はホストでは踏んでいない。純関数に出せる
  「/etc 列挙の一致判定」(`hsp_is_protected_basename`) までが試験の範囲。
- `sudo` / ループマウントの実挙動は模擬。sudoers や ext2 の振る舞いは何も言わない。
- bind mount による `<root>/etc` の別名は原理的に検出できない (運用で禁止し、
  symlink / 別マウント / 通常ファイルだけを `check_root_etc` が止める)。
- 往復 3 以降、配備ツリーに symlink があれば配備全体を拒否する (`check_tree`)。
  **symlink を含むツリーへ安全に配備できるとは言っていない** — 止めているだけ。
  走査は**保護対象名のディレクトリの内部**と**ルート直下の `lost+found`** には
  入らない (どちらも配備が中へ 1 バイトも書かない場所なので、そこの symlink は
  最終パスの意味に効かない)。つまり `/etc/settings.db/` と `/lost+found/` の中に
  何があるかは検査していない (§D.11)。

### D.6 実装レビュー 往復 1 の blocker 9 件 (2026-09-13、着地 `2c35f7c` に対して)

RED は着地版 (`feat/gui` の `tools/deploy_protect.py` / `nhd_deploy.py` /
`hostdrv_deploy.py` / `prune_stale.py`) に新しい試験だけを当てて取った。

```
RED  : Ran 86 tests — FAILED (failures=25, errors=4)     57 passed
GREEN: Ran 86 tests — OK
```

| # | 反例 (RED で通ってしまっていた道) | 試験 |
|---|---|---|
| B1 | 判定後に `cp` / `copy2` が basename を補う。`copy --dest / --rename etc <settings.db>`、manifest の `guest: /etc` (末尾 `/` 無し) が **`/etc/settings.db` を上書き**した | `ReviewB1DirDestination` 5 件。`resolve_dest` が**実ディレクトリを見て**最終ファイル名を確定し、`cp` にディレクトリを渡さないことも見る |
| B2 | `mkdir -p` / `makedirs` が未判定の祖先を作る (`/etc/settings.db/a` で DB がディレクトリ化) | `ReviewB2MkdirChain` 5 件。`mkdir_chain` が root から 1 段ずつ判定し `ProtectedPath` |
| B3 | clean の symlink 分岐が保護判定より前、`root/etc` が symlink でも消して成功、保護ディレクトリの中身を bottom-up が先に消す | `ReviewB3Clean` 4 件。top-down + 判定を最初に + `check_root_etc` で拒否 |
| B4 | 名前規則で即 return する経路が `check_root_etc` を通らない、`etc` が通常ファイルだと `ENOTDIR` を空集合に握り潰す | `ReviewB4EntryCheck` 4 件。前提検査を入口で 1 回、ENOENT 以外は失敗 |
| B5 | 小文字 5 名しか stat しないので `/etc/SETTINGS.DB` + hardlink を `hsync -f bin` が上書き、stat が EIO でも open して切り詰める | C 側 (D.4 の「/etc 列挙の一致判定」11 件)。`sys_ls` で列挙 → 大文字小文字無視で一致 → 全部 stat、`OS32_ERR_NOTFOUND` 以外は同期中止 |
| B6 | `mkdir -p` の ENOSPC / `os.walk` の onerror 未指定 / clean の `rmdir` 失敗が成功になる | `ReviewB6FailurePropagation` 4 件 |
| B7 | 保護対象ディレクトリへの `copy --dest` が exit 1 | `ReviewB2MkdirChain.test_copy_dest_under_protected_ancestor` (除外は成功) |
| B8 | remote 欠損の早期 return / マウント中拒否 / `do_mount()` 失敗で stamp が残る | `ReviewB8PullStamp` 4 件。入口で消し、**全部成功した最後**に書く |
| B9 | `--dest /bin/..` (= `/`) を空パスとして拒否 | `ReviewB9RootNormalization` 4 件。parts が空 = `/` は正当、root より上だけ拒否 |

non-blocker も同時に直した: `FakeRun` の `cp` を実物どおり「宛先が既存ディレクトリ
なら中へ」にし、`cp` / `rm` / `mkdir` の宛先が temp の外なら **AssertionError**
(隔離の保証)、`ManifestEntryPoints` で resolver を差し替えずに manifest の入口
(file / glob / tag) を通し、`do_copy` の `Done!` と prune の件数から保護除外を除いた。

### D.7 実装レビュー 往復 2 の blocker 9 件 (2026-09-13、着地 `10bc6ae` に対して)

RED は着地版 (`feat/gui` の 4 ツール) に新しい試験だけを当てて取った。

```
RED  : Ran 103 tests — FAILED (failures=9, errors=3)     91 passed
GREEN: Ran 103 tests — OK
```

| # | 反例 (RED で通ってしまっていた道) | 試験 |
|---|---|---|
| 1 | 補完した先がまた (symlink 越しに) ディレクトリだと cp が**もう一度** basename を補う。`<root>/bin/settings.db -> ../etc` を置き、`guest: /bin` + source 名 `settings.db` で `/etc/settings.db` に届いた | `Review2DoubleFill` 4 件。補完は 1 回まで、補完後がディレクトリなら `ProtectError`。「host_src を渡した `resolve_dest` はディレクトリを返さない」も固定 |
| 2 | 最終パスの**祖先**を見ていない。`/etc/settings.db/` がディレクトリなら中へ書けて、逆に通常ファイルなら `/etc/settings.db/sub/f` の stat が ENOTDIR で「判定できない失敗」になった | `Review2AncestorOnFinalPath` 4 件。`protected_ancestor` を新設し、`check_dest` が **stat より先に**当てて「書かずに成功除外」 |
| 3 | `/etc` の保護対象が 16 件を越えると 17 件目を黙って捨て、その hardlink を上書きできた | C 側 (D.4)。容量超過は同期を**拒否** (`g_prot_overflow` → エラー終了) |
| 4 | `sys_read` の負値を EOF 扱いで部分コピーを成功に、`sys_ls` / `mkdir` / `vfs_sync` の失敗を無視、`main` が `void` で終了コードに乗らない | C 側。全部 `g_errors` に数え、`main` は `int` (crt0_c が `sys_exit` へ渡す)。深さ / 件数の打ち切りもエラー |
| 5 | `src` / `dst` (256B) への連結に容量検査が無く、正規化や判定より前に溢れた | C 側 + `hsync_protect_host.c` の「長いパス」2 件。無検査の `str_cpy` / `str_cat` を撤去し `str_ncpy` / `str_ncat` だけにした |
| 6 | `json.dump` / close の ENOSPC で書きかけの `.pulled` が残る | `Review2StampWrite` 2 件。tmp へ書いて fsync → rename、失敗時は tmp と既存 stamp を消して例外 |
| 7 | `.pulled` が `[]` / `null` だと `.get` で例外になり `--force` に届かない | `Review2StampWrite.test_non_object_stamp_reaches_force` |
| 8 | 対象 0 件の `sync --tag` / stale 0 件の `prune --delete` で判定が一度も呼ばれず、壊れた `<root>/etc` を見逃した | `Review2EntryCheck` 5 件。各サブコマンドの入口で `check_root_etc` を 1 回 |
| 9 | 読めない `etc/settings.db/` を `os.walk` が先に scandir して CLI ごと失敗した | `Review2WalkPruning` 1 件。親の `dirnames` から保護対象を in-place で外して降りない |

non-blocker も同時に: `hsync` の `MAX_FILES` (128) / `MAX_DEPTH` (8) の打ち切りを
エラーとして報告 (件数つき)、この節の末尾に残っていた §D.5 の重複 3 行を除去。

### D.8 実装レビュー 往復 3 の 6 件 (2026-09-13、着地 `ef0bd60` に対して)

**方針が変わった**: symlink の迷路を 1 件ずつ塞ぐのをやめ、**配備ツリー
(HostDrv ルート / マウントした NHD ツリー) に symlink が 1 つでもあれば
「未対応の配置」として配備全体を拒否する** (`check_tree`、**ユーザー決裁済み
2026-09-13**)。走査は**保護対象名のディレクトリの内部には入らない** (§D.9)。
OS32 の ext2 に symlink を作る手段は無く、HostDrv は Windows のフォルダなので、
運用上の制約として成り立つ。これで往復 1〜3 の blocker の大半 (補完後の再補完、
解決後の祖先、中間リンクの削除、リンク越しの別名) が**到達不能**になる。
`is_protected_symlink` (symlink の削除許可) は撤回した。

```
RED  : Ran 121 tests — FAILED (failures=12, errors=6)     103 passed
GREEN: Ran 121 tests — OK
```

| # | 反例 / 変更 | 試験 |
|---|---|---|
| D1 / D2 | 解決後の祖先・中間 symlink の削除。**symlink 無しでも成立する変種は無い** (symlink が無ければ realpath == 字句パスなので `protected_ancestor` が既に覆う。ディレクトリへの hardlink は作れない)。念のため prune にも `protected_ancestor` を当てた | `Review3TreeSymlink` 8 件 + `Review3PruneStat.test_prune_keeps_entries_under_protected_ancestor` |
| D3 | source 名 `--target-directory=target` が `cp` のオプションに解釈され、判定外へ書けた | `Review3CpOptionInjection` 2 件。`cp` / `rm` / `mkdir` / `ls` の operand は必ず `--` の後、source は `os.path.abspath` で絶対化 |
| D4 | `<root>/etc/settings.db/` が既存ディレクトリだと、再補完禁止の `ProtectError` が名前判定より先に出て**非ゼロ**になった | `Review3ProtectedDirCompletion` 4 件。補完後が**保護対象名**なら再補完せず「成功除外」、保護対象でないディレクトリは従来どおり拒否 |
| D5 | `os.path.isdir` / `isfile` が EACCES を False に丸め、読めないディレクトリの候補が消えて「0 件で掃除済み」になった | `Review3PruneStat` 4 件。候補収集は `os.lstat`、ENOENT 以外の `OSError` は非ゼロ |
| D6 | `ls_cb` が名前を 63 文字で切り詰め、**別名のファイル**を作って成功と出た | `hsync_protect_host.c` の「名前の長さ」6 件。純関数 `hsp_name_fits` に切り出し、収まらない項目は取り込まずにエラーへ数える (`NAME_CAP` は 64 のまま = 静的配列の増分なし) |

non-blocker: `hsync_protect_host.c` の長いパス試験が自分のバッファ (1024B に
`"a/"`×512 + NUL = 1026B) を越えていたのを直した。

**hsync 本体の回帰証拠は純関数までである**。`ls_cb` / `sync_directory` /
`scan_protected_entities` の分岐 (切り詰め・打ち切り・I/O 失敗・inode 比較) は
ホストでは走らせていない — 見ているのは `hsp_*` の純関数と、同じソースが
i386-elf-gcc + PROGRAM_FLAGS で通ることだけ ([V4])。

### D.9 追加往復の 3 件 (P2、2026-09-13、着地 `da29800` に対して)

symlink を必要としない残りの 3 件。RED は着地版の 3 ツールに新しい試験だけを
当てて取った。

```
RED  : Ran 134 tests — FAILED (failures=6, errors=2)     126 passed
GREEN: Ran 134 tests — OK
```

| # | 反例 (RED で通ってしまっていた道) | 試験 |
|---|---|---|
| 1 | `<root>/etc/settings.db/settings.db/` が既存ディレクトリのとき、`guest: /etc/` + source 名 `settings.db` の補完先の親が `/etc` でないので名前判定が偽 → `ProtectError` で**非ゼロ**。守られているのに失敗していた | `Review4AncestorCompletion` 4 件。補完後がディレクトリなら **`protected_ancestor` を先に**見て、名前規則かそのどちらかで守られていれば成功除外。どちらでもなければ従来どおり拒否 |
| 2 | `os.path.isdir(HOSTDRV_DIR)` が親の EACCES を False に丸め、「HostDrvディレクトリが存在しません」と出して **成功で終了**していた | `Review4CleanRootStat` 3 件。`os.lstat` で ENOENT だけ「無い = 何もしない (成功)」、他の `OSError` と非ディレクトリは失敗 |
| 3 | `copy` が 1 件成功・1 件失敗でも `Done! (1 files copied)` を出し、明示 `pull` は mount / stamp で落ちた後も「完了!」が残っていた | `Review4ProgressVsSuccess` 4 件。進捗と全体の成否を分け、失敗があれば `FAILED (n copied, m failed)`。`pull` の「完了!」は mount と stamp まで通った最後 |

non-blocker: `sync-from-hostdrv` は宛先の NHD だけでなく **source の HostDrv
ツリー**も `check_tree` に通す (`Review4SourceTreeCheck` 2 件)。
併せて `check_tree` は**保護対象名のディレクトリには降りない** — 配備は中へ
1 バイトも書かないので最終パスの意味に効かず、降りると読めない残骸
(`etc/settings.db/` の chmod 000) だけで配備全体が止まってしまう
(往復 2 の 9 と噛み合わない)。

### D.10 最終往復の 1 件 (P2、2026-09-13、着地 `a383619` に対して)

| # | 反例 | 試験 |
|---|---|---|
| 1 | `hsync` が `g_errors > 0` でも最終行を `Done: 1 copied, 0 skipped, 1 errors, 0 protected` と出していた。終了コードは 1 で正しいが、コードを見ない目には成功に読める (「失敗時に成功表示へ進めない」、FOUNDATION §5) | `hsync_protect_host.c` の「最終行のラベル」3 件。純関数 `hsp_final_label(errors)` に出し、`errors` があれば `FAILED:`、無ければ `Done:`。**件数はそのまま**出す。色も失敗時は赤 |

```
RED  : hsync_protect_host.c がコンパイルできない (hsp_final_label が無い)。
       着地版の hsync.c:544 は "\nDone: %d copied, ..." を無条件に出していた。
GREEN: 57 checks / 0 failures、i386-elf-gcc も通過。
```

### D.11 実配備 1 回目の差し戻し — ext2 の `lost+found` (2026-09-13)

`make deploy-nhd` が実配備で止まった:

```
Error: 配備の前提検査に失敗: 配備ツリーを辿れない:
       [Errno 13] Permission denied: '/tmp/os32/lost+found'
```

`mkfs.ext2` は必ずルート直下に root 所有 mode 700 の `lost+found` を作る。
配備ツールは**非 root の Python** で走査する (実コピーだけ `sudo cp`) ので読めず、
`check_tree` の「辿れない = 失敗」に当たっていた。配備対象でも保護対象でもない
既知のディレクトリなので、走査から外す。

| 変更 | 内容 |
|---|---|
| `protect.skip_root_entries(root, dirpath, dirnames)` | `SKIP_ROOT_DIRS` (= `lost+found`) を **ルート直下の実ディレクトリのときだけ** `dirnames` から in-place で外す。symlink / 通常ファイルなら外さない |
| `protect.walk_root(root, onerror)` | `os.walk(followlinks=False)` の薄い包み。降りる前に上を適用する |
| 適用点 | `check_tree`、`nhd_deploy.do_sync_from_hostdrv`、`hostdrv_deploy._clean_tree` (こちらは `os.listdir` 再帰なので `skip_root_entries` を直接) |

```
RED  : Ran 142 tests — FAILED (failures=3, errors=2)     137 passed
GREEN: Ran 142 tests — OK
```

| 試験 | 見ていること |
|---|---|
| `test_check_tree_passes_with_unreadable_lost_found` | mode 000 の `lost+found` があっても `check_tree` が通る |
| `test_sync_passes_with_unreadable_lost_found` | 同じ状態で `do_sync` が成功し、成果物が入る |
| `test_sync_from_hostdrv_passes_with_unreadable_lost_found` | source / 宛先の両側にあっても成功し、**中身は配備しない** (宛先の `lost+found/secret` が元の内容のまま) |
| `test_prune_and_clean_pass_with_unreadable_lost_found` | `prune_nhd` / `prune_hostdrv` / `do_clean` も通り、clean が中へ降りない |
| `test_lost_found_symlink_is_still_refused` | `lost+found` が symlink なら**除外せず拒否** |
| `test_unreadable_dir_elsewhere_still_fails` / `test_other_unreadable_root_dir_still_fails` | ルート以外の `lost+found`、ルート直下の別名 (`data`) は**従来どおり失敗** (判定不能は失敗) |
| `test_lost_found_as_regular_file_is_not_skipped` | 通常ファイルなら外さない |

**この除外の代償**: `lost+found` の中に symlink があっても検査していない。
配備は中へ 1 バイトも書かないので最終パスの意味には効かないが、「ツリー全体に
symlink が無い」とは言えなくなった (§D.5 に同じ注記)。

## T. 初期値 tsv / 生成ツール / ビルド統合 (S0-T、2026-09-13)

### 範囲と契約

`assets/settings/defaults.tsv` (初期値の正典)、`tools/mk_settings_db.py` (tsv → DB)、
`build/assets.mk` / `build/image.mk` / `build/core_packages.yaml` (ビルド統合と媒体添付)、
`tools/mkpkg.py` (登録ファイルの欠損をエラーに) だけ。生成 DB は媒体 (FDD / CD) だけが持ち、
既存システムには tsv を通常配備する (TASK_S0 §3)。カーネル・KAPI・配備スクリプトは触らない。

試験: `python3 -B tools/tests/test_mk_settings_db.py` (45 件)。ホストのみ。エミュレータ・配備・`make`
(dry-run `-n` を除く) は実行していない。

### 実行済み RED → GREEN

**証拠を 2 つに分ける**: (a) 反例で落ちるところを実際に見た試験と、(b) 実装が先にあって
「今 通っている」だけの回帰試験。どちらなのかを書き分ける。

#### (a) 反例による RED (実装前、または実装を一時的に緩めて確認した)

| 試験 | RED の症状 | 直したもの |
|---|---|---|
| `BuildWiring` 3 件 | 結線が無い (assets.mk / image.mk / core_packages.yaml) | ビルド統合 |
| `MkpkgMissingFile.test_missing_file_is_error` | `exit 0` + `MINIMAL.PKG: 0 files, 43 bytes` | mkpkg: 欠損をエラーに |
| `test_glob_without_match_is_error` / `test_later_package_empty_glob_writes_nothing` | 0 件の glob を `no files, skipping` で飛ばし、正常側の `.PKG` を書いて `exit 0` | mkpkg: 展開 0 件も欠損 |
| `test_missing_defs_is_error` | 存在しない `--defs` を黙って無視して `exit 0` | mkpkg: 欠けた定義をエラーに |
| `RealPackageDefs.test_every_registered_glob_matches_something` | リポジトリ自身の登録に 0 件の glob が 3 つ (`assets/images/*.vbz` `*.vdp` `assets/manga/*.mgx`) → 厳格化した mkpkg では `make iso` が落ちる | `userland/package_defs.yaml` の死んだ 3 行をコメント化 |
| `Rejects.test_int32_overflow` | 範囲検査を外すと `2147483648` を**受理**する | int32 の範囲検査 |
| `Rejects.test_duplicate_key` | 重複検査を外すと `sqlite3.IntegrityError: UNIQUE constraint failed` の Traceback (理由が `path:line:` の形で出ない) | 重複検査 |
| `Rejects.test_cr_rejected` | CR 検査を外すと `text` 値の末尾 CR (`x\r`) がそのまま DB に入る。`int` 行は字句規則が拾ってしまうので、CR 規則だけが捕まえる反例 (text 行・コメント行) を試験に足した | CR 検査 |
| `Rejects.test_leading_zeros_accepted` / `test_leading_zeros_beyond_int32_rejected` | 先頭ゼロ 4301 桁の `int` (契約上は有効) が Python 3.14 の桁数制限で `ValueError` の Traceback になり、受理も理由付き拒否もできない | 変換の**前**に符号の後の `0` を畳み、畳んだ桁数が 10 を超えたら範囲外として拒否 |

- mkpkg の 4 件: 修正前の `tools/mkpkg.py` (feat/gui 6360618) に戻して `MkpkgMissingFile` を実行 →
  `Ran 7 tests, FAILED (failures=3)` と上表の症状を確認し、修正版へ戻した。
- tsv の 3 件: `tools/mk_settings_db.py` の当該検査を一時的に外して `Rejects` を実行 →
  `Ran 18 tests, FAILED (failures=3)`。確認後に元へ戻し (着地版との差分が空であることを確認)、
  全件 GREEN を再確認した。
- `RealPackageDefs` は `userland/package_defs.yaml` を戻して単独実行 → 0 件の 3 パターンを
  列挙して FAILED を確認し、コメント化した版へ戻した。
- `Rejects.test_text_255_boundary` は 1 回目に試験側の欠陥で落ちた (1 メソッド内で出力名 `out.db` を
  使い回し、直前の成功ビルドの残骸を「失敗なのに DB を作っている」と誤判定)。出力名を毎回変えて修正。
- 先頭ゼロの 2 件は試験を先に書き、着地版 (2adcb2e) の `tools/mk_settings_db.py` に対して
  `Rejects` を実行 → `Ran 20 tests, FAILED (failures=2)`
  (`ValueError: Exceeds the limit (4300 digits) ...`) を確認してから直した。

#### (b) 回帰試験 (実装を先に書いたので反例 RED を経ていない)

決定性 6 件、スキーマ 5 件、その他の規則違反の拒否 (不正 UTF-8 / NUL / key・scope 規則 / 列数 /
type / int の字句 / text 255B / blob の hex) と境界値の受理、`RealDefaults` 2 件。
これらは「今 通っている」以上のことは言えない。

#### GREEN

`Ran 45 tests — OK`。

### 見たもの (合格の中身)

- **決定性**: mtime の違う同内容 2 ファイル + 同じ epoch → 同一 sha256。同じ入力を 2 回 → 同一。
  epoch 違い / `SOURCE_DATE_EPOCH` / 既定 0 の順序も確認。入力の mtime は読んでいない。
- **スキーマ**: `meta(schema_version=1, created)`、`settings(scope,key,type,ival,tval,bval)` +
  `PRIMARY KEY(scope,key)` + `WITHOUT ROWID`、`page_size=1024`、`user_version=1`、
  `journal_mode=delete`。`integrity_check=ok`、`freelist_count=0` (VACUUM 後)。実物は 3 KB / 3 行。
- **規則**: int32 超過 (先頭ゼロ 4301 桁を含む) / int の字句 / 不正 UTF-8 / NUL (scope・text) / key 規則と 63B / scope 規則と
  63B / type / 重複 / 列数 / CR / text 255B 境界 (3B 文字を含む) / blob の奇数桁・空白・非 hex・
  8192 文字超 — すべて非ゼロ終了 + 理由 (`path:line:`) で、DB を作らない。境界値 (int32 端、key 63B、
  blob 4096B) は受理。末尾の空欄は NULL ではなく空の text として入る。
- **mkpkg**: 欠損 = 非ゼロ + `ERROR: <path> not found (package '<name>')`、途中まで書いた `.PKG` を
- **mkpkg**: 欠損 = 非ゼロ + `ERROR: <理由> (package '<name>')`。欠損は 3 種 — 登録ファイルが無い /
  glob の展開が 0 件 / `--defs` のファイルが無い。いずれも**全パッケージを先に解決してから**まとめて
  報告し、`.PKG` を 1 つも書かずに落ちる (後半のパッケージが欠損しても前半の `.PKG` を残さない)。
  簡易 parser は変えず、展開結果だけを見ている。
- **dry-run** (`make -n all` / `-n iso` / `-n images/os32_boot.d88`): `mk_settings_db.py` は 1 回だけ
  走り、`mkfat12.py` / `mkpkg.py` より前。`packages` の依存に結んだ既存入力
  (`programs` / `boot` / `vmkernel.lz4` / `unicode_bin` / `assets/fep.db`) も mkpkg より前に生成される。
- `python3 -B tools/check_constraints.py` → OK (規則 16 件)。

### 見ていないもの

- ゲストでの受入 T1 (媒体の中身を mount / mkpkg 一覧で確認、実機で `sqlite3` 読み出し) — `make` と
  配備が禁止のため未実施。ホスト側では `tools/mkfat12.py` に `/etc/settings.db=` を渡した
  smoke ビルドで FAT12 に 3 KB / 3 クラスタとして載ることまで確認した。
- 新規インストールでの seed (FDD の `install.bin` が `/kernel.bin` を要求する既存不整合、B10) は
  S3 の受入。本票では「媒体に入っている」までしか言えない。

票 [docs/archive/settings/TASK_S0.md](../../docs/archive/settings/TASK_S0.md)。
本書は **S0-K (KAPI v50 / `shm_write_row` の境界 / exec 回収順序)** 分。
S0-D (配備保護) と S0-T (初期値 tsv) は別票が同じファイルに節を足す。

走らせ方 (ホストのみ。`make`・配備・エミュレータ・ローカル AI は使っていない):

```
python3 -B tools/tests/test_kapi_db_v50.py            # 9 件 + 回収順の本文検査
python3 -B tools/tests/test_kapi_db_v50.py --target   # 上に i386-elf 単体コンパイル
python3 -B -m unittest discover -s tools/tests -p 'test_kapi_db_owned.py'
python3 tools/tests/test_vfs_fd_sqlite.py
python3 tools/tests/test_sqlite_groups.py
```

## K. KAPI v50 / 境界検査 / 回収順序 (S0-K、2026-09-13)

### 1. 何を実物で組んだか (S0-K)

`tools/tests/kapi_db_v50_host.c` は **実 `kapi/kapi_db.c` + 実 `lib/sqlite3/sqlite3.c`
+ 実 `lib/sqlite3/os32_sqlite_vfs.c` + 実 `fs/vfs_fd.c` + RAM バックエンド**
(`sqlite_groups_backend.h`) を組む。ホストのファイルシステムには触らない。
模型は次の 6 つ。それ以外はすべて実物を組む:

- `ring3_user_range_ok` — 許可帯と PTE はカーネルの番地とページテーブルに依存する。
  ホストでは 1 本の帯 + 1 枚の「非 present なページ」に見立てた等価な判定を置く。
  実物の帯判定は `exec/exec.c` にあり、CPL=3 の受入 (`userland/tests/db_v50_test.c`) が踏む。
- `MEM_SHM_BASE` — 試験側の配列へ向け、16KB の **後ろに 256B の番兵**を置く。
- `vfs_stat` / `vfs_rm` — RAM の fixture を見る試験側の実装。実物と同じく
  **中で `vfs_resolve_path` を呼ぶ**ので、相対名 + cwd の連結と切り詰めは再現する。
  障害注入の口 (`stat_fail_on` / `stat_fail_rc`) を持つ。
- `vfs_resolve_path` — `vfs_fd_sqlite_host.c` に **fs/vfs.c:52-121 を移植**した
  もの。連結 → `VFS_MAX_PATH` での切り詰め → `.` / `..` / 連続 `/` の正規化 →
  `VFS_MAX_PATH_DEPTH` (32) の成分表、という**順序と上限がそのまま**なので、
  「切り詰めた末尾が別の絶対名になる」も「溢れた成分が捨てられた後の `..` が
  保持済みの成分を消す」も再現する。mount 解決 (`vfs_route`) だけは模型のまま。
- `sqlite3_column_blob` / `sqlite3_column_text` — この **1 対だけ** 差し替えて
  「長さはあるのにポインタが返らない」(確保の失敗) を決定的に作る。実 SQLite では
  ホストの潤沢なメモリのせいで `sqlite3_step` の方が先に落ち、この形にできない
  (実機の MEMSYS5 384KB では accessor 側で起きうる)。
- ホストの `u32` / `i32` は `include/types.h` のとおり `unsigned long` / `long` で、
  ホストでは **64bit**。32bit の折り返しに依る判定はホスト幅で踏み直している。

### 2. RED → GREEN

| # | 対象 | RED (実際に落としたもの) | GREEN |
|---|---|---|---|
| 1 | v50 の 7 本そのもの | 実装前は `kapi_db_open_existing` 等が存在せず、`kapi_db_v50_host.c` は **リンクできない** (undefined reference)。**これは RED ではない** — 「対象の挙動が assertion で落ちる」ことを見せていないので、下の 2〜4 と 2b/2c/2d の反例が本来の RED。ここは経緯として残す | 9 ケース全通過 |
| 2 | `shm_write_row` の境界 (§1b) | 境界検査を `if (0 && ...)` で殺す → `FAIL shm_bound:359: kapi_db_step(h) == DB_STATUS_ERROR` (20000B の行が ROW を返す) | 検査を戻して `PASS shm_bound` + 番兵 256B が無傷 |
| 3 | exec 回収順序 (§1c) | `db_cleanup_owned` を元の (6) の位置へ戻す → `AssertionError: exec_reclaim_owned: db_cleanup_owned は vfs_close_owned より先` | 先頭へ移して PASS |
| 4 | 回収順序の**観測できる差** | `order_old` (FD を先に閉じる) で **後始末がバックエンドに届いた回数 = 2** | `order_new` (DB が先) で **21**。rollback の journal 読み戻しは生きた FD 越しにしか起きない |

### 2b. 実装レビュー 往復 1 (Codex、`08b4879`) の blocker 6 件 — RED → GREEN

反例はすべて `test_kapi_db_v50.py` に常設した。RED は **実装を 1 件ずつ元へ戻して**
採ったもの (実行した `FAIL` 行をそのまま写す)。

| # | blocker | ケース | RED |
|---|---|---|---|
| 1 | SHM にちょうど収まる TEXT/BLOB が欠落 ROW になる | `shm_exact` | writer を `len < remaining - 1` / `len < remaining` に戻す → `FAIL shm_exact:563: info->data_offset != 0` (16359B の TEXT が payload 無しの ROW) |
| 2 | journal の stat 失敗を不存在として SQLite に進む | `stat_faults` | stat の戻り値を NOTFOUND と区別しない実装に戻す → `FAIL stat_faults:610: kapi_db_open_existing("/f.db", 0) == -1` (I/O 障害なのに open が通る) |
| 3 | RW open の schema / I/O 障害が一律 CANTOPEN | `journal_mode` | `db_journal_mode_check` の戻りを `SQLITE_CANTOPEN` に潰す → `FAIL journal_mode:642: kapi_db_error_code(-1) == SQLITE_NOTADB` |
| 4 | stmt が無い `db_step` の DONE で診断が 0 に戻らない | `step_no_stmt` | 早期 DONE の `slot_note` を外す → `FAIL step_no_stmt:665: kapi_db_error_code(h) == SQLITE_OK` |
| 5 | 255B の path で本体を journal と誤認 | `path_len` | `journal_buf` を `VFS_MAX_PATH + 8` に戻す → `FAIL path_len:694: kapi_db_open_existing(longp, 0) == -1` (248B の path が通ってしまう) |
| 6 | 末尾の `\f` を複数 statement と誤判定 | (当時の) `sql_tail` | `sql_is_space` から `\f` を外す → `FAIL sql_tail:711: sql_is_space('\f') && sql_is_space('\r')`、続けて `"SELECT 1;\f "` が拒否される |

**6 は往復 2 でやり直した**。自前の空白表は 5 文字でも 6 文字でもずれる
(下の 2c の B1)。判定そのものを SQLite に委ねたので `sql_is_space` /
`sql_tail_is_blank` は消え、当時の `sql_tail` ケースは `sql_tail_sqlite` に置き換えた。

### 2c. 実装レビュー 往復 2 (Codex、`77d61b3`) の blocker 4 件 — RED → GREEN

| # | blocker | ケース | RED |
|---|---|---|---|
| B1 | 末尾判定が SQLite のトークナイザと一致しない | `sql_tail_sqlite` | 往復 1 の自前判定に戻す → `FAIL sql_tail_sqlite:766: kapi_db_prepare_only(h, "SELECT 1; \v") == 0` |
| B2 | 入力検証で拒否すると旧 stmt が bind 可能なまま残る | `prepare_replaces` | finalize を引数検証の後ろへ戻す → `FAIL prepare_replaces:829: kapi_db_column_int(h, 0) == 0` (拒否された prepare の後の step が前の INSERT を実行) |
| B3 | 列値の実体化が失敗しても欠落 ROW を成功で返す | `materialize_fail` | 長さだけ見る形に戻す → `FAIL materialize_fail:879: kapi_db_step(h) == DB_STATUS_ERROR` |
| B4 | journal 名の容量検査に cwd が含まれない | `resolve_len` | 解決をやめて入力名のまま検査する → `FAIL resolve_len:948: kapi_db_error_code(-1) == SQLITE_CANTOPEN` (251B の cwd で journal 名が切り詰められ本体に当たり `BUSY_RECOVERY`) |

**B1 の中身**: SQLite のトークナイザは CC_SPACE の run に入った**後**を
`sqlite3Isspace()` (6 文字、`\v` を含む) で走査するので `"; \v"` は受理するが、
`"\v"` で**始まる**末尾は `CC_ILLEGAL`。UTF-8 BOM は TK_SPACE。閉じていない
`/*` は空白にならない。自前の表ではこの 3 つを同時に写せないので、
`sql_tail_check` は **pzTail をもう一度 `sqlite3_prepare_v2` に渡し**、
「rc == OK かつ stmt が NULL」だけを単一 statement と認める。

**B3 の限界**: ホストの実 SQLite では `sqlite3_step` の方が先に NOMEM で落ちるため、
「step は通ったが accessor が NULL」という形を実メモリ圧では作れない
(`materialize_fail` の (c) がその経路)。決定的に踏むために
`sqlite3_column_blob` / `sqlite3_column_text` の **1 対だけ** を差し替えている。

### 2d. 実装レビュー 往復 3 (Codex、`769e1fc`) の blocker 1 件 — RED → GREEN

| # | blocker | ケース | RED |
|---|---|---|---|
| 1 | 切り詰め済みの絶対名を信頼して **別の DB** を開ける | `resolve_truncate` | `db_resolve_fits` の検査を外す → `FAIL resolve_truncate:1063: kapi_db_open_existing(evil, 1) == -1` (cwd `/tmp` + `"./"×122 + "a/../b.db"` が `/tmp/b` に化け、RW handle が返る) |

`fs/vfs.c` の `vfs_resolve_path` は VFS_MAX_PATH の作業領域に `cwd + "/" + input` を
**strlcat で切り詰めてから** `.` / `..` を畳む。だから溢れた入力は「短い別の絶対名」
として返り、**解決結果を見ても切り詰めは分からない**。そこで resolve の前に
`kstrlen(cwd) + 1 + kstrlen(path) + 1 <= VFS_MAX_PATH` (絶対名は cwd 抜き) を数えて
`SQLITE_CANTOPEN` で断る。正規化で短くなる入力も巻き添えで断る (安全側)。
ホストの `vfs_resolve_path` 模型は **fs/vfs.c と同じ順序** (連結 → 切り詰め → 正規化)
に書き直した (`tools/tests/vfs_fd_sqlite_host.c`)。順序が違うとこの反例は作れない。

### 2e. ユーザー承認の最終往復 (Codex、`c24f058`) の blocker 1 件 — RED → GREEN

| # | blocker | ケース | RED |
|---|---|---|---|
| 1 | パスの**深さ**超過で別の DB を開ける | `resolve_depth` | `db_resolve_fits` から深さの検査を外す → `FAIL resolve_depth:1124: kapi_db_open_existing(evil, 1) == -1` (`("/a"×32) + "/x/.." + ("/.."×31) + "/b.db"` が `/b.db` に化け、RW handle が返る) |

`fs/vfs.c` の成分表は `VFS_MAX_PATH_DEPTH` (32) 本しかなく、溢れた成分は
**黙って捨てられる**。捨てられた後ろに `..` があると、それは捨てられた成分では
なく **保持済みの成分**を 1 つ消す。上の入力は正しくは `/a/b.db` だが `/b.db` に
なる。長さは 167B なので往復 3 の検査では捕まらない。
そこで `db_resolve_fits` に **成分数**の検査を足した: 空成分と `.` は数えず、
`..` は 1 成分として数え (安全側)、相対名では cwd の成分も足して
`VFS_MAX_PATH_DEPTH` を超えたら `SQLITE_CANTOPEN`。resolver は変えていない。

### 2f. 実機 K2 の不合格 (2026-09-13、feat/gui `16bb298`) — 切り分けと穴埋め

実機の観測 (NP21/W、API v50、kselftest 87/0、cwd `/`、`fault_kill_count` 0):

```
> db_v50_test
db_v50_test: KAPI v50
  open failure code = 21
  FAIL: RW open of an existing db
db_v50_test: 5/6 passed, aborted
```

**21 = `SQLITE_MISUSE` が出た場所の絞り込み** (消去法。すべて `writable = 0` の
RO open の時点で成立していなければならない):

| 候補 | 判定 |
|---|---|
| `writable` が 0 / 1 以外 (`kapi_db.c:873`) | **除外**。試験は 0 と 1 しか渡さない。2 引数の受け渡しは同じ試験内の `db_exec` / `sys_stat` が成立しているので壊れていない |
| `vfs_stat` 失敗 (`:908`) | **除外**。CANTOPEN(14) か IOERR(10) にしかならない |
| 0 バイト (`:912`) | **除外**。NOTADB(26) |
| hot journal (`:923`) | **除外**。BUSY_RECOVERY(261) |
| `db_resolve_fits` / journal 名 / 解決結果 (`:890 :896 :918`) | **除外**。すべて CANTOPEN(14) |
| slot 満杯 (`:936`) | **除外**。FULL(13) |
| `sqlite3_open_v2` の失敗 (`:955`) | **除外**。ここへ着くには `vfs_stat` が「存在する」と答える必要があるが、同じ試験の assertion 3 (`sys_stat(missing) != 0`、`sys_stat` の target は `vfs_stat` そのもの) が**通っている** = NOTFOUND |
| `db_journal_mode_check` (`:973`) | **除外**。RW 専用で、RO open では通らない |
| `db_error_code(-1)` の owner 範囲外 | **除外**。owner を書くのは `exec/appslot.c` だけで、値は ID の池 (0〜5) < `DB_OWNER_SLOTS` (6) |
| **path のポインタ検証 (`:881` → `db_user_str_copy` → `ring3_user_range_ok`)** | **残るのはこれだけ** |

**ホスト側で分かったこと**:

- `paging_addrspace_pte_flags` は**シロに見えた**が、それは模型の組み方が
  実配置と同じになっていたから (2g で訂正)。実機ではここが犯人だった。
- **穴**: それまでのケースは全部 `host_cpl3 = 0` (= CPL=0 の直呼び) で走っていて、
  `db_user_str_copy` → `ring3_user_range_ok` の経路が **1 度も踏まれていなかった**。
  新ケース `cpl3_paths` で塞いだ (下の表)。実機と同じ形の拒否も踏む。

**次の 1 回で確定させるための計器** (どれも KAPI にしない。`fault_kill_count` と
同じくカーネルシンボルを `emu_read_mem` で読む):

| シンボル | 意味 |
|---|---|
| `ring3_range_reject_count` | 検証が断った回数 (正常系では増えない) |
| `ring3_range_reject_last` | 理由 1=NULL / 2=overflow / 3=`g_cur_app` が 0 / 4=帯外 / 5=非 present / 6=非 USER |
| `ring3_range_reject_addr` / `_page` | 断ったポインタとページ |
| `ring3_range_reject_heap_top` | そのとき `ring3_ptr_ok` が見ていた帯の上端 |

加えて `kapi_db.c` は「ポインタ検証の拒否」と「引数そのものの拒否」で
**SHM のエラー文を分けた** ので、`db_last_error()` だけでも切り分けられる。
`db_v50_test.c` は失敗のたびに `db_error_code(-1)` と `db_last_error()` を出し、
土台 DB の `sys_stat` の size と **RO open の可否** も先に出すようにした
(RW 固有の段 = `journal_mode` の照会を切り分けるため)。

### 2g. 実機 K2 の原因 (2026-09-13、計器版 `18682d4`) — 控えから PT を選んでいた

計器を入れた版を配備して得た値:

```
  open failure code = 21
  fixture /tmp/db_v50.db stat=0 size=2048
  FAIL: RO open of an existing db    open_fail=21 last_error=invalid handle
  FAIL: RW open of an existing db    open_fail=21 last_error=invalid handle
ring3_range_reject_count = 4   _last = 5 (非 present)
_addr = 0x50140e   _page = 0x501000   _heap_top = 0x7bf000   fault_kill_count = 0
```

2f の消去法どおり **path のポインタ検証**が犯人で、しかも理由は
「アプリ自身の .rodata (ロード先 0x500000 の次のページ) が非 present」。
その番地は CPL=3 のアプリが走っている最中のコードの隣で、カーネルは同じ
CR3 で読めている (`db_open` の `kstrncpy` も `kprintf` も通っている)。

**なぜ `as` 経由がずれたか**: `paging_addrspace_pte_flags` は PDE が指す PT を
辿らず、`struct addrspace` の**控え**から PT を選んでいた:

```c
if (pdi >= as->app_pde && pdi < as->app_pde + as->app_pde_count)
    pt = (u32 *)as->app_pt_phys[pdi - as->app_pde];   /* 控え */
else
    pt = page_tables[pdi];                            /* master の PT */
```

これは「exec がどの PT を使ったか」の**別勘定**で、MMU が実際に辿る表
(CR3 → PDE → PDE が指す PT) とは独立に外れうる。外れた先が master の PT
(`page_tables[APP_BAND_PDE]`) なら、アプリ帯は `clear_app_band` と per-app 物理化
の後なので **USER も present も持たない** — 観測された「非 present」そのもの。

**直し**: `paging_current_pte_flags(virt)` を新設し、**MMU と同じ辿り方**をする
(`paging_current_cr3()` → `PD[pdi]` → PS なら拒否 → `PDE & 0xFFFFF000` が指す PT
→ `PT[pti]`、返すのは PDE と PTE の論理積)。控えを一切見ないので、
exec がどう張ろうと答は MMU と一致する。`paging_addrspace_pte_flags` は
**削除**し、`ring3_user_range_ok` から `AppSlot.as` への依存も外した。

**ホスト試験の訂正**: `tools/tests/paging_bounds_host.c` の項を、exec の実配置を
写した fixture (`create_n` → `clear_app_band` → per-app 物理 3 領域 → shlib 相当の
RO+USER) に置き換え、**`host_cr3 = as.pd_phys` にしてから** `paging_current_pte_flags`
で歩く形にした。決め手の 1 行は「**PDE が指す PT だけを差し替えると答が変わる**」:
控えから選ぶ実装はここで古い PT を読み続ける。

| RED | 実際に落としたもの |
|---|---|
| PT の選び方を控え (`page_tables[pdi]`) に戻す | `FAIL: (flags & (PTE_PRESENT \| PTE_USER)) == (PTE_PRESENT \| PTE_USER)` — 実機とまったく同じ「.rodata が非 present」 |

**もう 1 件 (診断文言)**: `db_v50_test.c` が `db_last_error(0)` を読んでいた。
これは **slot の状態**を返す口なので、open が slot を掴む前に失敗すると
必ず "invalid handle" になる。open 失敗の理由は SHM の
`DB_ResultHeader.error_offset` の先にあるので、試験側を
`shm_base` から読む `shm_error()` に直した。

### 2h. 実機 K2 の決着 (2026-09-13、CR3 版 `d2322f0`) — 表は歩けない

CR3 → PDE → PDE が指す PT に直した版を配備しても **結果は変わらなかった**:

```
  FAIL: RO open of an existing db   open_fail=21 shm_error=path pointer rejected by the range check
ring3_range_reject_count = 4  _last = 5 (非 present)  _addr = 0x501427  _page = 0x501000
fault_kill_count = 0   kselftest 87/0
```

**根拠のある結論** (コードで確定できた):

- `paging_current_cr3()` は `mov %%cr3, %0` で**レジスタを直接読む**
  (`kernel/paging.c`)。控えではないので疑い (1) は外れ。
- 疑い (2) が当たり。カーネルはページテーブルを「**物理番地 = 仮想番地**」で
  読む。ところが PD もアプリ PT も `pgalloc` から取られ、
  **`PGALLOC_BASE` は 0x400000 = `MEM_APP_BAND_BASE`** (`kernel/pgalloc.h`)
  — つまり **アプリ帯そのもの**。
  `exec_run` は CPL=3 アプリでは `pgalloc_mark_used` で帯を押さえない
  (`exec_cpl0_claim` は CPL=0 の子だけ) ので、PD / PT の物理はふつうに
  0x4xxxxx〜0x7xxxxx から出る。
  そして `paging_addrspace_clear_app_band()` + per-app 物理化の後、
  **アプリの PD ではその仮想番地は per-app 物理へ張り替わっている**。
  だから syscall 中 (CR3 = アプリ PD) に表を辿ると、PT のつもりで
  **アプリ自身のデータ**を読む。中身のビット 0 はたいてい 0 なので
  「非 present」と答え、ページ自体は present なので **#PF も起きない**
  (`fault_kill_count = 0` と完全に整合)。
- `AppSlot.as` の控えから引いても PDE から引いても**同じ物理**を指すので、
  2 つの実装が同じ壊れ方をしたのは当然だった。表を歩けるのは master CR3 の
  下だけで、syscall の途中で CR3 を差し替えるのは割に合わない。

**採った方針 (B)**: PTE (present / USER) の検査を**外す**。範囲検査は
NULL / `p + len` の overflow / **各ページが `ring3_ptr_ok` の許可帯**だけ。
許可帯の中の非 present ページ (guard / 未マップ sbrk) をカーネルが写すと
#PF になるが、それは既存のフォールトガード (`ring3_in_syscall`) が呼び手を
kill する — `kprintf` の可変長 `%s` など他の KAPI と同じ既定の扱いで、
カーネルの整合は保たれる。契約 (FOUNDATION §2-6 の「untrusted pointer /
長さ / 境界を CPL3 経路で検証」) は帯 + 長さで満たす。
票 §1a と K2 の「guard をまたぐ範囲が -1」は **「kill される」** に改めた
(同じプロセスでは踏めないので K2 からは外した)。

**前回の訂正 ([V4])**: 2f で「`paging_addrspace_pte_flags` はシロ」と書いたのは
**誤り**だった。当時の host fixture は控えと実配置が一致する組み方しかして
おらず、しかも**カーネルと同じ「物理 = 仮想」の罠を再現していなかった**
(ホストでは `host_cr3` を差し替えても、テーブルはホストのアドレス空間に
そのまま見えている)。2g で「控えから選んでいたのが原因」と書いたのも
**半分だけ正しい** — 控えかどうかではなく、**アプリの PD の下で表を歩いたこと**
が原因だった。

**残した番人**: `tools/tests/paging_bounds_host.c` の項を
「`PGALLOC_BASE == MEM_APP_BAND_BASE` であること」と「exec と同じ順序で組んだ
AS では、アプリ PT の物理番地がアプリ PD の下で**別の物理**に解決される
(= 歩けない)」を固定する形に置き換えた。`kernel/paging.h` にも
「走っているアプリの PD を、そのアプリの syscall 中に歩いてはならない」を
警告として書いた。計器 (`ring3_range_reject_*`) はそのまま残す
(方針 B が効いていれば `count` は 0 のままになる)。

### 3. ケース一覧 (`test_kapi_db_v50.py`)

| ケース | 見るもの |
|---|---|
| `open_existing` | RO / RW の欠損 DB が**作られない**、0 バイト = `NOTADB`、hot journal = `BUSY_RECOVERY` かつ **journal が消えない**、`:memory:` / `file:` / 空 / `writable=2` / NULL の拒否、RO 接続で書けない、成功で「直前 open 失敗」が 0 に戻る |
| `prepare_only` | SELECT の先頭行が進まない / DML が実行されない、複数 statement の拒否、末尾の空白・`--`・`/* */`・`;` は可、NUL 込み 1024B ちょうどは可・1 バイト超は**切り捨てず**拒否 |
| `binds` | prepare 前 / step 後は不可、1-based と範囲外、負長、text 256B・blob 4097B の拒否、0B の text/blob が `typeof` で `text`/`blob` (NULL ではない)、**4096B blob の往復** |
| `error_code` | 範囲外 / 未使用 slot = `MISUSE`、失敗の保持と取得で消えないこと、成功で 0 に戻ること、**finalize / close が上書きしない**こと、close 後も再利用まで残ること、owner 別の `-1` 欄が混ざらないこと |
| `shm_bound` | 純関数 `shm_row_fits_n` のちょうど / 1 バイト超 / descriptor だけで溢れる列数、実接続で 20000B の行が `-1` + `SQLITE_TOOBIG` + 番兵無傷 |
| `user_range` | CPL=0 は素通し、CPL=3 は帯外 / 帯末尾またぎ / overflow を拒否。**帯の中は PTE を見ないので通る** (実機では写した瞬間に #PF → kill)。NUL 無し path の拒否、bind 後にユーザ側を書き換えても値が変わらない (スクラッチへ写っている) |
| `owner_isolation` | 子 owner の回収で親の接続と実行中 stmt が無事 |
| `order_new` / `order_old` | 上の RED → GREEN の 4 |

| `shm_exact` | 1 列の行で TEXT `room - 1` / BLOB `room` が**書ける** (`data_offset != 0`、長さ一致)、+1 は `-1` + `TOOBIG`、番兵は 4 とも無傷 |
| `stat_faults` | journal / 本体の stat が NOTFOUND 以外で落ちたら `IOERR` で断り **SQLite を呼ばない** (バックエンド呼び出し回数で確認)、NOTFOUND だけが `CANTOPEN` |
| `journal_mode` | 非空の非 DB を RW で開くと `NOTADB` (CANTOPEN に潰れない)、正常な DB では `db_journal_mode_check` が `SQLITE_OK` |
| `step_no_stmt` | prepare_only 失敗の後の `db_step` が DONE を返したら診断が 0 に戻る |
| `path_len` | `<path>-journal` が `VFS_MAX_PATH` に収まる 247B は開ける、248B は本体があっても `CANTOPEN` |
| `transient` | 同じスクラッチを 2 本目の bind で上書きしてから step しても 1 本目の値が残る (`SQLITE_TRANSIENT`) |
| `sql_tail_sqlite` | SQLite が受理する末尾 (`; \v` / BOM / `-- c` / `/* c */` / `;;` / `\f\r\n\t`) は通り、読めない末尾 (`/*` 未閉じ、先頭 `\v`) と 2 本目の statement は拒否。拒否のあと stmt が残らない |
| `prepare_replaces` | 空 SQL / 上限超過 / NULL / tail あり のどれで拒否しても旧 stmt は消えており、続く `db_step` は DONE で**何も実行しない** |
| `materialize_fail` | (a) 収まる BLOB は取れる (b) accessor が値を返せないとき ERROR + `NOMEM` + 部分 ROW なし + stmt は生存 (c) MEMSYS5 を締めて step 側で落ちても同じく部分 ROW なし |
| `resolve_len` | (a) cwd 251B で journal 名が切り詰められ本体に衝突する形でも `CANTOPEN` (b) 解決名 248B は `CANTOPEN` (c) 247B は開ける (d) SQLite には解決後の絶対名だけが渡る |
| `resolve_truncate` | 模型が実物と同じ順序で `/tmp/b` に化けることを見せたうえで、その入力が `CANTOPEN` で断られる。絶対名の 255 文字 / 256 文字の境界、短い相対名は従来どおり通る |
| `cpl3_paths` | **CPL=3 の規則を通した** open / prepare。範囲に収まる path / sql は通り、NUL まで届かない範囲は `MISUSE` + 「range check」の診断で断られ、引数そのものの拒否 (`:memory:`) は別の文言になる |
| `resolve_depth` | 模型が実物と同じ捨て方で `/b.db` に化けることを見せたうえで `CANTOPEN`。`db_path_depth` の数え方 (空 / `.` を除く、`..` は 1)、深さちょうど 32 は開ける・33 は断る、相対名では cwd の成分も数える |

### 4. ホストでは踏めなかったもの ([V4])

- **descriptor 領域だけで 16KB を溢れさせる列数の行**。溢れるには
  `(16384 - 12) / 12 = 1364` 列より多くが要るが、この build の
  `SQLITE_MAX_COLUMN` は **100** (`lib/sqlite3/os32_sqlite_config.h`) なので
  実接続では到達できない (SQL の 1024B 上限より前にこちらで止まる)。
  純関数 `shm_row_fits_n` の算術だけで覆ってある。
- `p + len` の **32bit** overflow。`include/types.h` の `u32` はホストでは `unsigned long`
  (64bit) なので、ホスト幅の端で同じ経路を踏むように書き換えてある。
- **rollback が本体ファイルを縮めること**。`os32 SQLite VFS` の `xTruncate` はまだ
  no-op 成功 (票 F3a が未実施) なので、どちらの回収順でもサイズは戻らない。
  だから順序の判定は「戻り値が成功か」ではなく「後始末がバックエンドに届いたか」で行う。
- 許可帯と PTE の**実物**の判定 (`ring3_ptr_ok` / `paging_current_pte_flags`)。
  ホストにページテーブルが無い。CPL=3 の受入 `userland/tests/db_v50_test.c` (K2) と
  ブート時の `kselftest` が実機側の担当で、**どちらもまだ実行していない**
  (コーダーは `make`・配備・エミュレータを行わない)。
  K2 の PTE 検査ケースは実装レビュー 往復 1 の指摘で、**許可帯の外**を指す番地から
  **許可帯の中の未マップページ** (`kapi->sbrk_heap_limit` = guard_a の先頭) へ
  置き換えた — 前者は `ring3_ptr_ok` だけで落ちるので PTE 検査を消しても通ってしまう。
- **「長さ 0 + NOMEM」の枝を実メモリ圧で**。`sqlite3_column_bytes` が 0 を返すのは
  正当な 0 バイト値と区別できないので根拠は `sqlite3_errcode` だけだが、実 SQLite では
  そこへ至る前に step が落ちる。`materialize_fail` の (b3) は `sqlite3_errcode` /
  `sqlite3_extended_errcode` を差し替えて決定的に踏んでいる。
- **実メモリ圧での「step は通ったが accessor が失敗」**。ホストの実 SQLite では
  `sqlite3_step` の方が先に NOMEM で落ちる (`materialize_fail` の (c) で確認)。
  実機の MEMSYS5 (384KB) で accessor 側が落ちる形は、accessor を 1 対だけ
  差し替えた (b) で決定的に踏んでいる。
- `vfs_route` の **mount 解決**。模型は path をそのまま 1 つのバックエンドへ渡す。
  `vfs_resolve_path` の方は fs/vfs.c から移植してあるので、切り詰めも深さ超過も
  実物と同じ形で踏めている (`resolve_truncate` / `resolve_depth`)。
- `PDE.PS` (4MB ページ) の経路。この OS は一度も 4MB ページを張らないので
  ホストでも実機でも作れない。`paging_current_pte_flags` は**明示的に**
  非 present 扱いで断る (安全側) というコードとコメントだけがある。

### 5. 実機の結果 (PM / テスター、2026-09-13、配備 4 回目 `8162a2c`)

- `db_v50_test` PASS 41/41。`ring3_range_reject_count` = 2 (どちらも BAND = 4、0x7fffff の帯末尾またぎ = 試験が意図した拒否)。`fault_kill_count` = 0。kselftest 87 / 0、regress 6 / 6。
- 方針 B (帯 + 長さのみ) で実機の open が MISUSE を返す症状は消えた。§4 の「ホストでは踏めなかったもの」のうち、帯内の非 present ページの kill は本票では踏んでいない。
