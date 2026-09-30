# タスク03: OS32 カスタム VFS 実装仕様

## 1. 概要

`SQLITE_OS_OTHER=1` を指定することで、SQLite は Unix/Win の組み込み VFS を除外し、
アプリケーションが提供するカスタム VFS のみを使用する。

OS32 では `os32_sqlite_vfs.c` を実装し、OS32 の VFS レイヤー
(`vfs.h` / `vfs_fd.c`) 上に SQLite VFS を構築する。

参考実装: `sqlite-src-3530000/src/test_demovfs.c` (組み込み向け最小 VFS)

---

## 2. VFS 操作マッピング

### 2-1. sqlite3_io_methods (ファイル操作)

| SQLite VFS | OS32 実装 | 備考 |
|------------|----------|------|
| `xClose` | ファイル状態の解放 | メモリ解放のみ (FD 未使用) |
| `xRead` | `ops->read_stream(ctx, path, buf, size, offset)` | オフセット指定読み込み |
| `xWrite` | `ops->write_stream(ctx, path, buf, size, offset)` | オフセット指定書き込み |
| `xTruncate` | **no-op (SQLITE_OK 返却)** | journal_mode=DELETE では不要 |
| `xSync` | `ops->sync(ctx)` | FS 全体の sync (後述) |
| `xFileSize` | `ops->get_file_size(ctx, path, &size)` | 実装済み |
| `xLock` | no-op (SQLITE_OK) | シングルタスクのため不要 |
| `xUnlock` | no-op (SQLITE_OK) | 同上 |
| `xCheckReservedLock` | `*pResOut = 0; return SQLITE_OK;` | 常にロックなし |
| `xFileControl` | `return SQLITE_NOTFOUND;` | 拡張制御なし |
| `xSectorSize` | `return 512;` | IDE セクタサイズ |
| `xDeviceCharacteristics` | `return 0;` | 特殊属性なし |

### 2-2. sqlite3_vfs (VFS メソッド)

| SQLite VFS | OS32 実装 | 備考 |
|------------|----------|------|
| `xOpen` | パス解決 + VfsOps 取得 | FD テーブル不使用 (後述) |
| `xDelete` | `ops->unlink(ctx, rel_path)` | `vfs_rm` 経由 |
| `xAccess` | `ops->stat(ctx, path, &st)` | stat でファイル存在確認 |
| `xFullPathname` | `vfs_resolve_path()` | 相対→絶対パス変換 |
| `xDlOpen/Error/Sym/Close` | 全て no-op | 動的ロード無効 |
| `xRandomness` | PIT カウンタ + RTC 値をミックス | **要新規実装** |
| `xSleep` | `cpu_delay_us()` | カーネル関数使用 |
| `xCurrentTime` | `sys_time()` → Julian Day 変換 | RTC ベース |

---

## 3. ファイルハンドル設計

### 3-1. FD テーブルを経由しない理由

SQLite VFS をカーネル内で実装する場合、ユーザー空間の FD テーブル
(`open_files[16]` in `vfs_fd.c`) を経由する必要がない。

代わりに、カスタム VFS のファイルハンドルにパスと VFS 操作テーブルを
直接保持する:

```c
typedef struct {
    sqlite3_file base;          /* 必須: SQLite 基底構造体 (先頭配置) */
    char path[VFS_MAX_PATH];    /* OS32 VFS 内の相対パス */
    VfsOps *ops;                /* FSドライバ操作テーブル */
    void *fs_ctx;               /* FSドライバコンテキスト */
    u32 file_size;              /* 現在のファイルサイズ */
} Os32File;
```

### 3-2. xOpen の実装

```c
static int os32_vfs_open(
    sqlite3_vfs *pVfs,
    const char *zName,
    sqlite3_file *pFile,
    int flags,
    int *pOutFlags
)
{
    Os32File *p = (Os32File *)pFile;
    char resolved[VFS_MAX_PATH];
    char rel_path[VFS_MAX_PATH];
    void *fs_ctx;
    VfsOps *ops;
    u32 file_size = 0;

    static const sqlite3_io_methods os32_io = {
        1,                      /* iVersion */
        os32_close,
        os32_read,
        os32_write,
        os32_truncate,
        os32_sync,
        os32_file_size,
        os32_lock,
        os32_unlock,
        os32_check_reserved_lock,
        os32_file_control,
        os32_sector_size,
        os32_device_characteristics
    };

    if (zName == 0) {
        return SQLITE_IOERR;  /* 一時ファイル名なし → エラー */
    }

    /* パス解決 + FS ドライバ取得 */
    vfs_resolve_path(zName, resolved, VFS_MAX_PATH);
    ops = vfs_route(resolved, rel_path, VFS_MAX_PATH, &fs_ctx);
    if (!ops) {
        return SQLITE_CANTOPEN;
    }

    /* ファイル存在確認 / 作成 */
    if (ops->get_file_size) {
        int rc = ops->get_file_size(fs_ctx, rel_path, &file_size);
        if (rc != 0 && (flags & SQLITE_OPEN_CREATE)) {
            /* ファイル作成 */
            ops->write_file(fs_ctx, rel_path, "", 0);
            file_size = 0;
        } else if (rc != 0) {
            return SQLITE_CANTOPEN;
        }
    }

    /* ハンドル初期化 */
    kmemset(p, 0, sizeof(Os32File));
    kstrncpy(p->path, rel_path, VFS_MAX_PATH);
    p->ops = ops;
    p->fs_ctx = fs_ctx;
    p->file_size = file_size;
    p->base.pMethods = &os32_io;

    if (pOutFlags) {
        *pOutFlags = flags;
    }

    return SQLITE_OK;
}
```

### 3-3. xRead / xWrite の実装

```c
static int os32_read(
    sqlite3_file *pFile,
    void *zBuf,
    int iAmt,
    sqlite3_int64 iOfst
)
{
    Os32File *p = (Os32File *)pFile;
    int rc;

    if (!p->ops->read_stream) return SQLITE_IOERR_READ;

    rc = p->ops->read_stream(p->fs_ctx, p->path, zBuf, (u32)iAmt, (u32)iOfst);

    if (rc == iAmt) {
        return SQLITE_OK;
    } else if (rc >= 0) {
        /* 短い読み込み → 残りをゼロ埋め */
        kmemset((u8 *)zBuf + rc, 0, iAmt - rc);
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_IOERR_READ;
}

static int os32_write(
    sqlite3_file *pFile,
    const void *zBuf,
    int iAmt,
    sqlite3_int64 iOfst
)
{
    Os32File *p = (Os32File *)pFile;
    int rc;

    if (!p->ops->write_stream) return SQLITE_IOERR_WRITE;

    rc = p->ops->write_stream(p->fs_ctx, p->path, zBuf, (u32)iAmt, (u32)iOfst);

    if (rc == iAmt) {
        /* ファイルサイズ更新 */
        if ((u32)(iOfst + iAmt) > p->file_size) {
            p->file_size = (u32)(iOfst + iAmt);
        }
        return SQLITE_OK;
    }
    return SQLITE_IOERR_WRITE;
}
```

---

## 4. xSync の実装方針

### 4-1. 現状

OS32 VFS の `sync` は FS 全体 (ext2_sync → スーパーブロック + ブロックグループ
ディスクリプタ + ビットマップのフラッシュ) で、ファイル単位ではない。

### 4-2. journal_mode=DELETE でのフロー

```
1. ジャーナルファイル書き込み
2. xSync(ジャーナル)      ← ここで sync 呼び出し
3. DB ファイル書き込み
4. xSync(DB)              ← ここでも sync 呼び出し
5. ジャーナルファイル削除 (xDelete)
```

FS 全体の sync でも **データ整合性は保たれる** (すべてのダーティブロックが
ディスクにフラッシュされるため)。パフォーマンス的には過剰だが、正確性は担保される。

### 4-3. 実装

```c
static int os32_sync(sqlite3_file *pFile, int flags)
{
    Os32File *p = (Os32File *)pFile;
    int rc;

    if (!p->ops->sync) return SQLITE_OK;

    rc = p->ops->sync(p->fs_ctx);
    return (rc == 0) ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}
```

---

## 5. xRandomness の実装

SQLite は暗号学的な乱数を必要としない。
初期化時のシードとして使用するのみ。

```c
static int os32_randomness(sqlite3_vfs *pVfs, int nByte, char *zByte)
{
    int i;
    u32 seed;

    /* PIT カウンタの下位ビット + RTC 秒数 + tick_count を組み合わせ */
    seed = tick_count;
    seed ^= (u32)sys_time() * 2654435761UL;  /* Knuth 乗算ハッシュ */
    seed ^= inb(0x71);                        /* RTC レジスタ */

    for (i = 0; i < nByte; i++) {
        seed = seed * 1103515245 + 12345;    /* LCG */
        zByte[i] = (char)(seed >> 16);
    }

    return nByte;
}
```

---

## 6. xCurrentTime の実装

SQLite は Julian Day 数を double で要求する。

```c
static int os32_current_time(sqlite3_vfs *pVfs, double *pTime)
{
    os_time_t t = sys_time();  /* UNIX epoch 秒 */
    *pTime = (double)t / 86400.0 + 2440587.5;  /* Julian Day 変換 */
    return SQLITE_OK;
}
```

---

## 7. VFS 登録

```c
sqlite3_vfs *os32_sqlite_vfs(void)
{
    static sqlite3_vfs os32vfs = {
        1,                        /* iVersion */
        sizeof(Os32File),         /* szOsFile */
        VFS_MAX_PATH,             /* mxPathname */
        0,                        /* pNext */
        "os32",                   /* zName */
        0,                        /* pAppData */
        os32_vfs_open,            /* xOpen */
        os32_vfs_delete,          /* xDelete */
        os32_vfs_access,          /* xAccess */
        os32_vfs_full_pathname,   /* xFullPathname */
        os32_vfs_dl_open,         /* xDlOpen */
        os32_vfs_dl_error,        /* xDlError */
        os32_vfs_dl_sym,          /* xDlSym */
        os32_vfs_dl_close,        /* xDlClose */
        os32_randomness,          /* xRandomness */
        os32_sleep,               /* xSleep */
        os32_current_time,        /* xCurrentTime */
    };
    return &os32vfs;
}

/* 初期化 (カーネル起動時またはプログラム開始時に呼び出し) */
void os32_sqlite_init(void)
{
    sqlite3_vfs_register(os32_sqlite_vfs(), 1);  /* デフォルト VFS として登録 */
}
```

---

## 8. xTruncate の将来対応

現在は no-op で対応するが、将来的に `VACUUM` や `journal_mode=TRUNCATE` を
サポートする場合は ext2 に truncate 機能を追加する必要がある。

### ext2_truncate の見積もり

| 作業 | 内容 | 工数 |
|------|------|------|
| inode ブロックリスト操作 | 不要なデータブロックを解放 | 1日 |
| 間接ブロック対応 | シングル/ダブル間接ブロックの部分解放 | 0.5日 |
| VFS レイヤー統合 | `VfsOps` に `truncate` 追加 | 0.5日 |
| テスト | ファイルサイズ縮小/拡大のテスト | 0.5日 |
| **合計** | | **2.5日** |

---

*Custom VFS Implementation — 2026-04-24*
