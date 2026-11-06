# FEP 辞書のメタ情報と学習データの別ファイル化 — 確定設計

> 出所: ~/os32-tmp/run/dict-meta-design/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、確定の直しは PM)。
> 状態: **現行 (2026-11-04)** — 確定設計。段の状態は票が持つ。実装に着手 (ユーザー 2026-11-05)。旧い学習データは移さない (ユーザー 2026-11-05)。設計はユーザー 2026-11-04「設計を今進める」)。

## r1→r2 の変更
- R1: 予算 adapter/G 段を削除し、既存 outer_boot_check を使用。
- R2: header/meta probe を core に移し、FEP 初期化前に結果を渡す契約へ変更。
- R3: PM 決定として既定192KiBから2接続を再測定、下限への丸めと記録を維持。
- R4: FEP 実消費への新しい強制上限を削除し、受入測定だけに変更。
- R5: PM 決定として学習版は meta のみとし、専用 SQLite export/H 段を削除。
- R6: PM 決定としてゲストの旧表検出を削除。
- R7: NP21/W・実機から host へ写しを取り出す道具と方向を追記。
- R8: 既存 host 検査を拡張する割当と所有者を明記。
- R9: FEPLEARN-1 を閉じる受入と文書担当を追加。
- R10: 票 §0 の古い D28・§4-6 参照を直す文書段を明記。
- R11: fep.elf の LOAD 実測を記載し、基準像不明の gate を削除。
- R12: アプリ予算を BUDGET_CAPACITY − reserve（SYSTEM 課金込み）へ訂正。
- R13: ime コマンドのパスを訂正、hsync.c の L 段は sol と指定。
- R14: hsync 収集の工数と24枠の余裕を再見積り、パス範囲だけを未決に。
- U1: PM 決定として重複 freq/last_ts は各最大値に確定。

- 作成: 2026-11-04 / Codex（実行モデル ID は取得不能。gpt-6-astra と断定しない）。
- 状態: 設計案。ユーザー決定は TASK_DICT_META §0、D27、2026-11-04 の設計着手指示に従う。
- 実装・ビルド・試験・NP21/W 操作は未実施。既存 ELF の readelf 表示だけ確認。参照は指定文書と指定コードの抽出箇所のみ。archive は根拠にしていない。
- シングルタスク・協調切替、整数演算、CLAUDE.md [C1]〜[D3] を前提とする。辞書品質・排他 open の新機能は対象外。

## 1. 実物の確認
| # | 票の記述との差 | 現在の根拠と設計への反映 |
|---|---|---|
| 1 | FD を直接 protect する説明は旧方式 | `kernel/ime/ime_dict.c:106,114,167` は RESIDENT group の acquire/open。2 接続とも group を持つ |
| 2 | 切替が ime.c:882 にあるという所在は古い | `kernel/ime/ime.c:376` が `ime_dict_api.inc` を include。結線担当がこの include 先を変更する |
| 3 | kapi_db の接続は単なる sqlite3_open ではない | `kapi/kapi_db.c:141,155` は EXEC group の acquire/open。予約判定を KAPI 内へ二重実装しない |
| 4 | FEP から SQLite を直接リンクする前提ではない | `tools/prototypes/fep_imports.def:160–277` は os32_sqlite_* outer facade を import。追加口も同じ境界へ |
| 5 | RO 化だけでは学習不能時の変換を保証しない | `kernel/ime/ime_dict.c:70–85,125–154` は dict_user と dict を同じ UNION で prepare。学習表無しでは prepare 失敗 |
| 6 | 指定された正典の参照先が欠落 | 現行 `docs/decisions/MEMMAP_V3.md:30,42–50` に D27 はあるが D28・§4-6 は無い。M1〜M6 は票 §0 を根拠とする |
| 7 | hsync の「settings.db*」は実装上 wildcard ではない | `userland/system/hsync_protect.inc:34–46,163–198` は /etc の 11 名の完全一致。FEP 用は明示して prefix 判定を追加 |
- 食い違いは上表の 7 件（同じ変更の行番号ずれを重複計上しない）。
- 生成器はまだ meta を作らない (`tools/fep_to_sqlite.py:194–230`)。S/M/L の閾値 800/1500/無制限は維持 (:34–37)。
- 閾値は raw IPADIC cost ではなく正規化後 (:128–178,260–262)。この設計で尺度を変更しない。
- 5,815,296B・97,514 語・38,784B は票の過去実測であり今回の実測ではない。COPYING 同梱済みは票 §2/§4 の記述を採用する。

## 2. システム辞書の形式
- 配置は `/db/fep_s.db`, `/db/fep.db`, `/db/fep_l.db` を維持。形式 1 でも dict/idx_dict_yomi は変更しない。
- `CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)`。数値も十進 ASCII、文字列は UTF-8、重複 key は作れない。
- ヘッダ先頭 100B を VFS で読み、magic・page_size (16–17)・user_version (60–63、BE) を SQL より先に検査する。
- 既存 `SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS` は変更不要。host 生成器だけ PRAGMA user_version=1 を使う。
- version=1 のみ通常形式。0 は旧形式専用経路、2 以上は理由付き拒否。1 の meta.format_version 不一致も拒否。
- 0 は page_size=1024 と既存 dict の prepare 成功を要求し、meta 欠落を許す。ヘッダ異常を旧形式扱いしない。
- 整数は文字列全体を検査し、桁あふれを検出。必須 key の欠落は拒否、未知 key は無視。kind=supplement は今回未対応。
| key | 値・表現 / 判定と用途 |
|---|---|
| format_version | `1`。ヘッダ値と同じ |
| min_fep_version | `1`。新設の辞書読取機能版 FEP_DICT_READER_VERSION と比較（KAPI 版とは独立） |
| dict_id | `ipadic-2.7.0`。S/M/L 共通、variant を含めない。品詞・尺度の非互換改訂では新 ID にする |
| kind | `system`。RO 接続を維持。kind を読んでから RW に切り替えることはしない |
| language / charset | `ja` / `UTF-8` のみ |
| variant | `S` / `M` / `L`。要求された切替先と一致 |
| source / source_version | `IPADIC` / `2.7.0` |
| license / attribution | 同梱 COPYING の条項と表示を全文格納。生成器は原文欠落なら失敗、空文字を許さない |
| build_tool / build_date | 生成器の形式版込み識別子 / UTC ISO 8601。再現ビルドは SOURCE_DATE_EPOCH を指定 |
| entry_count | 最終 dict の COUNT(*)。起動時は表示だけ、生成・受入で実数照合 |
| pos_table_version | `1`（現在の pos_id 対応）。学習側との一致が必要 |
| cost_scale | `os32-log2047-boost-v1`。現在の normalize_costs と補正を指す識別子 |
| mem_reserve_kb | **PM 決定**: 既定 `192` KiB から2接続・SQL文・cache・probeを再測定し、受入を満たす値を生成器でS/M/Lに書く |
| cache_pages | 初期案 `16`（システム接続）。学習接続は 8。積が予約内か生成時と読込時に検査 |
| checksum | `sha256:` + 64 hex。dict 行だけの canonical stream をハッシュ、meta は対象外 |
- canonical stream: BINARY の yomi,kanji,pos_id,cost 昇順。各 TEXT は UTF-8 の byte 長 u32 BE + bytes、整数は符号付き i32 BE。
- checksum は生成・配備照合で使う。ゲスト起動で全 dict を走査しない。SHA 実装を FEP に持ち込まない。
- 旧形式の既定値: 上の ID/品詞/尺度、variant は固定 3 パスから判定、予約192、cache16。license は「旧形式に未収録」と表示。

## 3. 学習 DB と候補の合成
- 既定配置 `/etc/fep_user.db`。`fep.user_dict_path` の絶対パス範囲は未決 R14（推奨は `/etc` 直下）。path と `-journal` が VFS 上限内に収まること。
- meta は同じ表。必須は format_version=1, kind=user, dict_id, pos_table_version, created（UTC ISO 8601）。
- **PM 決定 R5**: 学習 DB の版は meta.format_version のみで判定。page_size=1024、user_version は設定・判定しない。専用 outer/export を追加しない。
- `dict_user(yomi TEXT NOT NULL, kanji TEXT NOT NULL, freq INTEGER DEFAULT 1, last_ts INTEGER DEFAULT 0, PRIMARY KEY(yomi,kanji))`。
- `idx_user_yomi(yomi)` を維持。学習・列挙・削除・export・全消去は学習接続だけに送る。
- 通常は system RO + user RW の 2 接続。学習 DB 無しは作成、ROFS・不一致・破損では警告して system のみで変換。
- 不一致の user DB は SQL 更新前に閉じる。削除・再作成・別 ID の自動採用はしない。S/M/L では共通 ID なので同じ学習を使用。
- 初回作成の schema/meta は 1 transaction。COMMIT 前の異常では未完成ファイルを既存学習と誤認せず、明示の再作成対象として報告する。
- SQL_EXACT/PREFIX を各 DB 用に分ける。system の前方一致 cost-500、user の -10000-freq は現在の式を維持。
- 各側 ORDER BY cost LIMIT 32 の cursor を 2 本進め、小さい cost 順に合成、同点は user 優先。上位合計32行で打ち切る。
- 現行どおり LIMIT の後に完全表記で重複排除、表示切詰め・非短縮ペナルティを適用する。重複を除いた分は補充しない。
- SQL の TEXT pointer は次の step/reset で失効するため、比較に必要な完全表記だけコピー。現行の長語用確保を再利用する。
- 学習側 cursor が途中で失敗した結果は破棄し、system cursor を reset して system のみ検索し直す。
- `dict_ready/recover/close` 相当は接続別。stale 判定は step 前、close 失敗時の isolated を維持。user 障害で system を閉じない。

## 4. 旧学習データの扱い
- **旧い学習データは移さない** (ユーザー決定 2026-11-05「古い学習データは元のデータ変換側を変えたほうが良い。互換性の問題なら考えなくて良い」)。新しい形の学習 DB は空から作る。移行器・取り出しの道具 (旧 §8 の E・E2) は作らない。
- 辞書の形の変更は、変換の側 (段 B の `tools/fep_to_sqlite.py`) で受ける。ゲストで旧表を検出しない (PM 決定 R6 は維持)。新規媒体も既存媒体も、空の学習 DB から始める。
- hsync は学習本体・journal・一時配置名を保護する。新学習 DB は deploy/packages の配布ファイルにしない。

## 5. 部品境界・所有と関数契約
- 以下は **新設する内部契約の案**。既存 API 名は確認済みの group/outer のみ。未読の内部ヘッダに既にあるとは主張しない。
```c
/* kernel/ime/ime_store.h : core/FEP 共有内部契約、GNU11、libc 不使用 */
typedef struct {
    u32 format, min_reader, pos_version, reserve_kb, cache_pages, entries;
    char dict_id[64]; char variant; /* license は常駐コピーせず分割して読む */
} FepMeta;
typedef struct {
    void *db, *exact, *prefix, *learn;
    Os32SqliteGroup group;
    char path[VFS_MAX_PATH]; int isolated, io_retried;
} FepConn;
typedef struct {
    FepConn system, user; FepMeta meta;
    u32 boot_reserve_kb; int user_enabled;
} FepStore; /* g_ime が一つ所有。SQLite 内部へのポインタを KAPI に出さない */
/* 実体は kernel/sqlite_engine.c、services_settings の隣。FEP import はこの2口 */
int fep_meta_probe(const char *path, FepMeta *out); /* core: header -> 一時RO SQL -> close */
int fep_boot_config_get(FepMeta *out, u32 *reserve_kb); /* core所有の確定結果をコピー */
int fep_store_open(FepStore *s, const char *path, const char *user_path);
int fep_store_switch(FepStore *s, const char *path); /* budget照合後に既存を閉じる */
int fep_user_open(FepStore *s); /* 不一致/ROFS は user_enabled=0 */
/* core: probeを閉じた後、既存予約口を呼び、それから FEP をロード */
/* reserve_bytes = settings指定あり ? settings値 : meta値（旧形式は192KiB） */
/* os32_sqlite_outer_boot_check(0, reserve_bytes); -> fep_module_load(sqlite_handle); */
/* fep_module_init -> ime_init -> config_get -> ime_dict_boot（内部で2接続を開く） */
```
- header probe の FD は read/close の短い区間だけ所有。open まで協調 yield しない。以後は接続 group が main/後発 journal を所有する。
- `os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT,0,&conn.group)` → group_open(RO または RW|CREATE) を各接続に使用。
- group_begin_close → finalize 全 stmt → sqlite close → group_finish の既存順序を実装確認して維持。失敗時は隔離した group を再利用しない。
- 予算状態は core/SQLite 側が唯一の所有者。FEP の boot_reserve_kb は参照用の写し。kapi_db は引き続き EXEC group を取る。
- 既存 class 課金・予約をそのまま使う（vfs.c:39,55–70,259–281）。アプリ確保時は APP+SYSTEM+今回分 ≤ BUDGET_CAPACITY−reserve。FEP専用の強制上限は追加せず、最大使用量≤予約を受入で測る。
- core は settings 指定の有無と値、probe結果、実効予約を保持。settings→meta→192KiB の優先順。下限99KiBへの丸めとログは既存どおり（**PM 決定 R3**）。KiB→byte変換前に桁あふれを検査。
- FEP は初期化時に config_get の結果を FepStore へコピーする。core probeは切替でも利用でき、予約を更新しない。boot_checkをFEPへimportしない。
- 初期化後の既存 boot_check(1,reserve) は system+user の実在 RESIDENT 数（通常2、学習無効なら1）へ変更。異常時の隔離・rollback順序は維持。
- SDK には末尾追加 `ime_dict_info(key, offset, out, capacity)` の1口を提案。戻り値はコピーbyte数/EOF=0/負のエラー、NUL用に1byte確保。
- key は固定 enum（license, attribution, entry_count, status）。offset は byte。C89 の整数と buffer のみ、接続・SQLite pointer は渡さない。
- core の FEP ops → FEP 内 meta読取へ中継。`ime` コマンドが256Bずつ表示し、license全文を FEP BSS に置かない。
```c
/* userland/system/hsync_protect.inc : host でも同じ純関数をコンパイル */
int hsp_set_fep_user_path(const char *absolute_path); /* 正規化済み設定の写しを所有 */
/* hsp_name_protected(norm): 既存settings判定 || 既定/etc/fep_user.db prefix
 * || 設定pathの本体とそのsuffix。hsp_ancestor_protectedにも同じ判定を適用。 */
```
- hsync 呼出元が settings から fep.user_dict_path を取得し、コピー開始前に登録。DB・表・行が無ければ既定 path。open・表確認・prepare・step・値検査などで失敗したら、段と rc を警告1行に出し、宛先 /etc 直下の全 *.db とその journal・一時配置 suffix を大文字小文字を区別せず保護して同期を続ける。settings.db 自体の既存保護も維持する。
- 既定名は設定変更後も保護。現在のカスタム名とその journal/一時名も保護する。実在別名収集と HS_MAX_PROT の上限超過も失敗扱い。
- パスを変更する運用は旧学習を新パスに移してから設定更新。旧カスタムパスを自動探索・永久登録する機能は足さない。
- FEP imports の個数上限は無い。追加の core probe/config_getは登録担当が追加し、生成表を手編集しない。

## 6. 起動・切替と失敗
| 段 | 処理 / 失敗時 |
|---|---|
| 起動1 | SQLite → services_settings で指定有無と値/pathを退避 → 接続を閉じる。辞書無しは既存SQLITE_ONLY。辞書ありは core が header/meta を一時ROで読む（FEP未ロード） |
| 起動2 | probeを閉じ、settings→meta→192KiBで選択。99KiBへの丸め/ログを維持し、outer_boot_check(0,reserve_bytes) が容量/課金を検査。確定結果をcoreへ保存 |
| 起動3 | fep_module_load → 初期化でconfig_getを取得 → systemをROで開く → schema/metadata照合 → cache_size設定 → user照合/作成 → prepare。設定cacheを収容できなければ学習を止めて理由表示 |
| 起動4 | FEP初期化完了後に公開。辞書無しは現行SQLITE_ONLY、SQLite/FEP本体の実失敗は現行MINIMAL。学習だけの失敗で全体を落とさない |
| 切替1 | header/metaを先にprobe。meta要求がboot予約を超えたら再起動案内、現辞書を維持。ここで予約を増やさない |
| 切替2 | probeを閉じて旧systemを閉じ、新systemを開く。userは保持、ID/品詞不一致なら使用停止。3本を恒常保持しない |
| 切替失敗 | 新systemを閉じ旧pathを再open。復旧失敗なら辞書使用不可を表示、userデータは保持。隔離中groupがあれば再起動を案内 |
| hsync | ROでも使用中辞書の差替許可は追加しない（ime_dict.c:191–193）。非選択辞書を更新→切替→元辞書更新、または停止中配備 |
- 起動probeは予約確定前、切替probeは既存2接続と一時的に共存する。両方を測定対象に含める。FEP初期化・実在group数のboot_check完了までアプリdb_openは公開しない。
- settingsの小さい値は既存下限99KiBへ丸めて記録。2接続で不足すれば既存の初期化/学習失敗処理で理由を表示する。

## 7. 容量予算（基準像は実測、増分は推定）
| 対象 | 増分の割当と合否条件 |
|---|---|
| fep.mod image | meta表示1KiB + user接続/合成4KiB + 結線1KiB = **+6KiB以内** |
| fep.mod mem | image増6KiB + 状態/文字列/一時候補等4KiB = **+10KiB以内**。SQLite heapは別枠 |
| 64KiB上限 | 既存 build/out/fep/fep.elf の LOAD: FileSiz=0x3f6c（16.2KiB）、MemSiz=0x5d40（23.3KiB）。増分後約22.2/33.3KiB、双方64KiB以内（基準mem余裕40.7KiB） |
| core / sqlite.mod | coreのheader/meta probe・設定結線・表示中継 約4KiB / sqlite.modは追加exportなし、増分0を想定 |
| 起動FDの空き64KiB | FDはFEPなし（MEMMAP_V3:25,73）。追加core+SQLite+hsyncに**最大8KiB**を割当、空き56KiB以上。圧縮率を先取りしない |
| MEMSYS5 512KiB | 既定FEP192KiBから測定。BUDGET_CAPACITY=floor(BUDGET_HEAP_BYTES/65)×64、APP+SYSTEM上限は同容量−reserve。cache16+8KiBを含む最大使用量≤予約を受入で確認 |
| システムDB | 各版meta増分 約8〜16KiB（原文長に依存）。Mの過去値5,815,296Bに加算。S/L本体サイズは未測定 |
| 学習DB | 空約8KiB、N語で概算8KiB+N×(2Y+2K+80)B、Y/Kは平均UTF-8 byte長。Y=K=12,N=10000なら約1.23MiB |
- 常駐 license buffer・SHA-256 をFEPに入れないことで予算を抑える。変更後の像とFD収容は実装受入で再測定する。

## 8. ファイル別の割当（1行＝1ファイル＝1段＝1実装者）
- H=host Python、F=freestanding GNU11（host試験はlibcなし）、G=ゲストnewlib nano。
- 各段は半日〜1日。契約段→独立部品段を並列→結線段を直列。登録はすべてPM一本、他担当は追加行だけ報告する。
| 段/所有者 | ファイル（予定、新/変） | 機能・概算行数 / 組み方 |
|---|---|---|
| A/契約 | kernel/ime/ime_store.h 新 | 内部型・口・定数参照、100行/F |
| B/生成 | tools/fep_to_sqlite.py 変 | meta/checksum/原文取込、+130行/H |
| C/表示読取 | kernel/ime/ime_meta.c 新 | 開いたsystemのmeta分割表示、100行/F |
| C2/core結線 | kernel/sqlite_engine.c 変 | header/meta probe・settings優先・予約・結果提供・group数、+220行/F |
| D/学習 | kernel/ime/ime_user_store.c 新 | 接続・schema・meta版判定、240行/F |
| F/保護 | userland/system/hsync_protect.inc 変 | 固定/設定path保護、+70行/G兼F |
| I/検索結線 | kernel/ime/ime_dict.c 変 | 2cursor合成・接続別回復・user API移管、約250行/F |
| J/起動結線 | kernel/ime/ime.c 変 | store所有・初期化/abort、約50行/F |
| K/切替結線 | kernel/ime/ime_dict_api.inc 変 | 切替・再起動案内、約70行/F（所在のみ確認済み） |
| L/同期結線・sol | userland/system/hsync.c 変 | /etc直下案は約100行、外部許可案は約200行/G。設定登録・収集・完全path保持（2800行超のためsol担当） |
| M/表示 | userland/cmds/ime.c 変 | info表示、約60行/G |
| N/host検査 | tools/tests/test_fep_meta.py 新 | 下表host試験、約220行/H |
| O/host実体 | tools/tests/ime_dict_host.c 変 | 既存実体/stub試験へ2接続・合成・回復、+180行/F |
| O2/予算検査 | tools/checks/fep-budget-host.py 変 | 2接続とprobe最大使用量・192KiB基準の再測定、+70行/H |
| O3/保護実体 | tools/tests/hsync_protect_host.c 変 | custom/祖先/別名/24枠境界、+80行/F |
| O4/保護検査 | tools/tests/test_hsync_protect.py 変 | 上記入力と変異、+80行/H |
| O5/起動検査 | tools/tests/sqlite_module_host.c 変 | core probe順・優先順・丸め・group数・rollback、+120行/F |
| P/ゲスト検査 | userland/tests/fep_meta_test.c 新 | 実SQLite/設定/切替の受入、約180行/G |
- PM専有の共有登録: build/*.mk、fep_imports.def、試験CHECK/登録、packages.yaml、sdk/kapi.json。
- PMは各登録ファイルを個別の直列段として扱う（各+5〜30行）。KAPI生成物・FEP ops/外側中継・boot結線も実名照合後に1ファイルずつ担当を固定する。
- kapi_db.c は変更無し。既存EXEC groupと課金をそのまま使う。
- 文書担当が docs/tasks/fep/TASK_DICT_META.md（§0脚注を現行D27:30へ訂正、D28・§4-6欠落を明記）、同00_INDEX.md、docs/tasks/DEFERRED_TESTS.md を各1段・各約20行で所有。今回の成果物は本設計のみ。

## 9. 受入→試験・変異、実装時の検査
| 受入項目 | 試験（実施予定） | 変異（各1行、実行時RED） |
|---|---|---|
| 旧形式/未来版 | host: version0開ける、2とmeta不一致拒否、壊れたheader拒否 | 未来版拒否分岐を反転 |
| S→L/同期後に保持 | ゲスト: 学習→切替→非選択辞書更新→切替、freq照合 | user接続をsystem切替時に初期化 |
| ID/品詞不一致 | host+ゲスト: user利用停止・警告、system変換成功 | ID一致条件を常時真 |
| ROFS変換 | ゲスト: 空user/既存user双方で全write拒否、system変換 | systemをRWでopen |
| メタ表示 | host生成照合+ゲスト: 原文全byte、entry_count一致 | 分割表示のoffset更新を削除 |
| 予約/再起動 | host+ゲスト: settings値採用、超過切替で現辞書維持、アプリ上限 | 切替時の予約増を許可 |
| hsync保護 | host libcなし+ゲスト: 大小文字・正規化・祖先・custom・journal | FEP名保護条件を削除 |
| 境界/回復/容量 | host+ゲスト: cursor途中失敗、stale、close失敗、heap/像/FD測定 | user失敗時のsystem再検索を削除 |
- B/C/D/F/C2はA後に並列可。I/J/K/L/Mは部品統合後、共有登録/KAPI世代を揃えて直列。N/O/O2〜O5は契約後に各所有者が既存検査を拡張、Pは結線後。
- FEPLEARN-1（DEFERRED_TESTS.md:54）: ゲストで学習→FEP閉鎖→nhd_deploy sync／hsyncでsystem辞書更新→再起動後4列保持を確認し、文書担当が閉じる。FDRECOVで学習領域を復元する際の消失条件も手順書に記す。変異は配布物へuser DBを混入させ、保持検査をREDにする。
- 各実装段は触った像ビルド、`make -k check-changed BASE=<基点> MUTATE=0`、追加/変更試験の `--mutate`、`tools/checks/run.py --lint`。
- KAPI追加時はPMが生成・版上げ・clean/allを直列実施。全体変異とゲスト受入はPMが取り込み後1回。今回はいずれも回さない。

## 10. 未決（ユーザー判断のみ）
- **R14: fep.user_dict_path を /etc 直下に限定するか、外部も許すか。推奨は /etc 直下限定**。既存の列挙範囲を維持でき、今回の別ファイル化を小さく実装できる。未決はこの1件。
- 外部を許す場合は正規化した親ディレクトリも列挙し、basenameでなく同期先root配下の完全pathを保持、同じdev/ino別名を収集する。祖先保護・重複排除・上限超過時の中止を両案で維持する。
| HS_MAX_PROT=24 の見積り | 使用数 / 余裕（nameと実体の両配列で判定） |
|---|---|
| 既存settingsの候補11名が全て存在 | 11 / 13 |
| 既定user本体・journal・一時名の3名を追加 | 最大14 / 10 |
| 別custom名の同3名も存在 | 最大17 / 7 |
| その他prefix一致suffix・実在別名 | 上記余裕から消費。個数は固定保証しない。25件目で同期拒否、黙って捨てない |
- 推奨案はF段+70行/L段約100行、外部許可案はF段約120行/L段約200行。完全path配列によるBSS増もFD/メモリ受入で測定する。

## 反論
- なし（0件）。

## 確定 (2026-11-04、claude-opus-5-5 の第 2 回、PM が反映)
- N1: PM 決定 — (a) 常駐の group の枠 `OS32_SQLITE_RESIDENT_GROUPS` (lib/sqlite3/os32_sqlite_vfs.h:15) を 2→3 に広げ、辞書の切替の probe が既存の 2 接続 (system・user) と一時的に共存できるようにする (ユーザー決定 2026-10-28「上限は物理の制約が無ければ広げてよい」)。§5・§6 の切替の手順はこの前提のまま。§7 に枠 1 つ分の常駐 RAM の増分を、O5 に「3 本目を取れる・4 本目は空きなしで失敗」の試験を足す。
- N3: core が常駐の接続数 (`boot_check(live, …)` の第 1 引数、vfs.c:261-268 の完全一致) を受け取る口を FEP の ops (`boot_bytes` の隣) に 1 つ置く。学習が無効なら 1、有効なら 2 を返す。
- R14: PM 決定 — 学習ファイルの custom path は /etc 直下に限る (票 §3 の「settings で上書き可」の範囲内の絞り込み、既存の列挙 hsync_protect.inc:163-198・hsync.c:1201-1261 をそのまま使う)。ユーザーへの未決は無し。


## 追記 (2026-11-05、辞書のメタ情報を返す KAPI の口 — ユーザー「返す口だけでも設計させて」)

> 出所: ~/os32-tmp/run/dictinfo/DESIGN.final.md (astra 起草 r1、Opus レビュー 1 回、PM が確定)。本文 §5 の 4 キーは下の確定 A1 で 20 キーに置き換える。

# ime_dict_info — 辞書メタ情報を返す KAPI の詳細設計 r1
2026-11-05 / Codex（実行モデル ID は取得不能。gpt-6-astra と断定しない）。
対象は返却口だけ。リポジトリ編集・ビルド・試験・配備は未実施。
根拠は `/home/hight/os32-v3` の `docs/decisions/fep/DICT_META_DESIGN.md` §2・3・5・6、`docs/tasks/fep/TASK_DICT_META.md:106`。
追加規約は `docs/KAPI_SPEC.md` §3-1・3-3、`.claude/skills/os32-kapi-add/SKILL.md`、`docs/CONSTRAINTS.md` [C1]〜[D4]・[ABI]。

### 1. 公開契約
確定設計 §5 の分割返却案を採用。現在選択中のシステム辞書だけを対象とし、パス・SQL・接続・学習データを渡さない。
```c
/* sdk/include/os32/os32_kapi_shared.h: C89。公開構造体は作らない。 */
#define IME_DICT_INFO_CAP 256
/* IME_DICT_INFO_* のキー定数は下表の整数値を固定して定義する。 */
/* KernelAPI メンバ／生成ラッパーの型 */
int ime_dict_info(int key, int offset, char *out, int capacity);
```
公開構造体の `size` による版管理は不採用。可変長原文を固定長構造体に収めず、キー末尾追加と KAPI 版で拡張する。
`offset` は先頭からの byte 数（0 以上）、`capacity` は NUL 込み 2〜256、`out` はその全域を書ける呼出元バッファ。
成功はコピー byte 数 `n`（NUL を除く）。`n=min(残りbyte数,capacity-1)` とし `out[n]=0`。残りが無ければ 0 と `out[0]=0`。
長さ以上の offset は EOF=0。加算で範囲判定せず、まず offset と長さを比較してから残長を引く。長さ照会専用モードは作らない。
UTF-8 の文字途中でも byte 境界で分割する。呼出側は戻り値分を連結または逐次出力し、`offset += n`、0 で終了する。
文末改行は付加しない。数値も符号なし十進 ASCII として返す（内部は u32）、空文字と未収録は区別する。

### 2. キーと返す内容
キー名は `IME_DICT_INFO_` に下表の大文字名を続ける。全キーが同じ文字列契約であり、バイナリ数値・enum の配置を公開しない。
| 値 / キー名 | 確定設計の欄・値 / 元の型 |
|---|---|
| 1 STATUS | この口の状態文字列：`unloaded` / `metadata-unavailable` / `legacy` / `meta`（後述） |
| 2 FORMAT_VERSION / 3 MIN_FEP_VERSION | §2 format_version / min_fep_version、u32。後者は FEP_DICT_READER_VERSION との照合値で KAPI 版ではない |
| 4 DICT_ID / 5 KIND | §2 dict_id / kind、UTF-8。現設計では `ipadic-2.7.0` / `system` |
| 6 LANGUAGE / 7 CHARSET / 8 VARIANT | §2 language / charset / variant、`ja` / `UTF-8` / `S`・`M`・`L` |
| 9 SOURCE / 10 SOURCE_VERSION | §2 source / source_version、UTF-8 |
| 11 LICENSE / 12 ATTRIBUTION | §2 license / attribution、原文全文 UTF-8。要約・固定長切捨て・ファイル参照への置換をしない |
| 13 BUILD_TOOL / 14 BUILD_DATE | §2 build_tool / build_date、UTF-8 / UTC ISO 8601 |
| 15 ENTRY_COUNT / 16 POS_TABLE_VERSION | §2 entry_count / pos_table_version、u32 |
| 17 COST_SCALE | §2 cost_scale、UTF-8（現設計 `os32-log2047-boost-v1`） |
| 18 MEM_RESERVE_KB / 19 CACHE_PAGES | §2 mem_reserve_kb / cache_pages、u32。予約は辞書要求 KiB、settings 上書き後の実効予約ではない |
| 20 CHECKSUM | §2 checksum、`sha256:` + 64 hex。記録値であって今回の検証結果ではない |
page_size・実効予約・学習 DB の created/migration・学習有効状態は追加しない。S/M/L は同じ dict_id、学習別ファイル化の契約を変えない。

### 3. 状態・エラー・呼出文脈
FEP 未ロードまたは services_public=0 は全キー `OS32_ERR_NOSYS(-10)`。ロード済みでも辞書無しなら STATUS=`unloaded`、他キーは `OS32_ERR_AGAIN(-14)`。
引数不正（未知キー、負 offset、capacity 範囲外、NULL）は `OS32_ERR_INVAL(-9)`、既知キーの未収録は `OS32_ERR_NOTFOUND(-2)`。
SQL/読取失敗または形式1の必須値欠落・不正は `OS32_ERR_IO(-1)`、失効接続は `OS32_ERR_STALE(-11)`。エラーでは out を更新しない。
検査順は公開可否→引数→呼出元書込域→辞書状態/失効→値取得。CPL3 の不正書込域は既存契約どおり ring3_fault_kill（通常の負値復帰ではない）。
CUI/GUI の通常処理、CPL3 アプリ・CPL0/WM から呼べる。IME OFF でも可。IRQ/NMI・描画/入力コールバック・SQLite 処理中の再入からは呼ばない。
シングルタスク・協調切替を前提とし、この口の途中では yield・exec・コールバックを行わない。SQL 中に IRQ を長時間止めず、新しいロックは設けない。
各呼出しはその時点の辞書を読む。複数回に跨るスナップショットは保証しない。全文取得中は呼出側が切替・yield を挟まず、失効時は取得を中断する。
切替は成功した辞書の値だけを公開。旧辞書の復旧成功なら旧値、復旧失敗なら unloaded。照会から辞書 open・recover・切替を起動しない。

### 4. 今返せる値と DICT_META 後
現状 `kernel/ime/ime_dict.c` は meta/header を検査せず RW open、学習表同居。`ime.h` の db/path/isolated と接続の失効を確認する。
口だけを先行実装した段では、使用可能な辞書の STATUS=`metadata-unavailable`、VARIANT は現在の固定3パスと一致する場合だけ返す。他キーは NOTFOUND。
任意パスなら VARIANT も NOTFOUND にするだけで、辞書や呼出元を許可リストで拒否しない。現在のファイルを未検査のまま format=0/IPADIC と断定しない。
DICT_META 実装後は、読込検査済み形式1を STATUS=`meta` として全欄を返す。entry_count は記録値を使い COUNT(*) しない。checksum も計算しない。
検査済み旧形式は STATUS=`legacy`、format=0、確定設計 §2 の ID/品詞/尺度・固定パスの variant・予約192/cache16 を互換既定値として返す。
旧形式の license/attribution・件数・生成情報等、同節が既定値を定めない欄は NOTFOUND。表示側が「旧形式に未収録」と表示し、偽の原文や件数0を返さない。

### 5. 実装境界と登録（実装時の作業）
```c
/* sdk/kapi.json: api の末尾に1本。ret=int、args は §1、out="target" */
/* name=ime_dict_info, target=ime_dict_info_facade */
/* core fep_module.c -> FepModuleApi末尾 dict_info -> FEP ime_dict_api.inc */
int ime_dict_info_facade(int key, int offset, char *out, int capacity);
/* FEP 内部だけ。out は256Bのローカル staging、呼出元ポインタではない。 */
int ime_meta_read(int key, int offset, char *out, int capacity);
```
facade は既存 ime_user_list_facade の caller_access_get/check_caller_write_range/copy_to_caller を踏襲。SQL 前に全 capacity を検査、終了後に n+1 byte を再検査してコピー。
meta 読取は system 接続だけで既知キーをバインドし、長文は確定 A3 のとおり C の側で byte を切る。
SQLite の結果は step/reset/finalize 前に staging にコピーし、finalize 後に呼出元へ渡す。SQL失敗を EOF に丸めず、接続失効は SQL 前に検査。原文の常駐コピー・追加接続は作らない。
FepModuleApi は20→21スロット、FEP_MODULE_OPS_VERSION は1→2、fep_body.c・core stub（NOSYS）・ロード検査の契約を同期し、旧部品を混用しない。
現行 sdk/kapi.json は v79・255本。追加が先行しなければ v80/slot255。実装時に §3-2 と末尾を再確認し、KAPI_VERSION・関数表・依存 app.conf も更新する。
gen_kapi.py/kapi_rust_gen.py で再生成、差分確認、PM の clean→all、担当別検査と CHECK lint。外部 apps/game は H-7 再開決定後。今回はこれらを実行しない。

### 6. 試験と変異（実装時）
host：実 facade/meta読取を既存 ime_dict_host 系へ接続し、全キー・未ロード/未読込/旧形式/形式1・未知キー・負offset・capacity 0/1/2/256/257・EOF/INT_MAXを検査。
host：UTF-8途中分割を含む原文の全byte再結合、NUL/番兵、空値/未収録/SQL失敗、失効時SQL無し、caller RO/跨頁、切替失敗後の値、呼出元へSQLite pointerを残さないことを検査。
変異：capacity-1→capacity、byte分割→文字分割、NOTFOUND/IO→EOF、失効検査除去、コピー前の再検査除去、切替前の値を返す、stub→成功を各固有anchorで実行時REDにする。
NP21/W：停止→NHD配備→再起動後、実CPL3から256B分割し原文と照合、S/M/L切替、IME OFF/GUI、旧辞書・辞書無し・FEP無し・学習使用不可でもsystem情報取得を確認。先行段は未収録を期待する。
### 7. 未決
ユーザー判断が必要な未決：0件。推奨は上記の1口・文字列分割・構造体なし。学習パス等の既存未決はこの口に依存せず、今回再決裁しない。


### 確定 (2026-11-05、Opus 第 1 回の条件付き承認、PM が反映)
- A1: **20 キー** (ユーザー決定 2026-11-05「20」)。確定設計 DICT_META_DESIGN §5 の 4 キー (license・attribution・entry_count・status) を §2 の全欄の 20 キーに広げる。同じ SQL でキー名を引くだけで、後で版を上げずに済む。
- A2: SQL の後のコピー前の再検査と、それを狙う変異を削除。copy_to_caller は 1 回だけ。最初の検査で n+1 ≤ capacity が成り立つ (シングルタスクで途中に yield が無い、手本 kernel/ime/ime_dict_api.inc:30 も 1 回)。
- A3 (PM 決定、2026-11-05 実装で書き換え): SQL では byte を切らない (製品の SQLite は `SQLITE_OMIT_CAST`、outer facade に bind_int64 が無い)。`SELECT value FROM meta WHERE key = ?` を 1 回 step し、`sqlite3_column_blob` と `sqlite3_column_bytes` で byte の長さ len を取り、`offset < len` のときだけ C の側で [offset, offset + min(len - offset, capacity - 1)) を staging に写してから finalize する。offset >= len (INT_MAX を含む) は EOF。§5 の substr の文はこれで置き換える。
- A4 (PM 決定): この口は dict_ready を呼ばない。vfs_fd_is_stale だけで STALE を返し、STATUS キーも STALE を返す (既存の ime_user_* と違い、開き直さない)。
- A5 (PM 決定): v80 で一緒に入る 3 本 (STOP の宛先・gfx_osri_show・この口) の slot の並びは PM が統合の段で決める。
- A6 (PM 決定): capacity が 256 を超えたら 256 として扱う (INVAL にしない)。

## ユーザー決定 (2026-11-05)
- **R14: 学習データの置き場 (fep.user_dict_path) は /etc 直下に限る** (推奨案)。F 段は +70 行、L 段は約 100 行の案で実装する。外部を許す案の記述 (§10 の 2 行目以降) は採らない。
- 実装に着手する (ユーザー 2026-11-05「これに取り掛かる」)。hsync.c は先に `--fep-off` の段 (wt/hsync-fepoff、FEP の辞書を同期の間だけ閉じるオプション) を入れてから L 段に進む。
