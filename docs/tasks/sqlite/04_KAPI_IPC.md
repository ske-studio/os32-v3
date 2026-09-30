# タスク04: KernelAPI 拡張 + 共有メモリ IPC プロトコル

## 1. 概要

SQLite エンジンはカーネル内で動作するため、外部プログラムは KernelAPI
(関数ポインタテーブル) 経由でデータベース操作を行う。

クエリ結果は**共有メモリ** (0x201000〜) を介してやり取りし、
システムコールのオーバーヘッドを最小化する。

---

## 2. KAPI 関数一覧

### 2-1. kapi.json への追加

```json
{
    "name": "db_open",
    "ret": "int",
    "args": ["const char *path", "int flags"],
    "comment": "DBオープン。戻り値: ハンドルID (0以上) / 負数=エラー"
},
{
    "name": "db_close",
    "ret": "int",
    "args": ["int handle"],
    "comment": "DBクローズ。未finalize のステートメントも自動クリーンアップ"
},
{
    "name": "db_exec",
    "ret": "int",
    "args": ["int handle", "const char *sql"],
    "comment": "SQL実行。結果の1行目を共有メモリに書き込み。戻り値: 0=OK / 負数=エラー"
},
{
    "name": "db_step",
    "ret": "int",
    "args": ["int handle"],
    "comment": "次の行を取得。共有メモリを更新。戻り値: 1=行あり / 0=完了 / 負数=エラー"
},
{
    "name": "db_finalize",
    "ret": "int",
    "args": ["int handle"],
    "comment": "現在のステートメントを手動finalize (通常はdb_step完了時に自動)"
},
{
    "name": "db_read_blob",
    "ret": "int",
    "args": ["int handle", "int col", "u32 offset", "u32 size"],
    "comment": "巨大BLOBの分割読み込み。共有メモリのデータ領域に書き込み"
},
{
    "name": "db_last_error",
    "ret": "const char *",
    "args": ["int handle"],
    "comment": "直前のエラーメッセージ取得"
}
```

### 2-2. KAPI バージョン

```c
/* include/os32_kapi_shared.h */
#define KAPI_VERSION      29  /* 28 → 29: DB API 追加 */
```

---

## 3. 共有メモリ IPC プロトコル

### 3-1. メモリレイアウト

`db_exec()` / `db_step()` の呼び出し後、共有メモリの先頭に
以下のバイナリ構造体が配置される:

```
+0x0000  ┌─────────────────────────────────┐
         │  DB_RESULT_HEADER (12 bytes)    │
         │    status (4)                    │
         │    column_count (4)              │
         │    error_offset (4)              │
+0x000C  ├─────────────────────────────────┤
         │  DB_COLUMN_INFO[0] (12 bytes)   │
         │    type (4)                      │
         │    length (4)                    │
         │    data_offset (4)               │
+0x0018  ├─────────────────────────────────┤
         │  DB_COLUMN_INFO[1] (12 bytes)   │
         │    ...                           │
+0x????  ├─────────────────────────────────┤
         │  データ領域                     │
         │    INT: 4バイト (リトルエンディアン) │
         │    FLOAT: 8バイト (IEEE 754)     │
         │    TEXT: NUL終端文字列            │
         │    BLOB: 生バイナリ              │
         │    NULL: データなし (length=0)   │
+0x3FFF  └─────────────────────────────────┘
         (1ブロック = 16KB)
```

### 3-2. 構造体定義

```c
/* include/os32_kapi_shared.h に追加 */

/* DB結果ステータス */
#define DB_STATUS_DONE     0    /* クエリ完了 (行なし or 最終行到達) */
#define DB_STATUS_ROW      1    /* 行データあり (db_step で次を取得) */
#define DB_STATUS_ERROR   -1    /* エラー発生 */

/* DB カラム型 */
#define DB_TYPE_INT        1
#define DB_TYPE_FLOAT      2
#define DB_TYPE_TEXT       3
#define DB_TYPE_BLOB       4
#define DB_TYPE_NULL       5

#pragma pack(push, 4)  /* 4バイトアライメント (i386 最適) */

typedef struct {
    i32 status;         /* DB_STATUS_xxx */
    i32 column_count;   /* 列数 (0 = 結果セットなし) */
    i32 error_offset;   /* エラーメッセージのオフセット (0 = エラーなし) */
} DB_ResultHeader;

typedef struct {
    i32 type;           /* DB_TYPE_xxx */
    i32 length;         /* データサイズ (バイト) */
    i32 data_offset;    /* 共有メモリ先頭からのデータ位置 */
                        /* 巨大データ時: length に元サイズ、data_offset = 0 */
} DB_ColumnInfo;

#pragma pack(pop)
```

> **pack(4) を採用**: pack(1) は i386 でアライメント違反ペナルティが発生する。
> pack(4) は構造体サイズの増加なし (全メンバが 4 バイト) かつ最速。

### 3-3. 共有メモリの容量制限

| 項目 | 値 |
|------|-----|
| IPC ブロックサイズ | 16KB (SHM_BLOCK_SIZE) |
| ヘッダ | 12 bytes |
| カラム情報 (10列想定) | 120 bytes |
| データ領域 | ~16,252 bytes |

1 行あたりのデータが 16KB を超える場合は巨大データ扱い (後述)。

---

## 4. 巨大データ (BLOB) ハンドリング

### 4-1. 検出

カーネルは `sqlite3_column_bytes()` で各カラムの実サイズを取得し、
共有メモリに収まらない場合:

- `DB_ColumnInfo.length` に**元のサイズ**を設定
- `DB_ColumnInfo.data_offset` を **0** (無効値) に設定

### 4-2. 取得フロー

```
アプリ側:
  info = (DB_ColumnInfo *)(shm + sizeof(DB_ResultHeader) + col * sizeof(DB_ColumnInfo));
  if (info->data_offset == 0 && info->length > 0) {
      /* 巨大データ → 分割読み込み */
      remaining = info->length;
      offset = 0;
      while (remaining > 0) {
          chunk = MIN(remaining, 16000);
          api->db_read_blob(handle, col, offset, chunk);
          /* 共有メモリのデータ領域先頭に chunk バイトが書き込まれる */
          memcpy(dst + offset, shm + DATA_AREA_OFFSET, chunk);
          offset += chunk;
          remaining -= chunk;
      }
  }
```

### 4-3. カーネル側の実装

```c
int __cdecl kapi_db_read_blob(int handle, int col, u32 offset, u32 size)
{
    DbSlot *slot;
    sqlite3_blob *blob_handle;
    void *shm_data;
    int rc;

    slot = &db_slots[handle];
    if (!slot->in_use || !slot->active_stmt) return -1;

    /* Incremental BLOB I/O API */
    /* 注意: sqlite3_blob_open は行ID (rowid) が必要 */
    /* active_stmt から現在の rowid を取得する */

    shm_data = (void *)(MEM_SHM_BASE + sizeof(DB_ResultHeader));
    /* データ領域先頭に直接チャンクを読み込む */

    rc = sqlite3_blob_read(blob_handle, shm_data, (int)size, (int)offset);
    return (rc == SQLITE_OK) ? (int)size : -1;
}
```

---

## 5. ユーザー空間ラッパーライブラリ (libos32db)

### 5-1. ファイル構成

```
programs/libos32db/
    libos32db.h       ← ヘッダ (外部プログラムがインクルード)
    libos32db.c       ← 共有メモリパースロジック
```

### 5-2. API 設計

```c
/* libos32db.h — OS32 SQLite ユーザー空間ラッパー */

#ifndef LIBOS32DB_H
#define LIBOS32DB_H

#include "os32api.h"

/* DB ハンドル (カーネルのスロット番号をラップ) */
typedef int db_handle_t;

/* 結果行へのアクセサ (共有メモリを直接参照) */

/* 接続管理 */
db_handle_t db_open(const char *path);
int         db_close(db_handle_t h);

/* SQL 実行 (結果セットなしの DML/DDL) */
int db_exec(db_handle_t h, const char *sql);

/* クエリ実行 (結果セットあり) */
int db_query(db_handle_t h, const char *sql);

/* イテレータ */
int db_step(db_handle_t h);

/* カラムアクセサ (db_step 後に呼び出し) */
int         db_column_count(void);
int         db_column_type(int col);
i32         db_column_int(int col);
const char *db_column_text(int col);
int         db_column_bytes(int col);
const void *db_column_blob(int col);

/* 巨大 BLOB 読み込み */
int db_read_blob(db_handle_t h, int col, void *buf, u32 offset, u32 size);

/* エラー情報 */
const char *db_errmsg(db_handle_t h);

#endif /* LIBOS32DB_H */
```

### 5-3. 実装例 (db_column_int)

```c
/* 共有メモリの先頭ポインタ (KAPI で取得または固定アドレス) */
static void *db_shm = (void *)0x201000;

i32 db_column_int(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)db_shm;
    DB_ColumnInfo *info;

    if (col < 0 || col >= hdr->column_count) return 0;

    info = (DB_ColumnInfo *)((u8 *)db_shm + sizeof(DB_ResultHeader)
                             + col * sizeof(DB_ColumnInfo));

    if (info->type != DB_TYPE_INT || info->data_offset == 0) return 0;

    return *(i32 *)((u8 *)db_shm + info->data_offset);
}
```

---

## 6. 使用例

```c
/* 外部プログラムでの使用例 */
#include "libos32db.h"

void my_main(int argc, char **argv, KernelAPI *api)
{
    db_handle_t db;
    int rc;

    db = db_open("/db/test.sqlite");
    if (db < 0) {
        api->kprintf(ATTR_RED, "DB open failed\n");
        return;
    }

    /* テーブル作成 */
    db_exec(db, "CREATE TABLE IF NOT EXISTS memo(id INTEGER PRIMARY KEY, text TEXT)");

    /* データ挿入 */
    db_exec(db, "INSERT INTO memo(text) VALUES('Hello OS32')");

    /* クエリ */
    rc = db_query(db, "SELECT id, text FROM memo");
    while (db_step(db) == DB_STATUS_ROW) {
        i32 id = db_column_int(0);
        const char *text = db_column_text(1);
        api->kprintf(ATTR_WHITE, "id=%d text=%s\n", id, text);
    }

    db_close(db);
}
```

---

*KernelAPI & IPC Protocol — 2026-04-24*
