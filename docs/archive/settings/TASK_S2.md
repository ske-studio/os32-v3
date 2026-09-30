# S2 — `libos32cfg` (設定レジストリのクライアント)、`cfg` コマンド、`cfg init`

> 発行: PM (2026-09-13) / 状態: **受入完了 (2026-09-13)**

実装 `6aa8b7a`〜`e325fe4`、Codex 実装レビュー 3 往復 + 追加 1 往復で Approve、ゲスト受入 C1〜C7 (配備 4 回目 `e325fe4`、§8f)。残: C3 の 255/256B 境界と C7 の pool 復帰はゲストで踏めず (ホスト試験 / S5)。設計: 第 5 版 (ユーザー決裁 2026-09-13: 設定の読み書きはアプリも OS 経由 (libos32gui の 1 呼び出し完結 wrapper) で行い、アプリが DB を直接開く経路は無い)。前提: S0-K (KAPI v50、`8bfe...`〜`a70df4f`)、S0-T (`/etc/settings.tsv` を通常配備、生成 DB は媒体だけ)、S0-D (通常配備は `/etc/settings.db*` を触らない)。決裁: [S0_PLAN_2026-09-13.md](S0_PLAN_2026-09-13.md) §3 (2 = 明示 `cfg init`、4 = S4 の最初の消費者は数キー)。
契約の正典: [S0_FOUNDATION.md](S0_FOUNDATION.md) §2 (非破壊 / transaction / 値の上限)、[DESIGN.md](../../tasks/settings/DESIGN.md) §3 (スキーマ) / §4 (API) / §5 (起動時の振る舞い)。本票はそれを実装単位に切る。
規約: [C1] C89、外部プログラムは newlib 可、[V2] deploy.yaml、コーダーは worktree + ホスト TDD のみ。

## 0. 範囲と分担

| 票 | 範囲 | レーン | 触るファイル |
|---|---|---|---|
| **S2-C** | `userland/lib/cfg/` = `libos32cfg` (静的、C89)、`userland/cmds/cfg.c`、ホスト TDD | C | `userland/lib/cfg/*`、`userland/cmds/cfg.c`、`tools/tests/` (libs.mk / programs.mk は PM) |
| **S2-W** | libos32gui の末尾追記 (cfg get / set を **OS 側で完結する wrapper** として GUI アプリへ公開、次の空きエントリから) | W | `userland/rust/libos32gui/src/*` (ffi / lib の表)、`sdk/rust/os32api` の宣言、`tools/check_gui_proto.py` の期待値 |
| 共有 (PM) | `userland/deploy.yaml` (`userland/cmds/*.bin` の glob で **`/bin/cfg.bin`**、追加行なし)、`build/app.conf` (`userland/cmds/cfg 50 0`)、`build/sdk.mk` (check 登録、SDK 配布ヘッダ一覧に `libos32cfg.h`)、**`build/programs.mk`** (`cfg.bin` のリンクに `-los32cfg`、shlib のリンクに `libos32cfg.a`)、**`build/libs.mk`** (`DEFINE_LIB,libos32cfg` + `INC_libos32cfg` + `ALL_LIB_ARCHIVES`)、`tools/emu_agent/agent.py` (B12) | PM (C / W が必要行を報告) | — |

S2-C と S2-W は独立 (W は C の公開ヘッダに依存するので、C の `libos32cfg.h` を先に固定してから W を出す — ヘッダは本票 §1 で確定)。

## 1. `libos32cfg` の API (DESIGN §4 を確定、`userland/lib/cfg/libos32cfg.h`)

```c
typedef struct CfgDb CfgDb;                 /* 不透明。中身: KAPI handle、writable、schema_version、状態、txn */
#define CFG_OK       0
#define CFG_MISSING  1    /* DB が無い (fallback で動作) */
#define CFG_CORRUPT  2    /* 0 バイト / NOTADB / CORRUPT / hot journal (BUSY_RECOVERY) */
#define CFG_VERSION  3    /* schema_version が自分より新しい (読みは認識できる列だけ、書きは拒否) */
#define CFG_ERROR    4    /* I/O 等 (詳細は cfg_last_sqlite()) */

int   cfg_open(CfgDb **out, int writable);  /* /etc/settings.db。0 = 開けた (状態は cfg_status)。負 = OS32_ERR_* (引数不正・メモリ) */
int   cfg_close(CfgDb *db);                 /* 未 commit は rollback してから close。0 / 負 (rollback / close の最初の失敗コードを cfg_last_close_error() に保持、CfgDb は静的 1 本なので解放後も読める) */
int   cfg_last_close_error(void);           /* 直前の cfg_close の失敗コード (0 = 成功) */
int   cfg_schema_version(const CfgDb *db);  /* 読めた schema_version (VERSION 状態でも実版を返す、MISSING / CORRUPT は 0) */
int   cfg_status(const CfgDb *db);          /* CFG_* */
int   cfg_last_sqlite(const CfgDb *db);     /* 直前の SQLite 拡張コード (db_error_code の写し) */

int   cfg_get_int (CfgDb *db, const char *scope, const char *key, int def);
int   cfg_get_text(CfgDb *db, const char *scope, const char *key, char *out, int cap);   /* 戻り: 長さ (NUL 除く) / 負: OS32_ERR_NOTFOUND (無い→呼び手が既定値) / NOSPC (cap 不足: out は書かない) / INVAL */
int   cfg_get_blob(CfgDb *db, const char *scope, const char *key, void *out, int cap);  /* 同上 */
int   cfg_begin(CfgDb *db);                 /* BEGIN IMMEDIATE (writable のみ) */
int   cfg_set_int (CfgDb *db, const char *scope, const char *key, int v);
int   cfg_set_text(CfgDb *db, const char *scope, const char *key, const char *s);
int   cfg_set_blob(CfgDb *db, const char *scope, const char *key, const void *p, int n);
int   cfg_delete  (CfgDb *db, const char *scope, const char *key);
int   cfg_commit(CfgDb *db);  int cfg_rollback(CfgDb *db);
int   cfg_enum(CfgDb *db, const char *scope, const char *prefix,
               int (*fn)(const char *key, int type, void *ctx), void *ctx);   /* 戻り: 件数 / 負 (257 件以上は OS32_ERR_NOSPC、callback 内の cfg_* は INVAL) */
int   cfg_enum_scopes(CfgDb *db, int (*fn)(const char *scope, void *ctx), void *ctx); /* SELECT DISTINCT scope ORDER BY scope (list / export の全 scope 走査、B10) */
int   cfg_init(const char *tsv_path);       /* `cfg init` の実体。DB の場所は固定 /etc/settings.db (任意の db_path は取らない、B2) */
```

規則 (S0_FOUNDATION §2 を実装に写す):
1. **open (B1 / B3 / B4)**: 必ず **RO で開いて検査してから** RW に切り替える。(a) `db_open_existing("/etc/settings.db", 0)`。失敗は `db_error_code(-1)` を写像: CANTOPEN **かつ `sys_stat` が NOTFOUND** → MISSING、CANTOPEN で stat が成功 (長さ / 深さ / journal 名の容量超過など KAPI 側の拒否) → ERROR、NOTADB / CORRUPT → CORRUPT、BUSY_RECOVERY (hot journal) → CORRUPT (自動回復しない)、IOERR / 他 → ERROR。(b) schema 検査は 1 本の SQL (**WHERE 無し = 表全体**、往復 2 の 2): `SELECT COUNT(*), MIN(typeof(schema_version)), MAX(typeof(schema_version)), MIN(schema_version), MAX(schema_version), MIN(schema_version BETWEEN 1 AND 2147483647) FROM meta` を prepare-only + step し、**count == 1、両 typeof == 'integer'、min == max、範囲内 == 1** でなければ CORRUPT (0 行・複数行 (正常行との混在を含む)・不正型・範囲外・2^32+1 はすべて CORRUPT)。`meta` 表が無い (prepare の `db_error_code` が SQLITE_ERROR = 「no such table」) → CORRUPT。prepare の失敗が IOERR / NOMEM / BUSY 等なら §1 の定義どおり **ERROR** (表欠落と区別する、往復 3 non-blocker)。版が `CFG_SCHEMA_VERSION` (1) より大きければ **VERSION**、小さい (0 は BETWEEN で弾かれる) は無い。(c) writable=1 で **RO 検査が CFG_OK のときだけ** RO を close → `db_open_existing(..., 1)` → **同じ schema 検査をもう一度** (不一致なら close して ERROR)。VERSION / CORRUPT / MISSING では RW に切り替えず、VERSION は RO 接続を**保持**して読める状態のまま (往復 2 の 3)。RO 検査 → RW 切替の直列化 (FOUNDATION §2-4) は SQLite の lock が os32 VFS で no-op (F3b 後回し) なので**ライブラリでは実現できない**。→ **ユーザー決裁 (2026-09-13、§7)**: **設定の読み書きは OS 経由だけ** — gshell の設定 UI (S4)、`cfg` コマンド、そしてアプリには libos32gui の wrapper (S2-W: get / set とも **1 呼び出しの中で open → 操作 → commit → close** を OS 側のコードが完結させ、`CfgDb` も接続もアプリに渡さない。アプリが書けるのは `app:` scope だけ)。どの経路も open〜close を **1 回の実行の中で、間に yield せずに**終える。協調型ではその間に他のプロセスは走らないので、**接続は構造的に同時 1 本**になり、FOUNDATION §2-4 の直列化はこの規則で満たす (v1 では KAPI 側の門は作らない)。(d) open は **BEGIN しない**。MISSING / CORRUPT / VERSION でも `cfg_open` は 0 を返す (呼び手を分岐させない)。**VERSION は読める状態**: get / enum は認識できる列 (`settings` の 6 列) をそのまま読み、set / begin だけ拒否 (`OS32_ERR_INVAL`)。MISSING / CORRUPT は get が既定値 (NOTFOUND)、set / begin は拒否。
2. **get**: `SELECT type, ival, tval, bval FROM settings WHERE scope=? AND key=?` を prepare-only + bind (`db_bind_text`) + step (DONE で自動 finalize、次回は再 prepare)。型が違えば NOTFOUND 扱い。text / blob は SHM の row から private バッファへ**即コピー** (次の DB 操作で無効)。cap 不足は NOSPC で out を書かない。NULL と空 text / blob は**区別する** (FOUNDATION §2-5、往復 2 の 5): 型が text/blob で値が NULL (SHM の型情報が NULL) → `OS32_ERR_NOTFOUND` (未設定扱い)、空値 → 長さ 0 を返す。tsv の空 text は空値として格納する (NULL にしない)。get 失敗 (I/O) は `cfg_status` を ERROR にし既定値を返す。
3. **set / delete**: `cfg_begin` の後だけ (txn 外は INVAL)。set は `INSERT OR REPLACE INTO settings(scope,key,type,ival,tval,bval) VALUES(?,?,?,?,?,?)`、delete は `DELETE FROM settings WHERE scope=? AND key=?` (単一 statement)、いずれも bind。set 失敗で txn を failed にし、`cfg_commit` は拒否して rollback。値の上限: scope / key 63B、text 255B、blob 4096B、key `[a-z0-9_]+(/[a-z0-9_]+)*`、scope `system` / `gshell` / `user` / `app:[a-z0-9_]+` (S0-T と同じ規則を C で)。不正 UTF-8 を拒否。**C の文字列 API では終端 NUL より後の埋め込み NUL は検査できない** (長さ付きは blob だけ) と明記。
4. **begin / commit / rollback / close (B5)**: `cfg_begin` = `BEGIN IMMEDIATE` (writable で OK 状態のときだけ)。commit / rollback は単一 statement。**失敗コードは rollback を実行する前に保存** (成功した `ROLLBACK` は診断を 0 に戻すため)。`cfg_close` は未 commit なら rollback → `db_close`。rollback / close が失敗したら **最初の失敗コードを `cfg_last_close_error()` に残し、負を返す**; KAPI 側の slot は隔離 (F1) されているので再 close しない。CfgDb は 1 プロセス 1 本の静的領域 (malloc しない) なので、失敗後も `cfg_last_sqlite` / `cfg_schema_version` は読める。
5. **enum (B6 / B10)**: prefix は LIKE を使わず `WHERE scope=? AND substr(key, 1, ?) = ? ORDER BY key` (`db_bind_int(len)` + `db_bind_text(prefix)`、`_` / `%` を含む key でも前方一致)。全件を private 配列 (最大 256 件 × key 64B) に写してから callback。257 件目があれば NOSPC を返し callback は呼ばない。callback 内で cfg_* を呼んだら INVAL (再入フラグ)。`cfg_enum_scopes` は `SELECT DISTINCT scope FROM settings ORDER BY scope` (最大 32 件)。**list / export は callback の中では値を取らない** — key を集めてから callback の外で get する。
6. **X4 / IRQ / 描画 callback から呼ばない** (doc)。1 プロセス 1 接続。
7. **`cfg_init(tsv_path)` (B2 / B7 / B8 / B9)**: DB は固定 `/etc/settings.db` (任意パスは取らない → KAPI が拒否する深い / 長いパスの類は到達しない)。手順: (a) `sys_stat("/etc/settings.db")` が **NOTFOUND** であること (存在 / 0 バイト / IOERR は拒否: 存在は `already exists`、他は `needs recovery`)。(b) `/etc/settings.db-journal` が **存在しない**こと (残っていれば「journal が残っている、リカバリ (S3) の領分」で拒否、消さない)。(c) 前回の残骸 `/etc/settings.db.new` / `.new-journal` が**存在すれば拒否** (`needs recovery: stale .new`、**消さない**)。隔離された接続 (`kapi_db.c` の F1 規則: close 失敗の slot は owner 終了でも回収されない) が `.new` の inode を掴んだままか否かは**別プロセスからは判定できない** (往復 3 の B1) ので、残骸の存在だけを根拠に unlink しない。片付けは S3 のリカバリ (再起動後に `cfg recover` が `.new*` を消す) の領分。(d) tsv を最小 reader (`cfg_tsv.c`、純関数、**行長の上限を持たないストリーム処理**: 列ごとに上限 (scope 63 / key 63 / type 4 / value は text 255B・blob 8192 文字・int は先頭ゼロを畳みながら 10 桁まで) を数え、コメント行は改行まで読み進める — ただし読み捨てではなく **不正 UTF-8 と CR はコメント内でも拒否** (S0-T の生成ツールはファイル全体を先に検査するので同じ入力を同じ判定にする、往復 2 の 6 / 往復 3 non-blocker) で全行検証してから、旧 `db_open("/etc/settings.db.new")` (CREATE、v42) で作り、`db_exec` を **1 statement ずつ** (`PRAGMA page_size=1024`、`PRAGMA journal_mode=DELETE`、`PRAGMA user_version=1`、`CREATE TABLE meta…`、`CREATE TABLE settings…`、`BEGIN`、meta の INSERT、行ごとの INSERT は bind、`COMMIT`)。(e) `db_close` が失敗したら `.new*` を**消さず** (接続が隔離中)、負を返す (`init failed: close`。以後の `cfg init` は (c) で拒否され、片付けは S3 の再起動後リカバリ)。(f) `sys_rename(".new" → "/etc/settings.db")`。**rename は原子的と仮定しない** (ext2 の rename は「新名追加 → 旧名削除」で、巻き戻し削除の戻り値を無視する `fs/ext2_dir.c:485`): 失敗したら両方を `sys_stat` する。**本体と `.new` の両方が存在する** (新名追加は成功し旧名削除と巻き戻しが失敗 = 同じ inode を 2 名が指し、ext2 はリンク数を増やしていない) ときは**どちらも消さず** `needs recovery` (`.new` を unlink すると inode が解放され本体が壊れる、往復 2 の 1)。本体だけ存在 → RO で開いて schema 検査 → 有効なら成功、無効なら消さずに `needs recovery`。本体が無ければ `.new*` を消して失敗。(g) 成功したら RO で開き直して schema 検査 (自己確認)。二重 init (端末と CUI) は「同時に走らない」前提 (協調型で `cfg init` は KAPI の合間にしか譲らず、S2 では直列を契約に明記)。
8. エラー番号は既存の `OS32_ERR_*` に写像 (新規番号を取らない)。

## 2. `cfg` コマンド (`userland/cmds/cfg.c`、`/usr/bin/cfg.bin`)

```
cfg get <scope> <key> [default]        int/text は値を、blob は hex を出す。無ければ default か "(not set)"
cfg set <scope> <key> int|text|blob <value>   1 トランザクション。blob は hex
cfg del <scope> <key>
cfg list [<scope> [<prefix>]]          scope 省略時は cfg_enum_scopes → scope ごとに enum (key を集めてから callback の外で get)
cfg status                             OK / MISSING / CORRUPT / VERSION (+ sqlite code) と実 schema_version (cfg_schema_version)
cfg init [--tsv /etc/settings.tsv]     無いときだけ /etc/settings.db を tsv から生成 (決裁 2)
cfg export <file> / cfg import <file>  DESIGN §6b の JSON 1 行 1 レコード (blob は base64)、全 scope (enum_scopes) — **本票では export のみ**、import は S3
```
- GUI 配下 (端末) でも動く (KAPI だけ、TUI なし)。4096B blob の hex は 8192 文字で con_sink (8KB、古い出力を捨てる `kernel/con_sink.c`) を超えるので、`get` / `list` / `export` の出力は **DB を閉じた後** (直列化を崩さない) に 1KB ごとに `sys_yield` を挟んで書く (端末が読み出す時間を作る。往復 3 non-blocker)。終了コード: 成功 0、`get` で default を使ったときも 0 (値は出す)、MISSING / CORRUPT / VERSION で書けないとき 1、CFG_ERROR 1 (`status` は `ERROR sqlite=<code>` を出す)、close 失敗 1 (`close failed (<code>)`)。`list` / `export` の scope 上限 32 超は `too many scopes` で 1。`export` の 1 行目はヘッダ `{"schema_version":<cfg_schema_version() の実値>,"exported":"<tick>"}` (VERSION 状態なら実値 = 認識版より大きい値がそのまま入る。固定の 1 を書かない、往復 3 の B2。DESIGN §6b の「新しい版のバックアップは復元時に拒否」がこれで成立する)。MISSING / CORRUPT の `export` は `cannot export: <status>` で 1 (ヘッダも書かない)。MISSING のときは `settings.db missing: run 'cfg init'` を出す (DESIGN §2 の文言は S3 のリカバリ用に残す)。

## 3. libos32gui への末尾追記 (S2-W) — OS 側で完結する wrapper (決裁 2026-09-13、2 回目で set を追加)

- アプリに公開するのは末尾 **4 本**: `os32gui_cfg_get_int(scope, key, def)` / `os32gui_cfg_get_text(scope, key, out, cap)` / `os32gui_cfg_set_int(scope, key, v)` / `os32gui_cfg_set_text(scope, key, s)`。get は 1 呼び出しの中で `cfg_open(&db, 0)` → `cfg_get_*` → `cfg_close`、set は `cfg_open(&db, 1)` → `cfg_begin` → `cfg_set_*` → `cfg_commit` (失敗なら `cfg_rollback`) → `cfg_close` を**完結**させ、`CfgDb` も接続も呼び手に渡さない (アプリが DB を直接開く API は公開しない = 「設定の読み書きは OS 経由」)。間に yield する呼び出しを置かない。
- **scope の規則 (OS 側の方針)**: set は scope が `app:[a-z0-9_]+` のときだけ受け付け、`system` / `gshell` / `user` への set は `OS32_ERR_PERM` 相当で拒否 (それらは S4 の設定 UI と `cfg` コマンドの領分)。get は全 scope 可。自分の名前への束縛 (別アプリの `app:` scope に書けないこと) は**本票では保証しない** — アプリが自分の ID / 名前を知る KAPI が無い (kapi.json に self 系が無い) ので、S4 か S5 で「自分の scope を OS が決める」形 (wrapper が scope を取らない版) に寄せるかを決める。票にこの限界を明記する。
- 戻り値: get_int は open 失敗 / NOTFOUND / close 失敗のどれでも def。get_text は長さ / 負 (open が負ならその値、NOTFOUND / NOSPC / INVAL はそのまま、close 失敗は `OS32_ERR_IO` 相当)。set_* は 0 / 負 (open 失敗はその値、MISSING / CORRUPT / VERSION は `OS32_ERR_INVAL` (set が拒否される)、set / commit の失敗コード、close 失敗は `OS32_ERR_IO` 相当 — 直前の失敗を優先)。MISSING / CORRUPT / VERSION でも get は規則どおり (既定値 / NOTFOUND)。
- ジャンプ表は `tools/mkshlib.py --check` が `.long os32gui_*` を抽出して本数を照合する (B11)。表に載せる名前は wrapper (Rust の `#[no_mangle] extern "C"` か C の thin wrapper) で、実体の `cfg_*` (C、`libos32cfg.a` を shlib にリンク) を呼ぶ。既存エントリは不変。表の実体は `userland/rust/libos32gui/src/shlib.rs` (現在 101 本) と SDK stub `sdk/rust/os32api/src/gui/stub.rs`、本数定数は両方を **105** に更新し、`mkshlib.py --check` が通ること (`check_gui_proto.py` は C/Rust の共有定数・構造体の検査で表の本数は見ない)。SDK の C ヘッダ (`sdk/include/os32/` の GUI 呼び出し表) にも 4 本を足す。
- gshell 自身 (設定 UI、S4) は shlib 経由ではなく `libos32cfg.a` を直接リンクして RW で使う (S4 の票)。本票では gshell に書き込み経路を足さない。
- リンク: `build/programs.mk` の shlib 規則に `libos32cfg.a` を足す (PM の共有所有、W が必要行を報告)。C の静的ライブラリを shlib に混ぜる既存例は libos32gfx。

## 4. ホスト TDD (`tools/tests/`)

- `cfg_host.c` + `test_cfg.py`: 実 SQLite + 実 `os32_sqlite_vfs.c` + RAM backend (kapi_db_v50_host の作法) の上に `libos32cfg.c` を載せ、(1) MISSING → 0 / 既定値 / set 拒否、(2) CORRUPT (0 バイト、hot journal)、(3) VERSION (meta 2)、(4) get の型違い・cap 不足 (out 不変)、(5) begin → set → commit → close → reopen で読める、(6) set 失敗 → commit 拒否 → rollback、(7) close で rollback、(8) 上限 (63/255/4096、key / scope 規則、UTF-8)、(9) enum の件数・順序・再入拒否、(10) tsv reader の規則 (S0-T の test_mk_settings_db と**同じ fixture**を共有)、(11) `cfg_init` が NOTFOUND のときだけ作り、既存 / 0 バイト / journal 残存 / `.new` 残骸では規則どおり、生成 DB のスキーマが `mk_settings_db.py` と一致 (`user_version`、meta、行)、(12) RO 検査 → RW 再検査 (版 2 の DB を writable で開くと VERSION で set 拒否、読みは可)、(13) meta の 0 行 / 2 行 / TEXT / 2^32+1 が CORRUPT、(14) enum の `a_b` / `axb` と prefix `a_` (前方一致のみ)、257 件で NOSPC、(15) close 失敗のコード保持 (KAPI の close 失敗を模型で注入、rollback 前に保存)、(16) rename 失敗後の両名の状態 (模型で「新名あり・旧名削除失敗」を注入)、(17) 別接続による SHM 上書きの前に private コピーが済んでいる、(18) meta の正常行との混在 (`1` と `0`、`1` と `2^32+1`) が CORRUPT、(19) NULL の text が NOTFOUND・空 text が長さ 0、(20) rename 失敗で両名が同じ inode のとき `.new` を消さない (RAM backend の rename 模型に「新名追加成功・旧名削除失敗・巻き戻し失敗」と links_count を注入)、(21) tsv の 9000 桁の `0` と長いコメントが S0-T と同じ判定、コメント内の不正 UTF-8 / CR は拒否、(22) `.new` 残骸がある状態の `cfg_init` は拒否して残骸を消さない (別 owner で隔離させた slot を作ってから)、(23) meta が `2` の DB の export ヘッダが `"schema_version":2`、MISSING の export は失敗。tsv fixture は S0-T と共有 (先頭ゼロ、CR、コメント中の不正 UTF-8、末尾空欄、最大 blob 行 8328B を含む)。RED → GREEN を `tools/tests/s2_tdd.md` に。
- `cfg.c` の引数解釈は純関数に切り出して同じハーネスで。

## 5. 受入 (ゲスト)

| ID | 試験 | 合格条件 |
|---|---|---|
| C1 | `cfg status` (DB 無し) | `MISSING`、終了 1、`/etc/settings.db` は作られない |
| C2 | `cfg init` → `cfg status` → `cfg list` | `OK schema_version 1`、tsv の 3 行 (`gshell desktop/color int 1` 等) が出る。もう一度 `cfg init` → `already exists` で不変 (hash) |
| C3 | `cfg set gshell desktop/color int 5` → `cfg get gshell desktop/color` → 5。`cfg set … text` 255B 境界、256B 拒否 | 値と拒否 |
| C4 | `cfg export /tmp/s.json` | 1 行 1 レコード、行数 = 件数 + 1 (ヘッダ)、ヘッダの schema_version が `cfg status` の版と一致 |
| C5 | 端末 (GUI) から `cfg list` / `cfg set` | CUI と同じ結果 (CPL=3 で KAPI v50 経由) |
| C6 | 通常配備 (`make deploy` + `hsync`、`deploy-nhd`) の後も `cfg get` の値が保持される (D0) | 値不変 |
| C7 | S5 の実測の一部: FEP 辞書常駐 (`ime on`) で `cfg get` を 50 回 → `db_mem_used` が戻る、`cfg` 1 回の tick | 記録 |

## 6. レビューで見てほしい点

1. §1 の写像 (SQLite 拡張コード → CFG_*) と FOUNDATION §2-1〜2-6 の一致 (特に hot journal を CORRUPT にして自動回復しない、open が BEGIN しない、set 失敗後の commit 拒否)。
2. get / set が v50 の prepare-only + bind だけで書け、旧 `db_exec` (先頭 statement のみ) を使う箇所 (DDL、COMMIT) が単一 statement になっているか。
3. `cfg_init_from_tsv` の「欠損のときだけ」判定と `.new` → rename の原子性 (ext2 の rename)、途中失敗で残骸を残さない。
4. SHM の row (`db_column_*`) の寿命と private コピー、enum の再入。
5. libos32gui 末尾追記の作法 (Rust 側の表に C 関数を載せる)。
6. tsv reader の C 実装と Python 生成ツールの規則一致 (同じ fixture)。
7. 往復 1 の 12 件の反映: RO 検査 → RW、CANTOPEN + stat NOTFOUND、meta の 1 行 / integer / 範囲、VERSION は読める、close の戻り値と診断、substr 前方一致、journal 残存の拒否、`.new` の残骸と隔離、rename 失敗後の両名検査、enum_scopes と schema_version、os32gui_cfg_* wrapper と mkshlib --check、programs.mk / libs.mk の所有。

## 7. ユーザー決裁 (2026-09-13)

- **接続の直列化**: 「アプリが触ると同時接続になるのか」という問いに対する答えは「はい — 2 つのプロセスがそれぞれ接続を持った状態 (例: アプリの RW と gshell) が同時接続」。決裁は **設定の読み書きは OS 経由だけ** (1 回目「アプリは読みだけ」→ 2 回目「アプリの設定値の書き込みも OS 経由で出来るように」)。アプリは libos32gui の wrapper (§3、1 呼び出しで open → 操作 → close を OS 側コードが完結、`app:` scope だけ書ける) を使い、DB を直接開く API は公開しない。gshell の設定 UI / `cfg` コマンド / wrapper のどれも open〜close を yield なしで 1 回の実行に収めるので接続は構造的に同時 1 本 (§1-1 (c))。KAPI 側の門 (v51) は作らない。
- **3 往復で未承認**: 第 4 版のまま実装に進み、往復 3 の 2 件 (B1 `.new` 残骸は消さない、B2 export の版は実値) は実装レビューで併せて確認する (選択 3.b)。
- **実装レビューも 3 往復で未承認** (2026-09-13): 残 5 件は一意の直し方で C コーダーが修正中。ユーザーの決裁事項: 修正を着地したうえで (a) Codex にもう 1 往復 (確認のみ) するか、(b) 修正着地 + ゲスト再確認で S2 完了とするか。

## 8. 実装と受入の記録 (PM、2026-09-13)

### 8a. 着地
- `6aa8b7a` S2-C + S2-W + PM 登録。`799b05e` shlib 側で `kapi` を供給 (着地後の `make all` が `libos32gui.elf` のリンクで `kapi` 未定義 — crt0 の無い shlib に `cfg_backend.c` の extern が解決されなかった)。`26cda04` docs (07_shell.md / CLAUDE.md の件数)。`8d0247e` W の ⑮。
- 配備 (S2 1 回目、`799b05e`、vmkernel 470,756 B): 停止 → `nhd-pull` (stamp 16:18) → `os32-cycle deploy` → `make deploy` → kselftest 87 / 0。

### 8b. ゲスト受入 (配備 1 回目)
| ID | 結果 |
|---|---|
| C1 | **合格**: `cfg status` → `MISSING`、`/etc` に settings.db は作られない |
| C2 | **合格**: `cfg init` → `created /etc/settings.db` (3072 B)、`cfg status` → `OK schema_version 1`、`cfg list` に tsv の 3 行、再 `cfg init` → `already exists` |
| C3 | **一部**: `cfg set gshell desktop/color int 5` → `get` = 5、無い key の default (42) が出る。**255 / 256B の境界はゲストでは未確認** — `/api/cmd` 経由の rshell 行が 255B を超える引数で崩れ (複数行に分割されて `command not found`、応答がずれる)、`user t255` / `t256` に 103B の断片が入った。境界はホスト TDD (`c_limits`) で担保、ゲストは端末 (C5) かファイル経由の手段が要る |
| C4 | **合格**: `cfg export /tmp/s.json` → 3 records、`wc -l` = 4 (= 3 + ヘッダ)、ヘッダ `{"schema_version":1,"exported":"6650"}` |
| C5 / C6 / C7 | 配備 2 回目で実施 (下) |

### 8d. ゲスト受入 (配備 2 回目、`a1889c0`: 往復 1 の ②〜⑭⑯⑰ + ⑮ + kapi、vmkernel 470,756 B、cfg.bin 28,604 B、kselftest 87 / 0、stamp 16:44)
| ID | 結果 |
|---|---|
| C5 | **合格**: GUI (`os32gui`) → Start → Run... → `/usr/bin/t5a_display.bin` の端末で `cfg list` が全レコード (gshell 3 行、system、user の 2 行) を表示、`cfg set gshell desktop/color int 7` → `cfg get` = 7、`cfg status` = `OK schema_version 1` (画面 `c5_list.png` / `c5_set.png`)。端末を ESC で閉じ Start → CUI mode で戻った後、CUI の `cfg get` も 7 |
| C6 | **合格**: 配備 2 回目 (`deploy-nhd` + `make deploy` + ゲストで `hsync` = 0 copied / 219 skipped / 0 protected) の後も `desktop/color` = 5 (当時の値)、`system x` = 1、`settings.db` 3072 B のまま |
| C7 | **一部**: FEP 辞書常駐 (`ime on`) で `cfg get` 1 回 = 約 50 tick (0.5 秒、`/api/cmd` の往復と rshell の表示を含む)。50 回連続でも `cfg status` OK。**pool の戻り (`db_mem_used`) は未計測** — 唯一の表示手段 `db_test` (Test 9) が Test 5 で落ちる (`db_last_error` がカーネル帯のポインタを CPL=3 に返す、台帳 INHERITED_BUGS.md 記載の継承バグ、S2 とは無関係)。S5 で計測手段 (`cfg status` に `db_mem_used` を出す等) を用意する |

### 8e. ゲスト受入 (配備 3 回目、`21a6d19` = 往復 2 の 7 件、cfg.bin 29,500 B、kselftest 87 / 0、stamp 17:09)
| ID | 結果 |
|---|---|
| C1 | **合格** (`rm /etc/settings.db` 後 `cfg status` = MISSING、作られない) |
| C2 | **合格** (`cfg init` → OK schema_version 1、tsv の 3 行、再 init = already exists) |
| C3 | set/get/default 合格 (7 / 42)。255/256B の境界はゲストでは踏めない (8b と同じ) |
| C4 | **合格** (6 records → 7 行、ヘッダ実版、空 text は `"v":""`)。**export 先が DB 自身 / journal は `refusing to write the settings database itself` で拒否** (往復 1 の ③) |
| C5 | **合格** (端末で list / set 7 / get 7 / status OK、CUI に戻って 7) |
| C6 | **合格** (配備 3 回目の前後で値と settings.db を保持、hsync は 8d と同じ) |
| C7 | FEP 常駐で `cfg get` 1 回 = 53 tick (API 往復込み)。pool は 8d のとおり未計測 |

### 8f. ゲスト受入 (配備 4 回目、`e325fe4` + `c9570e0` = 往復 3 の B1〜B5 + shlib --api 50、vmkernel 470,758 B、cfg.bin 29,628 B、kselftest 87 / 0、stamp 17:24)
| ID | 結果 |
|---|---|
| C1 / C2 | **合格** (rm → MISSING → init → OK、3 行、set 7 → get 7) |
| C4 | **合格** (3 records → 4 行)。`cfg export /ETC/SETTINGS.DB` (大小違いの別名) も `refusing to write the settings database itself` (往復 3 の B1) |
| C5 | **合格** (端末で list / set 7 / get 7 / status OK、CUI に戻って 7) |
| C6 | **合格** (配備の前後で値 7 と settings.db を保持) |
| C3 / C7 | 8e と同じ (境界と pool はゲストで踏めない) |

受入中の教訓: (1) `/api/cmd` 経由の rshell 行は 255B 超の引数で崩れる (C3 の境界はゲストで踏めない)。(2) `ime on` のまま `/api/key` で打つと FEP がローマ字を変換する (`os32gui` → `お32ぐい`)。`SHIFT+SPACE` (urlencode) で切ってから台本を回す。

### 8c. Codex 実装レビュー
| 対象 | 判定 | 要旨 |
|---|---|---|
| `6aa8b7a` (往復 1) | Request changes | 17 件: ① shlib の `kapi` 未定義 (着地時に判明、`799b05e` で修正済み)、② init の stat 失敗を不存在扱い、③ export の出力先が DB 自身、④ 検証失敗の set で txn が failed にならない、⑤ list/export が取得障害を成功扱い、⑥ NULL を実値に変換、⑦ 破損 DB が ERROR、⑧ rollback 失敗で診断消失、⑨ open 内部 close の失敗が消える、⑩ get の bind 失敗、⑪ get が前方一致 enum で型を取る (257 件で既定値)、⑫ enum 再入の get_text が NOTFOUND、⑬ 出力の満杯 / short write、⑭ 64B scope の切り詰め、⑮ wrapper の ptr+len 検証順 (`8d0247e`)、⑯ tsv 重複控えの容量、⑰ INT_MIN の signed overflow。non-blocker: 配備先は `/bin/cfg.bin` (票の `/usr/bin` と差 → **票を `/bin` に改める**)、C 側 GUI wrapper 定義は無い (Rust 側の表が正典、票を改める)、export は DB を開いたまま書く |
| `a1889c0` (往復 2) | Request changes | 17 件中 12 修正・5 部分。残 7 件 (すべて C): 1 整数取得だけの失敗を 0 で出す (⑤残)、2 ERROR 状態で拒否した set が txn を failed にしない (④残)、3 close 自身の失敗が操作診断を上書き (⑧残)、4 NULL の export が type=3 で宣言型を失う (⑥の新規)、5 保存済み 64bit 整数 / 未知 type が SHM で 32bit 化された後に正当値扱い、6 読み出し値の境界 (256B / 4097B / 不正 UTF-8 / 埋込み NUL、列挙 key の NUL で 1 行欠落)、7 export 先の同一性検査が stat 障害を「別ファイル」扱い (③残)。non-blocker: fake.rs の `static mut kapi` 競合 (W)、s2_tdd の旧記述。B1 / B2 は適合 |
| `21a6d19` (往復 3 = 最終) | Request changes | 往復 1 の 15 件 + 往復 2 の 4 件は修正確認。残 5 件 (すべて C): B1 inode の無い FS (FAT ルート) で export 先の同一性が「別物」になり DB を潰せる、B2 enum callback からの set 拒否が txn を failed にしない、B3 enum の type に縮小前の値域検査が無い、B4 ERROR 状態の enum が 0 件を返す、B5 tsv の過長フィールドで int カウンタが overflow。non-blocker: shlib の `--api 42` (v50 依存になった)、s2_tdd の件数表記。**3 往復で Approve に至らず → ユーザー決裁 (§7、「1」= 着地 + 追加 1 往復)** |
| `e325fe4` + `c9570e0` (追加往復、確認のみ) | **Approve** | B1〜B5 の修正確認、修正が持ち込んだ新規 blocker なし、設計残件 B1 / B2 も実装で確認。範囲外 (wrapper 4 本、往復 1 の 17 件の全体再監査) は再レビューしていない旨を明記 |
