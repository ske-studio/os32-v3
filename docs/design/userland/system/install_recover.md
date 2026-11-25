# userland/system/install_recover.inc — 設計

> 状態: **現行 (2026-11-18)** — 出所: S0_FOUNDATION (§6 明示リカバリ契約)・TASK_S3 (S3-I) の契約を今のソースと照らして要約
ソース: `userland/system/install_recover.inc` (`install.c` が `#include` する)

`install --recover-settings [drive]` (媒体のマスタで `/hd0/etc/settings.db` を置き換える) と `install --revert-settings [drive]` (置き換えの前へ戻す) の本体。
設定レジストリの全体の約束 (DB を作るのは明示の操作だけ、壊れた DB を自動で直さない、保証の上限) は `docs/design/userland/lib/cfg/libos32cfg.md` の「設定レジストリの全体の約束」にある。ここはその上の復旧の約束。

## 原則

- 通常起動の自動分岐ではない。FDD から起動し、対象・置き換える範囲を表示して承認された操作だけを行う。新規インストールと復旧を混ぜない。
- **元の対 (`settings.db` + `settings.db-journal`) は最後の切り替えまで 1 バイトも動かさない**。退避も復帰も rename ではなく **コピー + 長さ + バイト比較** (`rc_clone`) で作る。rename は最終の 1 回 (`.new → settings.db`) だけ。
- 消してよいのは **今回自分が作ったファイル** (`rc_made`) と、承認済みの 1 世代の旧 `.bak*`・`.failed*`・`.recover-state` だけ。他人の生成物 (`.new*` の残骸) は消さずに止まる。
- hot journal を DB だけの写しから切り離さない (journal は対の一部として写し、戻す)。
- 切り替えの原子性を backend で確かめられないので「原子的な復元」とは名乗らない。切り替え後に失敗したときの戻し手順が `--revert-settings`。
- `system.cfg`・`profile`・boot 領域・無関係な設定には触らない。
- KAPI の口は全部関数ポインタ表 `RecoverOps` を通す (ゲストは `install.c` の `rop_*`、ホスト試験は贋物)。本体に `#ifdef` の分岐は無い。列は SHM を直接読む。

## 対象の名前 (`rc_path`)

`/hd0/etc/` の 11 名: `settings.db`・`-journal`・`.bak`・`.bak-journal`・`.new`・`.new-journal`・`.failed`・`.failed-journal`・`.recover-state` (印)・`.master.new`・`.master.new-journal` (マスタの検査用の写し)。
通常の配備・hsync は、この 11 名に `-wal`・`-shm` を足した 13 名を作らない・消さない・上書きしない (`tools/deploy_protect.py` の `PROTECTED_BASENAMES`、`userland/system/hsync_protect.inc` の `hsp_protected_names`)。

## 起動媒体と対象 (`rc_check_media`)

- `drive` は `hd0` だけ (省略時も `hd0`)。他は `unsupported drive`。
- `st_dev` は VFS の `(dev_type << 8 | unit) + 1` を復号する (`RC_DEV_TYPE`・`RC_DEV_UNIT`、種別 HD = 0・FD = 1。外部プログラムからカーネルヘッダが見えないので写しを置く。正典は `sys/fs/vfs.h`)。0 は不明。
- `/` が FDD でなければ (`st_dev == 0` を含む) `recover-settings must run from the install floppy`。revert も同じ文言。
- `/hd0` が未マウントなら回復専用に `sys_mount("/hd0", "hd0", "ext2")`、失敗は `cannot mount /hd0`。
- `/hd0` と `/hd0/etc` の `st_dev` が HDD unit 0 でなければ `target /hd0 is not hd0`。stat の失敗は `stat failed (<path>)`。

## 事前の走査 (`rc_scan`) と印の門

- 11 名を全部 stat して三値 (PRESENT・ABSENT・UNKNOWN)。NOTFOUND だけが ABSENT。UNKNOWN が 1 つでもあれば `stat failed (<name>)`。
- PRESENT で `st_ino == 0` は判定できないので `ambiguous: <name> has no inode - needs manual recovery`。PRESENT 同士で `(st_dev, st_ino)` が一致すれば `ambiguous: <a> and <b> share an inode - needs manual recovery`。どれも何も消さない。
- 印 `settings.db.recover-state` の書式は 1 行 `phase=<p> orig=<present|missing> journal=<present|absent> size=<n>\n`。phase は `backup`・`switching`・`switched`・`failed`・`reverting`・`done`。
  - 読み (`rc_mark_read`) は厳密: 欄の欠け・未知の値・複数行・埋め込み NUL・末尾 LF 無し・`RC_MARK_MAX` (128 byte) 以上・size の非数字・u32 の桁あふれ (`4294967295` は通し `4294967296` は拒否) は壊れている。壊れていれば `recover-state unreadable - needs manual recovery` で止まり、印も消さない。
  - 書き (`rc_mark_write`) は rename せず O_TRUNC で直接書き、読み戻して一致を確かめる。どちらかが合わなければ失敗。
- **recover の門は白リスト**: 印が無いか `phase=done` のときだけ進む。それ以外は `previous recovery incomplete (phase=<p>) - run install --revert-settings hd0 first`。
- recover は `.new`・`.new-journal` があれば `stale <name> present - inspect and remove manually`、`.master.new*` があれば `stale settings.db.master.new(+journal) present - inspect and remove manually` で止まる。revert は `.new*` の残骸では止まらない。

## recover の段

1. **マスタの検査**: FDD (FAT) の `/etc/settings.db` を直接は開かず、`/hd0/etc/settings.db.master.new` へコピー + バイト比較して (`master unreadable: copy/byte verification failed`)、その写しを `rc_db_check` で見る。
   - `rc_db_check`: `db_open_existing(path, 0)` → meta の検査 (`RC_SQL_META`、`docs/design/userland/lib/cfg/libos32cfg.md` の「schema の検査」と同じ判定: 1 行・型が `integer`・MIN = MAX・1〜int32 の範囲) → 件数と集計 (`RC_SQL_AGG`: 行数と scope・key・tval・bval の長さの和) → 全行を最後まで step して件数と一致 → close。
   - `PRAGMA integrity_check` はカーネルの SQLite で OMIT なので使えない。検査は meta・全行・バイト一致までで、索引や自由ページの健全性は保証しない。承認の前にその旨を表示する。
   - close の失敗は写しを消さず `master close failed (<code>) - settings.db.master.new kept; original untouched.` と再起動の手順。SQLite が無い (MINIMAL の起動で `db_open_existing` が `OS32_ERR_NOSYS`) なら `SQLite unavailable (MINIMAL) …`。他は `master unreadable`。
   - `.master.new` は recover の出口 (`rc_recover`) で、成功・失敗・取り消しのどれでも消す。消せなければ `cannot remove settings.db.master.new - inspect and remove manually` で終了 1。
2. **表示と承認**: マスタの版と件数、本体 (`present <size> B` / `missing`)、hot journal (`will be backed up as .bak-journal and removed before switch`)、置き換える旧 `.bak*` (`: will replace`)、前回の印。`Replace /hd0/etc/settings.db with master (schema <n>, <k> keys)? …` に `Y`・`y` 以外なら何もせず **終了 0**。
3. **退避** (元の対は不変): 旧 `.bak`・`.bak-journal` を消す (`cannot remove old backup`) → 本体があれば `settings.db → .bak`、journal があれば `-journal → .bak-journal` を `rc_clone`。失敗は `backup failed (<name>)` で、今回作った物だけ消す。続いて旧印を消し、`phase=backup` の印を書く (`recover-state write failed` なら作った物を消す)。ここから先 `.bak*` と印は「今回作ったから消してよい物」ではない。
4. **`.new` の作成と検証**: マスタの写しを `.new` へコピー (`copy failed`) → 長さとバイト比較 (`verify failed`) → `rc_db_check` で版・件数・集計がマスタと一致 (`verify failed`)。失敗なら `.new` を消す。**ただし検証の close が失敗したら、検証の結果に関わらず `.new` を消さない** (隔離された接続がその inode を掴んでいる)。`verify close failed (<code>) - settings.db.new kept; original untouched.`、検証も失敗していれば `verify failed (settings.db.new) as well` を足し、正式手順 `RC_NEW_CLOSE_STEPS` (再起動 → `--revert-settings` → `.new` を `rm` → recover) を出す。印は `backup` のまま = 次の recover は門で止まる。
5. **破壊段の始まり = hot journal の除去**: journal があれば消す (写しは `.bak-journal`)。失敗は `cannot remove hot journal` で `.new` を消す。
6. **切り替え**: 印を `switching` に → `rename(.new → settings.db)` → 両名を stat。
   - stat が NOTFOUND 以外で失敗 → `needs manual recovery: cannot stat after rename` (何も消さない)。
   - 両方ある → `needs manual recovery: settings.db and settings.db.new both present` (消さない)。
   - 本体が無い (置換先を消した後の新名の追加に失敗した形、両方無い形も含む) → 旧本体が残っているとは仮定せず、印の `orig`・`journal` に従って `.bak → settings.db`・`.bak-journal → settings.db-journal` を `rc_clone` で戻し、`.new` があれば消す。戻せたら `rename left settings.db.new only - restored from settings.db.bak; nothing was replaced` + 印 `done` + **終了 1**。戻せなければ印 `failed` + `restore failed - run install --revert-settings hd0 after reboot`。
7. **記録**: 印を `switched` に → `sys_sync` → 本体を `rc_db_check` で開き直す → `recovered: schema_version <n>, <k> keys, sync=<rc>, reopen=<ok|code>, close=<ok|code>` (reopen と close はどちらも数値のコードで出す)。sync・reopen・close のどれかが失敗なら、自動では戻さず `REBOOT from the floppy, then: install --revert-settings hd0` で終了 1 (印は `switched`)。全部通れば印を `done` にして終了 0。`.bak*` と印は成功後も残す (次の recover で 1 世代置き換え)。

## revert の段 (`rc_revert`)

1. 媒体と走査は recover と同じ。印が無ければ `nothing to revert (no recover-state)`。印が読めなければ止まる。
2. 表示 `current db(+journal) -> settings.db.failed(+.failed-journal), then restore: orig=<…> journal=<…>` と置き換える旧 `.failed*`、`Revert /hd0/etc/settings.db? [Y/N]`。断れば終了 0。
3. **前提**: `orig=present` なら `.bak`、`journal=present` なら `.bak-journal` が要る。無ければ `backup missing (<name>) - needs manual recovery` で何も触らない。
4. 旧 `.failed*` を消す (`cannot remove old failed copy`)。
5. **破壊段より前に印を `reverting` にする**。書けなければ何も触らずに終了 1。
6. 今の本体・journal があれば `.failed`・`.failed-journal` へ `rc_clone` (`save failed (<name>)`、作った物だけ消す)。
7. 今の journal を消す (旧 DB に新しい世代の journal を付けない)。失敗は `cannot remove current journal`。
8. `orig=present` なら `.bak → settings.db`、`orig=missing` なら本体を消して欠損に戻す。`journal=present` なら `.bak-journal → settings.db-journal`。失敗は `restore failed at <name> - current copy is in settings.db.failed`。journal の復元に失敗しても **復元先を消さない** (「DB だけ・journal 無し」に分離させない)。`.bak-journal` が残っている旨を出して手動へ。
9. `sys_sync` が通り、印を `done` に書けたときだけ `reverted: orig=<…>, sync=0` で終了 0。
- 6〜9 の失敗はすべて最後に `revert failed at <段> (phase stays reverting)` を出し、印は `reverting` のまま (次の recover は門で止まり、もう一度の revert だけが進める)。もう一度の revert は本体の欠損・孤立した journal も扱える。

## 戻り値

承認しないときは 0、成功は 0、それ以外は 1 (`install.c` の `main` は `int` を返す)。

## 検査

`install-recover-host` (`tools/tests/test_install_recover.py`、本体 `tools/tests/install_recover_host.c`)。実物の `install_recover.inc`・`sys/kapi/kapi_db.c`・SQLite・`os32_sqlite_vfs.c`・`sys/fs/vfs_fd.c` を RAM の backend で組み、stat の三値・inode の共有・rename の中途半端・read の負・short write・unlink・sync・db close の失敗を注入する。
各段の各失敗で「切り替え前なら元の対が 1 バイトも変わらない」「欠損は欠損のまま」「作った物だけ消える」「両名が残れば消さずに止まる」「journal が別の世代と混ざらない」を見る。連鎖 (失敗 → 再実行が門で止まる → revert → recover、recover → revert → recover、revert の途中の失敗 → 再 revert、`.new` の close 失敗 → 次回停止 → revert → `rm` → recover) も通す。
