# タスク05: リソースクリーンアップ・異常系対策・fsync

## 1. 概要

PC-98 ベアメタル環境特有の以下のリスクに対する対策を整理する:

1. **アプリケーションクラッシュ** — DB リソースの残存
2. **電源断** — ジャーナル/DB ファイルの破損
3. **メモリ枯渇** — MEMSYS5 プール不足

---

## 2. アプリケーションクラッシュ対策

### 2-1. 問題

外部プログラムが `db_close()` を呼ばずに異常終了 (フォールト、`sys_exit`、
ユーザーによる強制終了) した場合、カーネル側に以下のリソースが残存する:

- `sqlite3_stmt *` (未 finalize のステートメント)
- `sqlite3_blob *` (未クローズの BLOB ハンドル)
- `sqlite3 *` (未クローズの DB 接続)
- 共有メモリブロックの確保状態

### 2-2. 対策: exec_exit() への統合

`exec_exit()` (`exec/exec.c`) に DB クリーンアップを追加:

```c
/* exec_exit() 内、既存のクリーンアップシーケンス後に追加 */

/* (6) SQLite DB リソース自動クリーンアップ */
db_cleanup_all();
```

### 2-3. db_cleanup_all() の実装

```c
/* kapi/kapi_db.c */

void db_cleanup_all(void)
{
    int i;
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (db_slots[i].in_use && db_slots[i].owner == 1) {
            /* アプリケーション所有のスロットのみクリーンアップ */

            /* 1. 実行中ステートメントの finalize */
            if (db_slots[i].active_stmt) {
                sqlite3_finalize(db_slots[i].active_stmt);
                db_slots[i].active_stmt = (void *)0;
            }

            /* 2. DB 接続クローズ
             * sqlite3_close_v2 は未finalize のステートメントがあっても
             * 安全に動作する (リソースリーク防止) */
            sqlite3_close_v2(db_slots[i].db);
            db_slots[i].db = (void *)0;
            db_slots[i].in_use = 0;
        }
    }
}
```

### 2-4. クリーンアップの順序

`exec_exit()` 内の既存クリーンアップとの関係:

```
(1) FD リダイレクト解除
(2) 全オープン FD クローズ
(3) パイプバッファ解放
(4) 共有メモリ解放
(5) サウンドエンジンクリーンアップ
(6) ★ SQLite DB リソースクリーンアップ  ← ここに追加
```

> **順序の重要性**: DB クリーンアップは共有メモリ解放 (4) の後に実行する。
> SQLite が共有メモリを参照していないため順序依存はないが、
> カーネルの一般的なリソース解放パターンに従う。

---

## 3. 電源断対策 (fsync)

### 3-1. 問題

PC-98 特有の「突然の電源断」により、FS のライトバッファに残った
データが物理ディスクに書き込まれず、DB ファイルが破損するリスクがある。

### 3-2. SQLite のジャーナルと fsync の関係

`journal_mode=DELETE` の場合:

```
1. ジャーナルファイルに変更前データを書き込み
2. ★ xSync(ジャーナル) — ジャーナルがディスクに確実に存在することを保証
3. DB ファイルに変更データを書き込み
4. ★ xSync(DB) — 変更がディスクに確実に反映されたことを保証
5. ジャーナルファイルを削除 (コミット完了)
```

**ステップ 2 が最も重要**: ジャーナルが物理ディスクに書き込まれる前に
DB ファイルが変更されると、クラッシュ時に rollback 不能になる。

### 3-3. ext2 の現在の sync 実装

```c
/* fs/ext2_vfs.c */
static int ext2_vfs_sync(void *ctx)
{
    Ext2Ctx *ec = (Ext2Ctx *)ctx;
    return ec ? ext2_sync(ec) : EXT2_ERR_NOMOUNT;
}
```

`ext2_sync()` はスーパーブロック + ブロックグループディスクリプタ +
ビットマップをフラッシュする。

### 3-4. 対策方針

**現在の実装で十分**: `ext2_sync()` が呼ばれると、すべてのダーティデータが
IDE ドライバ経由で物理ディスクに書き込まれる。

IDE ドライバ (`ide_write_sector()` / `ide_write_sectors()`) は PIO モードで
`REP OUTSW` を使用しており、書き込み完了はドライブの BSY ビットクリアで
確認される。つまり**セクタ書き込みが I/O ポートレベルで完了してから
関数がリターンする**ため、OS バッファに滞留するリスクはない。

> **注意**: IDE ドライブ自体のライトキャッシュ (HDD 内部のバッファ) が
> 有効な場合、`FLUSH CACHE` (ATA コマンド E7h) を発行する必要がある。
> ただし NP21/W エミュレータ環境では実害なし。実機対応は将来課題。

### 3-5. HostDrvFS での sync

HostDrvFS は NP21/W の HostDrv hypercall 経由でホスト側ファイルを操作する。
ホスト側の Windows FS が sync を管理するため、ゲスト側からの追加対策は不要。

---

## 4. メモリ枯渇対策

### 4-1. MEMSYS5 のオーバーフロー

MEMSYS5 は固定サイズプールからのみメモリを確保する。
プール枯渇時は `sqlite3_malloc()` が NULL を返し、SQLite は
`SQLITE_NOMEM` エラーを返す。**カーネルパニックは発生しない**。

### 4-2. OOM 時の挙動

```
sqlite3_exec("SELECT ...")
  → 内部で sqlite3_malloc() 失敗
  → SQLITE_NOMEM 返却
  → kapi_db_exec() が DB_STATUS_ERROR を共有メモリに書き込み
  → アプリはエラーハンドリング可能
```

### 4-3. プール使用量のモニタリング (将来)

```c
/* KAPI でメモリ使用量を公開 (デバッグ用) */
{
    "name": "db_mem_used",
    "ret": "u32",
    "args": [],
    "body": "return (u32)sqlite3_memory_used();"
}
```

---

## 5. エラーハンドリング設計

### 5-1. エラーの伝播

```
SQLite → カーネル (kapi_db.c) → 共有メモリ → アプリ (libos32db)
```

| SQLite エラー | 共有メモリ status | アプリ API 返り値 |
|--------------|-----------------|----------------|
| SQLITE_OK | DB_STATUS_DONE | 0 |
| SQLITE_ROW | DB_STATUS_ROW | 1 |
| SQLITE_DONE | DB_STATUS_DONE | 0 |
| SQLITE_ERROR | DB_STATUS_ERROR | -1 |
| SQLITE_NOMEM | DB_STATUS_ERROR | -1 |
| SQLITE_BUSY | (発生しない: シングルタスク) | — |

### 5-2. エラーメッセージ

```c
/* カーネル側: エラー時に共有メモリにメッセージを書き込む */
if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
    const char *errmsg = sqlite3_errmsg(slot->db);
    hdr->status = DB_STATUS_ERROR;
    hdr->error_offset = data_offset;
    kstrncpy((char *)shm + data_offset, errmsg, remaining_space);
}
```

---

## 6. 制約と既知の制限事項

| 制限 | 内容 | 回避策 |
|------|------|--------|
| xTruncate 未実装 | VACUUM でファイルサイズが縮小しない | journal_mode=DELETE で実害なし |
| 同時接続数 2 | システム 1 + アプリ 1 | 定数変更で拡張可能 |
| 32bit ファイルオフセット | DB ファイルサイズ上限 ~2GB | PC-98 環境では十分 |
| ファイルロック不要 | シングルタスクのため | 将来マルチタスク化時に要対応 |
| IDE ライトキャッシュ | FLUSH CACHE 未実装 | NP21/W では問題なし。実機は将来課題 |

---

*Cleanup & Safety — 2026-04-24*
