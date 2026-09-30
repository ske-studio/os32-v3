/* ======================================================================== */
/*  OS32_SQLITE_VFS.C — OS32 カスタム SQLite VFS 実装                       */
/*                                                                          */
/*  OS32 の VFS (vfs.h) をバックエンドとする SQLite カスタム VFS。            */
/*  MEMSYS5 固定プールによるメモリ管理もここで初期化する。                    */
/*                                                                          */
/*  制約:                                                                   */
/*    - シングルタスク: ロック / ミューテックス は全て no-op                  */
/*    - xTruncate: ext2 truncate 未実装のため no-op                          */
/*    - ジャーナルモード: DELETE (デフォルト)                                 */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32_sqlite_vfs.h"
#include "vfs.h"
#include "types.h"
#include "kstring.h"
#include "kprintf.h"

/* ======== MEMSYS5 固定プール (200KB) + canary ========
 * 一時的に 400KB へ拡張していたが、エンジンlib (board/battle/econ/inv) が
 * 起動時のRAMキャッシュ読込後もDB接続を握りっぱなしにしていたのが原因で、
 * 各libが init 完了時に db_close するよう修正して同時接続を1本に戻した。
 * よって拡張前の 200KB に復帰する。
 * 注: FEP候補ゼロ問題 (2026-08-07解決) はプール枯渇ではなく、exec_exit の
 * FD一括クローズが FEP辞書の fd を回収していたのが原因 (vfs_fd_set_protect
 * で保護)。FEP常駐接続 + 一時接続1本は 200KB で動作確認済み。 */
/* MEMSYS5 固定プール。
   200KB では game (econ 常時接続 + battle/rpg/items/events の順次ロード) が
   最後の battle.db 二回目 open の prepare で out of memory になった
   (使用中 188KB で枯渇)。SQLite 拡張域 0x200000-0x2FFFFF は
   text+rodata+data で 374KB 使用なので、384KB に広げても 240KB 余る。 */
#define SQLITE_MEMSYS5_SIZE  (384 * 1024)
#define CANARY_VALUE 0xDEADBEEFUL
static u32 canary_before[4] = {
    CANARY_VALUE, CANARY_VALUE, CANARY_VALUE, CANARY_VALUE
};
static char sqlite_mem_pool[SQLITE_MEMSYS5_SIZE];
static u32 canary_after[4] = {
    CANARY_VALUE, CANARY_VALUE, CANARY_VALUE, CANARY_VALUE
};

/* MEMSYS5 プールの canary 検証
 * 戻り値: 0=正常, -1=前方破壊, -2=後方破壊 */
int memsys5_check_canary(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (canary_before[i] != CANARY_VALUE) return -1;
    }
    for (i = 0; i < 4; i++) {
        if (canary_after[i] != CANARY_VALUE) return -2;
    }
    return 0;
}

/* ======== OMIT_ATTACH スタブ ======== */
/* sqlite3DbIsNamed は OMIT_ATTACH で定義除去されるが、             */
/* sqlite3FindIndex 等から参照が残る。シングルDB構成なので常に true。 */
int sqlite3DbIsNamed(void *db, int iDb, const char *zName)
{
    (void)db; (void)iDb; (void)zName;
    return (iDb == 0) ? 1 : 0;
}

typedef struct {
    Os32SqliteGroup cookie;
    int state, kind, owner;
    void *db;
    int first_stage, first_rc;
    int sticky_rc;
    int open_attempted;
} Os32GroupSlot;
static Os32GroupSlot groups[OS32_SQLITE_GROUPS];

static Os32GroupSlot *group_identity(const Os32SqliteGroup *cookie)
{
    Os32GroupSlot *g;
    if (!cookie || cookie->group_index < 0 ||
        cookie->group_index >= OS32_SQLITE_GROUPS || !cookie->generation) return 0;
    g = &groups[cookie->group_index];
    if (g->cookie.generation != cookie->generation) return 0;
    return g;
}

static Os32GroupSlot *group_lookup(const Os32SqliteGroup *cookie)
{
    Os32GroupSlot *g = group_identity(cookie);
    return g && g->state != OS32_SQLITE_FREE ? g : 0;
}

int os32_sqlite_group_opened(const Os32SqliteGroup *cookie, void *db)
{
    Os32GroupSlot *g = group_lookup(cookie);
    if (!g || g->state != OS32_SQLITE_OPENING || !db) return SQLITE_MISUSE;
    g->db = db;
    g->state = OS32_SQLITE_LIVE;
    return SQLITE_OK;
}

int os32_sqlite_group_snapshot(const Os32SqliteGroup *cookie,
                               Os32SqliteGroupInfo *out)
{
    Os32GroupSlot *g = group_identity(cookie);
    if (!g || !out) return SQLITE_MISUSE;
    out->cookie = g->cookie;
    out->state = g->state;
    out->kind = g->kind;
    out->owner = g->owner;
    out->first_stage = g->first_stage;
    out->first_rc = g->first_rc;
    out->sticky_rc = g->sticky_rc;
    out->fd_count = vfs_count_sqlite(cookie);
    out->has_db = g->db != 0;
    return SQLITE_OK;
}

int os32_sqlite_group_begin_close(const Os32SqliteGroup *cookie)
{
    Os32GroupSlot *g = group_lookup(cookie);
    if (!g || (g->state != OS32_SQLITE_OPENING &&
               g->state != OS32_SQLITE_LIVE)) return SQLITE_MISUSE;
    g->state = OS32_SQLITE_CLOSING;
    return SQLITE_OK;
}

static int group_active(const Os32GroupSlot *g)
{
    return g && (g->state == OS32_SQLITE_OPENING ||
                 g->state == OS32_SQLITE_LIVE || g->state == OS32_SQLITE_CLOSING);
}

static void group_error(Os32GroupSlot *g, int stage, int rc)
{
    if (!g) return;
    if (!g->first_rc) { g->first_stage = stage; g->first_rc = rc; }
    if (!g->sticky_rc) g->sticky_rc = rc;
}

int os32_sqlite_group_quarantine(const Os32SqliteGroup *cookie, int stage, int rc)
{
    Os32GroupSlot *g = group_lookup(cookie);
    if (!g) return SQLITE_MISUSE;
    group_error(g, stage, rc);
    g->state = OS32_SQLITE_QUARANTINED;
    vfs_quarantine_sqlite(cookie);
    return SQLITE_OK;
}

int os32_sqlite_group_finish(const Os32SqliteGroup *cookie, int close_rc)
{
    Os32GroupSlot *g = group_lookup(cookie);
    if (!g || g->state != OS32_SQLITE_CLOSING) return SQLITE_MISUSE;
    if (close_rc == SQLITE_OK) g->db = 0;
    if (close_rc != SQLITE_OK || vfs_count_sqlite(cookie) != 0 || g->sticky_rc) {
        if (close_rc == SQLITE_OK)
            close_rc = g->sticky_rc ? g->sticky_rc : SQLITE_IOERR_CLOSE;
        os32_sqlite_group_quarantine(cookie, OS32_SQLITE_STAGE_CLOSE, close_rc);
        return close_rc;
    }
    g->state = OS32_SQLITE_FREE;
    return SQLITE_OK;
}

/* ======== ファイルハンドル構造体 ======== */
typedef struct Os32File {
    sqlite3_file base;   /* SQLite 基底 (先頭に置く) */
    int fd;              /* OS32 VFS ファイルディスクリプタ */
    int managed;
    VfsSqliteLease lease;
} Os32File;

static int file_valid(sqlite3_file *file)
{
    Os32File *p = (Os32File *)file;
    Os32GroupSlot *g;
    if (!p->managed) return 1;
    g = group_lookup(&p->lease.cookie);
    if (group_active(g) && p->fd == p->lease.fd &&
        vfs_validate_sqlite(&p->lease) == VFS_OK) return 1;
    group_error(g, OS32_SQLITE_STAGE_LEASE, SQLITE_IOERR);
    return 0;
}

/* 入出力に使ってよいか: file_valid に加えて、FD が**失効していない**こと
 * (Codex 実装レビュー ラリー 1 の B2)。unlink・置き換え rename・umount の後の
 * FD は、read / write だけでなく truncate・サイズ・sync・lock も成功扱いに
 * しない — hot journal の判定 (CheckReservedLock) や FileSize が古い値で
 * 通ると、失効した接続がジャーナルを再生・削除してしまう。
 * 失効は接続の誤用ではないので group の sticky エラーには数えない。
 * **close は file_valid だけを見る** (失効後も FD を解放できる)。 */
static int file_live(sqlite3_file *file)
{
    Os32File *p = (Os32File *)file;
    if (!file_valid(file)) return 0;
    return p->fd >= 0 && !vfs_fd_is_stale(p->fd);
}

/* ======================================================================== */
/*  VFS ファイルメソッド                                                     */
/* ======================================================================== */

static int os32Close(sqlite3_file *pFile)
{
    Os32File *p = (Os32File *)pFile;
    if (p->managed) {
        Os32GroupSlot *g = group_lookup(&p->lease.cookie);
        if (!group_active(g) || vfs_close_sqlite(&p->lease) != VFS_OK) {
            /* SQLite discards xClose rc and may immediately free pFile.
             * Save evidence now in the fixed table, never retain pFile. */
            group_error(g, OS32_SQLITE_STAGE_XCLOSE, SQLITE_IOERR_CLOSE);
            return SQLITE_IOERR_CLOSE;
        }
        p->lease.fd = -1;
        p->fd = -1;
        return SQLITE_OK;
    }
    if (p->fd >= 0) {
        vfs_close(p->fd);
        p->fd = -1;
    }
    return SQLITE_OK;
}

static int os32Read(sqlite3_file *pFile, void *buf, int iAmt,
                    sqlite3_int64 iOfst)
{
    Os32File *p = (Os32File *)pFile;
    int n;

    if (!file_live(pFile)) return SQLITE_IOERR_READ;
    vfs_seek(p->fd, (int)iOfst, 0);  /* SEEK_SET */
    n = vfs_read_fd(p->fd, buf, (u32)iAmt);
    if (n < 0) return SQLITE_IOERR_READ;
    if (n < iAmt) {
        /* 不足分をゼロ埋め (SQLite 仕様) */
        memset((char *)buf + n, 0, (u32)(iAmt - n));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int os32Write(sqlite3_file *pFile, const void *buf, int iAmt,
                     sqlite3_int64 iOfst)
{
    Os32File *p = (Os32File *)pFile;
    int n;

    if (!file_live(pFile)) return SQLITE_IOERR_WRITE;
    vfs_seek(p->fd, (int)iOfst, 0);  /* SEEK_SET */
    n = vfs_write_fd(p->fd, buf, (u32)iAmt);
    if (n < iAmt) return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int os32Truncate(sqlite3_file *pFile, sqlite3_int64 size)
{
    (void)size;
    if (!file_live(pFile)) return SQLITE_IOERR_TRUNCATE;
    /* ext2 truncate 未実装 — no-op */
    /* 影響: VACUUM はファイルサイズを縮小できない。 */
    /*       journal_mode=DELETE ではジャーナル truncate が発生するが、 */
    /*       no-op でも DELETE モードのコミットフローに実害なし。 */
    return SQLITE_OK;
}

static int os32Sync(sqlite3_file *pFile, int flags)
{
    Os32File *p = (Os32File *)pFile;
    (void)flags;
    (void)p;
    if (!file_live(pFile)) return SQLITE_IOERR_FSYNC;
    vfs_sync();
    return SQLITE_OK;
}

static int os32FileSize(sqlite3_file *pFile, sqlite3_int64 *pSize)
{
    Os32File *p = (Os32File *)pFile;
    if (!file_live(pFile)) return SQLITE_IOERR_FSTAT;
    *pSize = (sqlite3_int64)vfs_get_size(p->fd);
    return SQLITE_OK;
}

/* ロック系: シングルタスクのため全て no-op */
static int os32Lock(sqlite3_file *f, int l)
    { (void)l; return file_live(f) ? SQLITE_OK : SQLITE_IOERR_LOCK; }
static int os32Unlock(sqlite3_file *f, int l)
    { (void)l; return file_live(f) ? SQLITE_OK : SQLITE_IOERR_UNLOCK; }
static int os32CheckReservedLock(sqlite3_file *f, int *pOut)
{
    if (!file_live(f)) return SQLITE_IOERR_CHECKRESERVEDLOCK;
    *pOut = 0;
    return SQLITE_OK;
}

static int os32FileControl(sqlite3_file *f, int op, void *pArg)
{
    (void)f; (void)op; (void)pArg;
    if (!file_live(f)) return SQLITE_IOERR;
    return SQLITE_NOTFOUND;
}

static int os32SectorSize(sqlite3_file *f)
    { return file_live(f) ? 512 : 0; }
static int os32DeviceCharacteristics(sqlite3_file *f)
    { (void)file_live(f); return 0; }

/* ファイルメソッドテーブル */
static const sqlite3_io_methods os32_io_methods = {
    1,                          /* iVersion */
    os32Close,
    os32Read,
    os32Write,
    os32Truncate,
    os32Sync,
    os32FileSize,
    os32Lock,
    os32Unlock,
    os32CheckReservedLock,
    os32FileControl,
    os32SectorSize,
    os32DeviceCharacteristics,
    /* v2, v3 メソッドは NULL */
    0, 0, 0, 0, 0
};

/* DB接続の main ファイルが使う OS32 VFS fd を返す (失敗時 -1)。
 * FEP辞書などカーネル常駐接続の fd を exec_exit の自動クローズから
 * 保護する (vfs_fd_set_protect) ために使用する。 */
int os32_sqlite_db_fd(void *db)
{
    sqlite3_file *pf = (sqlite3_file *)0;
    if (sqlite3_file_control((sqlite3 *)db, "main",
                             SQLITE_FCNTL_FILE_POINTER, &pf)
            != SQLITE_OK || !pf) {
        return -1;
    }
    if (pf->pMethods != &os32_io_methods) return -1;
    return ((Os32File *)pf)->fd;
}

/* ======================================================================== */
/*  VFS メソッド                                                             */
/* ======================================================================== */

/* DB 本体を開く前に、`<名前>-journal` が下位層の名前の上限 (NUL 抜き
 * VFS_MAX_PATH - 1 = 255 バイト) に収まるかを見る (Opus 実装レビュー ラリー 1
 * の nb2)。SQLite はジャーナルを**開いた名前のまま** "-journal" を足して開く
 * ので、解決前の名前と解決後の絶対名の両方で数える。収まらない名前を通すと
 * 「読めるが最初の書き込みでジャーナルを開けない」接続を返してしまう。
 * 1 = 収まる / 0 = 収まらない (か、名前を解決できない) */
static u32 name_len(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    return n;
}

static int journal_name_fits(const char *name)
{
    static char abs_name[VFS_MAX_PATH];
    const u32 sfx = 8u;                     /* strlen("-journal") */
    if (!name) return 0;
    if (name_len(name) + sfx + 1u > (u32)VFS_MAX_PATH) return 0;
    if (vfs_resolve_path(name, abs_name, VFS_MAX_PATH) != VFS_OK) return 0;
    return name_len(abs_name) + sfx + 1u <= (u32)VFS_MAX_PATH;
}

static int os32VfsOpen(sqlite3_vfs *pVfs, const char *zName,
                       sqlite3_file *pFile, int flags, int *pOutFlags)
{
    Os32File *p = (Os32File *)pFile;
    int oflags = 0;
    Os32GroupSlot *g = (Os32GroupSlot *)pVfs->pAppData;

    p->base.pMethods = (sqlite3_io_methods *)0;
    p->fd = -1;
    p->managed = g != 0;

    /* 一時ファイル (zName==NULL) はメモリストアで処理されるはず */
    if (zName == (const char *)0) return SQLITE_CANTOPEN;
    if ((flags & SQLITE_OPEN_MAIN_DB) && !journal_name_fits(zName))
        return SQLITE_CANTOPEN;             /* 名前が長すぎる (NAMETOOLONG) */

    if (flags & SQLITE_OPEN_CREATE)   oflags |= 0x0100; /* KAPI_O_CREAT */
    if (flags & SQLITE_OPEN_READWRITE) oflags |= 0x0002; /* KAPI_O_RDWR */
    else                               oflags |= 0x0000; /* KAPI_O_RDONLY */

    if (g) {
        if (!group_active(g) || (flags & SQLITE_OPEN_URI)) return SQLITE_CANTOPEN;
        if (vfs_open_sqlite(zName, oflags, g->owner, &g->cookie,
                            flags, &p->lease) != VFS_OK) return SQLITE_CANTOPEN;
        p->fd = p->lease.fd;
    } else {
        /* Legacy default retained until all connection boundaries migrate. */
        p->fd = vfs_open(zName, oflags);
        /* 開いている DB / ジャーナルの rename は BUSY で断る (票
         * TASK_VFS_FD_PATH のユーザー決裁 ①)。group の FD (vfs_open_sqlite)
         * には最初から付いている。旧来の経路 (IME 辞書の常駐接続など) にも付ける */
        if (p->fd >= 0) (void)vfs_fd_set_sqlite_db(p->fd);
    }
    if (p->fd < 0) return SQLITE_CANTOPEN;

    p->base.pMethods = &os32_io_methods;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int os32VfsDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{
    int rc;
    (void)pVfs; (void)dirSync;
    if (pVfs->pAppData && !group_active((Os32GroupSlot *)pVfs->pAppData))
        return SQLITE_IOERR_DELETE;
    rc = vfs_rm(zPath);
    /* SQLite 仕様: ファイルが存在しない場合も SQLITE_OK を返す */
    /* (ジャーナル削除時にファイルが既にないケースがある) */
    if (rc < 0) {
        /* 注意: .sqlite_text からの kprintf は PAGE FAULT を引き起こす */
        /* (既知の制約: 06_PHASES.md §1.6 参照) */
    }
    return SQLITE_OK;
}

static int os32VfsAccess(sqlite3_vfs *pVfs, const char *zPath,
                         int flags, int *pResOut)
{
    OS32_Stat st;
    int rc;
    (void)pVfs;

    if (pVfs->pAppData && !group_active((Os32GroupSlot *)pVfs->pAppData))
        return SQLITE_IOERR_ACCESS;
    rc = vfs_stat(zPath, &st);
    if (flags == SQLITE_ACCESS_EXISTS) {
        *pResOut = (rc == 0) ? 1 : 0;
    } else {
        *pResOut = (rc == 0) ? 1 : 0;
    }
    return SQLITE_OK;
}

static int os32VfsFullPathname(sqlite3_vfs *pVfs, const char *zName,
                               int nOut, char *zOut)
{
    (void)pVfs;
    if (pVfs->pAppData && !group_active((Os32GroupSlot *)pVfs->pAppData))
        return SQLITE_CANTOPEN;
    kstrncpy(zOut, zName, (u32)nOut);
    return SQLITE_OK;
}

static int os32VfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut)
{
    extern volatile u32 tick_count;
    u32 val;
    int i;
    (void)pVfs;

    val = tick_count;
    for (i = 0; i < nByte; i++) {
        val = val * 1103515245 + 12345;
        zOut[i] = (char)(val >> 16);
    }
    return nByte;
}

static int os32VfsSleep(sqlite3_vfs *pVfs, int microseconds)
{
    extern volatile u32 tick_count;
    u32 ticks;
    u32 start;
    (void)pVfs;

    ticks = (u32)microseconds / 10000; /* 10ms/tick (100Hz) */
    if (ticks == 0) ticks = 1;
    start = tick_count;
    while ((tick_count - start) < ticks) {
        /* ビジーウェイト */
    }
    return microseconds;
}

static int os32VfsCurrentTime(sqlite3_vfs *pVfs, double *pTime)
{
    extern u32 sys_time(void);
    u32 unix_time;
    (void)pVfs;

    unix_time = sys_time();
    /* Unix epoch → Julian day number 変換 */
    /* JD of 1970-01-01 = 2440587.5 */
    *pTime = 2440587.5 + (double)unix_time / 86400.0;
    return SQLITE_OK;
}

static int os32VfsGetLastError(sqlite3_vfs *pVfs, int nBuf, char *zBuf)
{
    (void)pVfs;
    if (nBuf > 0) zBuf[0] = '\0';
    return 0;
}

/* ======================================================================== */
/*  sqlite3_os_init / sqlite3_os_end — SQLite が呼び出す VFS 登録           */
/* ======================================================================== */

static sqlite3_vfs os32_vfs = {
    1,                      /* iVersion */
    sizeof(Os32File),       /* szOsFile */
    256,                    /* mxPathname */
    0,                      /* pNext */
    "os32",                 /* zName */
    0,                      /* pAppData */
    os32VfsOpen,
    os32VfsDelete,
    os32VfsAccess,
    os32VfsFullPathname,
    0, 0, 0, 0,            /* xDlOpen, xDlError, xDlSym, xDlClose */
    os32VfsRandomness,
    os32VfsSleep,
    os32VfsCurrentTime,
    os32VfsGetLastError,
    0,                      /* xCurrentTimeInt64 */
    0, 0, 0                 /* xSetSystemCall, xGetSystemCall, xNextSystemCall */
};

/* Only mutable group state is reset on acquire; descriptor linkage is not. */
int os32_sqlite_group_acquire(int kind, int owner, Os32SqliteGroup *out)
{
    int i, first, end;
    Os32GroupSlot *g;
    if (!out || owner < 0 ||
        (kind != OS32_SQLITE_EXEC && kind != OS32_SQLITE_RESIDENT))
        return SQLITE_MISUSE;
    first = kind == OS32_SQLITE_EXEC ? 0 : OS32_SQLITE_EXEC_GROUPS;
    end = kind == OS32_SQLITE_EXEC ? OS32_SQLITE_EXEC_GROUPS : OS32_SQLITE_GROUPS;
    for (i = first; i < end; i++) {
        g = &groups[i];
        if (g->state != OS32_SQLITE_FREE ||
            g->cookie.generation == OS32_SQLITE_GROUP_GENERATION_MAX) continue;
        g->cookie.group_index = i;
        g->cookie.generation++;
        g->kind = kind;
        g->owner = owner;
        g->state = OS32_SQLITE_OPENING;
        g->db = 0;
        g->first_stage = 0;
        g->first_rc = 0;
        g->sticky_rc = 0;
        g->open_attempted = 0;
        *out = g->cookie;
        return SQLITE_OK;
    }
    return SQLITE_FULL;
}

/* Boot-lifetime descriptors; never reset/copy after successful registration. */
static sqlite3_vfs group_vfs[OS32_SQLITE_GROUPS];
static const char *const group_names[OS32_SQLITE_GROUPS] = {
    "os32-g0", "os32-g1", "os32-g2", "os32-g3", "os32-g4",
    "os32-g5", "os32-g6", "os32-g7", "os32-resident"
};
typedef char Os32GroupCapacityCheck[(DB_MAX_CONNECTIONS == 8) ? 1 : -1];
static int vfs_registered;

const char *os32_sqlite_group_vfs_name(const Os32SqliteGroup *cookie)
{
    Os32GroupSlot *g = group_lookup(cookie);
    if (!g || g->state == OS32_SQLITE_QUARANTINED) return 0;
    return group_names[cookie->group_index];
}

int os32_sqlite_group_open(const Os32SqliteGroup *cookie, const char *path,
                           int flags, sqlite3 **out)
{
    Os32GroupSlot *g = group_lookup(cookie);
    int rc;
    if (!g || g->state != OS32_SQLITE_OPENING || g->open_attempted ||
        !path || !out || *out ||
        (flags != SQLITE_OPEN_READONLY && flags != SQLITE_OPEN_READWRITE &&
         flags != (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) ||
        kstrncmp(path, "file:", 5) == 0) return SQLITE_MISUSE;
    g->open_attempted = 1;
    rc = sqlite3_open_v2(path, out, flags, os32_sqlite_group_vfs_name(cookie));
    g->db = *out;
    if (rc == SQLITE_OK) return os32_sqlite_group_opened(cookie, *out);
    if (!g->first_rc) { g->first_stage = OS32_SQLITE_STAGE_OPEN; g->first_rc = rc; }
    return rc;
}

int sqlite3_os_init(void)
{
    int i, rc;
    /* Progress survives a registration failure. Already registered linkage
     * is never copied/reset, even when initialization is retried. */
    if (!vfs_registered) {
        rc = sqlite3_vfs_register(&os32_vfs, 1);
        if (rc != SQLITE_OK) return rc;
        vfs_registered = 1;
    }
    while (vfs_registered <= OS32_SQLITE_GROUPS) {
        i = vfs_registered - 1;
        group_vfs[i] = os32_vfs;
        group_vfs[i].pNext = 0;
        group_vfs[i].zName = group_names[i];
        group_vfs[i].pAppData = &groups[i];
        rc = sqlite3_vfs_register(&group_vfs[i], 0);
        if (rc != SQLITE_OK) return rc;
        vfs_registered++;
    }
    return SQLITE_OK;
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}

/* ======================================================================== */
/*  os32_sqlite_init — カーネル初期化から呼ばれるエントリポイント              */
/* ======================================================================== */
int os32_sqlite_init(void)
{
    int rc;

    kprintf(0x07, "[SQT] init: pool=%x size=%x\n",
            (unsigned)sqlite_mem_pool, SQLITE_MEMSYS5_SIZE);

    /* MEMSYS5 固定プール設定 */
    rc = sqlite3_config(SQLITE_CONFIG_HEAP,
                        sqlite_mem_pool,
                        SQLITE_MEMSYS5_SIZE,
                        64);  /* 最小アロケーション粒度 */

    kprintf(0x07, "[SQT] sqlite3_config rc=%d\n", rc);
    if (rc != SQLITE_OK) return rc;

    /* SQLite 初期化 (内部で sqlite3_os_init が呼ばれる) */
    rc = sqlite3_initialize();

    kprintf(0x07, "[SQT] sqlite3_initialize rc=%d\n", rc);
    return rc;
}

/* ======================================================================== */
/*  os32_sqlite_dump_vfs — VFS メソッドテーブルのアドレスダンプ (デバッグ用)   */
/* ======================================================================== */
void os32_sqlite_dump_vfs(void)
{
    kprintf(0x07, "[VFS] io_methods=%x\n", (unsigned)&os32_io_methods);
    kprintf(0x07, "[VFS] xClose=%x xRead=%x xWrite=%x\n",
            (unsigned)os32_io_methods.xClose,
            (unsigned)os32_io_methods.xRead,
            (unsigned)os32_io_methods.xWrite);
    kprintf(0x07, "[VFS] xTruncate=%x xSync=%x xFileSize=%x\n",
            (unsigned)os32_io_methods.xTruncate,
            (unsigned)os32_io_methods.xSync,
            (unsigned)os32_io_methods.xFileSize);
}
