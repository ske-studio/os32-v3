# 設定レジストリ (settings.db) — 設計書 初版

> 発行: PM (2026-09-06) / 状態: **受入完了 (2026-09-14)**
> v1.3 で受け入れた設定レジストリの現行設計。旧ゲートの解消と実装の経緯:
> `docs/archive/settings/TASK_S0.md`・`docs/archive/settings/TASK_S2.md`。
> SQLite の基盤 API は [KAPI_SPEC.md v50](../../KAPI_SPEC.md#設定レジストリの基盤-v50) が正典。

Windows のレジストリに相当する「設定の置き場」を 1 つに決める。
実装は従来の DB API に加え、v50 の `db_open_existing`・`db_prepare_only`・bind 群・
`db_error_code` を使う (`userland/lib/cfg/cfg_backend.c:13`)。

---

## 0. 決定事項 (要約)

| 項目 | 決定 |
|---|---|
| 置き場 | **2 層**。起動に要る数キーは `/etc/system.cfg` (テキスト) のまま、それ以外は **`/etc/settings.db` (SQLite 1 ファイル)** |
| 初期値 | **マスタ `settings.db` はインストール媒体だけが持つ**。GUI にも CUI にも「初期値に戻す」機能は置かない。戻すのは **リカバリモード** (FDD ブート → `install --recover-settings`) だけ (ユーザー提案 2026-09-06) |
| 欠損時 | DB が無い / 開けない / 版が違うときは **壊さず、コード内の最小既定値で起動**し、欠損なら `cfg init`、破損ならリカバリを案内する。自動で消したり作り直したりしない |
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

よって **起動可否を左右するキーはテキストのまま** (数キー、`init/sysconfig.c` の固定バッファで足りる)。
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
- 欠損だけなら通常運用の `cfg init` で作成できる (`userland/cmds/cfg.c:535`)。既存 DB の初期化ではなく、残骸も消さない (`userland/lib/cfg/cfg_init.c:4`)。
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

## 4. API (`libos32cfg`、v50 の DB 基盤を利用)

C の規約は [CONSTRAINTS.md C1](../../CONSTRAINTS.md#c1-c11-gnu11-を内部実装の基準とする) に従う。静的リンクで、libos32gui は同じ関数をジャンプ表の末尾に追記して GUI アプリへ公開する
(v1.2 の entry 95〜100 の後、v1.3 で 101〜)。

```c
typedef struct CfgDb CfgDb;                 /* 不透明。中身は db handle + 版 */
int   cfg_open(CfgDb **out, int writable);  /* /etc/settings.db。0 / OS32_ERR_* */
int   cfg_close(CfgDb *db);
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
int   cfg_status(const CfgDb *db);                     /* CFG_OK / CFG_MISSING / CFG_CORRUPT / CFG_VERSION */
```

- `cfg_open(writable=0)` は読み取り専用で開き、`writable=1` は書き込み用に開く。open は `BEGIN` しないので、変更前に `cfg_begin(db)` を呼ぶ (`userland/lib/cfg/libos32cfg.c:424`)。
  **開きっぱなしにしない**: SQLite のプール (384KB、FEP 辞書と共有) を占有しないため、
  読むときは open → get → close、書くときは open → begin → set… → commit → close。
- 既定値は呼び出し側が渡す (`def`)。ライブラリは「無ければ def」で、DB が無いときも同じ振る舞い
  (`cfg_open` は `CFG_MISSING` を `cfg_status(db)` に残して **成功扱いの空 DB** として返す) — 呼び出し側の
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
 │    MISSING  : 最小既定値 (システム 16 色、壁紙なし、taskbar 24px) → 欠損を通知。作成は `cfg init` (§2)
 │    CORRUPT  : 同上 + "run install --recover-settings"。**ファイルは触らない**
 │    VERSION  : 読める範囲だけ写す + 通知。書き込みは拒否
 └─ 以後 DB は「設定が変わったとき」だけ開く (設定アプリの OK、窓位置の保存など)
```

- 書き込みは X3 かアプリの event loop から (v1.2 契約 S8 と同じ。X4 では VFS を触らない)。
- gshell の窓位置保存は「アプリ終了時に 1 回」など回数を絞る。毎フレーム書かない。
- 通知は WM 内蔵 MessageBox (v1.2 W4) を使う。ダイアログを閉じても既定値のまま動き続ける。

---

## 6. SQLite との共存で実測する項目 — **実測済み (2026-09-13、TASK_S5 §6)**

| 項目 | 見るもの | 受入 |
|---|---|---|
| プール | FEP 辞書が常駐した状態で `cfg_open` → 数十件の get → close を繰り返し、`db_mem_used()` が戻ること (§4-13 の -2 が出ない) | 合格 (S5 §6 (アーカイブ: `docs/archive/settings/TASK_S5.md`)) |
| ジャーナル | DELETE ジャーナルの回復・書込み順序・syncエラー伝播を確認する。強制終了試験は別途承認した使い捨てイメージのみ。MEMORY journal + 後置syncはクラッシュ回復の代替にしない | 合格 (S5 §6 (アーカイブ: `docs/archive/settings/TASK_S5.md`)) |
| 同期 | `os32_sqlite_vfs.c` の xSync が ext2 の書き戻しを待つか。待たないなら `cfg_commit` の後に `sys_sync` 相当を呼ぶ | 合格 (S5 §6 (アーカイブ: `docs/archive/settings/TASK_S5.md`)) |
| 速度 | NP21/W の実測構成で `cfg_open` + 20 件 get + close の時間 (tick)。gshell 起動が体感で遅れないこと | 合格 (S5 §6 (アーカイブ: `docs/archive/settings/TASK_S5.md`)) |
| 大きさ | ページサイズ 1KB / 数百件で DB が 64KB 以内に収まること (FDD の媒体にも載る) | 合格 (S5 §6 (アーカイブ: `docs/archive/settings/TASK_S5.md`)) |
| メモリ | 固定 SQLite プールでも FEP・設定 DB の共存時のピークと OOM・回収を実測する。v3 の最低条件は 8MB GUI + 私有 2MB のアプリ (V3_GOALS §3、目標)、構成別の判定は v3 最終段。必要 RAM と開発方針は [02_memory.md](../../02_memory.md) に従う | 合格 (記録はアーカイブ `docs/archive/settings/TASK_S5.md` §6) |

表の「合格」は NP21/W (15MB) での実測で、tick は相対値 (386 実機の計測ではない)。数値の記録はアーカイブ `docs/archive/settings/TASK_S5.md` §6。
守る決まり:
- **hot journal は自動回復しない**。`cfg_open` は BUSY_RECOVERY を CORRUPT として扱い、get は既定値、set / begin は拒否する (`userland/lib/cfg/libos32cfg.c`)。
- 回復は `install --recover-settings` だけが行う。残った journal は `.bak-journal` に写してから消し、切り替える。
- **強制終了・電源断の耐性 (DELETE journal の回復) は保証しない (未検証)**。ゲストで確かめたのは「`cfg set` → ハードリセット → 値が残る」までで、強制終了の試験は別途承認した使い捨てイメージでだけ行う。
- リカバリの失敗点の網羅はホスト試験 (`tools/tests/install_recover_host.c`) の範囲。

経緯: S2 `docs/archive/settings/TASK_S2.md`、S3 `docs/archive/settings/TASK_S3.md`。

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
- **tar**: `userland/cmds/tar.c` の `tar c ARCHIVE PATH...` / `tar x ARCHIVE [-C DIR]` (既定は `.`) / `tar t ARCHIVE` は実装済み。圧縮は持たず、外側で `lz4` を掛ける (`etc.tar.lz4`)。
  扱うのは通常ファイルとディレクトリ (再帰) だけ。所有者・時刻は保存しない (書庫では 0)。ustar の読み書きは `lib/microtar/` (MIT、改変点は `lib/microtar/README.OS32`)。
  名前は先頭の `/` を落として格納し、99B を超える名前は拒否して非ゼロ終了する。展開では `..` を含む名前と絶対パスを断る。
  試験は `tools/tests/test_tar_cmd.py`。経緯と受入の記録: `docs/archive/settings/TASK_S6.md`。
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

## 8. 実装・受入の記録

旧ゲートと実装順序の記録は S0 (アーカイブ: `docs/archive/settings/TASK_S0.md`)、
ライブラリとコマンドは S2 (アーカイブ: `docs/archive/settings/TASK_S2.md`)、
リカバリは S3 (アーカイブ: `docs/archive/settings/TASK_S3.md`)、GUI 統合は S4 (アーカイブ: `docs/archive/settings/TASK_S4.md`)、
共存・受入は S5 (記録はアーカイブ `docs/archive/settings/TASK_S5.md`)、tar は S6 (記録はアーカイブ `docs/archive/settings/TASK_S6.md`)。
通常配備で既存の設定を上書きしない契約: ゲストの `/etc` の `settings.db` とその付属 (`-journal`・`-wal`・`-shm`・`.bak`・`.bak-journal`・`.failed`・`.failed-journal`・`.new`・`.new-journal`・`.recover-state`、一覧の正典は `tools/deploy_protect.py` の `PROTECTED_BASENAMES`、大文字小文字を区別しない) は、
通常配備 (`nhd_deploy.py` の sync・copy・rm、`hostdrv_deploy.py` の sync・clean、`prune_stale.py`、ゲストの `hsync`) が作成・上書き・削除・rename しない
(判定は `tools/deploy_protect.py`、名前と実体 (symlink・hardlink の別名) の 2 段、除外は `protected:` のログ)。NHD 全体の deploy は pull の stamp と remote の hash が一致するときだけ書く (不一致を越えるのはプロファイル未指定の `deploy --force` だけ、プロファイル指定時は `--force` でも書かない)。
明示 install と `cfg init` は対象外。経緯: `docs/archive/settings/TASK_S0.md` の S0-D。
