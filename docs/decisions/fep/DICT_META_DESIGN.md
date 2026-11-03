# FEP 辞書のメタ情報と学習データの別ファイル化 — 確定設計

> 出所: ~/os32-tmp/run/dict-meta-design/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、確定の直しは PM)。
> 状態: **現行 (2026-11-04)** — 確定設計。段の状態は票が持つ。実装はまだ出さない (ユーザー 2026-11-04「設計を今進める」)。

## r1→r2 の変更
- R1: 予算 adapter/G 段を削除し、既存 outer_boot_check を使用。
- R2: header/meta probe を core に移し、FEP 初期化前に結果を渡す契約へ変更。
- R3: PM 決定として既定192KiBから2接続を再測定、下限への丸めと記録を維持。
- R4: FEP 実消費への新しい強制上限を削除し、受入測定だけに変更。
- R5: PM 決定として学習版は meta のみとし、専用 SQLite export/H 段を削除。
- R6: PM 決定としてゲストの旧表検出を削除し、host 移行と配備順に限定。
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
- 旧学習 last_ts は現実に 0 が書かれる (`kernel/ime/ime_dict.c:397`)。移行時に現在時刻へ置き換えない。
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
- checksum は生成・オフライン移行/配備照合で使う。ゲスト起動で全 dict を走査しない。SHA 実装を FEP に持ち込まない。
- 旧形式の既定値: 上の ID/品詞/尺度、variant は固定 3 パスから判定、予約192、cache16。license は「旧形式に未収録」と表示。
- 旧形式の任意パスを IPADIC と推定しない。移行元の指定は §4 の固定 3 辞書のみ。

## 3. 学習 DB と候補の合成
- 既定配置 `/etc/fep_user.db`。`fep.user_dict_path` の絶対パス範囲は未決 R14（推奨は `/etc` 直下）。path と `-journal` が VFS 上限内に収まること。
- meta は同じ表。必須は format_version=1, kind=user, dict_id, pos_table_version, created（UTC ISO 8601）。
- **PM 決定 R5**: 学習 DB の版は meta.format_version のみで判定。page_size=1024、user_version は設定・判定しない。専用 outer/export を追加しない。
- `dict_user(yomi TEXT NOT NULL, kanji TEXT NOT NULL, freq INTEGER DEFAULT 1, last_ts INTEGER DEFAULT 0, PRIMARY KEY(yomi,kanji))`。
- `idx_user_yomi(yomi)` を維持。学習・列挙・削除・export・全消去は学習接続だけに送る。全消去でも移行済み印は残す。
- 通常は system RO + user RW の 2 接続。学習 DB 無しは作成、ROFS・不一致・破損では警告して system のみで変換。
- 不一致の user DB は SQL 更新前に閉じる。削除・再作成・別 ID の自動採用はしない。S/M/L では共通 ID なので同じ学習を使用。
- 初回作成の schema/meta は 1 transaction。COMMIT 前の異常では未完成ファイルを既存学習と誤認せず、明示の再作成対象として報告する。
- SQL_EXACT/PREFIX を各 DB 用に分ける。system の前方一致 cost-500、user の -10000-freq は現在の式を維持。
- 各側 ORDER BY cost LIMIT 32 の cursor を 2 本進め、小さい cost 順に合成、同点は user 優先。上位合計32行で打ち切る。
- 現行どおり LIMIT の後に完全表記で重複排除、表示切詰め・非短縮ペナルティを適用する。重複を除いた分は補充しない。
- SQL の TEXT pointer は次の step/reset で失効するため、比較に必要な完全表記だけコピー。現行の長語用確保を再利用する。
- 学習側 cursor が途中で失敗した結果は破棄し、system cursor を reset して system のみ検索し直す。
- `dict_ready/recover/close` 相当は接続別。stale 判定は step 前、close 失敗時の isolated を維持。user 障害で system を閉じない。

## 4. 旧学習の移行（オフライン、配備より先）
- 新規 host 道具 `tools/fep_user_migrate.py` を使用。Python 標準 sqlite3/hashlib、ゲストに移行エンジンを常駐させない。
- 元媒体を停止した状態で、旧 S/M/L DB と journal が無いことを確認して写しを取得する。journal があれば旧環境で回復してから再取得。
- NP21/W: 停止済み NHD の写し → 新設 host `tools/fep_user_extract.py --nhd` → 旧3DBの写し。入力をROで扱い、NHDのディスク領域・partition・対象FSからファイルとjournalの有無を取り出す。
- 実機 Ra266: OS32停止後にHDDを別PCへ接続 → host `dd` でディスク全体をイメージ化 → 同道具 `--raw` → 旧3DBの写し。転送方向は実機→host、hsyncは使わない。
- 抽出道具は既存配備の媒体/FS形式を再利用する読取専用処理とし、欠落とI/O異常を区別、元パス・サイズ・SHA-256をmanifestへ記録する。
- host 上の 3 DB を mode=ro で開く。元媒体/NHD の直接編集機能はこの道具に入れない。元 DB と v2.x 用媒体の写しは保持する。
- 入力 manifest に固定パス→variant、存在しない辞書、基底 dict_id、品詞版を明示。読取不能を「無い」に丸めない。
- 形式0は固定 S/M/L の旧 IPADIC として移す。形式1は meta ID/品詞を照合。不一致は処理を中止する。
- 重複 `(yomi,kanji)` は **freq の最大値、last_ts の最大値** を採る（**PM 決定 U1**）。単独行は4列をそのまま保存。
- 元ごとに全ファイル SHA-256、行数を記録。target meta の `migration.S/M/L` に checksum と行数、`migration.complete=1` を保存。
- 順序: target BEGIN IMMEDIATE → 空の target へ合成 → 全キーと4列を比較 → 件数照合 → 印を挿入 → COMMIT。
- 照合は export API の表示用切詰めを通さず、元の TEXT/INTEGER を比較する。NULL・型不正は勝手に修正せず失敗を報告する。
- 同じ入力の再実行は印を照合して無操作。既に通常学習・削除した target へ再加算しない。印が異なる再移行は拒否する。
- 失敗は ROLLBACK。強制終了なら次回 SQLite の journal 回復後に印を確認、未完なら空の target から再試行する。
- 印の無い非空 target は自動マージしない。新旧どちらが正かユーザーが選べるよう両方を残す。
- 成功後 target を再び RO で開き全件照合し、停止中媒体へ配置する。配置途中の写しは採用しない（別名へコピー・照合後 rename）。
- 配備順は旧媒体の写し保持 → 学習移行/配置完了 → 新 FEP と辞書配備。失敗なら旧辞書の上書きを進めない。
- hsync は学習本体・journal・一時配置名を保護。新学習 DB は deploy/packages の配布ファイルにしない。
- **PM 決定 R6**: ゲストで旧表を検出しない。既存媒体の移行完了は host の照合と配備順で保証し、新規媒体は空 user を作成する。

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
- hsync 呼出元が settings から fep.user_dict_path を取得し、コピー開始前に登録。設定読取不能/不正は同期を開始しない。
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
| 移行ディスク | 旧3DBの写し + target + コピー中target + rollback journal最大target相当を確保。旧データを容量節約で消さない |
- FD の空きは媒体容量でありファイルディスクリプタ数ではない。移行PythonをFDへ入れない。
- 常駐 license buffer・SHA-256・移行器をFEPに入れないことで予算を抑える。変更後の像とFD収容は実装受入で再測定する。

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
| E/移行 | tools/fep_user_migrate.py 新 | コピー入力の移行・印・照合、220行/H |
| E2/抽出 | tools/fep_user_extract.py 新 | NHD/raw写しから旧DB・manifest抽出、約220行/H |
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
| 移行4列/重複 | host Python: S/M/L単独・重複・長いTEXT・last_ts=0を全件照合 | freq合成をmaxからsumに変更 |
| 再実行/途中失敗 | host Python: COMMIT前後で中断、再実行後同値、旧DB byte不変 | 移行済み印チェックを削除 |
| hsync保護 | host libcなし+ゲスト: 大小文字・正規化・祖先・custom・journal | FEP名保護条件を削除 |
| 境界/回復/容量 | host+ゲスト: cursor途中失敗、stale、close失敗、heap/像/FD測定 | user失敗時のsystem再検索を削除 |
- B/C/D/E/E2/F/C2はA後に並列可。I/J/K/L/Mは部品統合後、共有登録/KAPI世代を揃えて直列。N/O/O2〜O5は契約後に各所有者が既存検査を拡張、Pは結線後。
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
- N2: §4:89-90・§8 E2 を書き換える — 実機の学習の写しは「FD 起動 (FEP なし、旧辞書は開かれない) + LAN 版 SerialFS で /db/fep*.db を取り出す」(RA266 §0 の転送の優先度)。HDD を別の PC へつなぐ手段は使わない。NP21/W は既存の `tools/nhd_deploy.py` の pull と mount を読取りだけで使う。E2 は manifest を書く程度の数十行の道具に縮める。
- N3: core が常駐の接続数 (`boot_check(live, …)` の第 1 引数、vfs.c:261-268 の完全一致) を受け取る口を FEP の ops (`boot_bytes` の隣) に 1 つ置く。学習が無効なら 1、有効なら 2 を返す。
- R14: PM 決定 — 学習ファイルの custom path は /etc 直下に限る (票 §3 の「settings で上書き可」の範囲内の絞り込み、既存の列挙 hsync_protect.inc:163-198・hsync.c:1201-1261 をそのまま使う)。ユーザーへの未決は無し。
