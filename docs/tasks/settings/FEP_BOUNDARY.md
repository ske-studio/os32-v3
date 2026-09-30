# FEP_BOUNDARY — FEP user 境界と SQLite 中断禁止の最小実装契約

> 発行: PM (2026-09-09) / 状態: **設計中 (2026-09-30)** — **U6 決定 (ユーザー 2026-09-30、[TASK_MEMMAP_V3](../v3/TASK_MEMMAP_V3.md) D31): 独立票にせず TASK_MEMMAP_V3 §6 の T2 / T4 / T5a の要件として拾う** — B1 (uaccess の bounded copy) → T2 (新しい帯で `ring3_ptr_ok` を書き直す差分に含める)、B3 (旧 `db_exec` / `db_prepare` の checked copy) と B4 (`enter` / `leave` と in-flight の fail-stop、エクスポート表のラッパに置く) → T4、B2 (FEP facade、staging → finalize → copyout) → T5a。**旧 `db_exec` / `db_prepare` の SQL 1024B 超は「切捨てて実行」から「失敗」に変える** (D7 の範囲)。置き換わった部分 (§4 の帯判定 → T2、§6.3 の台帳 → T4 のエクスポート表、§6.2 の CTRL+STOP 保留 → §3-5 R1) は [U6_PENDING_REVIEW](../v3/U6_PENDING_REVIEW.md) §1-3。本文は 09-13 のままで書き換えない。
> それまでの状態: 設計中 (2026-09-13)

設計提案・独立レビュー R0 待ち／実装未承認。
本票は現行仕様や試験合格の宣言ではない。作成時の `feat/gui` HEAD は
`8b5642f`。既存 dirty の F1 DB 所有管理・F2a FD lease を含む作業ツリーを静的に照合した。
参照: [S0_FOUNDATION.md](../../archive/settings/S0_FOUNDATION.md)、[F2_OWNERSHIP.md](F2_OWNERSHIP.md) §6。
F2 の「SQLite 内部 fault は通常 cleanup で回復できない」という停止条件を具体化する。

## 1. 範囲・禁止事項

- 今回作成するのは本ファイルだけ。コード、他文書、環境、秘密情報、`docs/hw/`、
  エミュレータ、配備、ネットワーク、追加 agent、commit は扱わない。
- 後続実装の目的は **SQLite/VFS が caller の未検証ポインタを一度も読む／書く必要のない境界**。
  FEP facade 入力を bounded kernel-owned copy にし、list は kernel staging→finalize→checked copyout にする。
- FD group/lease、owner cleanup の順序は F2 の担当。本票で上書きしない。
  設定 DB、全 KAPI の uaccess 化、一般例外再設計、SQLite amalgamation 改造、WAL 導入は対象外。
- ABI 変更は許容されるが必須ではない。必要なら [ABI1–3] に従い `sdk/kapi.json` を正典として
  末尾追加・版更新・再生成、`make clean`→`make all`、SDK 利用側と `make external` まで全 rebuild。
  生成物の手編集や旧新 binary の混在は禁止。GNU89 [C1]、kstring [C2]、定数集約 [C4] を守る。
- **R0 独立レビューと PM の明示承認前に実装しない**。この文書の自己点検は独立レビューではない。

## 2. 実コードで確認した穴

行番号は本票作成時点。実装開始時は symbol と差分を再照合する。

| 根拠 | 現状／必要な変更 |
|---|---|
| `kernel/ime.c:895–912` | list/delete/export は caller pointer をそのまま dict 層へ渡す。clear は pointer なし |
| `kernel/ime_dict.c:271–325` | prefix を prepare 後に SQLITE_STATIC bind。ROW 中に out を直接書き、最後に finalize。途中の user fault は stmt を残して脱出し得る |
| `kernel/ime_dict.c:329–364` | NULL/空 kanji は同じ yomi の全候補削除。prepare 後に yomi/kanji を bind する。invalid→空文字変換は削除対象を拡大する |
| `kernel/ime_dict.c:368–407` | user path を VFS に直渡しし、O_TRUNC open が prepare より先。無効 path は file 操作より前に拒否する必要がある |
| `kernel/ime_dict.c:61–267,411–425` | open/close/reopen/search/learn/clear も直接 SQLite。search/learn の SQLITE_STATIC binding は reset だけでは解放されない。常駐 stmt の寿命も対象 |
| `kernel/ime.c:283–285,491,878–891` | 学習は kernel の候補配列、検索は kana_buf、switch は整数から選ぶ kernel literal path。user facade と trusted 内部呼出しを混同しない |
| `kernel/ime.h:28–35,64–74`、`sdk/include/os32/os32_kapi_shared.h:419–424` | 結果 yomi/kanji は各32 byte、入力 kana_buf は128 byte。**結果幅32を入力上限の根拠にできない** |
| `userland/cmds/ime.c:32,119–164` | list は entries[64]/max=64。prefix/yomi/kanji/path は argv を直接渡し、32 byte 制限なし。省略 prefix と kanji は空文字、export 既定は `/tmp/userdict.csv` |
| `kernel/paging.c:409–416` | paging_is_present は master の page_tables を参照するだけ。現在 caller PD の PDE/PTE USER/RW 判定に流用できない |
| `exec/exec.c:310–324,592–622` | ring3_ptr_ok は先頭番地の帯域判定。長さ、跨ぐページ、実効 USER/RW は検証しない。wrapper 中は caller CR3 のまま |
| `kernel/isr_handlers.c:246–251,337–349`、`exec/exec.c:651–662,448–516` | ring3_in_syscall が真なら kernel 内 fault も app kill→AS破棄→exec_exit。現在 exec_exit は FD cleanup 後に db_cleanup_all。エンジン中断後の cleanup 再入は安全でない |
| `kernel/ring3_entry.asm:44–63` | int80 本体は sti、出口は cli。49行の古い「cli不要」コメントでなく実命令と57行以降を正とする |
| `kapi/kapi_db.c:28–33,198–233,275–365` | path 256/SQL 1024 のコピーはあるが checked copy でなく切捨て。exec/prepare は **旧 active_stmt finalize が SQL copy より先** |

## 3. 上限・返値・NULL の提案を先に凍結する

以下は新しい API 方針の提案値であり、既存 argv の保証ではない。長さは UTF-8 の文字数でなく
**NUL を含む byte 容量**。上限内 NUL がなければ失敗、切捨てて実行しない。

| 用途 | 提案容量／規則 | caller との整合 |
|---|---|---|
| list prefix、delete yomi/kanji | 各256 byte（payload 最大255） | argv には従来上限がないため意図的な新制限。32 byte を超えるキーを拒否しない。kana_buf[128] も収まる |
| export path、後続 DB path | 256 byte（payload 最大255） | DB PATH_COPY_BUF_SIZE と os32 VFS mxPathname=256 に合わせる。実装時 VFS 共通 path 定数との一致も assertion で確認 |
| list max | 1〜64、範囲外は負値で拒否（clamp しない） | 実 caller の64を維持。IME_MAX_RESULTS=32 は変換候補数であり列挙件数とは別 |
| list staging | `64 * sizeof(IME_UserEntry)` | 現行 i386 layout は68 byte/entry、最大4352 byte。target sizeof/offset assertion を置き、padding を含め全領域をゼロ初期化 |
| 後続 DB SQL | 1024 byte（payload 最大1023） | SQL_COPY_BUF_SIZE を維持するが黙った切捨てを廃止。長文 caller は失敗を扱う。拡張は別の明示 ABI/容量決定 |

- `list(NULL,out,max)` と検証・コピー済み空 prefix は全件の先頭 max 件。
  out=NULL、max<=0、max>64 は -1。0 は有効な要求の空結果に限る。
- delete は yomi=NULL/空を -1。kanji=NULL または **正常にコピーした**空文字は同一 yomi の全候補削除を維持。
  両方の入力を確定してから SQL の分岐を選ぶ。NULL を別 status として保持し、copy failure と混ぜない。
  非NULL不正/長過ぎ/未終端 kanji は -1、DELETE を一度も実行しない。
  「同一 yomi 全候補」と全辞書消去 `ime_user_clear` は別操作。
- export path=NULL/空は -1。入力不正でファイル作成・truncate・診断用 `%s` 参照も行わない。
- facade の引数/容量/copy 失敗は -1。list の prepare/bind/step/finalize 失敗も負値とし部分行成功にしない。
  delete/export の既存 SQL/I/O エラー分類は維持可能だが全 rc を検査する。DB 側の失敗は既存負値へ翻訳する。
- dispatcher が既に拒否する帯域外 pointer/bad ESP は既存 app kill のままでよい。
  新しい bounded check で発見できる不正をわざと #PF にして kill しない。
- 結果構造体は本票では32 byte幅を維持。list の表示省略は UTF-8 境界で NUL 終端し、
  入力の切捨てと区別する。長い DB キーの省略表示を完全な削除キーとは保証しない。
  完全キー返却が必要なら構造体/新 API を別途承認し全 rebuild。CSV 全仕様の改変はしない。

## 4. checked copy の契約（新規内部 helper、公開 KAPI ではない）

提案責務は `copy_caller_cstr`、`check_caller_write_range`、`copy_to_caller`。
命名・配置は R0 で確定し、ページ walk を facade ごとに複製しない。

1. caller 種別は **保存された syscall の出自**から決める。現在 CPL=0、pointer の番地、master PD か否かで
   trusted と推測しない。ring3_in_syscall の外の kernel/常駐 shell 呼出しを明示 trusted 分岐として保持する。
   trusted でも容量/NUL は検証し kernel-owned 化するが USER bit を要求しない。
2. user は入口で現在 CR3 を取得し、実行中 caller PD と一致を確認。master page_tables の検査は禁止。
   各ページの **PDE と PTE 両方**に PRESENT|USER、copyout はさらに両方 RW を要求。
   copyin に RW を要求しない（shlib .rodata の入力は有効）。CR0.WP に安全性を委ねない。
3. 32bit unsigned の `[start,start+len)` を overflow 前に検査。非ゼロ長のNULL、上限超え、
   `len > limit-start`、件数積 overflow を拒否。ページ丸め時の加算 overflow も避ける。
   全ページを walk し、PDE 非present なら PT を読まない。未対応 PTE_PS は fail closed。
   PD/PT の frame は kernel が管理しアクセス可能な正しい table と検証してから参照する。
4. user の受入域は現 caller に許された通常 RAM（app/shlib/stack/SHM）かつ上記の実効権限。
   kernel/予約帯、未map、guard、MMIO/VRAM は対象外。既存 ring3_ptr_ok の VRAM 許可を
   FEP の文字列 I/O へそのまま移植しない。別 AS、非identity の shlib data も現在 PD の mapping で扱う。
5. C string は page ごと／byte ごとに必要範囲を検証して NUL までコピーする。
   NUL が page の最後にあれば次の unmapped page を読まない。最初に無検証 strlen/kstrncpy をしない。
   失敗時は caller に触らず、未完成 kernel buffer は絶対に dict/SQLite/VFS へ渡さない。
6. 検査とコピーの短い区間は EFLAGS 保存→cli→walk/copy→**入口 IF を復元**。
   成功、NULL、overflow、途中 page 失敗、busy など全出口を同じ復元規約にする。無条件 sti は禁止。
   現行単一CPU・非再入条件で mapping 変更を排除する。区間内で allocation、SQLite、VFS、描画、
   GUI pump、blocking、callback は呼ばない。将来別CPU/DMA writer まで snapshot 原子性を保証するものではない。
7. SQLite/VFS 実行中は通常 IF=1 を維持。IF=0 の呼出しは copy primitive の試験には許すが、
   I/O を行う facade は入力検査後・engine/VFS 前に失敗させ IF=0 のまま返す。長い処理を cli で囲まない。
   int80_stub の sti/出口 cli/iretd の責務は変更しない。

## 5. FEP の実行順序と lifetime

### list

1. scalar/max 検査→prefix を kernel buffer へ全コピー→最大返却範囲の出力を preflight。
   失敗時 engine/VFS entry=0、caller output 不変。prefix と out が重なってもコピー済み入力を使う。
2. 固定容量の kernel 専用 staging を予約・ゼロ初期化。4352 byte を kernel stack に置かない。
   推奨は専用 BSS と busy 所有フラグ（exec heap/SHM/user map に置かない）。同時再入は engine 前に拒否。
   busy 検査/set は短い IF保存区間。状態を消すために長い cli や owner の書換えはしない。
3. dict 層には kernel prefix と kernel staging だけ渡す。prepare→全 bind の rc 確認→step/column コピー。
   SQLite column pointer を staging に保存せず内容をコピー。異常時も生きた stmt を一度 finalize。
4. 全行 staging 後 **finalize 完了→engine leave→現在 caller PD/範囲を再検査→checked copyout**。
   件数に対応する bytes だけ出力する。途中失敗/late output failure は負値、成功件数を返さない。
   copyout は実際の書込み範囲を全検査してから一括コピーし、通常の不正 range で部分出力しない。
   異常な kernel fault の原子 rollback まで約束しない。
5. 空結果も finalize/leave を通す。全正常／エラー return で staging busy を解放する。

### delete/export/clear と内部検索・学習

- delete は yomi と optional kanji の copy を **両方**完了後に prepare/bind/step/finalize。
  binding の長さは確定した byte 長。SQLITE_STATIC を使うなら finalize まで kernel buffer を保持する。
- export は path copy 成功後のみ VFS open。SQLite に渡す SQL は kernel literal、CSV は kernel buffer。
  出力 FD は F2 のとおり GENERIC。open/prepare/bind/step/write/finalize/close の通常エラーで
  stmt/FD の局所後始末を一度だけ行う。入力拒否は既存出力ファイルを変えない。
- clear/switch/toggle/set_mode/feed_key からの lazy open も engine guard の対象。
  scalar/既存 kernel literal を使う経路に架空の user copy は追加しない。
- search/learn は kernel g_ime 由来だが常駐 stmt の SQLITE_STATIC を放置しない。
  結果取得完了後または失敗後、reset＋clear_bindings を行い、借用 kernel buffer の寿命を超えない。
  list/delete は finalize で借用を終える。SQLITE_TRANSIENT に変える場合も bind 時コピーと
  エンジン進入前の user copy は別条件である。学習経路に32 byte入力上限を新設しない。

## 6. 必須 followup: DB path/SQL とエンジン in-flight fail-stop

FEP copy だけを先行 host 検証してよいが、以下が未完なら F2 の終了統合安全性を受入しない。

### 6.1 DB boundary

`kapi_db_open/exec/prepare` に同じ checked bounded copy を適用。
SQL は **旧 active_stmt を finalize する前**に全コピーする。NULL/長過ぎ/unmapped SQL で
既存 stmt・transaction・slot・SHM結果を変更せず、SQLite 呼出しをゼロにする。
無効 handle 判定は kernel metadata のみで行い、不正入力の診断も sqlite3_errmsg を呼ばない固定文言とする。
path も slot の予約・SQLite open・VFS 操作前に確定する。
正常にコピーした SQL の実行失敗は別扱いで、既存 stmt 更新方針と F1/F2 teardown に従う。
現在の shared sql_copy_buf の非再入も明示し、上書きできる callback を持ち込まない。

### 6.2 engine in-flight は「回復可否」の印であり FD owner ではない

- kernel 内に小さな engine active 状態と enter/leave を置く。F2 の group/owner context とは分離。
  **全 outer SQLite 操作**の最初の SQLite call 前に enter、最後の SQLite call と borrowed pointer の
  kernel コピー・reset/finalize 等が終わってから leave。正常 SQL エラーでも必ず leave。
- 割込みは許すが SQLite の再入は許さない。busy を検知した outer 呼出しは SQLite へ入らず失敗。
  SQLite→VFS→sqlite3_os_init/vfs_register の正規 callback は外側 active 内にあり、
  新しい outer operation として enter し直さない。cleanup の内部 helper も二重 enter を避ける。
- #PF と一般例外の両 handler で **ring3_in_syscall による app-kill 判定より前**に active を調べる。
  active 中の fault は kernel 異常として既存停止経路へ送る。EIP が `.sqlite_text` 内だけという判定は禁止。
  VFS/allocator/column copy/IRQ 中でも同じ engine が中断されているため fail-stop。
- この分岐では ring3_fault_kill、exec_fault_recover、exec_exit、AS破棄、SQLite finalize/rollback/close、
  FD cleanup を呼ばない。固定診断だけを使い allocator/SQLite/VFS に再入せず停止。
  active をゼロにして shell に戻す、pool reset、接続 quarantine だけで継続する方式は不採用。
- active=0 の通常 app #PF/#GP/bad ESP は既存 app kill 方針。事前検査で拒否した bad pointer は
  engine entry=0 の通常エラー。**普通の不正引数とエンジン内部の故障注入は別テスト**。
- CTRL+STOP は従来の syscall 境界まで保留し、エンジン途中から longjmp させない。
  非SQLite例外の一般設計・ISR frame・syscall ABI・exec owner 回収を本票で作り直さない。

### 6.3 direct SQLite coverage（漏れゼロが受入条件）

| 実在ファイル | カバーする入口／扱い |
|---|---|
| `kernel/ime_dict.c` | open/partial-open cleanup、close/reopen、search/learn、list/delete/export/clear。file_control を呼ぶ dict_fd_protect も含む |
| `kapi/kapi_db.c` | open/exec/prepare/step/column_int/column_text/finalize/close、last_error/mem_used、SHM row/error helper、all/owned cleanup。読み取り・診断 API も SQLite entry |
| `lib/sqlite3/os32_sqlite_vfs.c` | config/initialize の boot 外側、os32_sqlite_db_fd の file_control。sqlite3_os_init→vfs_register は初期化の内側として扱う |
| `lib/sqlite3/os32_sqlite_test.c` | boot の :memory: open/PRAGMA/exec/close。file FD がなくても共有 kernel engine なので対象 |
| `userland/tests/sqlite_standalone/sqlite_standalone.c`、`sqlite_user_vfs.c` | 独立 userland SQLite の試験。kernel pool の guard を持ち込まない。実 build/link で別 engine と確認する |
| `lib/sqlite3/sqlite3.c/.h`、config header | engine内部・宣言・マクロは outer callsite と区別。amalgamation を機械的 wrapper 化しない |

実装 R0/R1 で tracked source と追加ソース、利用可能な apps/game を再検索し、
全 `sqlite3_*` 呼出しを「outer保護済み／保護内callback／独立engine／宣言」に分類した一覧を提出する。
上表は現時点のコード検索に基づく初期台帳であり、将来追加された callsite を免除しない。

## 7. 小さい段階・編集集合とゲート

各段階で実ソースをコンパイルした host harness に RED→最小変更→GREEN を記録する。
命令/PD/IRQ/I/O の境界だけ shim 化し、アルゴリズムの複製を試験して合格としない。
新規 helper/test のファイル名と build 登録は各段階の開始承認時に固定する。

| 段階 | 後日承認する小さいファイル集合 | 完了条件 |
|---|---|---|
| R0 | 本票の独立読取りレビューのみ | 上限、NULL、caller provenance、table walk、IF、guard 全caller一覧に重大未解決ゼロ。PM承認 |
| B1 uaccess | `kernel/paging.c/.h`＋専用 `tools/tests/` harness。出自 helper が必要な場合だけ別小差分 `exec/exec.c/.h` | synthetic caller/master PD、safe range/overflow/IF host 検証。既存 paging helper の意味を変えない |
| B2 FEP | `kernel/ime.c`、`kernel/ime_dict.c`、`kernel/ime.h`＋専用 host test | staged list、全入力copy、NULL、借用binding寿命、SQL/I/O失敗。SDK layout変更は別承認 |
| B3 DB followup | 排他移管した `kapi/kapi_db.c/.h`＋既存 F1/専用 host test | copy-before-finalize、無効 SQL/path の engine entry=0、F1/F2回帰 |
| B4a guard基礎 | 新規 kernel内部 guard `.c/.h`、必要な build source 登録、`kernel/isr_handlers.c`＋専用 host test | active fault→停止／非active→従来分類。ISR frame変更なし |
| B4b 全caller移行 | DB側、FEP側、boot/VFS側を上表のファイル集合ごとに直列小差分 | 全 outer callを保護、callback二重enterなし、早期returnのleave漏れゼロ。未移行状態はhost限定 |
| R1 target/guest | 独立 reviewer→承認された専任検証者 | 下記host/target/guest証跡を受領してから F2 統合へ引渡し |

B2/B3 は B1 に依存。B4a/b を含む統合済み単位まで guest 配備しない。
F2 の `ime_dict.c`/`kapi_db.c`/VFS、GUI の exec 編集者と共有ファイルは直列移管する。
本票で exec_exit を独自改修しない。新 guest test は別途許可された `userland/tests/` と
自層 `deploy.yaml` [V2]／build登録だけを追加範囲にする。

## 8. 必須試験（以下は期待値、今回未実行）

| ID | host assertion／後日の guest gate |
|---|---|
| H1 | masterはPだがcallerはNP、逆のケース、PDE/PTEそれぞれのP/USER/RW欠落、RO入力可/RO出力不可、非identity shlib。master参照で合格させない |
| H2 | NULL、page末NUL、次page NP、未終端、容量末NUL、容量超、start+len/件数積 overflow、範囲末跨ぎ、PS PDE、MMIO、guard。拒否でengine/VFS呼出しゼロ |
| H3 | IF=0/1それぞれの全copy出口、途中失敗、busy。入口IFと戻りIF一致。I/O facade のIF=0拒否、正常IF=1実行中のIRQ進行 |
| H4 | max=-1/0/1/64/65/INT_MAX、out NULL、prefix/out重複、32byte超の有効キー、255byteと256byte payload。kernel専用コピーだけがSQLiteへ渡る |
| H5 | list prepare/bind/step/finalize失敗、0/64 ROW、UTF-8省略、padding/canary。traceがcolumn-copy→finalize→leave→copyout、late invalid outでもstmt残存ゼロ |
| H6 | deleteのNULL/空/有効kanjiと、不正/未終端/長過ぎkanjiを区別。後者はDELETE=0、同一yomiの他候補を一切削除しない。export invalid pathでopen/truncate=0 |
| H7 | search/learn反復、SQLITE_STATICのclear/reset寿命、全エラー分岐のbusy解放。export SQL/I/Oエラー時にstmt/GENERIC FDを一度だけ後始末 |
| H8 | 旧active stmtと未commitのDBにbad SQLを渡す。finalize/step/errmsgを含むSQLite entry=0、旧stmt/transaction維持。bad pathもslot/FD不変 |
| H9 | 全outer APIでenter/leave対、正規VFS callbackはactive維持、busy再入拒否。engine/VFS途中の#PF/一般例外注入で停止経路だけ、kill/cleanup/close/rollback=0 |
| G1 | 新binary同一性確認後、ring3のvalid list/delete/export、RO/NP跨ぎ等を個別試験。エラーか既存入口killを区別し、通常bad pointerでkernel停止しない。正常反復のstmt/FD/pool baseline不変 |
| G2 | FEP初回open→検索/学習→子の正常終了/#PF/#GP/STOP→再検索/学習。F2統合後に親DB/遅延journal/常駐辞書の生存、子の通常回収を確認 |
| G3 | 別boot・使い捨て媒体の明示故障注入でengine/VFS in-flight faultがfail-stop。shell復帰やcleanupがない診断を確認し再起動。通常ゲートと混ぜず本番辞書を使わない |

host は (a) 実 facade/DB/PD判定＋呼出しtrace/mock故障注入、(b) 同梱 SQLite＋実 OS32 VFS/FD の
隔離 backend fixture に分ける。mock 合格だけで実 SQLite/実 PD/IRQ を証明したとしない。
target は GNU89 compile/link、必要な kselftest 追加、`make check`。ABI変更時は §1 の全 rebuild。
Make の環境自動読込み・network依存は実行前に確認し必要な許可を別途得る。
guest は [D1][D2][V1][V4] に従う。専任検証者が停止→承認済み配備→起動し、
配備された kernel/test の同一性、実行コマンド、exit status、counter/trace、失敗・skip を記録する。
電源断耐性・破損エンジンの復旧・全KAPI安全性を本票の合格から導かない。

## 9. 現時点の引渡し

完了は実ソース・既存差分の静的照合と本契約案の作成だけ。
R0/R1、コード実装、host試験、target build、guest実走、F2統合は未実施。
独立 reviewer は特に (1) caller PD/USER/RWとsafe range、(2) IF全出口、(3) invalid→delete拡大なし、
(4) 32byte入力仮定なし・64件caller維持、(5) finalize前SQL copyとfinalize後list copyout、
(6) 全direct callerと両ISRのfail-stop優先順位を承認または具体的修正要求として返すこと。
