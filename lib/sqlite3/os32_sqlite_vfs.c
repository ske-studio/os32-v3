/* ======================================================================== */
/*  OS32_SQLITE_VFS.C — OS32 カスタム SQLite VFS 実装                       */
/*                                                                          */
/*  OS32 の VFS (vfs.h) をバックエンドとする SQLite カスタム VFS。            */
/*  MEMSYS5 固定プールによるメモリ管理もここで初期化する。                    */
/*                                                                          */
/*  制約:                                                                   */
/*    - single task: SHARED locks are no-op; immutable CD cannot promote    */
/*    - mutable MAIN_DB: one connection per ext2 inode                      */
/*    - ジャーナルモード: DELETE (デフォルト)                                 */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32_sqlite_vfs.h"
#include "vfs.h"
#include "types.h"
#include "kstring.h"
#include "kprintf.h"

/* Product module receives its owner-backed pool. The source-local array is
 * retained only for isolated VFS host fixtures; product generation replaces it
 * with a pointer before compilation. */
#define CANARY_VALUE 0xDEADBEEFUL
#define CANARY_WORDS 4
#define CANARY_BYTES (CANARY_WORDS * sizeof(u32))
static char sqlite_mem_pool[SQLITE_MEMSYS5_SIZE];
/* Owner pool layout: BudgetState | canary | MEMSYS5 heap | canary. */
#define canary_before ((u32 *)(sqlite_mem_pool + BUDGET_STATE_BYTES))
#define canary_after ((u32 *)(sqlite_mem_pool + SQLITE_MEMSYS5_SIZE - CANARY_BYTES))

/* T4a-4's measured prefix allocator, inside the same 512KiB owner pool.
 * The two canaries and fixed accounting state are deducted before MEMSYS5.
 * Allocation tags preserve class when another caller frees/reallocates them. */
enum { BUDGET_SYSTEM, BUDGET_FEP, BUDGET_APP, BUDGET_CLASSES };
typedef struct { unsigned owner, charge; } BudgetTag;
typedef struct {
    sqlite3_mem_methods backing;
    unsigned context, live[BUDGET_CLASSES], reserve;
} BudgetState;
#define BUDGET_STATE_BYTES ((sizeof(BudgetState) + 7u) & ~7u)
#define BUDGET_HEAP_BYTES (SQLITE_MEMSYS5_SIZE - 2 * CANARY_BYTES - BUDGET_STATE_BYTES)
#define BUDGET_ATOM_BYTES 64u
#define BUDGET_CAPACITY ((BUDGET_HEAP_BYTES / (BUDGET_ATOM_BYTES + 1u)) * BUDGET_ATOM_BYTES)
#define budget ((BudgetState *)sqlite_mem_pool)

static int budget_roundup(int n)
{
    int raw;
    if (n <= 0 || n > 0x40000000 - (int)sizeof(BudgetTag)) return 0;
    raw = budget->backing.xRoundup(n + sizeof(BudgetTag));
    return raw ? raw - (int)sizeof(BudgetTag) : 0;
}
static void *budget_malloc(int n)
{
    BudgetTag *tag;
    unsigned charge;
    int rounded = budget_roundup(n);
    if (!rounded) return 0;
    charge = rounded + sizeof(BudgetTag);
    if (budget->context == BUDGET_APP &&
        budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] + charge >
        BUDGET_CAPACITY - budget->reserve) return 0;
    tag = budget->backing.xMalloc(charge);
    if (!tag) return 0;
    tag->owner = budget->context;
    tag->charge = budget->backing.xSize(tag);
    budget->live[tag->owner] += tag->charge;
    return tag + 1;
}
static void budget_free(void *p)
{
    BudgetTag *tag;
    if (!p) return;
    tag = (BudgetTag *)p - 1;
    budget->live[tag->owner] -= tag->charge;
    budget->backing.xFree(tag);
}
static int budget_size(void *p)
{
    return budget->backing.xSize((BudgetTag *)p - 1) - sizeof(BudgetTag);
}
static void *budget_realloc(void *p, int n)
{
    void *next;
    unsigned saved, size;
    if (!p) return budget_malloc(n);
    size = budget_size(p);
    if (n > 0 && (unsigned)n <= size) return p;
    saved = budget->context;
    budget->context = ((BudgetTag *)p - 1)->owner;
    next = budget_malloc(n);
    budget->context = saved;
    if (next) { kmemcpy(next, p, size); budget_free(p); }
    return next;
}
static int budget_init(void *p)
{
    (void)p;
    return budget->backing.xInit(budget->backing.pAppData);
}
static void budget_end(void *p)
{
    (void)p;
    budget->backing.xShutdown(budget->backing.pAppData);
}

/* MEMSYS5 プールの canary 検証
 * 戻り値: 0=正常, -1=前方破壊, -2=後方破壊 */
int memsys5_check_canary(void)
{
    int i;
    for (i = 0; i < CANARY_WORDS; i++) {
        if (canary_before[i] != CANARY_VALUE) return -1;
    }
    for (i = 0; i < CANARY_WORDS; i++) {
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
    int has_main;
    VfsSqliteIdentity main_identity;
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

int os32_sqlite_group_for_db(void *db, Os32SqliteGroupInfo *out)
{
    int i;
    if (!db) return SQLITE_MISUSE;
    for (i = 0; i < OS32_SQLITE_GROUPS; i++)
        if (groups[i].state != OS32_SQLITE_FREE && groups[i].db == db)
            return os32_sqlite_group_snapshot(&groups[i].cookie, out);
    return SQLITE_MISUSE;
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
    g->has_main = 0;
    g->state = OS32_SQLITE_FREE;
    return SQLITE_OK;
}

/* Called only by generated module adapters, on the active engine stack. */
void os32_sqlite_allocation_context(void *db, const Os32SqliteGroup *cookie)
{
    Os32GroupSlot *g = group_lookup(cookie);
    int i;
    if (!g && db) {
        for (i = 0; i < OS32_SQLITE_GROUPS; i++)
            if (groups[i].db == db && groups[i].state != OS32_SQLITE_FREE) {
                g = &groups[i];
                break;
            }
    }
    budget->context = g ? (g->kind == OS32_SQLITE_RESIDENT ? BUDGET_FEP : BUDGET_APP)
                        : BUDGET_SYSTEM;
}

int os32_sqlite_boot_check(int live, int reserve)
{
    int i, count = 0;
    for (i = 0; i < OS32_SQLITE_GROUPS; i++) {
        if (groups[i].state == OS32_SQLITE_FREE) continue;
        if (groups[i].state != OS32_SQLITE_LIVE ||
            groups[i].kind != OS32_SQLITE_RESIDENT) return SQLITE_BUSY;
        count++;
    }
    if (count != live) return SQLITE_BUSY;
    /* With no resident group there is no FEP to reserve for. A zero request
     * with a live dictionary remains an observation and preserves its budget. */
    if (!live && !reserve) budget->reserve = 0;
    if (reserve) {
        if (reserve > 0 && reserve < OS32_SQLITE_FEP_RESERVE_MIN) {
            kprintf(0x07, "[SQT] FEP reserve below minimum; using %d bytes\n",
                    OS32_SQLITE_FEP_RESERVE_MIN);
            reserve = OS32_SQLITE_FEP_RESERVE_MIN;
        }
        if (reserve < OS32_SQLITE_FEP_RESERVE_MIN ||
            (unsigned)reserve > BUDGET_CAPACITY - budget->live[BUDGET_SYSTEM] ||
            budget->live[BUDGET_FEP] > (unsigned)reserve) return SQLITE_NOMEM;
        budget->reserve = reserve;
    }
    return SQLITE_OK;
}

int os32_sqlite_shutdown(void)
{
    if (os32_sqlite_boot_check(0, 0) != SQLITE_OK) return SQLITE_BUSY;
    return sqlite3_shutdown();
}

/* ======== ファイルハンドル構造体 ======== */
typedef struct Os32File {
    sqlite3_file base;   /* SQLite 基底 (先頭に置く) */
    int fd;              /* OS32 VFS ファイルディスクリプタ */
    int managed;
    int readonly;
    int immutable;
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
    if (p->immutable) {
        VfsSqliteIdentity id;
        if (vfs_sqlite_fd_identity(p->fd, &id) != VFS_OK) return 0;
    }
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
    if (p->immutable && !file_live(pFile)) return SQLITE_IOERR_READ;
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
    if (p->readonly) return SQLITE_READONLY;
    vfs_seek(p->fd, (int)iOfst, 0);  /* SEEK_SET */
    n = vfs_write_fd(p->fd, buf, (u32)iAmt);
    if (n < iAmt) return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int os32Truncate(sqlite3_file *pFile, sqlite3_int64 size)
{
    Os32File *p = (Os32File *)pFile;
    if (!file_live(pFile)) return SQLITE_IOERR_TRUNCATE;
    if (p->readonly) return SQLITE_READONLY;
    if (size < 0 || (sqlite3_uint64)size > 0xffffffffUL)
        return SQLITE_IOERR_TRUNCATE;
    return vfs_resize_fd(p->fd, (u32)size) == VFS_OK
        ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int os32Sync(sqlite3_file *pFile, int flags)
{
    Os32File *p = (Os32File *)pFile;
    (void)flags;
    if (!file_live(pFile)) return SQLITE_IOERR_FSYNC;
    return vfs_sync_fd(p->fd) == VFS_OK ? SQLITE_OK : SQLITE_IOERR_FSYNC;
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
{
    if (!file_live(f)) return SQLITE_IOERR_LOCK;
    if (((Os32File *)f)->immutable && l > SQLITE_LOCK_SHARED) return SQLITE_READONLY;
    return SQLITE_OK;
}
static int os32Unlock(sqlite3_file *f, int l)
{
    if (!file_live(f)) return SQLITE_IOERR_UNLOCK;
    if (((Os32File *)f)->immutable && l > SQLITE_LOCK_SHARED) return SQLITE_READONLY;
    return SQLITE_OK;
}
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
    { (void)file_live(f); return 0; } /* Keep SQLite's lock/access callbacks:
                                      * they check media even with cached pages. */

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

/* Immutable distribution databases must already be clean. Probe both sidecars
 * before SQLite can attempt recovery; NOTFOUND alone means absent. */
static int immutable_clean(const char *name)
{
    const char *suffixes[2] = {"-journal", "-wal"};
    char side[VFS_MAX_PATH];
    OS32_Stat st;
    int i, rc;
    u32 n = name_len(name), j;
    for (i = 0; i < 2; i++) {
        kstrncpy(side, name, sizeof(side));
        for (j = 0; suffixes[i][j]; j++) side[n + j] = suffixes[i][j];
        side[n + j] = 0;
        rc = vfs_stat(side, &st);
        if (rc == VFS_OK) return SQLITE_CANTOPEN;
        if (rc != VFS_ERR_NOTFOUND) return SQLITE_IOERR;
    }
    return vfs_sqlite_backend(name) == VFS_SQLITE_IMMUTABLE_CD ? SQLITE_OK : SQLITE_IOERR;
}

static int os32VfsOpen(sqlite3_vfs *pVfs, const char *zName,
                       sqlite3_file *pFile, int flags, int *pOutFlags)
{
    Os32File *p = (Os32File *)pFile;
    int oflags = 0;
    int rc, i, backend;
    VfsSqliteIdentity id;
    Os32GroupSlot *g = (Os32GroupSlot *)pVfs->pAppData;

    p->base.pMethods = (sqlite3_io_methods *)0;
    p->fd = -1;
    p->managed = g != 0;
    p->immutable = 0;

    /* 一時ファイル (zName==NULL) はメモリストアで処理されるはず */
    if (zName == (const char *)0) return SQLITE_CANTOPEN;
    /* Reject unowned opens before even backend/path probes. */
    if (!g || !group_active(g) || (flags & SQLITE_OPEN_URI)) return SQLITE_CANTOPEN;
    if ((flags & SQLITE_OPEN_MAIN_DB) && !journal_name_fits(zName))
        return SQLITE_CANTOPEN;             /* 名前が長すぎる (NAMETOOLONG) */

    backend = vfs_sqlite_backend(zName);
    if (backend == VFS_SQLITE_IMMUTABLE_CD) {
        if (!(flags & SQLITE_OPEN_MAIN_DB) || !(flags & SQLITE_OPEN_READONLY) ||
            (flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE))) return SQLITE_READONLY;
        rc = immutable_clean(zName);
        if (rc != SQLITE_OK) return rc;
        p->immutable = 1;
    }
    if (backend == VFS_ERR_IO) return SQLITE_IOERR;
    if (flags & SQLITE_OPEN_MAIN_DB) {
        if (backend != VFS_SQLITE_EXT2 && backend != VFS_SQLITE_IMMUTABLE_CD)
            return SQLITE_CANTOPEN;
        if (flags & SQLITE_OPEN_READWRITE) {
            rc = vfs_sqlite_writable(zName);
            if (rc == VFS_ERR_ROFS && !(flags & SQLITE_OPEN_CREATE))
                flags = (flags & ~SQLITE_OPEN_READWRITE) | SQLITE_OPEN_READONLY;
            else if (rc != VFS_OK) return SQLITE_CANTOPEN;
        }
    }

    if (flags & SQLITE_OPEN_CREATE)   oflags |= 0x0100; /* KAPI_O_CREAT */
    if (flags & SQLITE_OPEN_READWRITE) oflags |= 0x0002; /* KAPI_O_RDWR */
    else                               oflags |= 0x0000; /* KAPI_O_RDONLY */

    if (vfs_open_sqlite(zName, oflags, g->owner, &g->cookie,
                        flags, &p->lease) != VFS_OK) return SQLITE_CANTOPEN;
    p->fd = p->lease.fd;
    if (p->fd < 0) return SQLITE_CANTOPEN;

    p->readonly = !(oflags & O_RDWR);
    if (flags & SQLITE_OPEN_MAIN_DB) {
        rc = vfs_sqlite_fd_identity(p->fd, &id);
        if (rc == VFS_OK) {
            for (i = 0; i < OS32_SQLITE_GROUPS; i++) {
                Os32GroupSlot *other = &groups[i];
                if (other->has_main &&
                    other->main_identity.mount_generation == id.mount_generation &&
                    other->main_identity.file_id == id.file_id) {
                    if (id.kind == VFS_SQLITE_IMMUTABLE_CD && id.readonly &&
                        other->main_identity.kind == VFS_SQLITE_IMMUTABLE_CD &&
                        other->main_identity.readonly &&
                        other->main_identity.media_generation == id.media_generation) continue;
                    rc = VFS_ERR_BUSY;
                    break;
                }
            }
        }
        if (rc == VFS_OK) rc = vfs_sqlite_claim_main(p->fd);
        if (rc != VFS_OK) {
            /* pMethods is still NULL: close only this newly opened FD. */
            if (os32Close(pFile) != SQLITE_OK && g)
                os32_sqlite_group_quarantine(&g->cookie,
                    OS32_SQLITE_STAGE_XCLOSE, SQLITE_IOERR_CLOSE);
            return rc == VFS_ERR_IO ? SQLITE_IOERR : SQLITE_CANTOPEN;
        }
        if (g) { g->main_identity = id; g->has_main = 1; }
    }
    p->base.pMethods = &os32_io_methods;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int os32VfsDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{
    int rc;
    (void)pVfs;
    if (pVfs->pAppData && !group_active((Os32GroupSlot *)pVfs->pAppData))
        return SQLITE_IOERR_DELETE;
    rc = vfs_sqlite_backend(zPath);
    if (rc < 0) return SQLITE_IOERR_DELETE;
    if (rc == VFS_SQLITE_IMMUTABLE_CD) return SQLITE_READONLY;
    rc = vfs_rm(zPath);
    if (rc == VFS_ERR_NOTFOUND) return SQLITE_IOERR_DELETE_NOENT;
    if (rc != VFS_OK) return SQLITE_IOERR_DELETE;
    if (dirSync && vfs_sync() != VFS_OK) return SQLITE_IOERR_DIR_FSYNC;
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
    rc = vfs_sqlite_backend(zPath);
    if (rc < 0) return SQLITE_IOERR_ACCESS;
    rc = vfs_stat(zPath, &st);
    if (vfs_sqlite_backend(zPath) < 0) return SQLITE_IOERR_ACCESS;
    if (rc == VFS_ERR_NOTFOUND) { *pResOut = 0; return SQLITE_OK; }
    if (rc != VFS_OK) return SQLITE_IOERR_ACCESS;
    if (flags == SQLITE_ACCESS_EXISTS) *pResOut = 1;
    else if (flags == SQLITE_ACCESS_READ) *pResOut = !!(st.st_mode & OS_S_IRUSR);
    else if (flags == SQLITE_ACCESS_READWRITE) {
        rc = vfs_sqlite_writable(zPath);
        if (rc != VFS_OK && rc != VFS_ERR_ROFS) return SQLITE_IOERR_ACCESS;
        *pResOut = rc == VFS_OK && (st.st_mode & OS_S_IRUSR) &&
                   (st.st_mode & OS_S_IWUSR);
    } else return SQLITE_IOERR_ACCESS;
    return SQLITE_OK;
}

static int os32VfsFullPathname(sqlite3_vfs *pVfs, const char *zName,
                               int nOut, char *zOut)
{
    (void)pVfs;
    if (pVfs->pAppData && !group_active((Os32GroupSlot *)pVfs->pAppData))
        return SQLITE_CANTOPEN;
    return vfs_resolve_path(zName, zOut, nOut) == VFS_OK
        ? SQLITE_OK : SQLITE_CANTOPEN;
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
    /* A failed boot/settings/FEP close must not borrow the other resident slot. */
    if (kind == OS32_SQLITE_RESIDENT) {
        for (i = first; i < end; i++)
            if (groups[i].state == OS32_SQLITE_QUARANTINED) return SQLITE_BUSY;
    }
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
        g->has_main = 0;
        *out = g->cookie;
        return SQLITE_OK;
    }
    return SQLITE_FULL;
}

/* Boot-lifetime descriptors; never reset/copy after successful registration. */
static sqlite3_vfs group_vfs[OS32_SQLITE_GROUPS];
static const char *const group_names[OS32_SQLITE_GROUPS] = {
    "os32-g0", "os32-g1", "os32-g2", "os32-g3", "os32-g4",
    "os32-g5", "os32-g6", "os32-g7", "os32-resident", "os32-resident1"
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
    int rc, i;
    sqlite3_mem_methods methods = {budget_malloc, budget_free, budget_realloc,
        budget_size, budget_roundup, budget_init, budget_end, 0};
    kmemset(budget, 0, sizeof(*budget));
    budget->reserve = OS32_SQLITE_FEP_RESERVE;
    for (i = 0; i < CANARY_WORDS; i++) {
        canary_before[i] = CANARY_VALUE;
        canary_after[i] = CANARY_VALUE;
    }

    kprintf(0x07, "[SQT] init: pool=%x size=%x\n",
            (unsigned)sqlite_mem_pool, SQLITE_MEMSYS5_SIZE);

    /* MEMSYS5 固定プール設定 */
    rc = sqlite3_config(SQLITE_CONFIG_HEAP,
                        sqlite_mem_pool + CANARY_BYTES + BUDGET_STATE_BYTES,
                        BUDGET_HEAP_BYTES,
                        BUDGET_ATOM_BYTES);  /* 最小アロケーション粒度 */

    kprintf(0x07, "[SQT] sqlite3_config rc=%d\n", rc);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_config(SQLITE_CONFIG_GETMALLOC, &budget->backing);
    if (rc != SQLITE_OK) return rc;
    rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &methods);
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
