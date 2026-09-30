# S3 — リカバリ (`install --recover-settings`) と `cfg import` のホスト TDD 記録

票: [`docs/archive/settings/TASK_S3.md`](../../docs/archive/settings/TASK_S3.md)、
契約の正典は [`docs/archive/settings/S0_FOUNDATION.md`](../../docs/archive/settings/S0_FOUNDATION.md) §6
(明示リカバリ契約) と [`docs/archive/settings/TASK_S0.md`](../../docs/archive/settings/TASK_S0.md) §D (配備保護)。

3 票 (S3-I / S3-C / S3-D) が同じファイルに書くので、節を票ごとに分ける。
**自分の節だけ**を書き、他の節には触れないこと。

- `## I.` — `install --recover-settings` / `--revert-settings` (S3-I)
- `## C.` — `cfg import` と最小 JSON reader (S3-C)
- `## D.` — 通常配備の保護にリカバリ生成物の名前を足す (S3-D)

---

## D. リカバリ生成物の配備保護 (S3-D) — 2026-09-13

### D.1 何を足したか、なぜ要るか

S0-D の配備保護は `/etc/settings.db` `-journal` `-wal` `-shm` `.bak` の 5 名しか
知らない。S3 のリカバリは同じ `/etc` に**残り 7 名**を作る (票 §1b の 9 名から
本体と `-journal` を除いたもの):

| 名前 | 中身 | 通常配備が掴むと何が起きるか |
|---|---|---|
| `settings.db.bak` | 切替前の本体の写し | (S0-D で既に保護済み) |
| `settings.db.bak-journal` | 切替前の journal の写し | 対が分離し、revert が「DB だけ・journal 無し」に戻す |
| `settings.db.failed` / `-journal` | revert が退避した「失敗した世代」 | 手動復旧の材料が消える |
| `settings.db.new` / `-journal` | 検証中のコピー (他人の生成物) | 隔離接続が掴む inode を配備が消す (票 §1f 手順 4) |
| `settings.db.recover-state` | phase の印 (`backup` / `switching` / `switched` / `reverting` / `failed` / `done`) | 門が効かなくなり、次の recover が壊れた本体を新しい `.bak` にして**元の唯一の写しを失う** |

つまり `.bak*` と `.recover-state` を 1 つでも失うと「元へ戻す」経路そのものが
消える。`.new*` / `.failed*` は他人の生成物なので、そもそも配備が作ることも
消すこともしてはいけない。

足した先は 2 か所だけ (新しい判定は増やさない。S0-D の 2 段判定にそのまま乗る):

- `tools/deploy_protect.py` の `PROTECTED_BASENAMES` (5 → **11 名**)。
  名前規則 (`name_is_protected`)、実体検査の収集対象 (`_protected_identities`)、
  試験側の `FakeRun.touched_protected` が同じ集合を参照しているので、
  nhd_deploy (manifest / HostDrv 丸写し / copy / copy-all / rm / remove_partial) /
  hostdrv_deploy (manifest / clean) / prune_stale (prune_nhd / prune_hostdrv) の
  **全経路**が 1 か所の変更で揃う。
- `userland/system/hsync_protect.inc` の `hsp_protected_names` (5 → **11 名**)。
  名前規則 (`hsp_name_protected` / `hsp_ancestor_protected`) と、hsync.c が
  `/etc` を `sys_ls` で列挙して実在名を拾うときの一致判定
  (`hsp_is_protected_basename` = 実体規則の入口) が同じ表を見る。

付随して `userland/system/hsync.c` の `HS_MAX_PROT` を **16 → 24**。これは
「`/etc` の実在名を何件まで集めるか」の上限で、越えると hsync は守れないとして
**同期を拒否する**。表が 5 名のときは余裕 11 だったが、リカバリ途中の `/etc` は
9 名 + `-wal` / `-shm` = 11 名になりうるので、16 のままだと大文字小文字違いの
別名が 5 つあるだけで通常同期が止まる。余裕を元と同じだけ保つ。

### D.2 試験の範囲と遮断

- `tools/tests/test_deploy_protect.py` — S0-D と同じ土台 (temp dir + mock)。
  `subprocess.run` は `FakeRun` に差し替わり、`mount` / `umount` / `losetup` /
  `mkfs.ext2` / `e2fsck` / `taskkill.exe` が呼ばれたら AssertionError で試験が
  落ちる。実配備・sudo・エミュレータ・`make` には一切触れない。
- `tools/tests/hsync_protect_host.c` (`test_hsync_protect.py` から実行) —
  `userland/system/hsync_protect.inc` を**実物のまま** `#include` してホストで
  走らせ、同じソースが `i386-elf-gcc` + PROGRAM_FLAGS でも通ることを別に見る
  ([C1] C89/GNU89、`-Werror`)。

新しい節は S3-D の 2 クラスだけで、既存 142 件 / 57 チェックには手を入れていない。

### D.3 RED → GREEN

RED は**試験だけ先に入れ**、`tools/deploy_protect.py` / `hsync_protect.inc` /
`hsync.c` を feat/gui `b1ceac6` の版 (5 名) に戻して取った。

```
RED  : test_deploy_protect.py   Ran 164 tests — FAILED (failures=21)   143 passed
       hsync_protect_host.c     114 checks — FAIL (39 failures)
GREEN: test_deploy_protect.py   Ran 164 tests — OK
       hsync_protect_host.c     114 checks — PASS (0 failures)
       + HOST GNU89 -Werror COMPILE PASS / TARGET i386-elf GNU89 COMPILE PASS
```

件数: Python 142 → **164** (+22)、C 57 → **114** (+57)。

RED で落ちた内訳 (Python 21 件):

| 試験 | RED の症状 |
|---|---|
| `S3RecoveryJudgement.test_table_has_all_recovery_names` | `PROTECTED_BASENAMES` に 6 名が無い (件数 5) |
| `…test_name_rule_covers_each_recovery_name` | `/etc/settings.db.recover-state` 等が名前規則に当たらない |
| `…test_uppercase_and_mixed_case_recovery_names` | 大文字 / 混在の別名も素通り |
| `…test_missing_recovery_name_is_still_protected` | 欠損した 7 名を通常配備が**作れて**しまう |
| `…test_symlink_alias_to_recovery_file` / `…dangling…` / `…hardlink…` | symlink / dangling / hardlink の別名から `.recover-state` 等に届く |
| `…test_protected_identities_collects_recovery_files` / `…uppercase_on_disk` | 実体検査 (inode 比較) の収集対象に入らない = 別名経由の上書きを拾えない |
| `…test_recovery_name_as_ancestor` | `/etc/settings.db.bak-journal/` が残骸ディレクトリのとき中へ書ける |
| `S3RecoveryDeployPaths.test_nhd_manifest_sync_keeps_recovery_generation` | manifest 配備が 7 名を別内容で上書き |
| `…test_sync_from_hostdrv_keeps_recovery_generation` | **通常同期**で HostDrv の別世代 (`.bak-journal` / `.recover-state` / `.failed`) が NHD の世代を潰した |
| `…test_sync_from_hostdrv_keeps_uppercase_recovery_names` | 宛先が大文字の実在名でも小文字の別世代で潰れた |
| `…test_sync_from_hostdrv_does_not_create_missing_recovery_files` | 宛先に無い 7 名を通常同期が**作った** |
| `…test_hostdrv_manifest_sync_keeps_recovery_generation` | HostDrv 配備が 7 名を上書き |
| `…test_hostdrv_clean_keeps_recovery_names` | `clean` が 7 名を消した |
| `…test_prune_nhd_keeps_recovery_names` / `…prune_hostdrv…` | 掃除が 7 名を stale として消した (実際に消した数が 8 件) |
| `…test_nhd_cli_copy_cannot_write_recovery_names` / `…rm…` | CLI から 7 名を書ける・消せる |
| `…test_recovery_directories_are_not_created` | `deploy.yaml` の `directories` に紛れると `etc/settings.db.new/` を mkdir した |

`S3RecoveryJudgement.test_near_miss_names_are_not_protected` は RED でも GREEN でも
通る (接頭一致で拾わないことの固定。`settings.db.bak2` / `.recover` /
`.recover-state.old` / `settings.dbbak` / `/etc/sub/settings.db.bak` /
`/settings.db.bak` は**通常配備の対象のまま**)。

RED で落ちた C のチェック (39 件) は、6 名 × 6 形 (名前規則 / `/etc` 列挙の一致 /
大文字の実在名 / 大文字の名前規則 / 祖先 / 正規化を挟んだ形) + `hsync -f etc`
連結 2 件 + 表の件数 1 件。`settings.db.bak` は S0-D で入っているので 6 形とも
RED から通る。

### D.4 「通常同期の前後で不変」の測り方

票が求めた形は `S3RecoveryDeployPaths` の
`test_sync_from_hostdrv_keeps_recovery_generation` に置いた。

1. 宛先 (NHD 側) の `etc/` に**ゲスト世代**の 7 名を置く (`GUEST_GEN`)。
2. HostDrv 側に**別世代**の `settings.db.bak-journal` / `.recover-state` /
   `.failed` を置く (`OTHER_GEN`、中身は別) + 通常のファイル
   (`etc/settings.tsv`、`bin/sh.bin`)。
3. `nhd_deploy.do_sync_from_hostdrv()` を stdout を捕まえて走らせる。
4. 判定: (a) 7 名の**存在一覧**が不変、(b) 7 名の **sha256 が不変**
   (`assertRecoveryIntact`)、(c) 出力に `protected:` が出る、
   (d) `FakeRun.touched_protected()` が空 = `cp` / `rm` / `mkdir` が
   保護対象名を宛先にしていない、(e) 通常のファイルは従来どおり写る。

大文字違いは `test_sync_from_hostdrv_keeps_uppercase_recovery_names` が
「宛先に `SETTINGS.DB.RECOVER-STATE` があり HostDrv に小文字の別世代がある」形で
見ている (上書きしない、かつ小文字の別名を**新規に作らない**)。
symlink / hardlink の別名は `S3RecoveryJudgement` の 3 件が判定そのものに当てて
いる (配備ツリーに symlink があれば `check_tree` が配備全体を拒否するので、
経路側ではなく判定側で固定する)。

### D.5 この試験が言えないこと ([V4])

- **実機 / エミュレータでの受入は未実施**。コーダーは配備・エミュレータ・`make`
  を実行しない。`make deploy` / `make deploy-nhd` / ゲストの `hsync` を実際に
  走らせた結果は何も言っていない。
- `hsync` の**実体規則** (`sys_stat` の inode 比較、`scan_protected_entities` /
  `is_same_as_protected`) はホストでは踏んでいない。純関数に出せる
  `hsp_is_protected_basename` までが範囲。`HS_MAX_PROT` を 24 にした効果
  (リカバリ途中の `/etc` で同期が止まらないこと) も**ゲストでは未確認**。
- `hsync.c` は `test_hsync_protect.py` が `i386-elf-gcc` でコンパイルが通ることを
  見るだけで、リンク・実行はしていない。`HS_MAX_PROT` 変更分の static 領域
  (+576 B) が外部プログラムの帯域に収まることは机上 (`make external` は PM)。
- S0-D から引き継いだ限界はそのまま: bind mount による `<root>/etc` の別名は
  検出できない。配備ツリーに symlink があれば配備全体を拒否するだけで、
  「symlink を含むツリーへ安全に配備できる」とは言っていない。
  `/etc/settings.db*/` と `/lost+found/` の**中**は走査していない。
- リカバリ本体 (S3-I) の各段が実際にこの 7 名を作る / 消すことは、この節では
  一切検査していない (§I の担当)。ここが固定したのは「**通常配備がその名前を
  掴まない**」ことだけ。

---

# S3 — リカバリのホスト TDD 記録

票: [`docs/archive/settings/TASK_S3.md`](../../docs/archive/settings/TASK_S3.md) 第 5 版。
契約の正典は [`S0_FOUNDATION.md`](../../docs/archive/settings/S0_FOUNDATION.md) §6
(明示リカバリ契約)、[`TASK_S2.md`](../../docs/archive/settings/TASK_S2.md) §1-1 (b) の meta 検査 SQL と
§1-7 (f) の rename 両名検査、[`TASK_S0.md`](../../docs/archive/settings/TASK_S0.md) (配備保護)。

---

# 節 I — `install --recover-settings` / `--revert-settings` (票 S3-I §1)

走らせ方:

```bash
python3 -B tools/tests/test_install_recover.py             # 14 ケース
python3 -B tools/tests/test_install_recover.py --target    # + i386-elf の -Werror コンパイル
python3 -B tools/tests/test_install_recover.py --sanitize  # + ASan
python3 -B tools/tests/test_install_recover.py happy chain # ケース指定
```

コーダーの範囲はホスト TDD までなので、**ゲスト受入 (票 §4 の I1〜I7) は未実行** ([V4])。
`make all` / `make check` / 配備 / エミュレータも触っていない。`make check` への登録行は
報告に書くだけで、`build/sdk.mk` には PM が入れる。

## I-0. 組み方

`tools/tests/install_recover_host.c` は `cfg_host.c` / `kapi_db_v50_host.c` と同じ作法で
「実物だけ」を組む。

| 実物 | 模型 (その形でしか作れないものだけ) |
|---|---|
| `userland/system/install_recover.inc` (install.c が `#include` するのと同じソース) | `exec` のポインタ検証 `ring3_user_range_ok` |
| `kapi/kapi_db.c` (KAPI v50 の本物 — `db_open_existing` / `db_prepare_only` / `db_step` / `db_close` / `db_error_code`) | SHM の置き場 (`test_shm`) |
| `lib/sqlite3/sqlite3.c` + `lib/sqlite3/os32_sqlite_vfs.c` | `stat` の三値 / UNKNOWN / `st_ino` 共有 / `st_dev` |
| `fs/vfs_fd.c` (`vfs_open` / `vfs_read_fd` / `vfs_write_fd` / `vfs_close`) + RAM backend | `rename` の 2 つの中途半端、`read` の負 / short write / 中身違い |
| — | `unlink` / `sync` / `db_close` / `db_open_existing` の失敗 |

ホストのファイルシステムには 1 バイトも触らない。マスタ (`/etc/settings.db`) も RAM fixture の
上に実 SQLite で毎回作り直す (`meta` 1 行 + `settings` 12 行)。

回復モードが呼ぶ KAPI は 1 枚の関数ポインタ表 `RecoverOps`
(`userland/system/install_recover.inc` の先頭) に集めてある。ゲストの実体は `install.c` の
`rop_*` (`api->sys_*` / `api->db_*`)、ホスト試験は同じ表を贋物で埋める。
**本体は 1 行も分岐していない** (`#ifdef` も無い)。

`st_dev` の復号は `fs/vfs.c` の `vfs_mount_dev_of` と同じ式 `(dev_type << 8 | unit) + 1`
(種別は `fs/vfs.h` の `VFS_DEV_HD` = 0 / `VFS_DEV_FD` = 1)。外部プログラムからカーネルヘッダは
見えないので `.inc` 側に写しがあり、試験も同じ式で値を作って**両方が同じ規則で動くこと**を見る。

## I-1. ケース (14 本)

| ケース | 票の段 | 何を固定するか |
|---|---|---|
| `media` | 1a | `drive` は `hd0` だけ / HDD ブートからの実行を拒否 (受入 I1、DB は不変) / `st_dev == 0` も拒否 / `/hd0` が hd1・FDD なら `target /hd0 is not hd0` / 未マウントなら回復専用に `sys_mount` / mount 失敗で何もしない |
| `scan` | 1b | 9 名の三値。UNKNOWN を「無い」に丸めない (`stat failed (<name>)`) / `(st_dev, st_ino)` の共有で停止 (何も消さない) / `st_ino == 0` は判定不能で停止 / `.new` `.new-journal` の残骸で停止 (消さない) |
| `gate` | 1b | 印の phase の白リスト。`backup` / `switching` / `switched` / `failed` / `reverting` は全部停止、`done` と印無しだけ通る / 壊れた印は `recover-state unreadable` で停止 (消さない) / **`phase=done\ngarbage` / 埋込み NUL / 末尾 LF 無しも受理しない** (往復 1 の B5) / 印の書式が往復する / 印の open・short write・読み戻し不一致はすべて失敗 / **4 つの phase 更新 (backup / switching / switched / done) の失敗**はどれも `done` に化けず、次の recover が門か unreadable で必ず止まり退避対は不変 |
| `master` | 1c | マスタ欠損・非 SQLite・`meta` 表欠落・`meta` 2 行はすべて `master unreadable` (HDD 側は不変) / 検査 close の失敗は `master close failed` / 保証の上限 (integrity_check は OMIT) を表示に出す |
| `approve` | 1d | `N` / ESC で 9 名が 1 バイトも変わらず終了 0 (受入 I5)。承認文はマスタの版と件数を出す |
| `happy` | 1e/1f | 受入 I2 / I3 / I4。本体 + hot journal → `.bak` と `.bak-journal` の**対**、元の journal は消える、本体はマスタと同一、マスタは不変、印は `phase=done orig=present journal=present size=1406` / 本体欠損なら `.bak` を作らない / 孤立 journal も対の一部として写す / 旧 `.bak*` は承認済み 1 世代として消える |
| `backup_fail` | 1e | 旧 `.bak` が消せない / `read` の負 / short write / 長さは合うが中身が違う / journal の退避失敗 / 清掃の unlink 自体の失敗 / 印が書けない — **どれでも元の対は 1 バイトも変わらず、今回作ったものだけが消える** |
| `newfail` | 1f | `.new` の short write / 中身違い / DB として開けない / 書き側が開けない / 比較の read だけ失敗 → `.new` を消して元は無傷 (`.bak` と印は残る) / **検証 close の失敗は `.new` を消さず正式手順を案内** / **検証も close も同時に失敗しても `.new` を消さない** (往復 1 の B1) / 検査だけ失敗し close が通れば `.new` は消してよい / その次の recover は印の門で止まる |
| `switch` | 1f | hot journal が消せなければ破壊段に入らない (写しは対で残る) / rename の**両名残存**は消さずに停止 (元の内容は `.bak` に、phase は `switching`) / **置換先 unlink 後の新名追加失敗**は「旧本体が残っている」と仮定せず写しから対で復元 / **両名 ABSENT** も同じ復元経路 / その復元が DB 側でも journal 側でも失敗したら `phase=failed` で停止し退避対を保護 / rename 後の stat が UNKNOWN なら何も消さない |
| `record` | 1f(7) | `sync` 失敗 / reopen 失敗 / reopen の close 失敗 — どれでも `phase=switched` のままで `REBOOT … install --revert-settings hd0` を案内し、勝手には戻さない |
| `revert` | 1g | 印が無ければ `nothing to revert` / I2 の後は現在の対が `.failed*` へ、`.bak` が本体へ / I3 の後は `orig=missing` で本体を消して欠損に戻す / **切替後に生まれた新世代の journal を旧 DB に付けたまま戻さない** / `.new` 残骸があっても進む / `N` で何もしない / `.bak` が無ければ何も触らない / 旧 `.failed*` は 1 世代置換 |
| `revert_fail` | 1g | 本体の復元失敗 → `phase=reverting` のまま・`.bak` は不変・次の recover は門で止まる・再 revert で復帰 / `.failed` への退避失敗は作ったものだけ消す / 旧 `.failed` が消せなければまだ `reverting` にしない / `phase=reverting` が書けなければ何も触らない / 現在 journal の unlink 失敗 / `orig=missing` で本体が消せない / 本体 ABSENT + 孤立 journal からの revert / `sync` 失敗も `done` にしない / **journal の復元失敗で復元先を消さない** (往復 1 の B2、両世代の写しを残して手動へ) |
| `chain` | 1h | (1) 失敗 → 再実行が門で止まる → revert → recover / (2) recover → revert → recover (1 世代前が `.bak` に) / (3) recover 成功 → revert 途中失敗 → recover が門で止まり `.bak` 不変 → 再 revert / (4) `.new` close 失敗 → 次回停止 → revert → `rm` → recover (票 §1f 4 の正式手順) |
| `pure` | — | 印の書式の受理範囲 (phase 不正 / orig 不正 / size 非数字 / 欄欠落 / 空 / **複数行 / 末尾 LF 無し / 埋込み NUL / 128B 超**) / **size は `4294967295` まで通し `4294967296` で回り込まず拒否** / 16KB 境界をまたぐコピーとバイト比較 / 1 バイト違い・長さ違いを検出 / `read` の負は EOF ではなく失敗 / short write は失敗 |

## I-2. RED → GREEN の確かめ方 (突然変異)

「全部 PASS」だけでは試験が何も掴んでいない可能性があるので、`install_recover.inc` に
**22 種の意図的な後退**を 1 つずつ入れて、どのケースが落ちるかを確かめた (全部が少なくとも
1 ケースで落ちる = MISSED 0)。

| 入れた後退 | 落ちたケース |
|---|---|
| `read` の負を EOF に丸める (現行 `copy_file` と同じ) | `pure` |
| short write を見逃す | `pure` `newfail` |
| 退避のバイト比較を省く | `backup_fail` `switch` `revert_fail` `chain` |
| 印の phase の門を外す | `gate` `newfail` `revert_fail` `chain` |
| 起動媒体の種別を見ない | `media` |
| `inode` 共有の検査を外す | `scan` |
| `stat` の UNKNOWN を「無い」に丸める | `scan` |
| 両名残存で `.new` を消す | `switch` |
| `.new` だけ残ったとき「旧本体が残っている」と仮定する | `switch` |
| `db_close` の失敗を無視する (`.new` を消す) | `newfail` `chain` |
| journal を退避せずに消す | `happy` `backup_fail` `switch` `revert` `revert_fail` `chain` |
| revert の `phase=reverting` を破壊段の後にする | `revert_fail` `chain` |
| revert の前提検査 (`.bak` の存在) を外す | `revert` |
| 退避失敗で元の対も消す | `backup_fail` |
| `sync` / reopen の失敗でも `phase=done` にする | `record` |
| `.new` の残骸でも進む | `scan` `chain` |
| revert が現在の journal を消さない (別世代と混ざる) | `revert` `revert_fail` |
| **B1 回帰**: 検証失敗の枝を close 失敗より先に評価する | `newfail` |
| **B2 回帰**: revert の journal 復元失敗で復元先を unlink する | `revert_fail` |
| **B5 回帰**: 印を最初の LF で打ち切って受理する | `pure` `gate` |
| size の最終桁を見ずに wrap させる | `pure` |
| `rc_show` が触らない世代の置換も予告する | `revert` |

## I-3. 実装で票からずらした点 / 票が決めていなかった点

1. **`RecoverOps` の表に `db_column_*` は入れない**。列は `libos32cfg` と同じく SHM を直接読む
   (`shm()` が表の 1 本)。KAPI の口は `db_open_existing` / `db_prepare_only` / `db_step` /
   `db_finalize` / `db_close` / `db_error_code` + `shm`。
2. `main()` を `void` から `int` に変えた。票の「終了 1 / 終了 0」を返す口が他に無い
   (`cfg.c` と同じ形)。通常インストール経路は最後に `return 0`、メモリ確保失敗だけ `return 1`
   で、**それ以外の振る舞いは変えていない** (それまでは戻り値が未定義だった)。
3. 票が文言を決めていなかった失敗に付けた文言:
   `stat failed (/)` / `stat failed (/hd0)` / `stat failed (/hd0/etc)` / `cannot mount /hd0` /
   `ambiguous: <name> has no inode - needs manual recovery` (`st_ino == 0`) /
   `cannot remove old recover-state` / `recover-state write failed` /
   `backup failed (<name>)` / `copy failed (settings.db.new)` / `verify failed (settings.db.new)` /
   `needs manual recovery: cannot stat after rename` / `cannot remove current journal` /
   `cannot remove old failed copy` / `save failed (<name>)` /
   `rename left settings.db.new only - restored from settings.db.bak; nothing was replaced`。
   票が決めている文言 (受入が読むもの) はそのまま。
4. 票 §1f の 6 で「`.new` だけ残って**復元に成功した**とき」の印を決めていなかった。
   元の対がそのまま戻っているので `phase=done` にして終了 1 とした (次の recover は通常に進める)。
   復元に失敗した側は票どおり `phase=failed`。
5. 票 §1f の 6 は「`settings.db` だけ / `.new` だけ / 両方」の 3 通りしか書いていない。
   **両方 ABSENT** も「`.new` だけ」と同じ復元経路に入れた (写しから戻す。`.new` の unlink は
   存在するときだけ)。
6. 票 §1g の 6 は失敗の文言を `revert failed at <段> (phase stays reverting)` と決めているので、
   段ごとの具体的な文言 (`restore failed at <name> - …` など) の**後に**この 1 行も出す。
   票 §1g-4 の「journal のコピーに失敗したら `settings.db-journal` を unlink して『DB だけ・
   journal 無し』で止めず」は **unlink しない**の意 (実装レビュー往復 1 の B2 で確定)。
   復元先も両世代の写しも残して手動復旧へ案内する。
7. `--revert-settings` を HDD ブートで実行したときの文言も
   `recover-settings must run from the install floppy` のまま (票 §1g が 1a の検査をそのまま
   使うと書いているため)。
8. 印 (`settings.db.recover-state`) は 1 行 + LF。書式は
   `phase=<p> orig=<present|missing> journal=<present|absent> size=<n>\n` で、読み側は
   **厳密**: 欄の欠落・未知の phase・非数字/桁あふれの size に加え、**複数行・埋込み NUL・
   末尾 LF 無し・`RC_MARK_MAX` (128B) 以上**もすべて「壊れている」(往復 1 の B5)。
9. 印は O_TRUNC で直接書く (票 §1e 3 が rename を禁じている) ので、**phase の更新に失敗すると
   前の phase も残らないことがある**。その場合の次回は「門」ではなく
   `recover-state unreadable - needs manual recovery` で止まる。どちらでも通常経路には進まず
   退避対は保護されるので、試験はその安全側の性質を固定している (票はこの分岐を決めていない)。
10. `.new` の検証 close 失敗の案内は正式手順に統一 (追加往復 non-blocker):
   `REBOOT from the floppy, then: install --revert-settings hd0, rm /hd0/etc/settings.db.new,
   install --recover-settings hd0`。検証も同時に失敗していたら
   `verify failed (settings.db.new) as well` を併記したうえで**消さない**。
11. `recovered:` 行の `reopen=` と `close=` は**どちらも数値コード**で出す (同時失敗で close を
   `failed` に省略しない)。承認前の「`: will replace`」は recover なら `.bak*`、revert なら
   `.failed*` だけを予告する。

## I-4. 踏めなかったもの ([V4])

- ゲスト受入 I1〜I7 (NP21/W、FDD ブート) は**未実行**。コーダーの範囲外。
- 隔離された SQLite 接続 (`kapi_db.c` の F1) が `.new` の inode を掴んだままかどうかは
  ユーザーランドから検出できない。ホスト試験が固定しているのは「`db_close` が失敗したら
  `.new` を消さず、再起動を前提にした文言を出す」ところまでで、**再起動しなければ本当に
  危ないこと自体は試験していない** (票 §1f 4 の限界そのまま)。
- `PRAGMA integrity_check` はカーネルの SQLite で OMIT (`lib/sqlite3/os32_sqlite_config.h:51`)。
  検査は meta + 全行走査 + バイト一致までで、索引や自由ページの健全性は見ていない。
- ext2 の rename が実際にどの中途半端で止まるかは注入した 2 形 (両名残存 / 置換先 unlink 後の
  新名追加失敗) だけ。実 FS での再現は受入 I6 の領分。

---

## C-0. 組み方

| | |
|---|---|
| 対象 (1) | `userland/lib/cfg/cfg_json.c` (新規) — export の形だけを読む純関数 `cfg_json_header` / `cfg_json_record`。行バッファ 6,144B |
| 対象 (2) | `userland/lib/cfg/cfg_import.c` (新規) — 2 巡・重複表・単一トランザクション (`cfg_import_file`) |
| 対象 (3) | `userland/lib/cfg/libos32cfg.c` — 追加 2 本 `cfg_delete_scope` / `cfg_set_null` |
| 対象 (4) | `userland/cmds/cfg.c` — `import` 副指令 (引数解釈と文言だけ) |
| ハーネス | 既存の `tools/tests/cfg_host.c` をそのまま拡張。実 SQLite + 実 `os32_sqlite_vfs.c` + 実 `kapi_db.c` + 実 `libos32cfg` + 実 `cfg.c`。**贋物は KAPI 境界と障害注入だけ** |
| 足した贋物 | `be_fseek` (`CfgBackend.sys_lseek`)、`inj_seek_fail` / `inj_fread_fail_at` / `inj_close_after_commit`、生成入力 `gen_*` |

**生成入力 (`gen_*`)**: 8,192 件の JSON は約 392KB あって `FixtureFile` (128KB) に入らないので、
`gen_path` に一致した open だけ**その場で組み立てる**仮想ファイルにした。行長が固定
(ヘッダ 36B / レコード 46B) なので `lseek` の当たり先も算術で出る。`gen_rows2` / `gen_dup` /
`gen_ver2` は **2 回目以降の open** にだけ効く = 「巡回の間にホスト側が差し替える」経路そのもの
(票 §2 / 往復 2 の R7)。

**commit 後の close 失敗**は `be_exec` が `COMMIT` を実行した**直後**に `inj_close_fail` を立てる。
先に立てると `cfg_open` の中の RO→RW 切り替えの close を巻き込み、open 自体が `CFG_ERROR` に
なって狙った段に届かない (既存 `open_close_fail` が固定している挙動)。

## C-1. ケース (8 本 / 294 CHECK)

| ケース | CHECK | 見ているもの |
|---|---|---|
| `s3_json` | 79 | reader 単体。ヘッダ (版 1 受理 / 2 は `E_VERSION` で**実値を返す** / 0 は拒否 / 空白 / 尻尾 / レコードをヘッダに渡す)、骨組み (順序違い・空白・余分なキー・閉じ忘れ・二重 `}`)、型 (`3` / `"0"` / 2 桁)、scope と key (大文字・`app:` 空・末尾 `/`・63B 受理 / 64B 拒否)、int32 の境界と正準形 (`+1` / `01` / `-0` / 空を拒否、`INT_MIN` で符号あふれを踏まない)、エスケープ 6 種の往復、`\uXXXX` (BMP / `U+0000` = `E_NUL` / サロゲート = `E_UTF8` / 非 hex)、writer が出さない `\b` `\/`、生の制御文字・不正 UTF-8・途中で切れた UTF-8、生の 4 バイト UTF-8 は通す、text 255B 受理 / 256B 拒否、**最長行 1,693B と 5,627B**、base64 (空・`AP8=` = `00 FF`・4 の倍数でない・詰めの位置・詰めの後の本体・エスケープ混入・4096B 超)、`v:null` が宣言型を保つ |
| `s3_roundtrip` | 15 | **実 writer の出力を import に通す**。CR を含む text / 全制御文字 255B text / 空 blob / `00`・`FF` を含む blob / 4096B blob / `v:null` / `app:` scope を入れた DB を export → 壊す → import (置換) → `cfg list` が一致 → **export し直すとバイト単位で一致**。接続を持ったままの yield が 0 |
| `s3_scope` | 29 | `--scope` の削除範囲 (対象 scope の余分な行だけ消える) と **scope 外の値の不変**、`--merge` が消さないこと (全 scope / 単一 scope)、NULL 行が `(unset)` で戻ること、無効な scope は DB を触らずに拒否 |
| `s3_args` | 12 | 引数解釈 (旗の有無・順序、`--scope` の値欠け、二重 `--merge`、未知の旗、path が旗、path 無し) と usage 行 |
| `s3_reject` | 25 | MISSING / CORRUPT で `cannot import: <status>`、版 2 の拒否、ヘッダ無し、壊れた行 (`bad line 3: key`)、無いファイル、重複 (`duplicate record at line 3`)、`--scope` で対象外なら重複も無視、行が長すぎ (`line 2 too long`)。**どれも DB が 1 行も変わらない** |
| `s3_fail` | 22 | 3 件目の set で落として置換の削除ごと巻き戻る、2 巡目の read 失敗で rollback、`lseek` が使えないと重複検出ができないので失敗、**hash が衝突する別物 2 件は両方受理**、commit 後の close 失敗は `imported … (close failed <code>)` + 終了 1 で**値は更新済み** |
| `s3_gen` | 15 | 8,192 件は検証を通る (DB が MISSING なので書く前で止まる) / 8,193 件は `too many records`、**巡回の間の差し替え**で件数違い = `input changed during import`、重複の注入 = `duplicate record at line 5`、版の注入 = `newer backup: schema_version 2` — いずれも rollback して `cfg list` が一致 |

## C-2. RED → GREEN

RED は 3 段で採った。

**(a) API が無い (リンク段)** — `cfg_host.c` に `cfg_json.c` / `cfg_import.c` を載せる前:

```
ld: undefined reference to `cfg_import_file'
ld: undefined reference to `cfg_import_detail_name'
```

**(b) 実装だけを「未実装」に戻す** — `tools/tests/` は一切いじらず、`cfg_cmd_parse` の
`import` 枝を外し (S2 と同じ「副指令として知らない」状態)、reader の `\r` エスケープを
拒否に戻して 7 本を通した:

```
FAIL c_s3_json:2997: jrec("…\"v\":\"q\\\"w\\\\e\\nr\\rt\\ty\"}") == CFG_JSON_OK
FAIL c_s3_roundtrip:3191: ran("import", "/b.json", NULL, NULL, NULL) == 0
FAIL c_s3_scope:3216:     ran("import", "/b.json", "--scope", "gshell", NULL) == 0
FAIL c_s3_args:3267:      cfg_cmd_parse(n, av, &a) == 0
FAIL c_s3_reject:3297:    cap_has("cannot import: MISSING")
FAIL c_s3_fail:3364:      cap_has("import failed (")
FAIL c_s3_gen:3424:       cap_has("cannot import: MISSING")
SUMMARY 0/7 PASS
```

実装を戻すと `SUMMARY 7/7 PASS`、全体で `SUMMARY 52/52 PASS` + `TSV PARITY 58/58`。

**(c) 書いた直後に落ちた 5 件** — どちらが違うかを決めた記録:

| RED | 原因 | GREEN |
|---|---|---|
| `jhdr("{\"schema_version\": 1,…")` が `E_SYNTAX` でない | 空白は**数値の位置**に出るので reader は `E_VALUE` を返す。レコードの空白 (`{"scope":"gshell", "key":…`) は `E_SYNTAX` のまま | 試験の期待を `E_VALUE` に直した (どちらもヘッダとしては拒否) |
| `strlen(big) == 1693` が合わない (1,572) | 票の「最長 text 行 1,695B」は **scope 63B / key 63B** 前提。`user` / `a` では届かない | 試験を `app:` + 59B / 63B の名前で組み直した |
| `strlen(big) == 5627` が合わない | 同上 (blob 4096B の行も名前が 63B / 63B 前提) | 同上 |
| `s3_fail`: `inj_seek_fail` を立てても import が成功する | 用意した衝突ペア (`user`/`qh` と `user`/`2a`) は **8192 スロット**用。表を 16384 に広げたので衝突しない | FNV-1a を 16383 で畳んで取り直した (`user`/`89` と `user`/`adp`) |
| `s3_gen`: 8,192 件が `MISSING` に届かず拒否される | 生成側が値を `%04d` で書いていた。reader は **正準形しか受けない** (先頭ゼロは `E_VALUE`) ので全行が不正 | 生成側の値を `1` 固定に (GEN_REC 49 → 46)。**reader の仕様どおりの拒否**だったので実装は直していない |

## C-3. 実装で票からずらした点

1. **重複表を 8192 スロット (32KB) → 16384 スロット (64KB)**。票 §2 は「8192 個の表 (32KB)」だが、
   件数上限 (8192) と同数だと上限いっぱいの入力で表が**満杯**になり、線形探査の塊が育って
   「先行行の読み直し」が **561,944 回**に膨らむ (実測、`k%04d` の 8192 件)。1 回が
   `lseek` + `read` + 行の再解析なので、ゲストでは止まって見える。半分の詰まり方
   (16384 スロット) なら **3,007 回**。表の作りも判定 (衝突 → 読み直して文字列比較) も票の
   ままで、定数だけを変えた。
2. **`cfg_delete_scope` / `cfg_set_null` を `libos32cfg.c` に置いた**。票の「触るファイル」には
   無いが、この 2 本は set / delete と**同じ前提検査** (txn の中だけ・失敗で failed・列挙中は
   拒否・名前は bind) を持つ必要がある。`cfg_import.c` に書くと契約の写しが 2 つになるので、
   `can_write` を `can_write_gate` (名前を見ない段) と `can_write` に割って共有した。
   既存の呼び出し経路と挙動は変えていない。
3. **ヘッダの空白は `E_VALUE`**。票は「空白は許さない」までで理由の番号を決めていない。
   数値の位置に出た空白なので `E_VALUE`、構造の位置なら `E_SYNTAX`。どちらもヘッダとしては拒否。
4. **`cfg_import_detail_name()` を公開ヘッダに 1 本足した**。`cfg.c` は `libos32cfg.h` しか
   見ないので、行の失敗理由 (`CFG_JSON_E_*` は `cfg_internal.h`) を文言にするために要る。
5. **`v:null` 以外の値は正準形だけを受ける** (`+1` / `01` / `-0` を拒否)。票は「数値は int32」
   としか書いていないが、reader の役目は「writer が出す形だけを読む」なので `fmt_int` が
   出さない形は拒否に倒した。

## C-4. 踏めていないもの ([V4])

- **ゲスト受入 C1〜C7 (票 §4) は未実行**。配備もエミュレータ操作もしていない。
  C7 の「`v:null` を import して `(unset)` を作る」はホスト (`s3_roundtrip` / `s3_scope`) までで、
  ゲストでは踏んでいない。
- `build/app.conf` / `build/image.mk` / `userland/deploy.yaml` は PM の持ち物なので触っていない。
  本票で新しい実行ファイルは増えない (`cfg.bin` は登録済み、`cfg_import.o` / `cfg_json.o` は
  `DEFINE_LIB` の wildcard が拾う)。
- `make all` / `make check` は未実行。
- **8,192 件の書き込みは `s3_bulk` で実測済み** (C-5、実 `os32_sqlite_vfs.c` の 384KiB
  MEMSYS5 プールの上で 1 transaction が通った)。ただし条件は**単一 scope の短い
  整数値**で、(a) 既に大量の行が入った DB を置換するときの DELETE journal の負荷、
  (b) 32 scope x 256 key を埋めた状態の `cfg export` → `cfg import` の往復、
  (c) ゲスト (ext2 + 実 `/etc` の空き容量) での同じ規模は**いずれも未確認**。
- `cfg_import.c` の静的領域は約 **85KB** (重複表 64KB + 行バッファ 6KB×2 + `CfgJsonRow` 4.4KB×2)。
  `cfg_import.o` を引くのは `cfg.bin` だけなので `libos32gui.shlib` や読むだけのアプリには
  乗らない (アーカイブのメンバ単位のリンク) — ただし**リンク後の実測はしていない**。

---

## K-0. 何を直したか、なぜ要るか

S3 の実機受入 (FDD ブート、root = FAT) で
`db_open_existing("/etc/settings.db", 0)` が `SQLITE_IOERR` (10) で落ちた。

KAPI v50 は open の手前で hot journal の有無を `vfs_stat("<path>-journal")` で見て、
**NOTFOUND 以外は IOERR** と断じる (`kapi/kapi_db.c`)。「無い」と言い切れるのは NOTFOUND の
ときだけ、という設計なので、この判断そのものは正しい。問題は FAT 側で、
`fs/fatfs/ffconf.h` は `FF_USE_LFN 0` なので `settings.db-journal` は 8.3 に収まらず、
`f_stat` が `FR_INVALID_NAME` を返す。これを `ff_to_vfs` が `VFS_ERR_INVAL` に写していた。

実機で見えていた区別:

| 問い | 実機の `ls -l` | 直す前の VFS |
|---|---|---|
| `/etc/settings.db-journal` (8.3 に収まらない) | `Invalid argument` | `VFS_ERR_INVAL` |
| `/etc/nosuch` (収まるが無い) | `No such file` | `VFS_ERR_NOTFOUND` |

**そのボリュームに存在しえない名前は、stat の意味では「存在しない」**。
`fs/fatfs_vfs.c` に `ff_stat_to_vfs()` を足し、`fatfs_vfs_stat` と `fatfs_vfs_get_size`
(どちらも「在るか」を問う入口) だけがこれを通る。`FR_INVALID_NAME` → `VFS_ERR_NOTFOUND`、
それ以外は `ff_to_vfs` へ委譲。**open / read / write / unlink / mkdir / rename / list は
`ff_to_vfs` のまま** — 呼び手が不正な名前を渡したときの診断を潰さないため。

## K-1. 試験の範囲と遮断

`tools/tests/fatfs_stat_host.c` が**実物の `fs/fatfs_vfs.c` をそのまま `#include`** し、
境界だけを贋物に差し替える。

| 差し替えたもの | 何を貰うか |
|---|---|
| `f_stat` | 「次に返す `FRESULT`」と `FILINFO`、渡された FatFs パスを控える |
| `f_open` / `f_unlink` / `f_mkdir` / `f_rename` / `f_opendir` | 「次に返す `FRESULT`」(INVAL 据え置きの確認用) |
| `f_read` / `f_write` / `f_lseek` / `f_close` / `f_closedir` / `f_readdir` | 成功で素通り |
| `f_mount` / `f_getfree` | 呼ばれない前提 (`FR_NOT_READY` / `FR_INT_ERR`) |
| `ide_get_info` / `dev_find` / `dev_blk_read_lba` / `diskio_set_*` | mount 経路だけが使う。試験は mount を通さず `FatFsCtx` を直接組む |
| `kzalloc` / `kfree` / `kprintf` / `kmemset` / `kstrncpy` / `kstrncat` / `kstrlen` | ホストの同義実装 |

実デバイス・実イメージ・実 FatFs・実 SQLite には一切触らない。
コンパイルは `gcc -std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement` ([C1])。

ケース (7 本):

| ケース | 見るもの |
|---|---|
| `stat_invalid_name_is_notfound` | `FR_INVALID_NAME` → `VFS_ERR_NOTFOUND`。組み立てたパスが `0:/etc/settings.db-journal` であること |
| `stat_missing_is_notfound` | `FR_NO_FILE` / `FR_NO_PATH` → `VFS_ERR_NOTFOUND` (元からの挙動を固定) |
| `stat_disk_err_is_io` | `FR_DISK_ERR` / `FR_NOT_READY` / `FR_NO_FILESYSTEM` → `VFS_ERR_IO`。**I/O 障害は不存在に化けない** |
| `stat_ok_fills_size_mode` | 正常時の `st_size` と `st_mode` (通常 `0100644` / `AM_DIR` `0040755` / `AM_RDO` `0100444`)、`buf == NULL` は `VFS_ERR_INVAL` のまま |
| `get_size_invalid_name_is_notfound` | `get_size` も同じ写像。`FR_DISK_ERR` は IO のまま |
| `open_paths_keep_inval` | `read` / `write` / `read_stream` / `write_stream` / `unlink` / `mkdir` / `rename` / `list` の `FR_INVALID_NAME` は `VFS_ERR_INVAL` 据え置き |
| `v50_journal_probe_on_8_3` | v50 の順序の再現 — 本体 stat が OK (`st_size` 8192) で、続く journal 名の stat が NOTFOUND (INVAL ではない) |

## K-2. RED → GREEN

`ff_stat_to_vfs` を入れる前 (`fatfs_vfs_stat` / `fatfs_vfs_get_size` が `ff_to_vfs` を呼ぶ状態):

```
COMPILE GNU89 -Werror PASS
FAIL case_stat_invalid_name_is_notfound:151: ... == VFS_ERR_NOTFOUND
EXIT stat_invalid_name_is_notfound=1
EXIT stat_missing_is_notfound=0
EXIT stat_disk_err_is_io=0
EXIT stat_ok_fills_size_mode=0
FAIL case_get_size_invalid_name_is_notfound:228: ... == VFS_ERR_NOTFOUND
EXIT get_size_invalid_name_is_notfound=1
EXIT open_paths_keep_inval=0
FAIL case_v50_journal_probe_on_8_3:282: rc == VFS_ERR_NOTFOUND
EXIT v50_journal_probe_on_8_3=1
SUMMARY 4/7 PASS
```

落ちた 3 本は写像を要求する側、通った 4 本は**壊していないこと**を見張る側 (不存在 / I/O 障害 /
正常時の値 / open 系の INVAL)。`ff_stat_to_vfs` を足した後:

```
COMPILE GNU89 -Werror PASS
SUMMARY 7/7 PASS
```

`ff_stat_to_vfs` を `return ff_to_vfs(fr);` だけに潰すと上の RED に戻る (この写像しか
3 本を通せない)。逆に `ff_to_vfs` の `FR_INVALID_NAME` 自体を NOTFOUND に変えると
`open_paths_keep_inval` が落ちる — 範囲を stat 系に閉じ込めていることが試験で留まる。

## K-3. この試験が言えないこと ([V4])

- **実機 (FDD ブート) では踏んでいない**。配備もエミュレータ操作もしていない。
  `db_open_existing("/etc/settings.db", 0)` が通るようになったかは未確認。
- 実 FatFs (`fs/fatfs/ff.c`) が 8.3 に収まらない名前で本当に `FR_INVALID_NAME` を返すことは
  **贋物で仮定している**。根拠は実機の `ls -l /etc/settings.db-journal` = `Invalid argument`
  (受入時の観測) と `ffconf.h` の `FF_USE_LFN 0` であって、この試験の中では確かめていない。
- HostDrv (`fs/hostdrvfs.c`) と iso9660 の stat は見ていない。本件は FAT だけの写像。
- `make all` / `make check` は未実行。`fs/fatfs_vfs.c` の単体クロスコンパイル
  (`i386-elf-gcc -Wall -Wextra -Werror`) だけ通した。

## C-5. Codex 実装レビュー 往復 1 (C レーン) — 2026-09-14

ケースは 7 本 → **8 本**、CHECK は 214 → **262**。`SUMMARY 53/53 PASS`
(+ TSV parity 58/58、`--target` の i386-elf `-Werror`、`--sanitize` で s3_* 全通し)。

### B3 — `--scope` 外の値の不正で対象 scope の import まで拒否した 〔P2〕

反例 (ホストで再現 → `s3_scope` に固定): 正常な `gshell` 行と、**構文は正常だが
text が 256B の `user` 行**を含むファイルに `--scope gshell` を指定すると、
`cfg_json_record()` が値上限まで見て失敗し、scope 除外に到達しなかった。
票 §2「他 scope は構文検証だけ」への違反。

直し方は**構文と意味の分離**:

- `cfg_json_record()` は**構文だけ** (骨組み・キーの順・エスケープ・base64 の形・
  数字の並び)。上限 / 値域 / 名前の規則は `CfgJsonRow` の傷
  (`scope_over` `scope_nul` `key_over` `key_nul` `val_over` `val_nul` `val_range`)
  に控えるだけで拒否しない。`j_int` は範囲外でも数字を読み切り (溜めるのは止める)、
  `j_b64` は上限超過でも形の検査を最後まで続ける。
- `cfg_json_check(row)` が**対象行にだけ**かかる意味の検証。
- `cfg_json_scope_usable(row)` — scope を丸ごと復号できたか。切り詰めた scope を
  対象名と比べると、**63B の対象名に前半が一致する長い scope** を対象と
  取り違える (この派生も `s3_scope` に入れた)。
- `imp_pass` は両巡とも `record` → (対象外なら次の行へ) → `check` の順。
  衝突時の読み直しも `scope_usable` を要求する。

型 (`"type":0|1|2`) は**構文のまま**にした — 型が値の形を決めるので、0/1/2 以外は
そもそも値が解けない。

### B4 — commit 前の rollback / close 失敗が CLI から消えた 〔P2〕

反例 (`s3_fail` に固定): 置換の削除後、2 巡目の read が失敗し、`cfg_close()` の中の
`ROLLBACK` も I/O エラーになる (`inj_exec_match[0]="ROLLBACK"` を実在しない表へ
差し替え)。旧版は `read failed` しか出さず、「1 行も残らない」保証が**成立して
いない**ことを利用者が判別できなかった。

- `CfgImportInfo` に `cleanup` を足し、`imp_close()` (失敗経路専用の close) が
  **この呼び出しの中で** `cfg_close` が失敗したときだけ `cfg_last_close_error()` を
  写す。検証だけで終わった場合は close していないので前回の診断を拾わない。
- 適用先は status 拒否 / begin / delete_scope / 2 巡目 (read・parse・set) / commit の
  **すべての失敗経路**。
- `cfg.c` は原因の文言を出したあと、`cleanup != 0` なら同じ行の尾に
  ` (rollback/close failed <code>)` を付けて改行する。
  例: `read failed (rollback/close failed 10)` /
  `cannot import: CORRUPT (rollback/close failed 10)`。
  rollback が成功する通常経路では尾が付かないことも固定した。

### non-blocker (両方とも直した)

- **base64 の未使用ビット**: `AB==` / `AAB=` を拒否 (正準形)。詰めで捨てるバイトに
  落ちるビットは 0 でなければならない — pad=1 なら最後の実文字の下位 2bit、
  pad=2 なら下位 4bit。`AA==` / `AAA=` / `/w==` は従来どおり受理。
  これは「writer が出す形」の規則なので**構文**側 (対象外の scope の行でも拒否)。
- **8,192 件の実書込み**: `tools/tests/sqlite_groups_backend.h` の `FIXTURE_BYTES` を
  128KiB → **1MiB** に広げ、ケース `s3_bulk` を足した。`cfg init` した DB に対して
  生成入力 8,192 件を `--scope user` で置換 import し、`imported 8192 records
  (user), replaced` と先頭 / 中間 / 末尾 / 範囲外の値、`gshell` の不変までを確認。
  **実 `os32_sqlite_vfs.c` の 384KiB MEMSYS5 プールの上で通った** (静的推定ではなく
  実測)。`FIXTURE_BYTES` を共有する `kapi_db_v50_host.c` (22/22) と
  `install_recover_host.c` (14/14) も再実行して回帰なし。
  ただし**ゲストでの 8,192 件は未実行** — ホストの VFS は RAM ファイルで、ext2 の
  ブロック確保や `/etc` の空き容量は模型化していない ([V4])。

### まだ踏めていないもの ([V4])

- ゲスト受入 (C4 = GUI 端末、C5 = FDD の `cfg status`、C6 = JSON からの復元の正式手順)
  は PM / テスターの担当で、本レーンでは未実行。
- `s3_bulk` は 8,192 件の **import** を通しただけで、同じ規模の `cfg export` /
  `cfg list` は通していない (export は出力バッファの再開経路が別)。

## C-6. Codex 実装レビュー 往復 2 (C レーン) — 2026-09-14

B1 / B2 / B4 / B5 は修正確認。**B3 が一部残っていた**。

### B3 残存 — base64 の非正準形が対象外の scope の行でも import を落とす

反例 (`s3_scope` に固定): `--scope gshell` で、対象外の `user` 行に `"AB=="`
(詰めの未使用ビットが非ゼロ) があると `j_b64()` が即 `CFG_JSON_E_VALUE` を返し、
scope 除外に到達せず `bad line 3: value` で終了 1 になっていた。往復 1 で
「4096B 超過」は傷 (`val_over`) に回したが、**同じ往復 1 の non-blocker で足した
未使用ビットの検査だけが即時 return のまま**残っていた。

- `CfgJsonRow` に `val_b64` を足し、`j_b64()` は非正準形を**傷として控える**だけに
  する。`cfg_json_check()` が `val_over || val_b64` を `CFG_JSON_E_VALUE` にする。
- 構文として残すのは「引用符・エスケープを含まないこと・字種・4 文字単位・
  詰めの位置と個数・詰めの後に本体が来ないこと」だけ。

固定した契約 (同じ入力、両巡とも):

| 入力の対象外の行 | 全 scope | `--scope user` | `--scope gshell` |
|---|---|---|---|
| `"AB=="` (非正準 base64) | `bad line 3: value` | `bad line 3: value` | **成功** (`imported 1 records`) |
| 4096B 超の blob | `bad line 3: value` | — | **成功** |
| `01` (先頭ゼロ = 構文) | `bad line 3: value` | — | **失敗** (構文は全行) |

失敗側では `gshell` の値が 1 行も動いていないこと、成功側では `user` の既存値が
動いていないことも見ている。`--merge` でも同じ。

RED の確かめ: `j_b64()` の 2 行を `return CFG_JSON_E_VALUE;` に戻すと
`s3_json` / `s3_scope` がともに落ちる (`jsyn(...AB==...) == CFG_JSON_OK &&
jr.val_b64` と `ran("import", "/b64.json", "--scope", "gshell", NULL) == 0`)。

`SUMMARY 53/53 PASS` (+ TSV parity 58/58、`--target`、`--sanitize` 8/8)。

## K-4. `fatfs_vfs_list` の列挙エラー伝播 (票 S3I2-K) — 2026-09-14

> 票 (`docs/archive/settings/TASK_S3I2.md` §0) が指した「§K-3」。この文書には S3-K の
> `## K-3. この試験が言えないこと ([V4])` が既にあるので、既存節を消さずに番号をずらした。

### K-4.0 何を直したか、なぜ要るか

`fs/fatfs_vfs.c` の `fatfs_vfs_list()` は `f_readdir()` の戻りとファイル名の終端を
同じ `if` で見て `break` し、**その後は必ず `VFS_OK`** を返していた。

```c
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;   /* 直す前 */
    ...
    return VFS_OK;
```

FDD (root = FAT) の列挙中に I/O が落ちると、そこまでに callback へ渡した項目だけで
打ち切られるのに、呼び手 (`vfs_ls` → `sys_ls` → `install` の `copy_directory`) には
「成功」が届く。S3I2-I の `install` は `sys_ls` の負を失敗に数える作りにするので、
FAT 側が負を返さない限り**欠けたファイルのまま `Installation complete` で終了 0** になる
(TASK_S3I2 往復 2 の R2)。

直したのは戻り値だけ。**部分列挙は取り消さない** (既に渡した項目を呼び戻す口が
`vfs_dir_cb` に無く、呼び手は負を見て全体を捨てればよい)。

```c
    rc = VFS_OK;
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) { rc = ff_to_vfs(fr); break; }   /* 直した後 */
        if (fno.fname[0] == '\0') break;
        ...
    }
    f_closedir(&dir);
    return rc;
```

`f_opendir` の失敗は元から `ff_to_vfs(fr)` の負を返しており**変更なし**。
`f_closedir` は成功・打ち切りのどちらでも通る位置にあり、呼び忘れは無い
(開けなかったときは `return` が先なので呼ばない = 正しい)。
`vfs_ls` (`fs/vfs.c:344`) は `ops->list_dir()` の戻りをそのまま返すので**触っていない**。

### K-4.1 試験の範囲と遮断

K-1 と同じ作法 — `tools/tests/fatfs_stat_host.c` が実物の `fs/fatfs_vfs.c` を `#include` し、
境界だけを贋物に差し替える。この票で足したのは列挙の 3 本:

| 差し替えたもの | 何を貰うか |
|---|---|
| `f_readdir` | 「台本」(`stub_rd_script[]`) を 1 段ずつ返す。`rc != FR_OK` でその段は失敗、`FR_OK` で `name` が空なら終端。呼ばれた回数を数える |
| `f_opendir` | 既存どおり `stub_open_rc` を返す + 回数を数える |
| `f_closedir` | 回数を数える |

実デバイス・実イメージ・実 FatFs には触らない。コンパイルは
`gcc -std=gnu89 -Wall -Wextra -Werror -Wdeclaration-after-statement` ([C1])。
実機カーネル側は `i386-elf-gcc -std=gnu89 -m32 -march=i386 -ffreestanding -O2 -Wall -Wextra -Werror`
で `fs/fatfs_vfs.c` を単体コンパイルして通した ([C1] / [C2] — 使う文字列関数は `kstrncpy` のみ)。

ケース (既存 7 本 + 4 本 = 11 本):

| ケース | 見るもの |
|---|---|
| `list_ok_enumerates_all` | 台本 3 件 + 終端で、callback に 3 件 (名前 / `VFS_TYPE_DIR` と `VFS_TYPE_FILE` / `size`) が届き戻り値は `VFS_OK`。`f_opendir` / `f_closedir` が 1 回ずつ。組み立てたパスが `0:/` |
| `list_readdir_error_propagates` | 2 段目が `FR_DISK_ERR` → callback は 1 件だけ、戻り値 `VFS_ERR_IO`、3 段目は読まない、`f_closedir` は 1 回。1 段目で `FR_NOT_READY` なら callback 0 件で `VFS_ERR_IO` |
| `list_opendir_error_propagates` | `f_opendir` が `FR_NOT_READY` → `VFS_ERR_IO`、`FR_NO_PATH` → `VFS_ERR_NOTFOUND`。`f_readdir` / `f_closedir` は 0 回 (開けていないものを閉じない) |
| `list_empty_is_ok` | 空ディレクトリは 0 件で `VFS_OK` — エラーと区別がつく |

既存の `open_paths_keep_inval` も `fatfs_vfs_list` の `FR_INVALID_NAME` → `VFS_ERR_INVAL`
(= `f_opendir` 失敗の据え置き) を見張り続ける。

### K-4.2 RED → GREEN

直す前 (`if (fr != FR_OK || fno.fname[0] == '\0') break;` + `return VFS_OK;`):

```
COMPILE GNU89 -Werror PASS
EXIT stat_invalid_name_is_notfound=0
EXIT stat_missing_is_notfound=0
EXIT stat_disk_err_is_io=0
EXIT stat_ok_fills_size_mode=0
EXIT get_size_invalid_name_is_notfound=0
EXIT open_paths_keep_inval=0
EXIT v50_journal_probe_on_8_3=0
EXIT list_ok_enumerates_all=0
FAIL case_list_readdir_error_propagates:399: fatfs_vfs_list(&test_ctx, "/sys", collect_cb, (void *)0) == VFS_ERR_IO
EXIT list_readdir_error_propagates=1
EXIT list_opendir_error_propagates=0
EXIT list_empty_is_ok=0
SUMMARY 10/11 PASS
```

落ちたのは伝播を要求する 1 本だけ。他の 10 本は**壊していないこと**を見張る側
(正常列挙の件数と値、空ディレクトリ、`f_opendir` 失敗、S3-K の stat 7 本)。
`rc` を返すようにした後:

```
COMPILE GNU89 -Werror PASS
SUMMARY 11/11 PASS
```

突然変異で範囲が留まることの確認:

- `rc = ff_to_vfs(fr);` を消して `break;` だけに戻すと `list_readdir_error_propagates` が落ちる
  (上の RED そのもの)。
- `f_closedir(&dir);` を `if (rc == VFS_OK)` で囲むと `list_readdir_error_propagates` の
  `stub_closedir_calls == 1` が落ちる (打ち切っても閉じる)。
- 終端判定 (`fno.fname[0] == '\0'`) を失敗側に混ぜて `rc` を付けると `list_empty_is_ok` と
  `list_ok_enumerates_all` が落ちる (正常終了をエラーに化けさせない)。
- `f_opendir` 失敗の後に `f_closedir` を呼ぶ形にすると `list_opendir_error_propagates` の
  `stub_closedir_calls == 0` が落ちる。

実行: `python3 tools/tests/test_fatfs_stat.py` (11 本)。

### K-4.3 この試験が言えないこと ([V4])

- **実機では踏んでいない**。ビルド (`make all` / `make check`)・配備・エミュレータ操作は
  コーダーの役割外で、一切行っていない。カーネル全体のリンクも未確認
  (`fs/fatfs_vfs.c` の単体コンパイルまで)。
- 実 FatFs (`fs/fatfs/ff.c`) が実際に列挙途中で `FR_DISK_ERR` / `FR_NOT_READY` を返す条件は
  **贋物で仮定している**。FDD の I/O 失敗をこの試験の中では起こしていない。
- 呼び手側 (`sys_ls` → `install` の `copy_directory`) が負を失敗に数えるかは **S3I2-I の範囲**で、
  ここでは見ていない。`ls` / `cat` など他の `vfs_ls` 呼び手が負を受け取ったときの表示も未確認。
