/* ======================================================================== */
/*  SQLITE_USER_VFS.C — ユーザー空間用 SQLite VFS 実装                      */
/*                                                                          */
/*  カーネル版 os32_sqlite_vfs.c をベースに、ファイル操作を newlib の         */
/*  open/read/write/close/lseek 経由 (= syscalls.c → KAPI) に差し替え。     */
/*                                                                          */
/*  目的: カーネル拡張域を経由せずに SQLite を実行し、Page Fault の          */
/*        原因がカーネル配置にあるかポート自体にあるかを切り分ける。          */
/* ======================================================================== */

#include "os32_sqlite_config.h"
#include "sqlite3.h"
#include "os32api.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

/* fabsスタブ — SQLite集計関数 (kahanBabuskaNeumaierStep) が参照する */
double fabs(double x)
{
    return (x < 0.0) ? -x : x;
}

/* KernelAPI へのポインタ (crt0_c.c で定義) */
extern KernelAPI *kapi;

/* ======== MEMSYS5 固定プール (100KB) + canary ======== */
#define SQLITE_MEMSYS5_SIZE  (100 * 1024)
#define CANARY_VALUE 0xDEADBEEFUL

static u32 canary_before[4] = {
    CANARY_VALUE, CANARY_VALUE, CANARY_VALUE, CANARY_VALUE
};
static char sqlite_mem_pool[SQLITE_MEMSYS5_SIZE];
static u32 canary_after[4] = {
    CANARY_VALUE, CANARY_VALUE, CANARY_VALUE, CANARY_VALUE
};

/* MEMSYS5 canary 検証 */
int user_memsys5_check_canary(void)
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
int sqlite3DbIsNamed(void *db, int iDb, const char *zName)
{
    (void)db; (void)iDb; (void)zName;
    return (iDb == 0) ? 1 : 0;
}

/* ======== ファイルハンドル構造体 ======== */
typedef struct UserFile {
    sqlite3_file base;
    int fd;
} UserFile;

/* ======================================================================== */
/*  VFS ファイルメソッド                                                     */
/* ======================================================================== */

static int userClose(sqlite3_file *pFile)
{
    UserFile *p = (UserFile *)pFile;
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    return SQLITE_OK;
}

static int userRead(sqlite3_file *pFile, void *buf, int iAmt,
                    sqlite3_int64 iOfst)
{
    UserFile *p = (UserFile *)pFile;
    int n;

    lseek(p->fd, (int)iOfst, 0);  /* SEEK_SET */
    n = read(p->fd, buf, (unsigned int)iAmt);
    if (n < 0) return SQLITE_IOERR_READ;
    if (n < iAmt) {
        memset((char *)buf + n, 0, (unsigned int)(iAmt - n));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int userWrite(sqlite3_file *pFile, const void *buf, int iAmt,
                     sqlite3_int64 iOfst)
{
    UserFile *p = (UserFile *)pFile;
    int n;

    lseek(p->fd, (int)iOfst, 0);  /* SEEK_SET */
    n = write(p->fd, (char *)buf, (unsigned int)iAmt);
    if (n < iAmt) return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int userTruncate(sqlite3_file *pFile, sqlite3_int64 size)
{
    (void)pFile; (void)size;
    return SQLITE_OK;
}

static int userSync(sqlite3_file *pFile, int flags)
{
    (void)pFile; (void)flags;
    /* ユーザー空間にはsync APIがないためno-op */
    return SQLITE_OK;
}

static int userFileSize(sqlite3_file *pFile, sqlite3_int64 *pSize)
{
    UserFile *p = (UserFile *)pFile;
    struct stat st;
    if (fstat(p->fd, &st) < 0) {
        *pSize = 0;
        return SQLITE_IOERR;
    }
    *pSize = (sqlite3_int64)st.st_size;
    return SQLITE_OK;
}

/* ロック系: シングルタスクのため全て no-op */
static int userLock(sqlite3_file *f, int l)
    { (void)f; (void)l; return SQLITE_OK; }
static int userUnlock(sqlite3_file *f, int l)
    { (void)f; (void)l; return SQLITE_OK; }
static int userCheckReservedLock(sqlite3_file *f, int *pOut)
    { (void)f; *pOut = 0; return SQLITE_OK; }

static int userFileControl(sqlite3_file *f, int op, void *pArg)
{
    (void)f; (void)op; (void)pArg;
    return SQLITE_NOTFOUND;
}

static int userSectorSize(sqlite3_file *f)
    { (void)f; return 512; }
static int userDeviceCharacteristics(sqlite3_file *f)
    { (void)f; return 0; }

/* ファイルメソッドテーブル */
static const sqlite3_io_methods user_io_methods = {
    1,
    userClose,
    userRead,
    userWrite,
    userTruncate,
    userSync,
    userFileSize,
    userLock,
    userUnlock,
    userCheckReservedLock,
    userFileControl,
    userSectorSize,
    userDeviceCharacteristics,
    0, 0, 0, 0, 0
};

/* ======================================================================== */
/*  VFS メソッド                                                             */
/* ======================================================================== */

static int userVfsOpen(sqlite3_vfs *pVfs, const char *zName,
                       sqlite3_file *pFile, int flags, int *pOutFlags)
{
    UserFile *p = (UserFile *)pFile;
    int oflags = 0;

    (void)pVfs;

    p->base.pMethods = (sqlite3_io_methods *)0;
    p->fd = -1;

    if (zName == (const char *)0) return SQLITE_CANTOPEN;

    if (flags & SQLITE_OPEN_CREATE)   oflags |= O_CREAT;
    if (flags & SQLITE_OPEN_READWRITE) oflags |= O_RDWR;
    else                               oflags |= O_RDONLY;

    p->fd = open(zName, oflags);
    if (p->fd < 0) return SQLITE_CANTOPEN;

    p->base.pMethods = &user_io_methods;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int userVfsDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync)
{
    (void)pVfs; (void)dirSync;
    unlink((char *)zPath);
    return SQLITE_OK;
}

static int userVfsAccess(sqlite3_vfs *pVfs, const char *zPath,
                         int flags, int *pResOut)
{
    OS32_Stat ost;
    int rc;
    (void)pVfs; (void)flags;

    rc = kapi->sys_stat(zPath, &ost);
    *pResOut = (rc == 0) ? 1 : 0;
    return SQLITE_OK;
}

static int userVfsFullPathname(sqlite3_vfs *pVfs, const char *zName,
                               int nOut, char *zOut)
{
    int i;
    (void)pVfs;
    for (i = 0; i < nOut - 1 && zName[i]; i++) {
        zOut[i] = zName[i];
    }
    zOut[i] = '\0';
    return SQLITE_OK;
}

static int userVfsRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut)
{
    u32 val;
    int i;
    (void)pVfs;

    val = kapi->sys_time();
    for (i = 0; i < nByte; i++) {
        val = val * 1103515245 + 12345;
        zOut[i] = (char)(val >> 16);
    }
    return nByte;
}

static int userVfsSleep(sqlite3_vfs *pVfs, int microseconds)
{
    (void)pVfs;
    /* ユーザー空間からは簡易的にビジーウェイト */
    {
        volatile int i;
        int loops = microseconds / 10;
        for (i = 0; i < loops; i++) {}
    }
    return microseconds;
}

static int userVfsCurrentTime(sqlite3_vfs *pVfs, double *pTime)
{
    u32 unix_time;
    (void)pVfs;

    unix_time = kapi->sys_time();
    *pTime = 2440587.5 + (double)unix_time / 86400.0;
    return SQLITE_OK;
}

static int userVfsGetLastError(sqlite3_vfs *pVfs, int nBuf, char *zBuf)
{
    (void)pVfs;
    if (nBuf > 0) zBuf[0] = '\0';
    return 0;
}

/* ======================================================================== */
/*  VFS 登録                                                                 */
/* ======================================================================== */

static sqlite3_vfs user_vfs = {
    1,                      /* iVersion */
    sizeof(UserFile),       /* szOsFile */
    256,                    /* mxPathname */
    0,                      /* pNext */
    "os32user",             /* zName */
    0,                      /* pAppData */
    userVfsOpen,
    userVfsDelete,
    userVfsAccess,
    userVfsFullPathname,
    0, 0, 0, 0,            /* xDlOpen, xDlError, xDlSym, xDlClose */
    userVfsRandomness,
    userVfsSleep,
    userVfsCurrentTime,
    userVfsGetLastError,
    0,                      /* xCurrentTimeInt64 */
    0, 0, 0                 /* xSetSystemCall, xGetSystemCall, xNextSystemCall */
};

int sqlite3_os_init(void)
{
    return sqlite3_vfs_register(&user_vfs, 1);
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}

/* ======================================================================== */
/*  user_sqlite_init — テストプログラムから呼ばれる初期化                    */
/* ======================================================================== */
int user_sqlite_init(void)
{
    int rc;

    /* MEMSYS5 固定プール設定 */
    rc = sqlite3_config(SQLITE_CONFIG_HEAP,
                        sqlite_mem_pool,
                        SQLITE_MEMSYS5_SIZE,
                        64);
    if (rc != SQLITE_OK) return rc;

    /* SQLite 初期化 */
    rc = sqlite3_initialize();
    return rc;
}
