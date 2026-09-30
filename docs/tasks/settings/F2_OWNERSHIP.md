# F2 — SQLite 接続単位の VFS FD 所有・失敗隔離・exec 終了統合

> 状態: **設計中 (2026-09-30)** — **U6 決定 (ユーザー 2026-09-30、[TASK_MEMMAP_V3](../v3/TASK_MEMMAP_V3.md) D30): 残り全部 (F2b の呼び出し側接続・F2c・F2d の隔離経路・default VFS の fail-closed・R0 / R1) は独立票にせず、TASK_MEMMAP_V3 §6 の T4 (SQLite 分離、`kapi_db` / `ime_dict` の接続部) と T5a (FEP モジュール) の受入に畳む。** 前提の変化 (RESIDENT group は 1 → 2 本、FEP の接続は起動時 live AS = 0 で開く、group 表はモジュールの data、R7 で v3 では取り外さない) は [U6_PENDING_REVIEW](../v3/U6_PENDING_REVIEW.md) §1-2。本文 (§1〜§8) は 09-13 のままで書き換えない。
> それまでの状態: 設計中 (2026-09-13 から動きなし) — F2b の内部基盤だけ着地 (be445f4e、`os32_sqlite_group_*` は呼び出し側に未接続 — 2026-09-29 に grep で確認)。§7 の設計レビュー R0・実装レビュー R1 は未実施。「実装中」は実態と違うので直した。v3 の入力として残す。(2026-09-29 の棚卸しで更新)
>
> 発行: PM (2026-09-09) / それまでの状態: **実装中 (2026-09-13)**

初稿時の状態: 設計提案／実装前の独立レビュー待ち。最新の scoped 実装進捗は §7.1。
F1 は受入済みという依頼条件を前提とする。
本票作成時の作業ツリーでは `kapi/kapi_db.c/.h` に F1 の未コミット変更があり、
`db_cleanup_owned(int owner)` は存在するが **exec には未統合**。本票は実装承認・試験合格の記録ではない。

上位契約: [S0_FOUNDATION.md](../../archive/settings/S0_FOUNDATION.md) §2、§3 F1/F2/F3。
本票は S0 の F2 に必要な内部 VFS 前提作業を具体化する。終了順序の変更だけを先行させない。

## 1. 範囲と受入の意味

目的は、接続を開いた exec owner と、その接続が後から開く全 OS32 FD を結び付け、
正常時は SQLite に閉じさせ、close 失敗時は接続と残存 FD を再利用不能に隔離してから子を終了すること。
親接続を子から操作する既存挙動は維持する。アクセス制御や公開 handle の世代化とは別問題である。

- 今回の書込みは **本ファイルのみ**。コード・SDK・生成物・他文書・設定・環境・秘密情報・
  `docs/hw/`・配備・エミュレータ・ネットワーク・commit・追加 agent を扱わない。
- 後続実装は GNU89 [C1]、kernel 文字列は kstring [C2]、容量定数は管理元へ集約 [C4]。
- **公開 KAPI/ABI の追加・変更は不要**。ユーザーは ABI break と全体再 build を許容しているが、
  それを理由に F0/F4、SDK、loader、世代付き公開 DB handle へ拡張しない。
- F3 の seek/truncate/sync/delete/access/lock/RO 契約は別票。F2 で I/O の成功 stub を正当化しない。
- TEMP_STORE=3／OMIT_WAL／OMIT_ATTACH を維持。匿名 temp の実装や WAL 導入はしない。
- SHM 全回収の一般改修、GUI 終了理由、親 heap 復元、redirect/pipe の所有規則は変えない。
- 証明するのは FD 生存・所有・回収順・隔離である。**電源断耐性／完全 crash durability は保証しない**。

## 2. 現在の根拠（本票作成時のソース位置）

| 場所 | 現状と F2 に必要な差分 |
|---|---|
| `kapi/kapi_db.c:37–45,198–233,247–272,462–478` | DbSlot は owner／isolated を保持。finalize→必要なら ROLLBACK→sqlite3_close、close 失敗時は slot のみ隔離。FD の情報はない。owner/all cleanup は isolated を再試行しない |
| `tools/tests/kapi_db_owned_tdd.md:3–5,55–65` | F1 の既存 host 証跡は実 DB slot＋SQLite mock。実 VFS／FD／exec／FEP 生存の証拠ではない |
| `lib/sqlite3/os32_sqlite_vfs.c:68–85,180–222` | Os32File は fd だけ。xOpen は現在 owner の `vfs_open`。xClose は void の `vfs_close` を呼ぶ。`os32_sqlite_db_fd` は main しか返さない |
| `fs/vfs_fd.c:38–150` | FD 表は in_use／protect／owner。vfs_open は res_owner_get で付与。vfs_close は protect を無視して表を空ける。vfs_close_owned だけが protect を除外する。backend close callback は現在ない |
| `fs/fd_redirect.c:18–22,109–137` | owner は再利用される nest 整数。redirect reset は直接 vfs_close を呼ぶため、「protect したから全 close 経路で安全」ではない |
| `exec/exec.c:448–511` | heap reset 後、redirect→FD→pipe→SHM→sound→**db_cleanup_all**→GUI→nest 減算。親 DB の破壊と SQLite より先の FD 解放が残る |
| `exec/exec.c:386–442,1016–1019,1112,1208` | nest 更新後の起動失敗は exec_launch_abort。現在は子コード未実行・owner 資源なしを前提に owner cleanup を省略 |
| `exec/exec.c:252–258,514–533,565–636,651–663,1288` | CTRL+STOP→ring3_fault_kill→exec_fault_recover→exec_exit。通常 sys_exit と CPL0 entry return も exec_exit。STOP は SQLite/KAPI の途中ではなく静かな境界へ持ち越す |
| `kernel/ime_dict.c:25–31,61–127,220–267,270–426` | FEP は直接 SQLite。open 完了後に main のみ protect、close 前に解除し close の結果を捨てる。検索・学習・列挙・削除・export・clear がある。export 出力 FD は SQLite FD ではない |

**接続から xOpen へ所有情報を運ぶ既存経路**:
`lib/sqlite3/sqlite3.c:189567–189584` は指定 VFS を `db->pVfs` に保存して main を開く。
`:64484–64491` は Pager に pVfs を保存、`:64737,64878` は journal を同じ VFS で開く。
メモリ journal の遅延実体化も `:109886–109892` の `copy.pVfs` を使う。
temp pager は `:63227–63240`、sorter は `:107820–107831` で接続由来の VFS を使う。
**この経路を使い、現在 owner の一時書換えやパス末尾からの接続推測を不要にする。**

## 3. 凍結する最小方式: 接続専用 VFS instance＋FD lease

### 3.1 接続 group と専用 VFS

`lib/sqlite3/os32_sqlite_vfs.c/.h` に kernel 内部の `Os32SqliteGroup` を導入する。
各 group は固定寿命の descriptor で、以下を持つ。

- `state = FREE / OPENING / LIVE / CLOSING / QUARANTINED`。
- `kind = EXEC / RESIDENT` と immutable な open_owner。RESIDENT は nest 0 の別名ではない。
- 内部 cookie `{group_index, generation}`。公開 DB handle と分離する。
- 専用 `sqlite3_vfs`、一意な不変 zName、pAppData→当該 group。
- 接続ポインタ（close 成功後は NULL）、最初の失敗段階/rc のコピー、残存 FD 数の診断。
- quarantine descriptor は exec heap／SQLite が破棄する sqlite3_file 領域に置かない。

**実装は SQLite amalgamation を変更せず**、既存 os32_vfs のメソッドを共有する小さな
VFS instance を各 group に一つ作る。初期化時に非 default として一度 register する。
登録リストの pNext を単純コピーしない。zName、pAppData、descriptor は boot 中ずっと有効。
FREE でも VFS descriptor を解放/unregister せず、次の接続には generation を進める。
同じ instance で同時に複数接続を open しない。generation の wrap 時はその descriptor を再使用しない。
容量は KAPI の DB_MAX_CONNECTIONS 分と FEP 常駐分を分け、FEP の予約を KAPI が消費しない。
現在の FEP 利用数を実装前に確認して名前付き容量定数を定める。隔離分を自動増設しない。

提案する内部 API（型の定義も内部 header のみ）:

```c
int os32_sqlite_group_acquire(int kind, int owner,
                              Os32SqliteGroup **out);
const char *os32_sqlite_group_vfs_name(Os32SqliteGroup *g);
int os32_sqlite_group_opened(Os32SqliteGroup *g, void *db);
int os32_sqlite_group_begin_close(Os32SqliteGroup *g);
int os32_sqlite_group_finish(Os32SqliteGroup *g, int close_rc);
void os32_sqlite_group_quarantine(Os32SqliteGroup *g,
                                 int stage, int rc);
```

- acquire は FD/file 操作前に予約、失敗なら DB open 自体を行わない。
- KAPI の旧 sqlite3_open を `sqlite3_open_v2(path, &db,
  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, group_vfs_name)` に置換する。旧 create 用途を維持する。
  FEP は READWRITE のみを維持。URI による VFS 差替えは許さず、実 build の URI 設定もレビューする。
- open 中も main/journal が開き得るため OPENING から FD を所属させる。成功後に owner を後付けしない。
- opened は db と group を関連付けて LIVE にする。open エラーで非 NULL db が返れば、
  OPENING の group を保持したまま F1 と同じ teardown を実行する。
- begin_close は OPENING/LIVE→CLOSING。**CLOSING でも rollback が必要とする新規 FD open は許す**。
- finish は close の成否だけでなく、当該 cookie の残存 FD 数も確認する（§4）。
- FREE／QUARANTINED の VFS への xOpen は file 操作前に失敗する。default の os32 VFS からの
  **未所属の file xOpen も fail closed** にする。`:memory:` boot selftest は FD を作らず継続可能。
  後続実装時に全 direct SQLite open callsite を列挙し、未移行の file 接続を残さない。

これにより、親 A が owner=1 で開いた接続を子 B（current owner=2）が step して初めて
journal を作っても、xOpen は **A の pVfs->pAppData** から A の cookie/owner を得る。
B の owner は一度も変更しない。FEP が B 実行中に初めて学習しても同様に RESIDENT group へ入る。
全 SQLite entry の push/pop context 追加や、IRQ/longjmp に残り得る「現在の SQLite 接続」global は採用しない。

### 3.2 OS32 FD 表の内部 lease

`fs/vfs_fd.c` の VfsFile に `lifetime = GENERIC / SQLITE`、cookie、FD generation、
quarantined、SQLite open flags（診断用）を加える。既存 protect と混用しない。
**この FD 表を所属集合の正典**とし、解放される Os32File の intrusive list を隔離後に辿らない。
Os32File は fd に加えて cookie と FD generation の lease を持つ。

提案内部 API（`fs/vfs.h`、SDK へ公開しない）:

```c
int vfs_open_sqlite(const char *path, int mode, int owner,
                    const VfsSqliteCookie *cookie, int sqlite_flags,
                    VfsSqliteLease *out);
int vfs_close_sqlite(const VfsSqliteLease *lease);
int vfs_quarantine_sqlite(const VfsSqliteCookie *cookie);
int vfs_count_sqlite(const VfsSqliteCookie *cookie);
```

- open は既存 vfs_open と共通内部実装を使うが、owner/class/cookie/generation を引数で確定する。
  FD を公開する前に全 metadata を設定する。通常 vfs_open は current owner＋GENERIC を渡す。
  cookie の有効性を SQLite 側で検証し、FD generation 更新不能や容量不足なら I/O 前に失敗する。
  後付け登録用の別 malloc を作らず、FD 確保成功後に所属記録だけ失敗する窓をなくす。
- SQLite xOpen は group を検証して vfs_open_sqlite を呼ぶ。成功した全 file 種別を登録する。
  MAIN_DB／MAIN_JOURNAL だけの allowlist にしない。TEMP_DB／TEMP_JOURNAL／SUBJOURNAL 等も
  **実際に FD が開けば同じ契約**。zName==NULL は現行通り CANTOPEN、FD/記録はゼロ。
  メモリ temp/journal は OS32 FD を持たないため、架空の FD を作らずゼロと記録する。
- xClose は現在 owner や main の存否を参照せず、保存 lease でのみ閉じる。
  close_sqlite は in_use/class/cookie/FD generation 一致、非 quarantine を検証してから解放する。
  不一致なら新しい FD を閉じず内部エラーを返す。成功時だけ Os32File の lease を無効化する。
- 現在の vfs_close は void かつ表の解放だけで backend close 障害はない。
  F2 の int 戻り値は lease 検証のためであり、実在しない backend close failure を主張しない。
  将来 backend close を導入するなら失敗時 in_use を維持する契約を別途守る。
- **vfs_close_owned は SQLITE class を必ず除外**。これは OS32 全 FD の protect ではなく、
  xOpen で確認された一接続の各 FD の close 権限を SQLite へ委譲するもの。
  通常の子 FD／redirect／pipe は従来通り回収する。
- 直接 vfs_close も SQLITE class を解放しない（void の既存シグネチャは維持し拒否を診断）。
  `vfs_fd_set_protect(fd,0)` でもこの制約は解除できない。redirect reset 等による迂回を防ぐ。
  xClose は専用 close_sqlite を使うため正常解放可能。
- quarantine は cookie が一致する **現在生存中の FD だけ**に sticky な隔離状態を付ける。
  FD 数走査は許すが、無関係な FD に protect/owner 変更を一切しない。
  SQLite の FD 操作入口でも lease／group 状態を検証し、隔離後の callback を無操作で拒否する。
  本票は推測した fd に対する公開 read/write の全面的な認可改修までは含めない。

### 3.3 F2a HOST 限定の確定契約（F2b–d の承認・完了とは別）

F2a の編集対象は `fs/vfs_fd.c`、`fs/vfs.h`、`tools/tests/` の専用 harness／TDD 記録、
および本節の契約明確化のみ。初稿 §1 の文書限定作業と区別する。exec／SQLite／FEP は未接続。

- `VfsSqliteCookie = {int group_index, u32 generation}`、
  `VfsSqliteLease = {int fd, u32 generation, VfsSqliteCookie cookie}`。
  open は `VFS_OK` でのみ out を更新し、失敗時は out と FD 表を変更しない。
  owner は非負、cookie index は非負、cookie generation は非ゼロ、path/cookie/out は非 NULL。
  sqlite_flags は診断値としてそのまま保持し、種別 allowlist は設けない。
- 内部 `vfs_validate_sqlite(const VfsSqliteLease *)` を追加する。in_use／SQLITE class／
  cookie 両成分／FD generation／非隔離を検査して OK または INVAL。
  検査自体は FD 表・lease を変更せず、backend、path probe、owner setter を呼ばない。
  dedicated close は同じ検査の成功時だけ FD 表を解放する。backend close callback は存在しない。
  呼出側 lease の無効化は F2b xClose の責任であり、この const API は変更しない。
- SQLITE の直接 generic close は無操作、owner cleanup は必ず除外、protect の設定／解除は
  INVAL。`vfs_fd_is_protected` は従来の protect bit の問い合わせのままで SQLITE class の診断ではない。
  この段階では generic void close の拒否ログ／新公開診断 API を追加しない。
- count は現在生存中の同 cookie FD 数（隔離分を含む）を返す。quarantine は同集合だけを
  sticky に隔離し OK、反復しても同じ。いずれも NULL cookie は INVAL。
  無関係な FD・同 index の別 generation・既に閉じた FD の再利用先を変更しない。
- FD generation は GENERIC も含め成功 open ごとに進み、`VFS_FD_GENERATION_MAX = 0xffffffffUL`
  到達後の空きスロットは永久に選択しない。未使用の別スロットは利用可能。
  全スロット使用中／世代上限時は **resolve／route／path_kind を含む全 probe より前**に NOSPC。
  GENERIC でも満杯時のエラー優先順位は NOSPC になる。失敗 open は世代を消費しない。
- FD 表は group registry ではない。cookie の実在・group の OPENING/LIVE/CLOSING／QUARANTINED、
  open_owner の一貫性、group generation の非再利用、**隔離後の将来の同 cookie open 拒否**は
  F2b 呼出側の責任。この基礎 API 単独では group 隔離済み cookie の新規 open を認可検査しない。
  現存 FD の隔離解除 API はなく、close／protect／同 owner cleanup で解除されない。
- 既存 VFS の非再入呼出し前提を維持する。並行 open の同期を導入したとは主張しない。
  公開 read/write/seek 等の認可改修は行わず、SQLite callback が validate を使う統合は F2b。

F2a の HOST 証跡は `tools/tests/vfs_fd_sqlite_tdd.md`。
F2 全体の R0/R1、実 SQLite、exec 終了、FEP、target link／guest の受入を代替しない。

## 4. teardown と quarantine の完了条件

### 正常 close

1. group を CLOSING にする（FD は引き続き有効）。
2. F1 通り active stmt を finalize（FEP は全保持 stmt）、未commit なら明示 ROLLBACK。
   finalize の rc が非 OK でも stmt 自体の寿命終了を尊重し二重 finalize しない。
3. sqlite3_close を一度呼ぶ。journal/temp/main の xClose は SQLite が要求する順に処理する。
   **main が最初/最後という前提は置かない**。main xClose で group を解放しない。
4. close OK かつ残存 FD=0 のときだけ group と KAPI slot を FREE にする。
   finalize/rollback が失敗していれば F1 の最初の診断を残し、明示 close は失敗を返す。
   「slot が空いた」と「transaction が正常終了した」は別の結果。

### 失敗 close／不整合

- sqlite3_close != OK: DbSlot の in_use を維持し isolated=1、group を QUARANTINED。
  **残存 main/journal/temp の集合を cookie で隔離してから** exec の汎用回収へ進む。
  一部 xClose 済みなら、その FD は集合から外れている。再利用された同じ整数 FD に触れない。
- close OK でも残存 FD>0、または xClose lease 不一致: 成功と報告しない。
  db は既に破棄済みなら NULL とし、**FD だけの孤立 group と slot を隔離**して証拠を保持する。
  sqlite3_close が xClose の失敗を必ず返す、という仮定を置かない。
- nonNULL の failed open は同じ経路。NULL の failed open は残存 FD=0 なら descriptor を戻し、
  FD が残れば db=NULL の孤立 group として隔離する。open の最初の診断を上書きしない。
- quarantine は malloc／SQLite 呼出し／path 再open／FD close を行わない固定表の状態遷移。
  資源不足時にも実行可能にする。診断は kernel 側から slot/group/owner/kind/rc/残存 FD を記録し、
  `.sqlite_text` 内での新たな kprintf に依存しない。
- 隔離後は公開操作、明示 close、owner cleanup、all cleanup、同 nest の次の子からの cleanup の
  全てで SQLite へ再接触しない。保存診断だけ読む。再試行・解除・強制回収 API は F2 では作らない。
- 隔離 FD は in_use のまま、cookie と FD generation も不変。group/slot も再利用不能なので、
  owner 整数が再使用されても前の失敗接続へ混線しない。通常 slot の stale 公開 handle 問題は F1 同様範囲外。
- 容量枯渇は新規 open を失敗させる。隔離を解除して空きを捏造しない。
  「回収済み」には数えず隔離 slot/FD 数と pool 使用量を別表示する。kernel 再初期化まで保持する方針であり、
  ファイル削除や自動 DB 修復を伴わない。

## 5. FEP direct SQLite の移行（exec 統合の前提）

`kernel/ime_dict.c` と必要なら内部 `kernel/ime.h` に group 参照・隔離状態・保存診断を持たせる。
KAPI DbSlot へ入れず、RESIDENT group を open **前**に取得する。全 SQL 操作は既に db->pVfs を継承するので、
検索/学習/列挙/削除/clear ごとの current owner 書換えは不要。ただし隔離中は全入口で操作を拒否する。

- `dict_fd_protect` の main だけの保護と close 前の解除を撤去する。
  `os32_sqlite_db_fd` は利用箇所確認後に内部削除可、残すなら診断用途のみ。安全性の根拠にしない。
- open の途中失敗（exact/prefix prepare 失敗を含む）も共通 teardown へ集約する。
  保持 stmt finalize→未commit rollback→close→group_finish。最初のエラーを保存する。
- 内部 ime_dict_close は成否を返す形へ変更し、reopen は正常に寿命を終えた場合だけ次の open へ進む。
  close 失敗を `dict->db=NULL` と kmemset で隠して再openしない。
  隔離時は辞書機能を利用不能として通知し、保持状態を上書きしない。
- export の `vfs_open` 出力先は **current owner の GENERIC FD のまま**。
  「FEP 関数の中で開いた全 FD」を RESIDENT にする方式は禁止。
- THREADSAFE=0 の既存非再入契約を維持。IRQ/描画 callback から SQLite を呼ばない。
  別接続 VFS は mutex や競合 writer lock の代用ではない（F3）。

## 6. exec 統合点と終了経路

上の仕組みの host 検証と独立レビューが通るまで `exec/exec.c` を変更しない。
端末/GUI 担当から排他移管後、内部 helper `exec_cleanup_owned_resources(int owner)` を置き、
**DB → redirect → generic FD → pipe** の四つをこの順にまとめる。
DB は `db_cleanup_owned(owner)` であり current owner 再取得や db_cleanup_all ではない。
隔離処理は db cleanup が戻る前に完了している。helper は繰返し呼出しに対し冪等にする。

`exec_exit` では子 owner を保存し、可能な限り heap reset より前にこの helper を呼ぶ。
SQLite 資源は MEMSYS5／kernel 固定表に置き、close 時に子ユーザメモリを参照する設計を持ち込まない。
その後の guard/heap、SHM/sound/GUI、親 nest/owner 復元と longjmp は既存の意味を維持する。
末尾の db_cleanup_all 呼出しは削除する。親が SHM の ROW/text を子終了越しに保持する保証は追加しない。

| 終了理由 | 到達経路・扱い |
|---|---|
| CPL0 entry return | exec_run→exec_exit(EXEC_SUCCESS)→共通 helper |
| CPL3 正常 sys_exit | kapi_sys_exit が master PD 復帰・AS 破棄→exec_exit→helper。cleanup は子 AS 不要 |
| アプリ #PF/#GP／不正 KAPI 引数 | ring3_fault_kill→exec_fault_recover→exec_exit→helper。CPL0 回復側も exec_fault_recover の到達を確認 |
| CTRL+STOP | IRQ1 の CPL3 復帰点、syscall 入口/出口→ring3_abort_check→ring3_fault_kill→同上。STOP の status/counter を変更しない |
| nest 更新前の load/header 等失敗 | 子 owner 未成立。親 owner の DB cleanup は呼ばない。既存局所巻戻しのみ |
| nest 更新後の AS create／shlib attach 失敗 | exec_launch_abort 内で **nest 減算前**に同 helper。現状は空集合だが、故障注入で子資源があっても親を触らない契約を作る。GUI/SHM 全 cleanup は追加しない |
| 明示 db_close 済み／二度目の cleanup | FREE／QUARANTINED は無操作。次の子が同 nest を使っても隔離 group は変化しない |

**fault の限界をレビューで混同しない**:
通常のアプリ fault は SQLite 呼出しが完了した状態で残った接続を上記で回収する。
現在の ring3_in_syscall ガードは wrap 内 fault も回復するため、実装前に「SQL/path の user copy は
SQLite 進入前」「stmt が user pointer を保持しない」「cleanup 自体は user memory 不要」を点検する。
SQLite エンジン内部を fault で中断したケースに通常 close を再入させて安全とは主張しない。
それが実到達するなら **統合を停止し、busy/in-flight 検出と kernel 異常時方針の独立した狭い票を先に承認**する。
壊れた共有 MEMSYS5 を group 隔離だけで安全に継続できるとはしない。境界 fault 試験とエンジン破損試験を分ける。

## 7. 最小の段階・編集担当範囲

全段階で RED（compile 成功後の挙動 assertion）→最小変更→GREEN→独立レビュー。
**実装前の設計レビュー R0 と、実装後のレビュー R1 は別ゲート**。現時点で双方未実施。

| 段階 | 許可を得た後の変更候補 | 提出物／次段への条件 |
|---|---|---|
| R0 設計凍結 | 本票の独立読取りレビュー | 専用 VFS の全 xOpen 経路、lease 寿命、close 失敗／同 nest、FEP、fault 前提を照合。未解決の重大指摘ゼロ、PM 承認後のみコード開始 |
| F2a FD lease | `fs/vfs_fd.c`, `fs/vfs.h`、専用 host 試験 | GENERIC 回帰、明示 owner、managed close 拒否、lease 再利用防止、隔離と容量上限。exec はまだ触らない |
| F2b 接続 VFS＋F1 接続 | `lib/sqlite3/os32_sqlite_vfs.c/.h`, `kapi/kapi_db.c/.h`、専用 host 試験 | main/遅延 journal/temp 分類・失敗 open・close 順序・隔離。F1 既存試験も再実行 |
| F2c FEP | `kernel/ime_dict.c`, 必要時 `kernel/ime.h`、専用 host 試験 | 常駐 group、全エラー分岐と reopen 拒否、export GENERIC を確認。default file open の未移行利用者ゼロ |
| F2d 終了統合 | 排他移管された `exec/exec.c`、終了経路 host 試験 | 共通 helper と実 callsite の到達・順序。正常/fault/STOP/起動失敗、親資源を検証 |
| R1／実機ゲート | 独立 reviewer、後日承認された専任検証者 | host 実行証拠→target compile/link→ゲスト終了理由別測定。今回の作業では実施しない |

順序は a→b→c→d。b/c 途中で default 未所属 file open の拒否を有効にして FEP を壊す配備をしない。
段階途中は host 限定、ゲストへ渡せる単位は a–d の統合済み成果物。
新規 host 試験は `tools/tests/` に限定し、実 FD/VFS/DB 実装をコンパイルして境界だけ shim 化する。
実装の複製で順序が合っているように見せない。exec の privileged 部分は host seam を最小化し、
callsite の静的検査も組み合わせる。新規 guest binary の必要性・名前・deploy.yaml 登録 [V2] は後日承認する。

## 7.1 F2b scoped 内部基盤の進捗（F2 全体の受入ではない）

今回の明示依頼では §7 表の F2b をさらに限定し、**接続専用 VFS/group の内部基盤だけ**を
`lib/sqlite3/os32_sqlite_vfs.c/.h` と専用 `tools/tests/sqlite_groups_*` /
`test_sqlite_groups.py` に実装した。F2a の §3.3 API を使用。KAPI/FEP/exec 呼出し側は
未移行で、default `os32` の legacy file open は維持する。境界 prerequisite 前の
呼出し側移行・default fail-closed 化・配備の承認ではない。

- 専用 descriptor は DB_MAX_CONNECTIONS=8 + RESIDENT=1、非 default 一度登録。
  再 acquire では cookie/世代などの可変状態だけを更新し、登録済み pNext を触らない。
- §3.1 の pointer 型 API 案に対し、実装の `Os32SqliteGroup` は **cookie の値コピー**。
  再利用された slot pointer で古い呼出しを新世代へ認証してしまう窓を避ける。
  接続 open は専用 helper で一回だけ行い、URI flags/`file:` による VFS 差替えを拒否する。
- xClose/lease の失敗は callback 戻り前に固定表へ sticky 保存。sqlite3_file の pointer は
  保存しない。finish は close OK + 0FD + sticky-clear を必要とし、成功 close 後は
  caller の db=NULL を診断より先に行う契約を内部 header と harness に明記した。
- HOST + ASan 32/32、i386-elf GNU89 VFS object compile 合格。F2a 回帰 15/15 と
  既存 F1 host 回帰も合格。同梱 SQLite + 実 OS32 VFS/FD + RAM backend で
  `/real.db-journal` が group=0/1、open_owner=1、current_owner=2 を保つこと、
  TEMP_STORE=3 の temp SQL 80 行/追加 FD=0、SQLite close OK + stale lease + 0FD が
  group finish では隔離となることを検証した。証跡・警告・試験の限界は
  `tools/tests/sqlite_groups_tdd.md`。

**未完了**: F2b の F1 caller 接続、F2c/F2d、境界 prerequisite、独立実装レビュー R1、
kernel link、make check、guest/FEP の実走。full F2 は未受入。以下 §8 は引き続き
統合全体の期待値であり、今回の scoped host 結果で全項目を合格扱いしない。

## 8. 必須テスト表（期待値。まだ実行していない）

trace は少なくとも group/cookie、open_owner/current_owner、fd/generation、flags、
xOpen/xClose、finalize/rollback/close、quarantine、generic cleanup を区別する。
通常時の baseline と隔離時の残存数を別集計する。

| ID | 条件 | assertion／証拠 |
|---|---|---|
| T01 | parent A＋child B＋同 owner の別接続 C | B cleanup は B/C のみ。A の stmt の次 ROW と次の書込みを継続できる。SHM 内容保持は要求しない |
| T02 | A open 後 current=B で A の最初の DML。小さい journal が遅延実体化する量も投入 | main と**後発 journal** の cookie/owner が A。B cleanup 後 journal が生存、A rollback/close で一度ずつ解放。res_owner は B のまま |
| T03 | 接続別 xOpen を交互に実行、main/journal/named temp/subjournal の複数 FD | 各 FD の所属が正しい。main→temp→journal と逆順の synthetic xClose でも最終 close 完了まで group は生存 |
| T04 | 本番 TEMP_STORE=3 の temp SQL、NULL name の VFS 呼出し | 実行結果と実際の xOpen trace を記録。FD がない経路はゼロ、匿名 xOpen は CANTOPEN＋FDゼロ。named temp flags の host callback 試験は実 temp 対応の証拠と混ぜない |
| T05 | B 未完 stmt＋未commit で終了 | finalize→ROLLBACK→sqlite3_close 中は必要 FD が生存。generic close はその後。最初の失敗診断が保持される |
| T06 | finalize/rollback 失敗、close 成功 | slot/group/FD は戻るが close は失敗。診断保持。F1 回帰 |
| T07 | main/journal/temp を持つ B の sqlite3_close BUSY 注入 | slot/group/全残存 FD が隔離。generic/redirect 直接 close/protect解除でも FD 不変。all/owned/明示 close は再試行ゼロ |
| T08 | 部分 xClose 後に close 失敗。解放済み整数 FD を unrelated open に再利用 | 旧 lease の close は拒否し新 FD の owner/内容/in_use は不変。隔離集合は残存分だけ |
| T09 | T07 後、同 nest で次の子を開始・終了 | 隔離 cookie/FD/slot 不変、新しい子の通常資源だけ回収。descriptor/FD 枯渇時も隔離解除ゼロ |
| T10 | open 前 group 枯渇、FD 枯渇、open エラー NULL/nonNULL、途中 journal open 失敗 | 無所属 FD ゼロ。非 NULL は通常 teardown、NULL＋残存は孤立隔離。最初の open 診断保持 |
| T11 | close OK を返すが FD が残る故障、xClose stale lease | group_finish が成功にしない。db を再参照せず slot＋孤立 FD を隔離 |
| T12 | 子実行中に FEP 初回 open/初回学習→子終了→検索/再学習 | main だけでなく遅延 journal も RESIDENT。KAPI 子 cleanup は触らない。export 出力 FD は子 GENERIC |
| T13 | FEP open 部分失敗、各 stmt finalize/rollback/close 失敗、reopen | 正常は FD baseline 復帰。close 失敗は保持・全入口拒否・reopen 拒否。kmemset による隔離消失ゼロ |
| T14 | return/sys_exit/#PF/#GP/STOP/起動失敗前後を各々 | 実終了 callsite→helper を確認。親 DB/redirect/pipe/FEP 不変、子通常 FD 回収。nest 更新前に親 cleanup ゼロ |
| T15 | boot `:memory:` selftest、default VFS への未所属 file open、無関係 generic FD | memory は FDゼロ、未所属 file は file I/O 前拒否、通常 FD は従来通り open/close/owner cleanup |

host を二層に分ける:
1. 実 DB/VFS/FD の制御経路＋故障注入。BUSY、部分 close、lease mismatch、残存 FD を確実に再現。
2. **同梱 sqlite3.c＋実 os32 VFS＋実 FD 管理**、ファイル backend だけ隔離 host fixture。
   main/journal の実際の遅延 open と close を観測する。ホスト標準 SQLite VFS だけの試験では T02 不合格。
   本番 compile config と差分が必要なら差分を明記し、TEMP_STORE=3 を変えた証拠を本番動作扱いしない。

後日の target/guest ゲートでは既存 F1 テスト、専用 host テスト、GNU89 target compile、kernel link、
`make check` を実施し、実行コマンド・exit code・使用 fixture・スキップを記録する。
Make 起動前に環境自動読込みやネットワーク依存を確認し、必要な権限は別途取得する。
ゲストでは承認済み使い捨て媒体で終了理由別に KAPI slot／group／FD／MEMSYS5 used／canary を
FEP 常駐後の baseline と比較。正常反復は増加なし、障害反復は予測した隔離増加だけであること。
STOP による rollback の通常再open結果を見ても、電源断試験の代わりにしない。
配備と検証は [D1][D2][V1][V4] に従い、配備 binary の同一性を確認してから受入する。

## 9. レビュー依頼と現時点の残ゲート

R0 の独立 reviewer は特に次を回答すること:

- 同梱 SQLite の有効な全 file-open 経路が専用 VFS を保持するか（journal spill/temp を含む）。
  URI/VFS override、未移行 direct open、instance 寿命の抜けはないか。
- group/FD generation と sticky quarantine により同 nest・整数 FD 再利用・partial close を区別できるか。
- xClose 順序非依存、close OK＋残存 FD、NULL failed open、FEP reopen に所有情報消失の窓がないか。
- generic cleanup と直接 close の双方が managed FD を誤回収しない一方、unrelated FD を漏らさないか。
- exec の変更前後の status/owner/AS/heap 復元が同じか。SQLite 内部 fault を通常アプリ fault と混同していないか。

初稿時は R0 承認、F2a–d、R1、host 実 SQLite、target link、guest/FEP 実走は未実施で、
静的照合と設計文書作成のみだった。今回の F2b scoped の実施済み範囲・残ゲートは §7.1。
本票だけで「親 DB 生存を実終了で保証」「FD 回収済み」「F2 完了」と報告しない。
