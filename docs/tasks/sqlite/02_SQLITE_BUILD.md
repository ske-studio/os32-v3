# タスク02: SQLite コンパイル設定

## 1. Amalgamation の生成

SQLite ソースツリー (`src/sqlite-src-3530000`) から amalgamation 版を生成する必要がある。
amalgamation はすべてのソースを `sqlite3.c` + `sqlite3.h` の 2 ファイルに結合した形式で、
組み込み環境での利用に最適化されている。

### 生成手順

```bash
cd /mnt/c/WATCOM/src/sqlite-src-3530000

# ホスト用 configure (amalgamation 生成のため)
mkdir build_host
cd build_host
../configure
make sqlite3.c

# 生成物を os32 ソースツリーにコピー
cp sqlite3.c sqlite3.h /mnt/c/WATCOM/src/os32/lib/sqlite3/
```

> **注意**: amalgamation 生成にはホスト環境の `tclsh` が必要な場合がある。
> 代替として、sqlite.org から amalgamation 版を直接ダウンロード可能:
> `https://www.sqlite.org/2024/sqlite-amalgamation-3530000.zip`

---

## 2. コンパイルオプション (CFLAGS)

### 2-1. 必須オプション

```makefile
SQLITE_CFLAGS = \
    -DSQLITE_THREADSAFE=0 \
    -DSQLITE_OS_OTHER=1 \
    -DSQLITE_TEMP_STORE=3 \
    -DSQLITE_DISABLE_LFS \
    -DSQLITE_DQS=0
```

| マクロ | 効果 |
|--------|------|
| `SQLITE_THREADSAFE=0` | ミューテックスコード完全削除。シングルタスク OS に最適 |
| `SQLITE_OS_OTHER=1` | Unix/Win VFS を除外し、カスタム VFS のみ使用 |
| `SQLITE_TEMP_STORE=3` | 一時ファイルを常にメモリ上に配置 (ディスク I/O 削減) |
| `SQLITE_DISABLE_LFS` | 64bit ファイルオフセット無効化 (2GB 制限で十分) |
| `SQLITE_DQS=0` | ダブルクォートを文字列リテラルとして解釈しない (SQL 標準準拠) |

### 2-2. 機能削減オプション (OMIT 系)

```makefile
SQLITE_OMIT_FLAGS = \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_OMIT_WAL \
    -DSQLITE_OMIT_SHARED_CACHE \
    -DSQLITE_OMIT_AUTOINIT \
    -DSQLITE_OMIT_DEPRECATED \
    -DSQLITE_OMIT_VIRTUALTABLE \
    -DSQLITE_OMIT_TRIGGER \
    -DSQLITE_OMIT_AUTHORIZATION \
    -DSQLITE_OMIT_PROGRESS_CALLBACK \
    -DSQLITE_OMIT_TRACE \
    -DSQLITE_OMIT_EXPLAIN \
    -DSQLITE_OMIT_TCL_VARIABLE \
    -DSQLITE_OMIT_COMPLETE \
    -DSQLITE_OMIT_DECLTYPE \
    -DSQLITE_OMIT_JSON
```

| マクロ | 削除される機能 | 削減効果 |
|--------|---------------|---------|
| `OMIT_LOAD_EXTENSION` | 動的ライブラリロード | 中 |
| `OMIT_WAL` | WAL ジャーナルモード | 大 (wal.c 全体) |
| `OMIT_SHARED_CACHE` | 共有キャッシュ | 小 |
| `OMIT_AUTOINIT` | 自動初期化 (手動 `sqlite3_initialize()` 必要) | 小 |
| `OMIT_DEPRECATED` | 非推奨 API | 小 |
| `OMIT_VIRTUALTABLE` | 仮想テーブル (vtab.c) | 大 |
| `OMIT_TRIGGER` | トリガー (trigger.c) | 大 |
| `OMIT_AUTHORIZATION` | 認可コールバック | 小 |
| `OMIT_PROGRESS_CALLBACK` | 実行中断コールバック | 小 |
| `OMIT_TRACE` | SQL トレース | 小 |
| `OMIT_EXPLAIN` | EXPLAIN 文 | 中 |
| `OMIT_JSON` | JSON 関数群 (json.c) | 大 (177KB ソース) |
| `OMIT_CTE` | WITH 句 (再帰 CTE) | 中 |
| `OMIT_WINDOWFUNC` | ウィンドウ関数 (window.c) | 大 |
| `OMIT_ALTERTABLE` | ALTER TABLE | 中 |
| `OMIT_ANALYZE` | ANALYZE | 小 |
| `OMIT_ATTACH` | ATTACH DATABASE | 小 |
| `OMIT_AUTOINCREMENT` | AUTOINCREMENT | 小 |
| `OMIT_CAST` | CAST 式 | 小 |
| `OMIT_FOREIGN_KEY` | 外部キー制約 | 中 |
| `OMIT_GET_TABLE` | sqlite3_get_table | 小 |
| `OMIT_INTEGRITY_CHECK` | PRAGMA integrity_check | 小 |
| `OMIT_LOCALTIME` | localtime 変換 | 小 |
| `OMIT_LOOKASIDE` | Lookaside アロケータ | 小 |
| `OMIT_REINDEX` | REINDEX | 小 |
| `OMIT_SCHEMA_PRAGMAS` | PRAGMA table_info 等 | 小 |
| `OMIT_SCHEMA_VERSION_PRAGMAS` | PRAGMA schema_version | 小 |
| `OMIT_UTF16` | UTF-16 サポート | 中 |
| `OMIT_XFER_OPT` | INSERT...SELECT 最適化 | 小 |

> **注意**: `OMIT_CTE` と `OMIT_WINDOWFUNC` は公式amalgamationではパーサー不整合で使用不可。
> OMITマクロ付きでソースからamalgamationを再生成することで対応済み。

### 2-3. 保持する機能

以下は削除**しない** (OS32 での利用が想定される):

| 機能 | 理由 |
|------|------|
| VIEW | 読み取り用の抽象化に有用 |
| SUBQUERY | `WHERE col IN (SELECT ...)` が実用上必要 |
| COMPOUND SELECT | `UNION` / `UNION ALL` がデータ結合に必要 |
| BLOB I/O | 巨大データハンドリング設計で必須 |
| LIKE / GLOB | 文字列検索に必須 |

### 2-4. リソース制限オプション

```makefile
SQLITE_LIMIT_FLAGS = \
    -DSQLITE_DEFAULT_CACHE_SIZE=10 \
    -DSQLITE_DEFAULT_PAGE_SIZE=1024 \
    -DSQLITE_MAX_COLUMN=100 \
    -DSQLITE_MAX_SQL_LENGTH=10000 \
    -DSQLITE_MAX_EXPR_DEPTH=100 \
    -DSQLITE_MAX_COMPOUND_SELECT=5 \
    -DSQLITE_MAX_VARIABLE_NUMBER=100 \
    -DSQLITE_MAX_ATTACHED=0
```

| マクロ | デフォルト | OS32 設定 | 目的 |
|--------|-----------|-----------|------|
| `DEFAULT_CACHE_SIZE` | -2000 (2MB) | **10** (~10KB) | メモリ節約 |
| `DEFAULT_PAGE_SIZE` | 4096 | **1024** | メモリ節約、小規模 DB 向き |
| `MAX_COLUMN` | 2000 | **100** | メモリ節約 |
| `MAX_SQL_LENGTH` | 1000000000 | **10000** | スタック保護 |
| `MAX_EXPR_DEPTH` | 1000 | **100** | スタックオーバーフロー防止 |
| `MAX_ATTACHED` | 10 | **0** | ATTACH DATABASE 無効化 |

### 2-5. MEMSYS5 オプション

```makefile
SQLITE_MEM_FLAGS = \
    -DSQLITE_ENABLE_MEMSYS5
```

初期化時に `sqlite3_config()` で固定プールを設定:

```c
static u8 sqlite_mem_pool[200 * 1024];  /* 200KB 固定プール */

void sqlite_engine_init(void)
{
    sqlite3_config(SQLITE_CONFIG_HEAP,
                   sqlite_mem_pool,
                   sizeof(sqlite_mem_pool),
                   64);  /* 最小アロケーション粒度 */
    sqlite3_initialize();
}
```

---

## 3. C89 互換性パッチ

OS32 は `-std=gnu89` でコンパイルするため、SQLite ソースの一部に修正が必要。

### 3-1. 問題箇所と対策

| 問題 | 対策 |
|------|------|
| `//` コメント | `/* */` に変換 (sed スクリプト) |
| `long long` 型 | `-Wno-long-long` で警告抑制 (GCC 拡張として許容) |
| ブロック途中の変数宣言 | 大半は SQLite 内のマクロ展開で発生。`-std=gnu89` でも GCC は許容するが警告あり |
| `_Bool` 型 | 未使用 (SQLite は int を使用) |
| 可変長配列 (VLA) | SQLite は VLA を使わない設計 |

### 3-2. パッチ生成スクリプト

```bash
#!/bin/bash
# sqlite3_c89_patch.sh — C89 互換パッチ適用
# 適用対象: sqlite3.c (amalgamation)

# 1. // コメントを /* */ に変換
# 注意: 文字列リテラル内の // は変換しない
sed -i 's|^\([ \t]*\)//\(.*\)$|\1/*\2 */|' sqlite3.c

# 2. 既知の問題パターンへのパッチ
# (実際のパッチ内容はコンパイル後のエラーに基づいて更新)
```

> **実際のパッチ**: amalgamation 生成後にクロスコンパイルを試行し、
> 発生するエラー/警告に基づいて個別パッチを作成する。

---

## 4. 推定バイナリサイズ

| 構成 | 最適化 | .text | .data | .bss |
|------|--------|-------|-------|------|
| 公式amalgamation + 基本OMIT | `-O2` | 617KB | 4.3KB | 864B |
| OMIT付きamalgamation再生成 + 全OMIT | `-O2` | 527KB | 3.7KB | 789B |
| **OMIT付きamalgamation + 全OMIT** | **`-Os`** | **360KB** | 3.7KB | 789B |
| MEMSYS5 プール | — | — | — | **200KB** (固定配置) |

> **`-Os` (サイズ最適化) が必須**。`-O2` 比で 30% 削減。
> `.text` 360KB はカーネル拡張域 `0x18A000-0x1FFFFF` (472KB) に収まる。
> リンカの `--gc-sections` で未使用コード削除時はさらに縮小の余地あり。

---

## 5. Makefile 統合

### Phase 1: 外部プログラム

```makefile
# programs/tests/db_test.c をビルド
# sqlite3.c を直接コンパイルリストに含める
C_DB_TEST = programs/tests/db_test.c \
            lib/sqlite3/sqlite3.c \
            lib/sqlite3/os32_sqlite_vfs.c

db_test.bin: $(C_DB_TEST)
    $(CC) $(CFLAGS) $(SQLITE_CFLAGS) $(SQLITE_OMIT_FLAGS) \
          $(SQLITE_LIMIT_FLAGS) $(SQLITE_MEM_FLAGS) \
          -o $@ $^ -lc -lm -lnosys
```

### Phase 2: カーネル統合

```makefile
# sqlite3.o をカーネルのオブジェクトリストに追加
C_KERNEL += lib/sqlite3/sqlite3.c \
            lib/sqlite3/os32_sqlite_vfs.c \
            kapi/kapi_db.c
```

---

*SQLite Build Configuration — 2026-04-24*
