# S0 — settings 前提基盤契約

> 発行: PM (2026-09-09) / 状態: **受入完了 (2026-09-13)**

本票の作成は基盤実装・試験合格を意味しない。
正典: [DESIGN](../../tasks/settings/DESIGN.md)、[v1.3 PLAN §4](../../tasks/gui/v13/PLAN.md#4-s0で凍結する事項)。
本書はその未成立条件を狭い実装票へ分解する。設定仕様の重複した正典にはしない。

## 0. 権限・境界

- v1.3 全体の継続開発は承認済み。通常の実装・レビュー・非破壊試験は各ゲート通過後に継続する。
  **媒体作成・フォーマット・NHD上書き・既存設定置換・強制終了試験を暗黙に承認したものではない**。
- 本書作成時の書込み所有範囲は本ファイルのみ（後続F1の限定範囲は§3）。DESIGN/PLAN/SDK/生成物は変更しない。
  端末票担当とはファイルを共有しない。後続の `exec/exec.c`、ビルド・SDK・配備共有ファイルは
  PMが排他所有を割り当てて直列統合する。エミュレータ、ネットワーク、環境設定、秘密情報、
  `docs/hw/`、commit、追加エージェント起動は本作業の対象外。
- 以下の「試験」は後続票の受入条件であり、本書作成時は未実行。[C1]〜[C4]、[ABI1]〜[ABI3]、
  [V1]〜[V4]、[D1]〜[D3]を適用する。

## 1. 現行コードで確認した根拠

| 接点 | 現状と必要な修正 |
|---|---|
| `kapi/kapi_db.c` `DbSlot` / `slot_get` / `db_cleanup_all` | ownerなし、全接続回収。closeの戻り値を無視してslotを解放する |
| `exec/exec.c` `exec_exit` | `vfs_close_owned` が `db_cleanup_all` より先。SQLiteのrollback/close時にはFDが既に無効になり得る。親DBも全回収される |
| `fs/vfs_fd.c` | FDは `res_owner_get()` でタグ付け。protect付きFDはowner回収対象外。`vfs_close` はvoid、`vfs_get_size` は無効FDも0なので失敗と空ファイルを区別できない |
| `kernel/ime_dict.c` | FEPはKAPI slot外の直接SQLite接続。main FDをprotectする。これだけでは後から開くjournal等の寿命は保証しない |
| `kapi_db_open` | `sqlite3_open` はRW+CREATE。DELETE PRAGMAの結果も無視。readonly/no-createを保証しない |
| `kapi_db_prepare` / `exec` | prepareは最初のstepまで実行。execは先頭statementだけ実行。SQLコピー領域1024B、長いSQLは切捨て。bindはない |
| `shm_write_row` | 全接続共通16KB SHM。列記述子領域の上限検査不足。大きい値はoffset=0になる。kernelポインタを返す `db_last_error` だけでは安全な診断契約にならない |
| `lib/sqlite3/os32_sqlite_vfs.c` | seek失敗無視、truncate成功no-op、delete失敗全て成功、access失敗を不存在化、fullpathは単なるコピー。lock/unlock/reserved検査はno-op |
| 同 `os32Sync` / `fs/vfs.c` / `fs/ext2_super.c` | xSyncは `vfs_sync()` の失敗を捨てる。vfs_sync自体はbackend失敗を返す。ext2_syncはsuper/GD書込みであり、デバイスcache flush・電源断耐性の証明ではない |
| SQLite config / init | THREADSAFE=0、OMIT_WAL、TEMP_STORE=3、手動初期化、MEMSYS5 384KB。`os32_sqlite_init` はpool設定・initialize/VFS登録であり媒体生成ではない |
| `build/deploy.mk` | deploy-kernelはHostDrv sync→**manifestを見ない**sync-from-hostdrv→prune→NHD deploy。単一kernelファイルだけの配備ではない |
| `tools/nhd_deploy.py` | do_syncはmanifestからcp、do_sync_from_hostdrvは再帰的に全ファイルcp。既存/欠損とも対象になる。cp失敗後continueし成功を返す経路あり |
| `tools/hostdrv_deploy.py` / `prune_stale.py` | HostDrvもcopy2で新規・上書き。pruneは所定system dir直下のbinのみで、現行ではetcを削除しない。manifestから除くだけでは再帰コピーを防げない |

## 2. 先に凍結する非破壊・transaction契約

1. **runtime初期化とDB生成を分離**する。カーネルSQLite初期化、cfg状態・メモリ既定値の初期化は許すが、
   `/etc/settings.db` のcreate/DDL/seed/自動migrationを行わない。欠損はSQLite空DBでなくライブラリ内の
   fallbackオブジェクト。DB/journalを生成せず、読取りは最小既定値、書込みは拒否。
2. cfgはまず真のRO+no-createでschemaを検査する。空・破損・開けない・異版は元ファイルとjournalを
   保存して通知する。異版は認識済みの列/型だけ読む。認識不能なら既定値。全更新を拒否する。
   ROでhot journalの復旧が必要なら失敗として通知し、RW再openやjournal削除で自動回復しない。
3. writable要求でもopenがBEGINを実行しない。検査済み既存DBをRW+no-createで開き、schemaを再検査、
   `cfg_begin` の明示呼出しのみで `BEGIN IMMEDIATE`。BEGIN/set/COMMITは別々の単一statement。
   set/delete/importはtransaction外では拒否。set失敗でtransactionをfailed状態にし、commitは拒否してrollback。
   close/owner終了は未commitをrollbackしてからclose。rollback/close失敗を成功扱いせず、診断・回復待ちにする。
4. schema検査からRW切替までの競合は接続直列化で防ぐ。検査のためのstatや `query_only` は
   RO/no-createの代用にしない。DBが無効な場合にRWで修復を試みない。
5. cfg用値: int32、scopeはUTF-8で最大63B（本票の追加提案）、keyはDESIGNの小文字階層名で最大63B、
   textはUTF-8最大255B、blobは0〜4096B。文字列の不正UTF-8/埋込みNUL/長さ超過を拒否。
   NULLと空text/blobは区別。SQL引用やblobのhex化で回避せずbindを使う。
   cap不足はエラーで出力を部分更新しない。get_intのdefとは別にcfg_statusへエラーを保持する。
   enumはrowをprivate領域へコピーしてcallbackを呼び、同一cfg接続の再入・close・更新を拒否する。
6. X4/IRQ/描画callbackからSQLite/VFSを呼ばない。THREADSAFE=0のSQLite呼出し途中に別DB操作を再入させない。
   SHM row/textは次のDB操作で無効。同時保持せず即コピーする。接続の生存とSHM内容の生存を混同しない。

## 3. 狭い基盤票とゲート

順序: **F1 → F2 → F3 → F4 → F5**、**F0 は F4 公開前の ABI ゲート**。
PM判断により、公開ABIを変更しない内部F1はF0より先行可能。配備保護D0は独立にホスト試験可能だが、
共有ファイルを同時編集しない。**F5 + D0 → S1（媒体用マスタ生成）→ S2 → S3/S4/S6 → S5**。
S1の通常NHD登録は許さない。各票はRED（対象挙動がassertionで落ちる）→最小実装→GREEN→独立レビュー。
既存挙動の初期GREENは回帰証拠であってREDの代わりではない。

### F0 — ABI割当・一括再build方針の凍結（F4公開前）

- 所有予定: PMによる `KAPI_SPEC §3-2` 等の予約調停。実装票とは分離する。
- §4の関数・error形式・data field移動対策を承認し、端末/Host Services追加と同じ予約台帳で直列化。
- ゲート: 現行JSON、両generator、loader、C/Rust利用側を照合したslot/offset差分表。
  版だけ上げて「旧ABI互換」としない。予約未確定ならF4の公開・S2統合を止める。

### F1 — DB slot寿命をowner単位にする（FD変更より先）

- 所有予定: `kapi/kapi_db.c/.h` と専用ホスト試験のみ。open時ownerを記録し、
  `db_cleanup_owned(int owner)` を内部関数として追加。通常終了用の全接続cleanupを廃止する準備。
- finalize→未commit rollback→closeを明示close/owner回収/既存全回収で共通化し、
  **close成功時だけslot解放**。途中のfinalize/rollback失敗はclose成功でも成功扱いせず、
  最初の診断をslot再利用まで保持する。close失敗slotは再操作・再利用・再cleanupを禁止し、
  同じexec nestが別の子へ再利用されてもSQLite呼出しを再試行しない。
- **F1の隔離はDB slotだけで、close失敗後のFD生存を保証しない**。
  main/journal/temp全体の追跡・隔離を汎用FD回収より前に成立させるまで**F2統合はblocked**。
  mainだけprotect、全FD protect、無条件slotゼロ化による回避は禁止。
- 今回のF1書込み範囲は `kapi/kapi_db.c/.h`、専用host試験・TDD記録、
  本書のF1境界/F0依存の明確化のみ。exec/VFS/fs/kernelのcleanup統合は変更しない。
  `exec_exit` の既存 `db_cleanup_all()` 呼出しと全active slot回収はF2まで維持するため、
  現時点の実終了経路で親DBが生存するとは主張しない。FEPの直接SQLite接続も変更しない。
- gate: 親A/子Bの接続、同owner複数接続、未完stmt/未commit、失敗open、close/rollback障害、
  二重cleanup、slot再利用を試験。Bだけ消えAを継続できる。隔離資源を「回収済み」と数えない。
- 互換: 既存公開シグネチャ・prepareのfirst-row動作を維持。FEP直接接続は全slot回収の対象にしない。
  数値slotの再利用を跨ぐstaleハンドル保証は現行にはなく、generation付きABIは本票では導入しない。

### F2 — exec終了でDBをFD owner回収より先に片付ける

- 依存: F1、およびmain/journal/temp全体のFD追跡・隔離（F1では未実装）。
  単なる呼出し順変更だけでF2を開始・完了しない。
  所有予定: `exec/exec.c` と終了経路専用試験。端末担当から排他移管後のみ編集。
- `db_cleanup_owned(exec_nest_level)` を汎用FD回収より前へ置く。SQLiteが閉じたFDの再利用後に
  遅延closeしない。F1で隔離した接続のFD群を追跡・隔離し、汎用回収に渡さず、漏れを報告して再利用を防ぐ。
  起動失敗・正常終了・fault・CTRL+STOPの実到達経路を列挙し同じowner契約を適用する。
- gate: mockでrollback時FD生存と順序をassert。子終了後の親DB・redirect・pipe・FEP検索/学習を確認。
  open時だけでなくjournalを後から開く場合も追う。ゲストでは終了理由別にslot/FD/poolの基準復帰を測る。
- 互換: GUI/端末の終了理由・親復帰・redirect仕様は変えない。SHM全cleanupの一般改修は別票、
  本票では親がSHM rowを持ち越さない条件を守る。

### F3 — SQLite VFSの失敗と排他を正直にする

一括変更にしない。以下を順に提出する。公開KAPIはまだ追加しない。

| 小票 | 実装境界 | RED→GREEN / 互換ゲート |
|---|---|---|
| F3a I/O | `os32Read/Write/FileSize/Truncate/Sync/Delete/Access`。負offset・int範囲超過・加算overflowをI/O前に拒否。seek rcを検査。sizeは `vfs_fstat` 等のstatus付き取得。truncate未対応は `SQLITE_IOERR_TRUNCATE`。sync失敗は `SQLITE_IOERR_FSYNC`。deleteはNOTFOUNDだけ無害、他は `SQLITE_IOERR_DELETE`。accessは不存在とI/O失敗を分離 | short readの残りゼロ、short write、seek/size/sync/delete障害を注入。失敗後に誤ったoffsetでI/Oしない。既存DB読取り/DELETE journal transactionが新しいエラーで壊れないこと。必要なtruncate実体は別小票で実装・試験し、no-op成功へ戻さない |
| F3b identity/lock | fullpathの正規化・切捨て拒否。mount/FS/inode等の同一性を使うlock表。相対/絶対・mount別名で同じDBを二重writerにしない。SHARED/RESERVED/PENDING/EXCLUSIVEの競合、unlock、reserved照会、close解放を実装 | 2接続のreader/writer・writer/writer・upgrade失敗・終了解放を試験。識別不能なbackendは安全側に拒否。最小代案は同一DBの全接続を排他openする方式だが、FEP/既存複数接続との互換性をPMが承認してから採用。「single taskだからlock不要」は不可 |
| F3c open/RO | xOpen flags/pOutFlagsを実際のmodeと一致させ、EXCLUSIVE/DELETEONCLOSE等は実装するか未対応失敗。RO接続はmainと関連journalへのwrite/create/delete/truncateを拒否。RW要求のsilent RO fallbackは公開側で検出 | missing/空/破損/異版/hot journal/権限不足で、ROの全経路の副作用traceがゼロ。未対応のflagを成功と返さない。READWRITE(no CREATE)でmissingが作られない。旧db_openの明示的create用途は維持 |

F3aは「sync呼出しの失敗を伝える」ゲートであり、デバイスへの永続化完了を証明するゲートではない。
backendの書込み順序・エラー伝播に穴があれば追加の狭い票へ分離し、settingsの書込み受入を止める。

### F4 — RO/no-create・prepare-only・bindの末尾KAPI追加

- 依存: F0/F1/F2/F3。所有予定: `sdk/kapi.json`、手書き共有定数/DB実体、generatorによる生成物、専用試験。
  正確な追加案は§4。既存db_prepareをprepare-onlyへ変更しない。
- gate: prepareだけではSELECTの先頭行もDMLも進まない→bind→stepで先頭行/更新を一度だけ取得。
  1-based parameter、範囲外、空blob/NULL、4096B roundtrip、長すぎるSQL/path、複数statementを検査。
  untrusted pointer/長さ/境界をCPL3経路で検証し、SQLiteにcallerポインタを保持させない。
- ABIゲート: JSON正典から再生成、既存関数の順序/型不変、C/Rust一致、`make clean`→`make all`、
  `make external`、`make check`。新API要求版をapp設定へ反映。旧成果物と新kernelの混在試験を§4方式に沿って行う。

### F5 — settings向けtransaction・SHM統合検証

- 依存: F4。DB試験だけを追加し、cfg本体/S1資産を先に作らない。使い捨てfixtureを用いる。
- `shm_write_row` のheader+全列descriptor+payloadの境界を事前検査し、超過時に範囲外書込みや
  部分ROWを返さない。既存SHMレイアウト・型番号は維持。最大blobはSQL hexでなくbind経由。
- gate: BEGIN→複数set→COMMIT→close/reopen、ROLLBACK、set失敗→commit拒否、未commit close/子終了、
  disk full、journal削除/sync失敗、4096B blob/0B blob/NULL、列数過大、別接続によるSHM上書き。
  FEPと交互に反復しslot/FD/`db_mem_used`/canaryを測る。意図した常駐分以外の増加なし。
- transactionエラー後のファイル群は勝手に削除しない。エラー後の状態・再open結果を証跡化する。
  commit成功後の通常reopen整合性と、電源断時の完全crash durabilityを区別する。

## 4. 正確なABI追加案と衝突処理

現行SSOTは `sdk/kapi.json` **version=42、api=180、末尾ime_set_render**。
`KAPI_SPEC §3-2` は **v43をHost Services用に予約済み**。同書の概要の「41」は旧記載であり割当根拠にしない。
本書は版番号を取得しない。凍結時点で再読し、次のどちらかを予約正典へ反映する。

- A: Host Servicesが先にv43を実装し、その実際の末尾へsettingsを次版で追加する（v44候補、未予約）。
- B: settingsが先ならPMが予約を変更しsettingsをv43、Host Servicesを後続へ移す。
  端末の追加が先行する場合も同じ台帳で順序と全offsetを再計算。未実装の予約だけの架空slotは作らない。

`api` 配列へ以下をこの順でappendする。JSONは現行と同じ
`{"name": NAME, "ret": "int", "args": [宣言文字列...], "target": "kapi_" + NAME}`。
`includes` の `kapi_db.h` は既存。手書きprototypeはそのheaderへ、実体は `__cdecl`。

| name | args（配列要素順、正確なC宣言） |
|---|---|
| db_open_existing | `const char *path`, `int writable` |
| db_prepare_only | `int handle`, `const char *sql` |
| db_bind_int | `int handle`, `int index`, `int value` |
| db_bind_text | `int handle`, `int index`, `const char *text`, `int length` |
| db_bind_blob | `int handle`, `int index`, `const void *data`, `int length` |
| db_bind_null | `int handle`, `int index` |
| db_error_code | `int handle` |

- open_existing: writableは0=SQLITE_OPEN_READONLY、1=SQLITE_OPEN_READWRITE、他は拒否。CREATE/URIを付けず、
  空path、`:memory:`、一時DB、長すぎるpathを拒否。成功はhandle>=0、失敗は-1。
  ROで変更PRAGMAを実行しない。RWはDELETE modeを確認し、要求mode不成立は失敗。
- prepare_only/bind: 成功0、失敗-1。SQLはNUL込み1024B以内、超過を切捨てず拒否。
  単一の非空statementのみ、末尾は空白/コメントまで（次のstatementがあれば拒否）。prepare-onlyはstepしない。
  同接続の旧stmtをfinalizeして置換。bindはprepare後・最初のstep前のみ、indexは1-based。
  intはint32、textはlength指定UTF-8で0〜255B、blobは0〜4096B。負長・超過は拒否。
  text/blobはカーネルへ検証付きコピー後 `SQLITE_TRANSIENT`。0Bでも非NULLの空値を渡し、NULLはbind_nullのみ。
- 新handleには旧 `db_step` / `db_finalize` / `db_close` を使用可能。stepはROW=1/DONE=0/失敗=-1、
  DONEで自動finalizeする既存動作を維持。反復は再prepareし、reset APIは追加しない。
- error_code: 直前DB操作のSQLite extended result code（成功0、引数不正はSQLITE_MISUSE）。
  **handle=-1はowner別の直前open失敗診断**、他の無効handleはMISUSE。エラー取得自体は診断を消さない。
  close/finalizeが最初の失敗を上書きしない保存規則を実装する。cfgはこれを既存OS32_ERR/CFG状態へ写像する。
  BUSY/READONLY/CORRUPT/NOTADB/CANTOPEN/IOERR/FULL/NOMEMを区別できること。
  新しいOS32_ERR番号は取らない（-14以降を無断使用しない）。CANTOPENだけでMISSINGと断定しない。

### data_fields移動は一括再buildで扱うABI境界（旧binary互換は要求しない）

両generatorのレイアウトを確認すること。現行C generatorは **全apiの後にdata_fields** を出すため、
関数appendだけでも `sbrk_heap_limit` / `shm_base` が移動する。
他の追加がない場合の計算値は旧data=0x2D8/0x2DC、追加関数slot=180〜186
（offset=0x2D8, 0x2DC, 0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F0）、新data=0x2F4/0x2F8。
これは**仮配置**であり予約ではない。旧binaryは旧data位置から関数アドレスを読む危険がある。
`exec_run` の現行 `min_api_ver > KAPI_VERSION` 検査は古いbinaryを拒否しないので防止にならない。

F0の選択方針（ユーザー確認済み）:

**外部利用者はいないため意図的ABI breakを許容し、kernel/SDK/userland/apps/gameを
clean full rebuildして一括配備する**。旧binary互換view/thunkや旧binary拒否機構の新設を
F0/F1のブロッカーにしない。旧成果物との混在は運用上排除し、下位互換とは主張しない。
F0には版予約の調停・生成物の再生成・一貫したbuild/配備対象の確定を残す。
F1は公開ABI不変の内部実装に限定し、この方針を理由にSDK/loader等へ範囲を拡張しない。
生成物の手編集は禁止。

## 5. D0 — 通常配備のsettings保護（S1より先）

所有予定: 配備ツールの小さな共通判定と各既存コピー経路・専用ホスト試験。新しい配備frameworkは作らない。
通常更新ではguest正規化パス `/etc/settings.db`、`/etc/settings.db-journal` を**無条件除外**する。
安全側に `settings.db-wal` / `settings.db-shm` / `settings.db.bak` と回復用退避群も保護する
（WAL対応を追加する意味ではない）。

- manifest明示指定・glob・tag・HostDrv残骸・sync-from-hostdrvの全てに、実コピー直前の共通保護を適用。
  pathの `..` / symlink等による保護対象への迂回も拒否する。manifest削除だけ、copy-if-missingだけでは不合格。
- 既存ファイルは内容/存在を維持、欠損ファイルは欠損のまま。通常deployはDBやjournalを
  作成・上書き・削除・renameしない。`NO_PRUNE` に依存せず保護する。
  `/etc`をprune対象に広げない。HostDrv clean/nhd-initは通常更新の代替にしない。
- コピーやsync失敗で処理全体を失敗させ、成功表示/後続NHD上書きへ進めない。保護対象の除外は明示ログにする。
- RED→GREEN: temp dirとmockだけで source/destination各々のDB・journal存在/欠損の全組合せ、
  異なる内容・hot journal・manifest直指定/glob/tag/再帰コピーを試験。前後の存在一覧とhashを比較。
  unrelated binaryの更新は成功、失敗注入時は非zero、保護対象へのwrite系呼出しはゼロ。
  script import時の環境ファイル読込みやsudo/mount/実配備を試験から遮断する。

媒体用マスタはS1の隔離出力へ生成し、通常HostDrv/NHD manifestには載せない。
新規インストールは対象が新規であることを確認した明示install操作だけでseedする。
既存媒体masterの再生成・FDD/CD/NHD作成・フォーマットは操作範囲を別途承認する。

## 6. S3へ渡す明示リカバリ契約と最終受入

`install --recover-settings [drive]` は通常起動の自動分岐ではない。専用起動媒体から、対象drive・
DB/journal・backup置換範囲を表示して承認された操作のみ実行する。新規installと既存DB復旧を混ぜない。

1. 対象DB利用者を停止し、媒体masterのschema/整合性を読取り検証。元DB、journal等を対として保存。
   hot journalをDBだけのbackupから切り離さない。`.bak`既存の一世代置換も承認対象。
2. 別名へmasterを完全コピーし、長さ/内容/schemaを検証してから切替。copy/backup/sync失敗では
   元DBを消さず停止する。元が欠損なら失敗後も欠損を保つ。失敗した一時ファイルの扱いを記録する。
3. rename/切替の原子性をbackendで確認できなければ「原子的復元」と呼ばない。
   切替後失敗時の旧DB+journal復帰手順を用意し、無条件成功やjournalだけの削除を禁止する。
4. `system.cfg`、profile、boot領域、無関係な設定は変更しない。復元後にschema表示・sync結果・
   close/reopen結果を記録。JSON importはマスタ復元とは別機能で、検証後の単一transactionに限定する。

受入はmock→実kernel/guestの順。専任検証者が承認済み使い捨てイメージで正常/障害/復旧を実走する。
実配備時はbuild→停止/終了確認→**停止後の現行イメージをローカルへ取得**→承認範囲の既存配備経路→起動。
古い作業NHDからの上書きをしない。配備したbinaryの同一性を確認し、HostDrvだけの成功をゲスト合格にしない。

**保証上限**: DELETE journalを使いrollback・I/O失敗伝播・通常再open整合性を検証する。
MEMORY journalやcommit後の追加syncをcrash recoveryの代わりにしない。
xSyncの現行成功stubを直しても、全層の順序・device flush・切替原子性と承認済み障害試験がない限り
「電源断を含む完全なcrash durability」は未検証と記す。未達は残ゲートとして報告し、S5完了に数えない。

本書作成時の検証: 指定正典と上記コードを静的照合、JSONの版/件数と仮offsetをPythonで算出。
基盤変更・生成・build・配備・guest・障害注入試験は実施していない。
