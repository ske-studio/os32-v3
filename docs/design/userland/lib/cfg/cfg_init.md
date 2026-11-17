# userland/lib/cfg/cfg_init.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S2 の契約を今のソースと照らして要約
ソース: `userland/lib/cfg/cfg_init.c` (`cfg_init`・`cfg_last_init_reason`)

`cfg init` の実体。tsv (既定 `CFG_TSV_PATH` = `/etc/settings.tsv`) から `/etc/settings.db` を新しく作る。作るのは本体が明示的に無いときだけで、既存の DB や残骸は消さない。
レジストリ全体の約束は `docs/design/userland/lib/cfg/libos32cfg.md` の「設定レジストリの全体の約束」。

## 手順

1. 本体を stat する。ある (0 byte でも) → `CFG_INIT_EXISTS` (`OS32_ERR_EXIST`)。NOTFOUND 以外の失敗 → `CFG_INIT_STAT` (`OS32_ERR_IO`)。stat は三値 (ある・明示的に無い・判定不能) で持ち、判定不能を「無い」に丸めない。
2. 残骸を見る。`settings.db-journal` が無いと言い切れなければ `CFG_INIT_JOURNAL`、`settings.db.new`・`settings.db.new-journal` なら `CFG_INIT_STALE_NEW` (どちらも `OS32_ERR_NOTEMPTY`)。残骸は消さない。
3. tsv を 1 巡目で全行検証する (書かない)。開けない → `CFG_INIT_TSV` (`NOTFOUND`)、規則違反 → `CFG_INIT_TSV` (`INVAL`)。この時点では何も作らない。
4. `.new` を作り (`db_open`)、`page_size=1024`・`journal_mode=DELETE`・`user_version=1` (カーネルの SQLite では無効)・DDL・`BEGIN`・meta 1 行 (版 1、`created` は `get_tick` の 10 進)・tsv の 2 巡目の流し込み・`COMMIT`。重複した `(scope, key)` は `settings` の PRIMARY KEY が `SQLITE_CONSTRAINT` で弾き、`CFG_INIT_TSV` になる。
5. `.new` を close する。close の失敗は `CFG_INIT_CLOSE` で、コードを `cfg_last_close_error` に残し、`.new*` を消さない (接続が隔離されているため)。close が成功して生成だけが失敗したら、手順 2 で無かったことを確かめた自分の `.new*` だけを消して `CFG_INIT_BUILD` (または `CFG_INIT_TSV`)。
6. `.new` → 本体へ rename する (下の「rename の後の両名検査」)。
7. 本体を `cfg_open` の RO で開き、`CFG_OK` で版 1 であることを確かめる。だめなら `CFG_INIT_VERIFY`。

生成物の形: `meta (schema_version INTEGER NOT NULL, created TEXT)` 1 行、`settings (scope, key, type, ival, tval, bval, PRIMARY KEY (scope, key)) WITHOUT ROWID`。ホストの `tools/mk_settings_db.py` が作る媒体の DB とは `user_version` (こちらは 0) と `created` の書式だけが違う。

## rename の後の両名検査

rename は原子的と仮定しない。失敗を返したら本体と `.new` の両方を stat して決める。

| 本体 | `.new` | 扱い |
|---|---|---|
| ある | ある | 同じ inode を 2 名が指しうる。どちらも消さず `CFG_INIT_AMBIGUOUS` |
| ある | 明示的に無い | 旧名の削除は済んでいる。本体を RO で検査して通れば成功、だめなら `CFG_INIT_AMBIGUOUS` |
| ある | 判定不能 | `CFG_INIT_AMBIGUOUS` |
| 明示的に無い | 無い・ある | 何も起きていない。`.new*` を片付けて `CFG_INIT_RENAME` |
| 判定不能 | — | どちらも触らず `CFG_INIT_AMBIGUOUS` |

`.new*` を消すのは本体が明示的に無いときだけ。

## 失敗理由と `cfg` コマンドの文言

`cfg_init` の戻りは `OS32_ERR_*` (新しい番号は取らない) で、理由は `cfg_last_init_reason` で引く。

| 理由 | 文言 |
|---|---|
| `CFG_INIT_EXISTS` | `already exists` |
| `CFG_INIT_JOURNAL` | `needs recovery: journal present` |
| `CFG_INIT_STALE_NEW` | `needs recovery: stale .new` |
| `CFG_INIT_TSV` | `cannot read the tsv` |
| `CFG_INIT_CLOSE` | `init failed: close` |
| `CFG_INIT_AMBIGUOUS` | `needs recovery: rename left both names` |
| `CFG_INIT_RENAME` | `init failed: rename` |
| `CFG_INIT_VERIFY` | `init failed: verify` |
| `CFG_INIT_STAT` | `needs recovery: cannot stat` |

## 検査

`tools/tests/test_cfg.py` の `init` (既存・0 byte・journal・`.new` の残骸で拒否して消さない、生成物の形、tsv の欠損と違反で `.new` が残らない) と `rename` (VFS の模型で「新名追加 → 旧名削除」の途中失敗を注入)。ext2 の rename の実物はホストでは踏まない。
