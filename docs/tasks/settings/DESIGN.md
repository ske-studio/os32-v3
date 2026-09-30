# 設定レジストリ (settings.db) — 設計書 初版

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-14)**
> (v1.3 で実装、v1.2 は触らない)
> **実装ゲート保留**: 静的監査で readonly/no-create、transaction、4KB blob、通常配備との衝突を検出。
> 以下は未凍結の設計案。S1/S2着手前に [v1.3 PLAN の S0](../gui/v13/PLAN.md) を完了する。
> 「KAPI追加なし」は未成立の目標であり、通常NHD配備へのsettings.db登録は承認されていない。
> 関連: [../gui/v12/CONTRACTS.md](../gui/v12/CONTRACTS.md) (S6 の `system.cfg` 更新)、
> [../../archive/v21/sqlite/00_INDEX.md](../../archive/v21/sqlite/00_INDEX.md) (カーネル内 SQLite)、[../../02_memory.md](../../02_memory.md)
> (SQLite 帯 0x200000、MEMSYS5 384KB)、[../../KAPI_SPEC.md](../../KAPI_SPEC.md) (`db_*` v42)

Windows のレジストリに相当する「設定の置き場」を 1 つに決める。**KAPI は増やさない**
(v42 の `db_open` / `db_exec` / `db_prepare` / `db_step` / `db_column_*` / `db_finalize` /
`db_close` だけで実装できる)。

---

## 0. 決定事項 (要約)

| 項目 | 決定 |
|---|---|
| 置き場 | **2 層**。起動に要る数キーは `/etc/system.cfg` (テキスト) のまま、それ以外は **`/etc/settings.db` (SQLite 1 ファイル)** |
| 初期値 | **マスタ `settings.db` はインストール媒体だけが持つ**。GUI にも CUI にも「初期値に戻す」機能は置かない。戻すのは **リカバリモード** (FDD ブート → `install --recover-settings`) だけ (ユーザー提案 2026-09-06) |
| 欠損時 | DB が無い / 開けない / 版が違うときは **壊さず、コード内の最小既定値で起動**し、リカバリを促す表示を出す。自動で消したり作り直したりしない |
| アクセス | ライブラリ `libos32cfg` (C、SDK 配布) の `cfg_*`。GUI は libos32gui の末尾追記で同じ関数を公開 |
| 書き込みの作法 | 開いて読んで閉じる。書き込みはトランザクション 1 回にまとめ、X4 (syscall 境界ポンプ) では触らない。gshell は起動時に読んだ値をメモリに持つ |
| スキーマ | `settings(scope, key, type, ival, tval, bval)` + `meta(schema_version)`。階層は key の `/` 区切り文字列で表す |

---

## 1. なぜ 2 層か

`GUI=` / `GFX=` はカーネルがシェル起動ループと最初の `gfx_init` の前に読む。SQLite の初期化は
その前に終わっているので技術的には DB から読めるが、**壊れたときの復旧手段**が違う:

- テキストなら FDD ブート → `mount /hd0` → `edit /hd0/etc/system.cfg` で直せる (ROADMAP「ハング復旧」、
  TASK_K4 §4)。
- DB が壊れると GUI が上がらないだけでなく、直す道具が GUI 側にしか無いと詰む。

よって **起動可否を左右するキーはテキストのまま** (数キー、`kernel/sysconfig.c` の固定バッファで足りる)。
それ以外 — デスクトップの見た目、窓の位置、関連付け、MRU、アプリごとの設定 — はすべて DB に集める。

`system.cfg` に残すキー (増やすときはここに書く):

| キー | 読み手 | 意味 |
|---|---|---|
| `GUI=0/1` | kernel.c 起動ループ | 次回起動の既定シェル |
| `GFX=pc98\|pegc\|cirrus\|auto` | kernel.c → `gfx_set_backend_pref` | グラフィクスバックエンドの強制 |
| (将来) `MOUSE=`、`KBD=` | ドライバ初期化 | 入力装置の種別 |

---

## 2. 初期値はインストーラだけが持つ (リカバリモード)

**方針**: 「設定を初期値に戻す」は破壊的操作なので、通常運用の GUI / CUI に入口を置かない。
ESC で無言のまま CUI へ落とさない (v1.2 S6) のと同じ考え方で、誤操作で設定を失う経路を作らない。

- インストール媒体 (FDD / CD の `/etc/settings.db`) が **マスタ**。`install` / `cdinst` は通常インストール時に
  これを `/hd0/etc/settings.db` へコピーする (既存の `/etc/` 一括コピーで済む)。
- **リカバリモード** = FDD からブートして `install --recover-settings [drive]`:
  1. `/hd0/etc/settings.db` があれば `/hd0/etc/settings.db.bak` へ退避 (上書きは 1 世代)。
  2. 媒体のマスタをコピー。
  3. `meta.schema_version` を読んで表示し、`sync`。
  4. `system.cfg` は触らない (別の復旧手順。両方壊れたときは `edit` で直す)。
- ライブラリ側は「壊れている」と判定しても **自分で消さない**。起動時のメッセージ
  (`settings.db unreadable: boot from install disk and run install --recover-settings`) を出し、
  コード内の最小既定値で動く (§5)。

初期値の正典はマスタ DB を生成するホスト側スクリプト `tools/mk_settings_db.py` (`assets/settings/*.tsv`
→ `settings.db`)。デスクトップの既定色や壁紙もここに書き、コードの既定値は「DB 無しでも
デスクトップが出る」最小限 (システム 16 色、壁紙なし) に留めて二重管理を避ける。

---

## 3. スキーマ

```sql
CREATE TABLE meta (
  schema_version INTEGER NOT NULL,     -- 1 から。ライブラリが自分の版と照合
  created        TEXT                  -- マスタ生成日時 (情報のみ)
);
CREATE TABLE settings (
  scope  TEXT    NOT NULL,             -- 'system' / 'gshell' / 'app:filer' / 'user'
  key    TEXT    NOT NULL,             -- 'desktop/wallpaper' のような階層パス (最大 63B)
  type   INTEGER NOT NULL,             -- 0=int 1=text 2=blob
  ival   INTEGER,                      -- type 0
  tval   TEXT,                         -- type 1 (UTF-8、最大 255B)
  bval   BLOB,                         -- type 2 (最大 4KB。窓位置の配列やアイコン)
  PRIMARY KEY (scope, key)
) WITHOUT ROWID;
```

- **scope** の命名: `system` (OS 全体、CUI も読む: 関連付け、ロケール)、`gshell` (WM)、
  `app:<名前>` (アプリごと。名前は `.bin` のベース名)、`user` (v1.x は 1 ユーザー固定、将来の拡張点)。
- **key** は `/` 区切りの小文字。列挙は `WHERE scope=? AND key LIKE 'desktop/%'`。
- 大きさの上限は SHM と同じ流儀 (text 255B、blob 4KB) で、`GuiString` と往復できる。
- インデックスは主キーだけ。件数は数百件を想定 (WITHOUT ROWID で 1 表 1 B-tree)。

移す予定の既存設定:

| 現在 | 移行先 |
|---|---|
| `assets/filetypes` (ファイラの関連付け) | `system` / `filetype/<ext>` = 実行パス |
| gshell の壁紙・色・カーソル | `gshell` / `desktop/*` |
| ファイラの最後のディレクトリ、窓位置 | `app:filer` / `last_dir`、`window/main` (blob) |
| `/etc/profile` の環境変数 | **移さない** (シェルスクリプトのまま。PATH は起動に要る) |

---

## 4. API (`libos32cfg`、KAPI 追加なし)

C89、静的リンク。libos32gui は同じ関数をジャンプ表の末尾に追記して GUI アプリへ公開する
(v1.2 の entry 95〜100 の後、v1.3 で 101〜)。

```c
typedef struct CfgDb CfgDb;                 /* 不透明。中身は db handle + 版 */
int   cfg_open(CfgDb **out, int writable);  /* /etc/settings.db。0 / OS32_ERR_* */
void  cfg_close(CfgDb *db);
int   cfg_get_int (CfgDb *db, const char *scope, const char *key, int def);
int   cfg_get_text(CfgDb *db, const char *scope, const char *key, char *out, int cap); /* 戻り: 長さ / <0 */
int   cfg_get_blob(CfgDb *db, const char *scope, const char *key, void *out, int cap);
int   cfg_set_int (CfgDb *db, const char *scope, const char *key, int v);
int   cfg_set_text(CfgDb *db, const char *scope, const char *key, const char *s);
int   cfg_set_blob(CfgDb *db, const char *scope, const char *key, const void *p, int n);
int   cfg_delete  (CfgDb *db, const char *scope, const char *key);
int   cfg_enum    (CfgDb *db, const char *scope, const char *prefix,
                   int (*fn)(const char *key, int type, void *ctx), void *ctx);
int   cfg_begin(CfgDb *db); int cfg_commit(CfgDb *db); int cfg_rollback(CfgDb *db);
int   cfg_status(void);                     /* CFG_OK / CFG_MISSING / CFG_CORRUPT / CFG_VERSION */
```

- `cfg_open(writable=0)` は読み取り専用で開き、`writable=1` は `BEGIN` を伴う書き込み用。
  **開きっぱなしにしない**: SQLite のプール (384KB、FEP 辞書と共有) を占有しないため、
  読むときは open → get → close、書くときは open → begin → set… → commit → close。
- 既定値は呼び出し側が渡す (`def`)。ライブラリは「無ければ def」で、DB が無いときも同じ振る舞い
  (`cfg_open` は `CFG_MISSING` を `cfg_status()` に残して **成功扱いの空 DB** として返す) — 呼び出し側の
  コードを DB の有無で分岐させない。
- スキーマ版の不一致は `CFG_VERSION`: 読みは許す (前方互換の範囲で)、書きは拒否する。
- CUI 用のコマンド `cfg get|set|list|export` を `userland/cmds/` に足す (内部は同じライブラリ、
  `dbq` でも生 SQL で覗ける)。

---

## 5. 起動時の振る舞いと既定値

```text
gshell 起動
 ├─ cfg_open(0) → status
 │    OK       : gshell scope を読み、メモリの GuiConfig に写す → close
 │    MISSING  : 最小既定値 (システム 16 色、壁紙なし、taskbar 24px) → 通知バーに "settings.db missing"
 │    CORRUPT  : 同上 + "run install --recover-settings"。**ファイルは触らない**
 │    VERSION  : 読める範囲だけ写す + 通知。書き込みは拒否
 └─ 以後 DB は「設定が変わったとき」だけ開く (設定アプリの OK、窓位置の保存など)
```

- 書き込みは X3 かアプリの event loop から (v1.2 契約 S8 と同じ。X4 では VFS を触らない)。
- gshell の窓位置保存は「アプリ終了時に 1 回」など回数を絞る。毎フレーム書かない。
- 通知は WM 内蔵 MessageBox (v1.2 W4) を使う。ダイアログを閉じても既定値のまま動き続ける。

---

## 6. SQLite との共存で実測する項目 — **実測済み (2026-09-13、TASK_S5 §6)**

| 項目 | 見るもの | 実測 (NP21/W 15MB pc98、tick = 1/100 秒の相対値) |
|---|---|---|
| プール | FEP 辞書が常駐した状態で `cfg_open` → 数十件の get → close を繰り返し、`db_mem_used()` が戻ること (§4-13 の -2 が出ない) | **戻る**: 20 回 × 20 get で 38,784 → peak 63,616 → 38,784 B、失敗 0 (`cfg_bench`) |
| ジャーナル | DELETE ジャーナルの回復・書込み順序・syncエラー伝播を確認する。強制終了試験は別途承認した使い捨てイメージのみ。MEMORY journal + 後置syncはクラッシュ回復の代替にしない | hot journal は自動回復せず `CORRUPT sqlite=261` (S2 の契約)、消せば復帰。強制終了試験は S3 の領分 |
| 同期 | `os32_sqlite_vfs.c` の xSync が ext2 の書き戻しを待つか。待たないなら `cfg_commit` の後に `sys_sync` 相当を呼ぶ | ext2 は write-through (`ext2_write_block` → デバイス)、xSync = `vfs_sync` は super / GD を書く。set → ハードリセット → get で保持。**追加の sync 不要** |
| 速度 | 386 相当で `cfg_open` + 20 件 get + close の時間 (tick)。gshell 起動が体感で遅れないこと | open + 20 get + close = **114 tick**、write 1 回 (open RW + begin + set + commit + close) = **59 tick**、gshell 起動の load = 24 tick、Settings の save = 59 tick |
| 大きさ | ページサイズ 1KB / 数百件で DB が 64KB 以内に収まること (FDD の媒体にも載る) | 300 件で **29,696 B** |
| メモリ | 固定 SQLite プールでも FEP・設定 DB の共存時のピークと OOM・回収を実測する。CUI 最低 8MB を GUI の受入ゲートにしない。必要 RAM と開発方針は [02_memory.md](../../02_memory.md) に従う | 共存ピーク **64,640 B** / 384KB、OOM なし、GUI 端末からも同値 |

---

## 6b. バックアップと復元 (ユーザー提案 2026-09-06)

DB ファイルの生コピーは SQLite の版・スキーマ・ページ形式に縛られ、壊れた DB をそのまま
複製してしまう。**正式なバックアップは JSON への書き出し**とし、圧縮は既存の LZ4 を使う。

```text
cfg export /hd0/backup/settings.json          # 全 scope。schema_version と日時をヘッダに持つ
cfg import /hd0/backup/settings.json [--scope app:filer]   # トランザクション 1 回で置換 / 併合
lz4 c settings.json settings.json.lz4         # 既存 /bin/lz4 (lib/lz4.c)
tar c /hd0/backup/etc.tar /etc/settings.json /etc/system.cfg /etc/profile   # 束ねるとき
```

- JSON の形は 1 レコード 1 行 (`{"scope":"gshell","key":"desktop/wallpaper","type":1,"v":"..."}`)。
  blob は base64。人が `edit` で直せる = FDD からの復旧でも扱える。
- 読み書きは `libos32cfg` に**最小の JSON writer / reader** を持たせる (この形しか読まない。
  汎用 JSON ライブラリは持ち込まない — 依存を増やさない方針)。
- `import` は `schema_version` を見て、新しいファイルは拒否、古いファイルは読める範囲で取り込む。
- **tar**: `.tar` を扱う道具は無いので、束ねたいときのために **ustar のサブセット** (regular file と
  directory だけ、100B 名、圧縮なし) の `tar c|x|t` を `userland/cmds/` に足す (ホスト側は Python の
  `tarfile` で同じ形が読める)。圧縮は `tar` に持たせず `lz4` を外で掛ける (`etc.tar.lz4`)。
- リカバリモードとの関係: `install --recover-settings` はマスタ DB を戻す。バックアップからの
  復元は `cfg import` (通常運用) で、リカバリモードにも `--from <json|tar.lz4>` を足せる (S3 に含める)。
- 自動バックアップ: 設定を書き戻すたびに JSON を出すのは重いので、**`install` / 大きな変更の
  前だけ** (設定アプリの「書き出し」ボタン、または `cfg export` を手で)。世代管理はしない。

票への反映: S2 に `cfg export|import` と JSON の最小 reader/writer、新しい **S6 = `tar` コマンド
(ustar サブセット) と `lz4` との組合せの確認**。

---

## 7. 対象外・将来

- ユーザー別 (マルチユーザー) の分離 — `user` scope を予約するだけ。
- 変更通知 (他アプリへの broadcast) — v1.x は「アプリが自分で読み直す」。
- アクセス制御 — CPL=3 アプリは `db_*` KAPI で任意の DB を開けるので、v1.x は命名規則 (自分の
  `app:<名前>` だけを書く) に留める。
- `system.cfg` の廃止 — しない (§1)。
- `/etc/profile` の DB 化 — しない。

---

## 8. 票 (v1.3 で切る)

| 票 | レーン | 内容 |
|---|---|---|
| S1 | PM / ツール | `tools/mk_settings_db.py`、`assets/settings/*.tsv` (初期値の正典)、配備登録 (`/etc/settings.db` を FDD / CD / NHD の 3 媒体に) |
| S2 | C | `libos32cfg` (C89)、`cfg` コマンド、libos32gui への末尾追記 (101〜) |
| S3 | システム | `install --recover-settings` / `cdinst` 同等 (退避 → コピー → 表示 → sync) |
| S4 | W | gshell の `GuiConfig` 読み込みと通知、設定の書き戻し点 (窓位置、壁紙)、`assets/filetypes` の移行 |
| S5 | PM / 検証 | §6 の実測、リカバリの実走 (壊した DB → FDD ブート → 復元 → GUI 復帰)、3 バックエンド回帰。`export` → 壊す → `import` の往復も |
| S6 | C | `tar` コマンド。**自作せず既製の単一ファイル実装を vendor する** (方針「車輪の再発明を避ける」): 第一候補 **microtar** (rxi、MIT、約 500 行、ustar の読み書き、I/O はコールバックなので KAPI の `sys_open/read/write` に差し替えるだけ)。GNU tar は不可 (gnulib + autotools、fork/exec で圧縮子を呼ぶ、数万行、GPL)。busybox tar (GPLv2) は MIT の本体と混ぜない。`lz4` と組み合わせて `etc.tar.lz4`。ホストの `tarfile` で読めることを確認。**実装済み (2026-09-14、[TASK_S6.md](../../archive/settings/TASK_S6.md))** — vendor は `lib/microtar/` (MIT)、ホスト試験 `tools/tests/test_tar_cmd.py` が Python `tarfile` との往復を見る |

順序: **S0 (非破壊DB契約の確定)** → 必要な基盤修正 → S1 → S2 → (S3 ∥ S4 ∥ S6) → S5。
S1のNHD登録は新規インストールと通常更新を分離するまで保留。v1.4 の設定アプリはこの上に載る。
