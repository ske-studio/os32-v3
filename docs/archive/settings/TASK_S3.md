# S3 — リカバリ (`install --recover-settings`) と `cfg import`

> 発行: PM (2026-09-13) / 状態: **受入完了 (2026-09-14)**

実装 `6332dac` (D) / `c2a1cdd` + `7b26058` (I) / `e1f6791` + `5f2ee25` + `cedfc6a` (C) / `b2a1580` (K: FAT stat)、Codex 実装レビュー 3 往復で Approve、ゲスト受入 I1〜I5 / I7、C1〜C7 (§9b / §9d / §9f)。残ゲート: S3-I2 (別票)、I6 の全失敗点網羅はホスト試験の範囲、8192 件は単一 scope の実書込みまで、電源断耐性は未保証。設計: 第 5 版 (Codex 3 往復 + 追加 1 往復)。ユーザー決裁 2026-09-13「3. リカバリ」。前提: S0 / S2 / S4 / S5 完了 (main `02cefcc`)。
正典: [DESIGN.md](../../tasks/settings/DESIGN.md) §2 (初期値はインストーラだけが持つ、リカバリモード) / §6b (JSON バックアップと `cfg import`)、[S0_FOUNDATION.md](S0_FOUNDATION.md) §6 (**明示リカバリ契約**: 自動分岐なし、表示と承認、元 DB と journal を対で保存、別名へ完全コピー → 検証 → 切替、失敗で元を消さない、原子性は backend で確認できなければ名乗らない、system.cfg 等は触らない、復元後に schema / sync / reopen を記録)、[TASK_S2.md](TASK_S2.md) §1 (規則) / §2 (`cfg export` の JSON 形、import は S3)、[TASK_S0.md](TASK_S0.md) (配備保護: `settings.db*` を通常配備が触らない)。
規約: [C1] C89、コーダーは worktree + ホスト TDD のみ、[D2] (使い捨てイメージ / ini) はユーザー承認。

## 0. 範囲と分担

| 票 | 範囲 | レーン | 触るファイル |
|---|---|---|---|
| **S3-I** | `userland/system/install.c` に **`--recover-settings [drive]`** と **`--revert-settings [drive]`** (§1)。要求 KAPI 7 → **50** (db_* v50 を使う)。通常インストール経路は不変 — ただし TASK_S0 §3 が S3 に送った前提不整合 (`install` が `/kernel.bin` を必須とするが FDD は `/VMKRNL.LZ4` + `/boot` を収録 → FDD からの新規インストールが `/etc` コピーまで到達しない) を**別の残ゲート S3-I2 として §7 に明記** (本票では直さない、ユーザー決裁) | C (system) | `userland/system/install.c` (+ `install_recover.inc` に分ける)、`tools/tests/install_recover_host.c` / `test_install_recover.py` / `s3_tdd.md` |
| **S3-C** | `cfg import <file> [--scope <scope>] [--merge]` (§2)。`libos32cfg` に最小 JSON reader (`cfg_json.c`) と `cfg_import_*` API、`cfg.c` のサブコマンド。`cfg export` は不変 | C | `userland/lib/cfg/{cfg_json.c, libos32cfg.h (追記のみ), cfg_import.c}`、`userland/cmds/cfg.c`、`tools/tests/cfg_host.c` / `test_cfg.py` / `s3_tdd.md` §C |
| **S3-D** | 配備保護に回復ファイルの名前を追加 (往復 3 の B5): `tools/deploy_protect.py` の `PROTECTED_BASENAMES` と `userland/system/hsync_protect.inc` の名前規則に `settings.db.bak` / `.bak-journal` / `.failed` / `.failed-journal` / `.new` / `.new-journal` / `.recover-state` を足し、通常同期の前後で対と印が不変であることをホスト試験 (`test_deploy_protect.py` / `hsync_protect_host.c`) に追加 | ツール | `tools/deploy_protect.py`、`userland/system/hsync_protect.inc`、`tools/tests/` |
| PM | `build/app.conf` (install 50)、`build/image.mk` (FDD に `install.bin` は既に載る。`cfg.bin` を **FDD_MIN_CMDS に足す**: リカバリ後の `cfg status` 確認用)、`userland/deploy.yaml` 確認、受入 (§4、FDD ブートは NP21/W の起動引数 = ini 変更なし) | PM / テスター | — |

S3 に**含めない**: `--from <json|tar.lz4>` (**FDD の `cfg` は FDD 自身の `/etc/settings.db` を見るので HDD の復元には使えない**、往復 1 の B11。JSON からの復元の正式手順は「FDD で `--recover-settings` (マスタ復元) → HDD で再起動 → 通常の `cfg import`」= §2 の受入 C6)、S6 `tar`、F3a-c、自動バックアップ、通常インストールの `/kernel.bin` 不整合 (S3-I2、§7)。

## 1. `install --recover-settings [drive]` / `--revert-settings [drive]` (S3-I)

設計原則 (往復 2 の R1〜R5 を構造で解く): **元の対 (`settings.db` + `-journal`) は最後の切替まで 1 バイトも動かさない**。退避も復帰も **rename ではなくコピー + バイト比較** で作る (新しい inode に書くので「両名が同じ inode を指す」「対が分離する」状態が生まれない)。rename は最終切替の 1 回だけ (`.new → settings.db`) で、その両名残存は消さずに停止。消してよいのは**今回自分が作った**ファイル (作成一覧を持つ) と、承認済み 1 世代の旧 `.bak*` / `.failed*` だけ。

### 1a. 前提と起動媒体・対象デバイスの検査 (往復 1 の B1、往復 2 の R6)
- `drive` は `hd0` のみ (既定 `hd0`。他は `unsupported drive` で終了 1)。
- **FDD ブートの確認**: `sys_stat("/")` の `st_dev` を復号 (VFS は `(dev_type << 8 | unit) + 1` を入れる、`fs/vfs.c:457`。fd0 = 257、hd0 = 1) → 種別が FDD でなければ `recover-settings must run from the install floppy` で終了 1。
- **対象デバイスの確認**: `sys_is_mounted("/hd0")` が偽なら回復専用に `sys_mount("/hd0", "hd0", "ext2")` (現行 install の format 後の mount 経路は共用しない)。マウント済みでも **`sys_stat("/hd0")` と `sys_stat("/hd0/etc")` の `st_dev` を復号して種別 = HDD、unit = 0 であること**を確認 (回復シェルで `/hd0` に hd1 が刺さっていても hd1 を書かない)。不一致は `target /hd0 is not hd0` で終了 1。
- DB の利用者: FDD ブートでは gshell も FEP も起動しておらず、`install` 以外に接続は無い (FOUNDATION §6-1 は起動媒体の規則で満たす)。

### 1b. 名前の集合と事前検査 (往復 1 の B2 / B3、往復 2 の R4)
対象 `/hd0/etc/` の **9 名**: `settings.db`、`settings.db-journal`、`settings.db.bak`、`settings.db.bak-journal`、`settings.db.new`、`settings.db.new-journal`、`settings.db.failed`、`settings.db.failed-journal`、`settings.db.recover-state` (印、§1e。**rename を使わず直接書く**、往復 3 の B1)。承認前に**全部** `sys_stat` して三値 (PRESENT / ABSENT / UNKNOWN)。
- UNKNOWN が 1 つでもあれば `stat failed (<name>)` で終了 1。
- **同一性**: PRESENT 同士で `(st_dev, st_ino)` が一致する組があれば `ambiguous: <a> and <b> share an inode - needs manual recovery` で終了 1 (何も消さない・書かない)。`st_ino == 0` は判定不能で終了 1。
- **印の phase による門** (往復 3 の B2 / B4、追加往復の 1): 印が PRESENT なら読み、**`phase=done` (前回の recover / revert が完了) か印無しのときだけ recover は通常に進む** (1 世代置換)。それ以外 (`backup` / `switching` / `switched` / `failed` / **`reverting`**) はすべて `previous recovery incomplete (phase=<p>) - run install --revert-settings hd0 first` で停止 (許可条件を白リストで書く)。印が読めない / 壊れている → `recover-state unreadable - needs manual recovery` で停止。
- **`.new` / `.new-journal` が PRESENT** なら `stale <name> present - inspect and remove manually` で終了 1 (他人の生成物は消さない)。**例外**: `--revert-settings` は `.new*` が残っていても進む (§1g、往復 2 の R2 — `.new` は元の対とは別 inode なので触らずに復帰できる)。
- 表示 (承認前): マスタの版と件数、本体 `present <size> B` / `missing`、journal (`hot journal present - will be backed up as .bak-journal and removed before switch`)、旧 `.bak*` / `.failed*` (`will replace`)、`.recover-state` の内容 (前回の記録があれば)。

### 1c. マスタの検査 (往復 1 の B5)
FDD の `/etc/settings.db` を `db_open_existing(path, 0)` → meta 検査 (S2 §1-1 (b) の SQL) → `settings` 全行走査 (`SELECT COUNT(*), SUM(length(scope)+length(key)+coalesce(length(tval),0)+coalesce(length(bval),0)) FROM settings` は集計、加えて `SELECT … ORDER BY scope,key` を最後まで step して読めることを確認) → close。**保証の上限**: `PRAGMA integrity_check` はカーネルの SQLite で OMIT (`os32_sqlite_config.h:51`)。検査は「meta + 読み出した行 + コピーのバイト一致」までで、索引や自由ページの健全性は保証しない (票と表示に明記)。open / 検査 / close の失敗は `master unreadable` / `master close failed (<code>)` で終了 1。

### 1d. 承認
`Replace /hd0/etc/settings.db with master (schema <n>, <k> keys)? Current db(+journal) will be copied to settings.db.bak(+.bak-journal) [Y/N]`。`Y` 以外は何もせず終了 0。

### 1e. 退避 (コピー、元は不変) (往復 1 の B4、往復 2 の R1 / R3)
1. 旧 `.bak` / `.bak-journal` が PRESENT なら unlink (承認済み 1 世代。1b で他の名前と inode を共有しないことを確認済み)。失敗は `cannot remove old backup` で終了 1 (元は無傷)。
2. 本体 PRESENT なら **コピー** `settings.db → settings.db.bak` (O_CREAT | O_TRUNC、16KB、read の負は失敗 (現行 `copy_file` は負を EOF 扱いにするので回復専用の `rcopy_file` を書く)、short write は失敗) → **バイト比較**で一致を確認。journal PRESENT なら同様に `-journal → .bak-journal`。失敗: 今回作った `.bak*` を unlink して終了 1 (**元の対は無傷**)。
3. **印**: 旧 `settings.db.recover-state` が PRESENT なら unlink (1b の同一性検査済み、`phase=done` のものだけがここへ来る)。`settings.db.recover-state` を **直接** (O_CREAT | O_TRUNC、rename しない) 1 行 `phase=backup orig=present|missing journal=present|absent size=<n>` で書き、**読み戻して一致**を確認 (往復 3 の B1: rename の両名問題を持ち込まない)。書けない / 一致しない → 今回作った `.bak*` と印を unlink して終了 1 (元の対は無傷)。印の phase はこの後 `switched` (手順 6 の後) → `done` (手順 7 の後) と**同じ方法 (上書き + 読み戻し)** で更新する。更新に失敗したら終了 1 (状態は印の phase が示す = 次回は門で止まり revert へ)。

### 1f. コピー・検証・切替 (往復 1 の B3 / B5 / B6)
4. マスタを `settings.db.new` へ `rcopy_file` → (i) 長さ一致、(ii) **バイト比較**、(iii) `db_open_existing(".new", 0)` → meta + 全行走査で 1c の件数・集計と一致 → close。失敗 (close 失敗以外): `.new` を unlink → 終了 1 (元の対は無傷、`.bak*` と印は残してよい = 次回は 1 世代置換)。**close の失敗** (接続が隔離され、owner 終了でも回収されない — `kapi_db.c:501`): `.new` を消さず `verify close failed (<code>) - settings.db.new kept; original untouched. REBOOT from the floppy, then rm /hd0/etc/settings.db.new and retry` で終了 1。**再起動を明示の前提**にする (往復 3 の B4: 同じ起動のまま `rm` すると隔離接続が掴む inode を消す。ユーザーランドから隔離接続の有無は検出できないので手順と試験 (文言) で担保し、票の限界として明記)。印は `phase=backup` のまま = 次回 recover は門で止まる。**正式手順 (追加往復 non-blocker で統一)**: 再起動 → `--revert-settings` (元は無傷なので `.bak` を本体へ写すだけ、印が `done` になる) → 残った `.new` を確認して `rm` → recover。
5. **破壊段の開始 = journal の除去** (ここから先は「元の対は不変」ではなく「元の対は `.bak*` に写しがあり、印が phase を持つ」が保証): 元の journal が PRESENT なら (2 で `.bak-journal` にバイト一致の写しがある) `settings.db-journal` を unlink。失敗は `cannot remove hot journal` で終了 1 (`.new` を unlink、元の対は無傷、印は `phase=backup` のまま = 次回は門 → revert が journal を写しから戻す)。
6. **切替**: 印を `phase=switching` に更新 → `sys_rename(.new → settings.db)`。両名検査: 両方 PRESENT → `needs manual recovery: settings.db and settings.db.new both present` (消さない、終了 1。**元の内容は `.bak` に**、journal は `.bak-journal` に、印にも状態がある)。`settings.db` だけ → 成功。`.new` だけ → **ext2 の rename は置換先の unlink の後で新名追加に失敗しうる** (往復 3 の B2) ので「旧本体が残っている」と仮定しない: 印の `orig=present` なら `.bak → settings.db` を**コピー + バイト比較**で戻し、`journal=present` なら `.bak-journal → settings.db-journal` も戻す (`.new` は unlink)。どちらかの復元に失敗したら印を `phase=failed` に更新して `restore failed - run install --revert-settings hd0 after reboot` で終了 1 (退避対は保護、次回 recover は門で止まる = 世代置換に流れない)。stat UNKNOWN → 消さず `needs manual recovery`。**「原子的復元」とは名乗らない**。
7. **記録**: 印を `phase=switched` に更新 → `vfs_sync()` の戻り値、`db_open_existing("/hd0/etc/settings.db", 0)` で reopen → 1c と同じ検査 → close。`recovered: schema_version <n>, <k> keys, sync=<rc>, reopen=<ok|code>, close=<ok|code>` を表示。すべて成功なら印を `phase=done` に。どれかが失敗なら終了 1 で自動では戻さず、`REBOOT from the floppy, then: install --revert-settings hd0` を案内 (§1g。reopen の close 失敗は接続が隔離されるので再起動が前提、往復 3 の B4)。
8. `system.cfg` / `profile` / boot 領域 / `.bak*` / 印 (成功後も残す。次回 recover で 1 世代置換) には触らない (FOUNDATION §6-4)。

### 1g. `install --revert-settings [drive]` (往復 1 の B6、往復 2 の R2 / R3 / R4 / R5)
1a / 1b の検査 (`.new*` 残骸では**停止しない**、`.failed*` も同一性検査に含む) → 印 `settings.db.recover-state` を読む (無ければ `nothing to revert (no recover-state)` で終了 1) → 表示 (`current db(+journal) -> settings.db.failed(+.failed-journal), then restore: orig=<present|missing> journal=<present|absent>`) → 承認 → 
0. **前提検査** (往復 3 の B3): 印を読み、`orig=present` なら `.bak` が PRESENT、`journal=present` なら `.bak-journal` が PRESENT であることを確認 (無ければ `backup missing (<name>) - needs manual recovery` で終了 1、何も触らない)。現在の本体・journal は PRESENT / ABSENT で分岐 (**欠損なら写す段を省く**)。
1. 旧 `.failed*` が PRESENT なら unlink (承認済み 1 世代)。
1b. **印を `phase=reverting` に更新 (上書き + 読み戻し)** — 破壊段 (3 以降) より前 (追加往復の 1: revert の途中失敗で `done` が残ると、次の recover が壊れた本体を新しい `.bak` にして元の唯一の写しを失う)。更新に失敗したら終了 1 (何も触っていない)。
2. **現在の対を退避 (コピー)**: 本体が PRESENT なら `settings.db → .failed`、journal が PRESENT なら `→ .failed-journal`、バイト比較。失敗は今回作った `.failed*` を unlink して終了 1 (現在の対は無傷)。
3. 現在の journal が PRESENT なら unlink (2 で写しがある。旧 DB に新世代の journal を付けない、往復 2 の R5)。
4. 印が `orig=present`: `.bak → settings.db` を**その場コピー** (O_TRUNC で上書き、rename しない) → バイト比較。`journal=present` なら `.bak-journal → settings.db-journal` をコピー → バイト比較。コピー失敗は `restore failed at <name> - current copy is in settings.db.failed` で終了 1 (両方の写しが残っているので手動復旧可能。**対を分離したままにしない**ため、journal のコピーに失敗したら `settings.db-journal` を unlink して「DB だけ・journal 無し」で止めず、`.bak-journal` の存在を表示して手動へ)。
5. 印が `orig=missing`: `settings.db` を unlink (2 で `.failed` に写しがある)、`journal=present` (孤立 journal だった) なら `.bak-journal → settings.db-journal` をコピー。元の欠損状態に戻す (往復 2 の R3、FOUNDATION §6-2)。
6. `vfs_sync` → **復元・バイト比較・sync がすべて成功したときだけ** 印を `phase=done` に更新 (復帰済み = 次の recover は通常に進める)。どれかが失敗したら印は `reverting` のまま (次の recover は門で止まり、再 revert だけが進める) → 表示 `reverted: orig=<…>, sync=<rc>` / `revert failed at <段> (phase stays reverting)`。再 revert は 0 の分岐で「本体 ABSENT」も扱えるので害はない (往復 3 の B3)。close 失敗 (手順 4 のバイト比較で DB は開かないので、revert に SQLite の close は無い) は起きない。

### 1h. ホスト TDD (`install_recover_host.c`)
`install_recover.inc` を `#include` し、KAPI 表を贋物 (RAM backend: stat 三値と UNKNOWN 注入、`st_ino` 共有の注入、`st_dev` の復号 (種別 / unit)、rename の「新名追加成功・旧名削除失敗」注入、open / read (負) / write (short) / unlink / sync 失敗、db close 失敗) に差し替え、1a〜1g の**各段の各失敗**で「元の対が 1 バイトも変わらない (切替前)」「欠損は欠損のまま」「自分が作ったファイルだけ消える」「両名残存で消さず停止」「journal が別世代と混ざらない」を固定。**連鎖**: 失敗 → 再実行 (1b の検査で停止 / 1 世代置換で進む) / recover 成功 → revert → 再 recover / **recover 成功 → revert 途中失敗 (本体の O_TRUNC 後の read/write 失敗) → recover 再実行が門で止まり `.bak` の内容が不変 → 再 revert で復帰** / `.new` close 失敗 → 次回 recover 停止 → `rm` 後に成功、を試験 (往復 2 の R1〜R5)。実 SQLite の検査部は実 kapi_db.c に通す。

## 2. `cfg import <file> [--scope <scope>] [--merge]` (S3-C)

- 入力: `cfg export` が書く形だけ (1 行目ヘッダ `{"schema_version":N,"exported":"..."}`、以降 1 行 1 レコード `{"scope":"…","key":"…","type":0|1|2,"v":<int>|"<text>"|"<base64>"|null}`)。最小 reader (`cfg_json.c`、純関数、**この形しか読まない**: キー順固定、空白なし、エスケープは `\"` `\\` `\n` `\r` (往復 1 の B7: writer は CR を `\r` で出す) `\t` `\uXXXX` (BMP のみ。非 BMP は writer が生 UTF-8 で出すのでサロゲートは拒否) だけ、数値は int32、他は拒否)。汎用 JSON は持ち込まない (DESIGN §6b)。**行バッファは writer の最長行から決める**: text 255B が全部 `\u0001` の行 1,695B、blob 4096B の行 5,629B (LF + NUL 込み) → 6KB。復号後の上限 (63 / 255 / 4096、UTF-8、NUL 無し) は別に検査。
- 版: ヘッダの `schema_version` が **自分 (1) より新しければ拒否** (`newer backup: schema_version N`)、古い (0 は無い) は読める範囲で取り込む。
- 対象の抽出 (往復 1 の B10): `--scope <s>` があれば **読み取り・検証・適用のすべてを scope = s の行だけ**に限定 (他 scope の行は構文検証だけして無視)。scope 外の**値は不変**。
- 2 巡: **1 巡目で対象行を全部検証** (S2 の tsv reader と同じ規則: scope / key / type / 値の上限、UTF-8、`v:null` の型、base64 の妥当性、重複)。**件数上限は現行 export の最大 = 32 scope × 256 key = 8192 件** (往復 1 の B8)。重複検出は (scope, key) の 32bit hash (FNV-1a) 8192 個の表 (32KB) + 衝突時は先行行を `sys_lseek` で読み直して文字列比較 (誤検出も見逃しも無い)。**1 行でも不正なら何も書かない**。
- 2 巡目 (ファイルを先頭から読み直す。**1 巡目と同じ検証関数を全行に再適用**し (構文・値・重複 (hash 表を作り直す)・件数・ヘッダの版が 1 巡目の結果と一致)、違反や不一致は commit 前に失敗 = rollback (往復 2 の R7: 巡回の間にホスト側でファイルが差し替わる経路)。再 open / read / parse の失敗も rollback 対象): `cfg_open(&db, 1)` → `cfg_begin` → 置換 (`--merge` 無し): `cfg_delete_scope(db, scope)` (**新規 API**: `scope` が NULL なら `DELETE FROM settings`、非 NULL なら `WHERE scope=?`。txn 必須・失敗で txn failed・bind・再入禁止は既存規則を継承) → 対象行を `cfg_set_*` / **`cfg_set_null(db, scope, key, type)`** (**新規 API**、往復 1 の B9: `v:null` は「宣言型つきの NULL 行」として格納し、`cfg list` の `(unset)` 行と export の `v:null` が往復で保たれる。`INSERT OR REPLACE … VALUES(?,?,?,NULL,NULL,NULL)`) → `cfg_commit` → `cfg_close`。置換では削除後なので**素の INSERT** でよいが、実装は既存の `cfg_set_*` (INSERT OR REPLACE) を使い、重複は 1 巡目の検査に任せる (INSERT OR REPLACE に重複検出をさせない)。`--merge` は削除せず set だけ。**単一トランザクション** (DESIGN §6b、FOUNDATION §6-4)。
- 失敗の保証: commit 前の失敗 (set / 2 巡目の read / parse) は `cfg_close` の rollback で **1 行も残らない** (rollback が成功した場合。rollback 失敗は `cfg_last_close_error` に出る)。**commit 成功後の close 失敗は更新済み** (`imported … (close failed <code>)` と表示、終了 1)。
- 状態: MISSING / CORRUPT / VERSION では `cannot import: <status>` で終了 1 (S2 の規則 3: set は OK 状態でだけ)。
- 出力: `imported <n> records (<scope>|all scopes), replaced|merged`。
- `cfg export` との往復: export → import (置換) で `cfg list` (NULL 行の `(unset)` を含む) が一致することをホスト試験で固定。

ホスト TDD (`cfg_host.c` §C): reader の受理 / 拒否 (エスケープ 6 種、**文字列だけ**に UTF-8 / NUL 禁止を適用し復号 blob には適用しない (空 blob、`00` / `FF` を含む blob、全制御文字の 255B text)、`\u` サロゲート拒否、数値境界、順序違い、空白入り、余分なキー、最長行 5,629B、ヘッダ無し、版 2 拒否)、CR を含む text の往復、NULL 行の往復、8192 件の受理と 8193 件目の拒否、hash 衝突の非重複 2 件の受理、`--scope` の削除範囲と **scope 外の値の不変**、`--merge`、途中失敗 (set / 2 巡目の read) で 1 行も残らない (RAM backend に注入)、**巡回の間の差し替え** (2 巡目に重複行 / 件数違い / 版違いを注入) で rollback、`CfgBackend` に `sys_lseek` を足す (衝突再読)、commit 後 close 失敗の表示、MISSING / VERSION の拒否。

## 3. 共有ファイルと FDD

- `build/app.conf`: `userland/system/install 50 262144` (PM)。`cfg` は 50 で登録済み。
- `build/image.mk`: `FDD_MIN_CMDS` に `cfg` を足す (PM) — FDD の `/bin/cfg.bin` で **FDD 自身のマスタ**の `cfg status` を確認する用途 (HDD の DB には使えない、B11)。容量は D88 のファイルサイズではなく **生成の成功とクラスタ使用量 / 空き** (`tools/mkfat12.py` の出力) で判定。
- 配備: `install.bin` は `/sbin` (HostDrv / NHD) と FDD の両方に載る (既存)。

## 4. 受入 (ゲスト、PM / テスター。FDD ブートは NP21/W を `os32_boot.d88` 引数付きで起動 = ini 変更なし、memory `os32-np21w-launch`)

| ID | 試験 | 合格 |
|---|---|---|
| I1 | HDD ブートで `install --recover-settings` | `must run from the install floppy` で終了 1、DB 不変 (root の st_dev で判定) |
| I2 | `cp /etc/settings.tsv /etc/settings.db` (壊す) → `os32gui` → 起動時通知 CORRUPT (S4) → FDD ブート → `install --recover-settings` → 表示 → `Y` | `recovered: schema_version 1, sync=0, reopen=ok`。`/hd0/etc/settings.db.bak` = 壊れた元 (1406 B)、`settings.db` = マスタ 3072 B。HDD ブート → `cfg status` OK → GUI 通知なし |
| I3 | DB 欠損 (`rm`) → FDD ブート → recover | `missing` 表示 → `Y` → 復元。`.bak` は作られない。派生: 欠損 + 孤立 journal → journal が `.bak-journal` へ (対の一部)、欠損 + 旧 `.bak` → 旧 `.bak` は消える (承認済み)、欠損 + `.new` 残骸 → `stale settings.db.new present` で停止 |
| I4 | 偽 journal あり (`cp` で `settings.db-journal`) → recover | 表示に `hot journal present` → `Y` → `.bak` と `.bak-journal` の**対**が残り、`settings.db-journal` は無い |
| I5 | `N` で承認しない | 何も変わらない (hash 一致)、終了 0 |
| I6 | 障害注入 (ホスト TDD のみ): 各コピー失敗 / バイト不一致 / 切替 rename の両名残存 / **置換先 unlink 後の新名追加失敗 (`.new` だけ → 写しから復元)** / close 失敗 (文言に REBOOT) / 印の書込み・読み戻し失敗 / 印の phase による門 / 本体欠損時の revert と、**連鎖** (失敗 → 再実行、recover → revert → recover、revert 途中失敗 → 再 revert、`.new` close 失敗 → 次回停止 → `rm` → 成功) | 切替前の失敗では元の対が 1 バイトも変わらない、欠損は欠損のまま、自分が作ったものだけ消える、両名残存で消さず停止、journal が別世代と混ざらない |
| I7 | `--revert-settings` (I2 の後、および I3 の後) | I2: 現在の対が `.failed` に写され、`.bak` の内容が `settings.db` に戻る (`cfg status` = CORRUPT = 壊した元)。I3: 印 `orig=missing` により `settings.db` が消え、欠損に戻る (`cfg status` = MISSING) |
| C1 | `cfg export /tmp/a.json` → `cfg set` で 2 件変更 → `cfg import /tmp/a.json` | `cfg list` が export 時点に戻る (置換)、`imported <n> records` |
| C2 | `cfg import` で `--scope gshell` / `--merge` | scope 外が残る / 既存が消えない |
| C3 | 版 2 のヘッダ (手で書く) / 壊れた行 | 拒否、DB 不変 (hash) |
| C4 | GUI 端末から `cfg import` | CUI と同じ |
| C5 | FDD ブートの `cfg.bin` で `cfg status` (FDD 自身の `/etc/settings.db` = マスタ) | `OK schema_version 1`。**マスタの検査に限定** (FDD から HDD の DB は読めない・直せない) |
| C6 | JSON からの復元の正式手順: `cfg export /tmp/b.json` → DB を壊す → FDD で `--recover-settings` → HDD 再起動 → `cfg import /tmp/b.json` | export 時点の `cfg list` に一致 |
| C7 | CR を含む text と NULL 行の往復 | CR はシェル行に入れられないのでホスト試験で担保。NULL 行は **`v:null` を含む JSON を `cfg import` すれば作れる** (往復 2 non-blocker) → ゲストで import → `cfg list` に `(unset)` → export → import で一致を確認 |

## 5. レビューで見てほしい点

1. §1 の各段が FOUNDATION §6 の 4 契約を満たすか (特に「失敗で元を消さない」「両名残存で停止」「原子的と名乗らない」「journal を対で」「再実行で壊さない」「切替後失敗の復帰手順 = `--revert-settings`」)。
2. 起動媒体の検査 (HDD ブートからの実行拒否) が確実か、FDD ブートで DB の利用者が本当にいないか (FEP は `ime on` するまで開かない、gshell は起動していない)。
3. `install.c` の要求 KAPI を 50 に上げることの影響 (FDD の古いカーネルでは動かない = 同じ媒体に載るので問題ないか)。
4. `cfg import` の 2 巡と単一トランザクション、`v:null` の扱い、置換の削除範囲、版の判定、JSON reader の受理範囲の狭さが export の出力を**すべて**読めるか (blob base64、`\u` エスケープ、255B text の最長行)。
5. FDD の容量と `cfg.bin` の追加、`install.bin` のサイズ増 (db_* v50 の meta 検査を持つ)。
6. ホスト TDD の障害注入が §1 の全段を覆うか。

## 6. ユーザー判断が要る点

- **受入イメージ (決裁 2026-09-13)**: 作業 NHD をバックアップしたうえで実走する (ini ツールは HDD キー未対応で、recover が触るのは `/hd0/etc/settings.db*` の 9 名だけ)。使い捨てイメージ (ini ツールの HDD キー拡張) は S3-I2 (format が走る) のときに別タスクで。

## 7. 残ゲート (本票で直さない)

- **S3-I2 (ユーザー決裁 2026-09-13: 別票にする)**: 通常インストール (`install` 無印) は `/kernel.bin` を必須とし FDD は `/VMKRNL.LZ4` + `/boot` を収録するので、FDD からの新規インストールが `/etc` コピー (settings.db の seed) まで到達しない (TASK_S0 §3 B10、S0-T の T1 は「媒体に入っている」まで)。修正は install の lz4 カーネル + `/boot` レイアウト対応で、受入には**使い捨て NHD** ([D2]) が要る。本票の後に別票で。
- ジャーナル: DELETE journal の回復・電源断の crash durability (FOUNDATION §6「保証上限」) は未検証のまま (S5 で「set → ハードリセット → 保持」までは確認)。

## 8. レビュー記録

| 版 | 判定 | 要旨 |
|---|---|---|
| 第 4 版 (追加往復) | Request changes | 1 件: revert の途中失敗が `phase=done` を残し、次の recover が壊れた本体を `.bak` にして元の唯一の写しを失う → revert は破壊段の前に `phase=reverting` を書き、復元・比較・sync がすべて成功したときだけ `done`。non-blocker: close 失敗後の手順を「再起動 → revert → rm .new → recover」に統一、門の許可条件を白リスト (`done` / 印無し) で明記 |
| 第 3 版 | Request changes | 5 件: B1 印の rename 失敗で確定印を壊す → 印は直接書いて読み戻す、B2 最終 rename の `.new` だけの枝で元 DB が消えている (置換先 unlink 後の新名追加失敗) → 写しから本体も復元、失敗は `phase=failed` で門、B3 現在の本体が欠損すると revert が止まる → PRESENT / ABSENT で分岐、B4 close 失敗後に同じ起動で rm / 上書きすると隔離接続の対象 → 再起動を明示の前提、B5 回復ファイルが通常配備から保護されない → S3-D で名前を追加。**3 往復で Approve に至らず → ユーザー決裁** |
| 第 2 版 | Request changes | 7 件: R1 restore_pair の部分失敗後の再実行で元 journal を消す、R2 検証 close 失敗後の revert が入口で停止、R3 元欠損の切替後失敗を欠損に戻せない、R4 `.failed` が同一性検査から漏れる、R5 revert が現在の journal を旧 DB に付ける、R6 `/hd0` に別ドライブが刺さっていても対象にする、R7 2 巡の間に入力が差し替わると未検証の重複を書く。→ 第 3 版: 退避 / 復帰を rename ではなく「コピー + バイト比較」に組み替え (元の対は最終切替まで不変、両名 / 分離の状態が生まれない)、10 名の同一性検査、印 `recover-state` で元の欠損を記録、revert は現在の対を `.failed*` に写してから戻す、対象デバイスは st_dev で確認、import は 2 巡目も全検証 |
| 第 1 版 | Request changes | 11 件: B1 `/hd0` マウント有無の判定が逆 → root の st_dev で FDD を判定し `/hd0` は別に mount、B2 両名残存の後の再実行で `.bak` unlink が本体を壊す → 事前に 6 名の同一性検査、B3 既存 `.new` を自分の生成物扱い + `.new-journal` で RO open が BUSY → 残骸があれば停止、B4 失敗時に DB と journal が分離 → 状態機械 + `restore_pair()`、B5 長さ + meta + 件数では内容一致を保証しない → バイト比較 + 全行走査 (integrity_check は OMIT)、B6 切替後失敗の復帰手順と close 失敗 → `--revert-settings` と close 記録、B7 reader が `\r` を拒否 → 許可、B8 4096 件上限 → 8192 + hash 重複検出、B9 NULL 行を捨てると list が一致しない → `cfg_set_null`、B10 `--scope` が削除だけ限定 → 抽出も限定、B11 FDD の cfg import は HDD 復元の代替にならない → 正式手順 C6。non-blocker: 最長行 5,629B、`cfg_delete_scope` の契約、2 巡の失敗保証の文言、三値 stat、容量の判定法、受入イメージの決裁、S3-I2 の残ゲート |

## 9. 実装と受入の記録 (PM、2026-09-13)

### 9a. 着地
- `b1ceac6` PM: install の要求 KAPI 50、FDD の最小コマンドに `cfg`。
- `6332dac` S3-D: `PROTECTED_BASENAMES` / `hsp_protected_names` 5 → 11 名、`hsync.c` の `HS_MAX_PROT` 16 → 24 (票外の付随変更: リカバリ途中の `/etc` は 9 名 + wal/shm = 11 名になりうるので、実在名の収集上限 16 だと別名が 5 つで通常同期が止まる。表の件数 11 を Python / C の両試験で固定)。ホスト 164 / 114 (RED 21 / 39 → GREEN)。
- `c2a1cdd` S3-I (install_recover.inc、host 14/14、後退 17 種を全検出、install.bin 18,320 B)、`e1f6791` S3-C (cfg_json.c / cfg_import.c、host 52/52 + 214 CHECK、cfg.bin 37,312 B、shlib は 105,544 B で不変 = cfg_import.o は shlib に乗らない)。`make all` / `check` exit 0。FDD イメージに INSTALL.BIN / CFG.BIN / SETTINGS.DB / VMKRNL.LZ4 を確認。
- 配備 (S3 1 回目、`e1f6791`)、NHD バックアップ `os32.nhd.bak-s3-20260913-233601` (scratchpad)、kselftest 87 / 0。

### 9b. ゲスト受入 (HDD ブート、`e1f6791`)
| ID | 結果 |
|---|---|
| I1 | **合格**: `install --recover-settings` (HDD ブート) → `recover-settings must run from the install floppy`、DB 不変 |
| C1 | **合格**: export (3 件) → set で 2 件変更 → `cfg import /tmp/b.json` → `imported 3 records (all scopes), replaced`、`cfg list` が export 時点に戻る (`user note` も消える) |
| C2 | **合格**: `--scope gshell` で `user note` が残る、`--merge` で既存が消えない |
| C3 | **合格**: 版 2 ヘッダ → `newer backup: schema_version 2`、不正 key → `bad line 3: key`、いずれも DB 不変 |
| C7 | **合格**: `v:null` を含む JSON (HostDrv 経由 `/host/s3_null.json`) を import → `cfg list` に `app:demo title text (unset)`、blob `blob:4` → export → `"v":null` / `"AAECAw=="` → import で一致、`cfg get app:demo icon` = `00010203` |

### 9c. FDD ブートの受入 (1 回目、`e1f6791`) — **I2 で不合格 → S3-K**
- FDD ブート (`np21x64w.exe os32_boot.d88`、ini 変更なし) は成立: root = FAT (`LOADER.BIN` / `VMKRNL.LZ4` / `SYS` / `BIN` / `SBIN` / `ETC`)、`/hd0/etc` に壊した `settings.db` (1406 B) が見える。
- `install --recover-settings hd0` → **`master unreadable`** で終了 1 (対象には触っていない = 契約どおり)。FDD の `cfg status` も `ERROR sqlite=10` (IOERR)。
- 原因 (PM の切り分け): FDD は FAT で `FF_USE_LFN 0`。v50 の open 前検査が `settings.db-journal` を `vfs_stat` すると 8.3 に収まらない名前で `f_stat` が `FR_INVALID_NAME` → `VFS_ERR_INVAL` → 「NOTFOUND 以外は IOERR」。実機: `ls -l /etc/settings.db-journal` = `Invalid argument`、`ls -l /etc/nosuch` = `No such file`。**S0-K の設計時に FAT (FDD ブート) で v50 を通す検証が無かった** (S0-T の T1 は「媒体に入っている」まで)。
- 直し (S3-K): `fatfs_vfs_stat` で `FR_INVALID_NAME` → `NOTFOUND` (そのボリュームに存在しえない名前は存在しない)。カーネル変更なので `deploy-nhd` + FDD イメージ再生成 (同じ vmkernel.lz4) が要る。

### 9d. FDD ブートの受入 (2 回目、`b2a1580` = S3-K 反映、vmkernel 470,761 B、FDD イメージ再生成)
- 手順: NHD に `deploy-nhd` (カーネル) → `make deploy` → NP21/W を `os32_boot.d88` 引数で起動 (ini 変更なし)。FDD の `cfg status` = `OK schema_version 1 pool 24768 B` (マスタが v50 で開ける)。対話は rshell を ESC で抜けて `/api/key` で打ち、`/api/tvram` で `[Y/N]` と結果行を読む (`/api/cmd` が保留中だと tvram が応答しないため)。
| ID | 結果 |
|---|---|
| I2 | **合格**: 壊した DB (1406 B) → recover → 表示 (`master: schema_version 1, 3 keys` / `settings.db: present 1406 B` / 承認文) → `Y` → `recovered: schema_version 1, 3 keys, sync=0, reopen=ok, close=ok`。`settings.db` = 3072 B (マスタ)、`.bak` = 1406 B、印 `phase=done orig=present journal=absent size=1406` |
| I4 | **合格**: 壊した DB + 偽 journal → 表示に `hot journal present - will be backed up as .bak-journal and removed before switch` → `Y` → `.bak` 1406 + `.bak-journal` 1406、`settings.db-journal` は無い、DB = マスタ、印 `journal=present` |
| I7 | **合格**: I4 の後に `--revert-settings hd0` → 表示 (`settings.db: present 3072 B`、印 `orig=present journal=present`) → `Y` → `reverted: orig=present, sync=0`。`settings.db` = 1406 (元)、`settings.db-journal` = 1406 (元)、`.failed` = 3072 (マスタ)、`.bak*` は残る |
| I5 | **合格**: `N` → 何も変わらない (直後の recover の表示が同じ状態を示す) |
| I3 | **合格**: `rm /hd0/etc/settings.db` → recover → 印 `orig=missing journal=absent`、DB = マスタ、`.bak` は作られない → `--revert-settings` → `reverted: orig=missing, sync=0`、`settings.db` は**消えて欠損に戻る** (`.failed` = 3072 にマスタの写し)。最後にもう一度 recover してマスタを置いた (`.failed` は残る、印 `phase=done`) |
| I6 | ホスト TDD のみ (14 ケース、後退 17 種検出)。往復 1 の B1 / B2 / B5 の複合ケースは修正後に追加 |
| C5 | **合格**: FDD ブートの `cfg status` = `OK schema_version 1 pool 24768 B` (S3-K の後。マスタの検査に限定) |
| C6 | **合格**: HDD ブートに戻して `cfg status` OK、`/etc` は `settings.db` 3072 + `.failed` + 印。壊す前に取った `/tmp/b.json` を `cfg set` で変えた後に import → export 時点の値に戻る。`os32gui` は起動時通知なしで通常のデスクトップ (`s3_gui_after.png`) |
| C4 | **合格** (配備 2 回目、`5f2ee25`): GUI 端末で `cfg import /tmp/b.json` → `imported 3 records (all scopes), replaced`、`cfg list` が戻る (`s3_c4_term.png`)。CUI の `cfg get` = 12 |

### 9e. Codex 実装レビュー
| 対象 | 判定 | 要旨 |
|---|---|---|
| `6332dac` + `c2a1cdd` + `e1f6791` (往復 1) | Request changes | 5 件: B1 `.new` の検証失敗と close 失敗の複合で `.new` を消す、B2 revert の journal 復元失敗で journal を消す、B3 `--scope` 外の値の不正で対象まで拒否、B4 commit 前の rollback/close 失敗が CLI に出ない、B5 複数行の壊れた印を `done` と受理。non-blocker: base64 の未使用ビット、印の size の wrap、案内の統一、8192 件の実書込み未検証、試験網羅。設計の残 1 件 (`phase=reverting`) は「防いでいる」と確認 |
| `cedfc6a` (往復 3) | **Approve** | B3 解消 (非正準 base64 は傷として控え対象行だけ拒否)、修正による新規 blocker なし、B1 / B2 / B4 / B5 と S3-K の範囲、`phase=reverting` の保護を維持。non-blocker: 試験コメントの範囲、8192 件の限定 |
| `7b26058` (I) + `5f2ee25` (C) + `b2a1580` (K) (往復 2) | Request changes | B1 / B2 / B4 / B5 修正確認、S3-K の写像範囲は妥当。**B3 が一部残存**: 対象外 scope の base64 未使用ビット (`AB==`) で `j_b64()` が即 E_VALUE を返し scope 除外に到達しない → 意味上の不正はフラグに控えて対象行だけ `cfg_json_check()` で拒否。non-blocker: 全失敗点の網羅 (一部未固定)、8192 件試験の意味の限定、C-4 の記録更新、`.new` だけの復元成功枝の done 更新の戻り値 |
| 付随 (PM) | — | `285653e` / `a57eaf6`: `install_recover.inc` を直しても `install.bin` が再ビルドされない (userland の .d は Makefile の `-include` 対象外) → `install.elf` / `hsync.elf` に .inc の明示依存。**配備 2 回目の install.bin (18,320 B) は往復 1 の修正前の版だった** (I2〜I7 の合格は 1 回目の実装での結果。修正後の install.bin 18,512 B で I2 / I7 を再走する) |

### 9f. 再検証 (修正後の install.bin 18,512 B、配備 3 回目 `a57eaf6`)
| ID | 結果 |
|---|---|
| I2 | **合格** (壊した DB → recover → `recovered: schema_version 1, 3 keys, sync=0, reopen=ok, close=ok`、`.bak` 1406、DB 3072) |
| I7 | **合格** (`reverted: orig=present, sync=0`、DB 1406 に戻る、`.failed` 3072) → 再 recover でマスタに戻し HDD ブート → `cfg status` OK |
| B3 の入力 (配備 4 回目 `cedfc6a`、cfg.bin 38,272 B) | **合格**: `cfg import /host/s3_mix.json` (対象外 `user` 行に `"AB=="`) → 全 scope は `bad line 3: value`、`--scope gshell` は `imported 1 records (gshell), replaced` で `desktop/color` = 7。`/tmp/b.json` で既定に戻す。regress 6 / 6、kselftest 87 / 0 |
