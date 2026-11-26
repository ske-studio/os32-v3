# userland/lib/cfg/cfg_import.c — 設計

> 状態: **現行 (2026-11-18)** — 出所: TASK_S3 (S3-C) の契約を今のソースと照らして要約
ソース: `userland/lib/cfg/cfg_import.c` (`cfg_import_file`・`cfg_import_detail_name`)・`userland/cmds/cfg.c` (`do_import`)

`cfg import <file> [--scope <scope>] [--merge]` の実体。`cfg export` が書いた JSON を検証してから単一トランザクションで書き戻す。
JSON の読み方は `docs/design/userland/lib/cfg/cfg_json.md`、トランザクション・値の規則は `docs/design/userland/lib/cfg/libos32cfg.md` の「設定レジストリの全体の約束」にある。マスタからの復旧 (`install --recover-settings`) とは別の機能。

## 対象の行

- `--scope <s>` があれば、検証・重複の検出・書き込みのすべてを scope = s の行だけに絞る。他の scope の行は構文だけ見て (`cfg_json_record`)、値の上限・名前の規則 (`cfg_json_check`) は見ない。scope 外の値は変わらない。
- scope を丸ごと復号できなかった行 (`cfg_json_scope_usable` が偽) を対象と取り違えない。63 byte の対象名に前半が一致する長い scope は対象ではない。
- 不正な `--scope` は DB を開かずに `CFG_IMPORT_E_ARG` (`bad scope`)。

## 2 巡

1. **1 巡目 (検証だけ、DB は開かない)**: ヘッダ → 各行の構文 → 対象行の意味の検証 → (scope, key) の重複。1 行でも不正なら何も書かない。対象行の上限は `CFG_IMPORT_MAX_ROWS` (8192 = 現行 export の最大 32 scope × 256 key)、超えたら `CFG_IMPORT_E_MANY`。
2. `cfg_open(&db, 1)` → 状態が `CFG_OK` でなければ `CFG_IMPORT_E_STATUS` (`cannot import: <状態>`、MISSING なら `settings.db missing: run 'cfg init'` を足す) → `cfg_begin` → 置換なら `cfg_delete_scope(db, scope)` (scope が NULL なら全部)。`--merge` は消さない。
3. **2 巡目**: ファイルを先頭から読み直し、**1 巡目と同じ検証を全行に掛け直しながら** `cfg_set_*`・`cfg_set_null` で書く。重複表も作り直す。件数・ヘッダの版が 1 巡目と違えば `CFG_IMPORT_E_CHANGED` (`input changed during import`)。巡回の間にファイルが差し替えられた場合を commit の前に止める。
4. `cfg_commit` → `cfg_close`。

- `v:null` は宣言型つきの NULL 行 (`cfg_set_null`) として入れる。`cfg list` の `(unset)` と export の `v:null` が往復で保たれる。
- 置換は削除の後なので INSERT OR REPLACE でも重複を上書きしない。重複は 1 巡目で検出し、INSERT OR REPLACE に任せない。

## 重複の検出

(scope, key) の FNV-1a を `IMP_HASH` (16384 スロット、件数上限の 2 倍) の開番地の表に入れる。衝突したら先行行を `sys_lseek` で読み直して文字列で比べるので、誤検出も見逃しも無い (hash が衝突する別物 2 件は両方受ける)。`lseek` が使えなければ失敗。
表を件数上限と同じ大きさにすると上限いっぱいの入力で読み直しが膨らみ、ゲストでは止まって見える。

## 失敗と保証

| 戻り | `cfg` の表示 | DB |
|---|---|---|
| `E_HEADER` | `not a settings backup` | 開いていない |
| `E_VERSION` | `newer backup: schema_version <n>` (版が自分より新しい) | 同上 (2 巡目なら rollback) |
| `E_LINE` | `bad line <n>: <理由>` (`cfg_import_detail_name`) | 同上 |
| `E_DUP` | `duplicate record at line <n>` | 同上 |
| `E_LONG` | `line <n> too long` | 同上 |
| `E_MANY` | `too many records` | 開いていない |
| `E_OPEN`・`E_IO` | `cannot open the import file`・`read failed` | 2 巡目なら rollback |
| `E_WRITE` | `import failed (<code>)` | rollback |
| `E_CLOSE` | `imported <n> records (…), … (close failed <code>)` + 終了 1 | **commit 済み = 更新されている** |

- commit 前の失敗は `cfg_close` の rollback で 1 行も残らない。ただし rollback・close が失敗したら、その保証は成り立たない。`CfgImportInfo.cleanup` に **この呼び出しの中の** `cfg_close` の最初の失敗コードを置き、`cfg` は原因の文言の同じ行に ` (rollback/close failed <code>)` を足す。rollback が通る通常の経路では付かない。
- 成功の表示は `imported <n> records (<scope>|all scopes), replaced|merged`。
- path は export と同じく絶対名へ正規化してから開く。

## 静的領域

重複表 64KB + 行バッファ 6KB × 2 + `CfgJsonRow` × 2 で約 85KB。`cfg_import.o` を引くのは `cfg.bin` だけで、読むだけのアプリには乗らない (アーカイブのメンバ単位のリンク)。

## 検査

`cfg-host` (`test_cfg.py (旧版 f84b12760091)`、本体 `cfg_host.c (旧版 947ea5937a47)`) の `s3_roundtrip` (実 writer の出力を import して `cfg list` と再 export がバイト単位で一致)・`s3_scope`・`s3_args`・`s3_reject` (どの拒否でも DB が 1 行も変わらない)・`s3_fail` (途中の失敗で削除ごと巻き戻る、commit 後の close 失敗)・`s3_gen` (8192 件は通り 8193 件は拒否、巡回の間の差し替え)・`s3_bulk` (8192 件の実書き込み)。贋物は KAPI の境界と障害の注入だけ。
