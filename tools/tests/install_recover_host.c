/* ========================================================================= */
/*  INSTALL_RECOVER_HOST.C — 票 S3-I §1h のホスト TDD                        */
/*                                                                           */
/*  実物だけを組む: 実 `userland/system/install_recover.inc` + 実             */
/*  `kapi/kapi_db.c` + 実 `lib/sqlite3/sqlite3.c` + 実 `os32_sqlite_vfs.c` +  */
/*  実 `fs/vfs_fd.c` + RAM バックエンド。模型は exec 側のポインタ検証、SHM の  */
/*  置き場、そして「その形でしか作れない障害」の注入 (stat の三値 / UNKNOWN /  */
/*  st_ino 共有 / st_dev、rename の 2 つの中途半端、read の負 / short write / */
/*  unlink / sync / db_close の失敗) だけ。                                   */
/*  ホストのファイルシステムには 1 バイトも触らない。                          */
/*                                                                           */
/*  実行: python3 -B tools/tests/test_install_recover.py                     */
/* ========================================================================= */

#define main f2a_main
#include "vfs_fd_sqlite_host.c"
#undef main

#include <stdarg.h>

/* ホストの size_t はカーネルの u32 と違うので libc の実体を使い回す。 */
#define __KSTRING_H
char *kstrncpy(char *dst, const char *src, u32 size)
{ str_cpy(dst, src, (int)size); return dst; }
char *kstrncat(char *dst, const char *src, u32 size)
{
    u32 used = (u32)strlen(dst);
    if (used + 1u < size) str_cpy(dst + used, src, (int)(size - used));
    return dst;
}
u32 kstrlen(const char *s) { return (u32)strlen(s); }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
int kstrncmp(const char *a, const char *b, u32 n) { return strncmp(a, b, n); }
void *kmemcpy(void *dst, const void *src, u32 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }

#include "../../lib/sqlite3/sqlite3.h"
#include "../../lib/sqlite3/os32_sqlite_vfs.c"
#include "sqlite_groups_backend.h"

volatile u32 tick_count;
u32 sys_time(void) { return 0; }
void kprintf(u8 color, const char *fmt, ...) { (void)color; (void)fmt; }
int vfs_sync(void) { probes++; return 0; }

static const char *host_resolve(const char *path)
{
    static char resolved[VFS_MAX_PATH];
    vfs_resolve_path(path, resolved, (int)sizeof(resolved));
    return resolved;
}
int vfs_rm(const char *path) { probes++; return fixture_rm(host_resolve(path)); }

/* ---- st_dev / st_ino の模型 ------------------------------------------- */
/* fs/vfs.c の vfs_mount_dev_of と同じ式: (dev_type << 8 | unit) + 1。
 * 種別は fs/vfs.h の VFS_DEV_HD = 0 / VFS_DEV_FD = 1。 */
#define HOST_DEV(t, u) ((u32)((((t) & 0xFF) << 8) | ((u) & 0xFF)) + 1u)
static u32 host_root_dev = HOST_DEV(1, 0);   /* 起動媒体 = fd0 */
static u32 host_hd0_dev  = HOST_DEV(0, 0);   /* 対象 = hd0 */
/* st_ino を共有させる注入 (同一 inode を 2 名が指す) */
static const char *inj_ino_share_a;
static const char *inj_ino_share_b;
static const char *inj_no_ino_path;          /* st_ino = 0 を返す名前 */

static u32 host_dev_of(const char *p)
{
    if (!strncmp(p, "/hd0", 4)) return host_hd0_dev;
    return host_root_dev;
}

static int host_is_dir(const char *p)
{
    return !strcmp(p, "/") || !strcmp(p, "/hd0") || !strcmp(p, "/hd0/etc") ||
           !strcmp(p, "/etc");
}

int vfs_stat(const char *path, OS32_Stat *st)
{
    FixtureFile *f;
    const char *r;
    probes++;
    r = host_resolve(path);
    if (host_is_dir(r)) {
        if (st) {
            memset(st, 0, sizeof(*st));
            st->st_mode = OS_S_IFDIR | OS_S_IRWXU;
            st->st_nlink = 2;
            st->st_dev = host_dev_of(r);
            st->st_ino = 900u + (u32)strlen(r);
        }
        return 0;
    }
    f = fixture_find(r, 0);
    if (!f) return OS32_ERR_NOTFOUND;
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_size = f->size;
        st->st_nlink = 1;
        st->st_dev = host_dev_of(r);
        st->st_ino = (u32)(f - fixture_files) + 1u;
        if (inj_ino_share_a && inj_ino_share_b &&
            (!strcmp(r, inj_ino_share_a) || !strcmp(r, inj_ino_share_b)))
            st->st_ino = 4242u;
        if (inj_no_ino_path && !strcmp(r, inj_no_ino_path)) st->st_ino = 0u;
    }
    return 0;
}

/* ---- exec 側の模型 (kapi_db.c が唯一使う口) ---------------------------- */
int ring3_user_range_ok(u32 p, u32 len)
{
    (void)len;
    return p != 0;                   /* CPL=0 の直呼びと同じ扱い */
}

#define SHM_CANARY 256
static unsigned char test_shm[DB_SHM_BLOCK_SIZE + SHM_CANARY];
#include "../../kapi/kapi_db.c"

/* ========================================================================= */
/*  RecoverOps の贋物 (障害注入)                                             */
/* ========================================================================= */

#define CHECK_STR(want) do { if (!strstr(cap_buf, (want))) { \
    fprintf(stderr, "FAIL %s:%d: message missing: %s\n--- output ---\n%s", \
            __func__, __LINE__, (want), cap_buf); exit(1); } } while (0)
#define CHECK_NOSTR(bad) do { if (strstr(cap_buf, (bad))) { \
    fprintf(stderr, "FAIL %s:%d: unexpected message: %s\n--- output ---\n%s", \
            __func__, __LINE__, (bad), cap_buf); exit(1); } } while (0)

static char cap_buf[32768];
static int cap_len;

static void rop_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    int n;
    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(cap_buf + cap_len, sizeof(cap_buf) - (size_t)cap_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        cap_len += n;
        if (cap_len >= (int)sizeof(cap_buf)) cap_len = (int)sizeof(cap_buf) - 1;
    }
}

/* 注入用のつまみ */
static int inj_stat_unknown_n;            /* UNKNOWN にする名前 (部分一致) */
static const char *inj_stat_unknown;
static const char *inj_open_fail;         /* open を失敗させる名前 */
static const char *inj_read_fail;         /* read が負を返す名前 */
static int inj_read_fail_after;
static const char *inj_write_short;       /* short write する名前 */
static int inj_write_short_after;
static const char *inj_write_garble;      /* 書けるが中身が違う名前 */
static int inj_write_garble_after;        /* 最初の N 回は素通しする */
static const char *inj_unlink_fail;       /* unlink を失敗させる名前 */
static int inj_rename;                    /* 0=普通 1=両名残る 2=.new だけ
                                             3=両名 ABSENT */
static int inj_sync_rc;
static int inj_db_close_fail;             /* != 0 = db_close が失敗 */
static const char *inj_db_close_path;     /* その対象 (部分一致、NULL = 全部) */
static const char *inj_db_open_fail;      /* db_open_existing を失敗させる名前 */
static const char *inj_db_prepare_fail;   /* その path の handle の prepare を失敗させる */
static int host_hd0_mounted = 1;
static int host_mount_calls;
static int host_mount_rc;
static const char *key_script = "Y";
static int key_pos;

/* fd -> path (注入の対象を決めるため) */
#define HOST_FDS 32
static char fd_path[HOST_FDS][VFS_MAX_PATH];
static char last_open_path[VFS_MAX_PATH];

/* 注入の対象は **完全一致** で決める。部分一致にすると
 * "/hd0/etc/settings.db" が .bak / .new / -journal にも当たってしまう。 */
static int hit(const char *pat, const char *path)
{ return pat && !strcmp(path, pat); }

static int rop_stat(const char *p, OS32_Stat *st)
{
    if (inj_stat_unknown && hit(inj_stat_unknown, p)) {
        if (inj_stat_unknown_n > 0) inj_stat_unknown_n--;
        else return OS32_ERR_IO;
    }
    return vfs_stat(p, st);
}

static int rop_open(const char *p, int mode)
{
    int fd;
    if (inj_open_fail && hit(inj_open_fail, p)) return OS32_ERR_IO;
    str_cpy(last_open_path, p, (int)sizeof(last_open_path));
    fd = vfs_open(p, mode);
    if (fd >= 0 && fd < HOST_FDS) str_cpy(fd_path[fd], p, VFS_MAX_PATH);
    return fd;
}

static int rop_read(int fd, void *b, u32 n)
{
    if (fd >= 0 && fd < HOST_FDS && inj_read_fail && hit(inj_read_fail, fd_path[fd])) {
        if (inj_read_fail_after > 0) inj_read_fail_after--;
        else return OS32_ERR_IO;
    }
    return vfs_read_fd(fd, b, n);
}

static int rop_write(int fd, const void *b, u32 n)
{
    if (fd >= 0 && fd < HOST_FDS && inj_write_short &&
        hit(inj_write_short, fd_path[fd])) {
        if (inj_write_short_after > 0) {
            inj_write_short_after--;
        } else {
            u32 half = n > 1u ? n / 2u : 0u;
            vfs_write_fd(fd, b, half);
            return (int)half;              /* short write */
        }
    }
    if (fd >= 0 && fd < HOST_FDS && inj_write_garble &&
        hit(inj_write_garble, fd_path[fd])) {
        if (inj_write_garble_after > 0) {
            inj_write_garble_after--;
        } else {
            /* 長さは合うが中身が違う (バイト比較だけが気付ける形) */
            static unsigned char junk[16384];
            memset(junk, 0x5A, n > sizeof(junk) ? sizeof(junk) : n);
            return vfs_write_fd(fd, junk, n);
        }
    }
    return vfs_write_fd(fd, b, n);
}

static void rop_close(int fd)
{
    if (fd >= 0 && fd < HOST_FDS) fd_path[fd][0] = '\0';
    vfs_close(fd);
}

static int rop_unlink(const char *p)
{
    if (inj_unlink_fail && hit(inj_unlink_fail, p)) return OS32_ERR_IO;
    return vfs_rm(p);
}

/* ext2 の rename は「新名追加 → 旧名削除」で、途中で失敗しうる (票 §1f の 6)。
 *   1 = 新名追加は成功したが旧名が消えない → 両名 PRESENT
 *   2 = 置換先を unlink したあと新名追加に失敗 → `.new` だけ PRESENT       */
static int rop_rename(const char *o, const char *n)
{
    FixtureFile *src = fixture_find(host_resolve(o), 0);
    FixtureFile *dst;
    if (!src) return OS32_ERR_NOTFOUND;
    if (inj_rename == 2 || inj_rename == 3) {
        dst = fixture_find(host_resolve(n), 0);
        if (dst) dst->exists = 0;
        /* 3 = 置換先も旧名も残らなかった (両名 ABSENT) */
        if (inj_rename == 3) src->exists = 0;
        return OS32_ERR_IO;
    }
    dst = fixture_find(host_resolve(n), 1);
    if (!dst) return OS32_ERR_NOSPC;
    memcpy(dst->data, src->data, src->size);
    dst->size = src->size;
    if (inj_rename == 1) return OS32_ERR_IO;       /* 旧名が残る */
    src->exists = 0;
    return 0;
}

static int rop_mount(const char *prefix, const char *dev, const char *fs)
{
    (void)prefix; (void)dev; (void)fs;
    host_mount_calls++;
    if (host_mount_rc == 0) host_hd0_mounted = 1;
    return host_mount_rc;
}
static int rop_is_mounted(const char *prefix)
{ (void)prefix; return host_hd0_mounted; }
static int rop_sync(void) { return inj_sync_rc; }

/* kapi_db.c の DbSlot は path を持たないので、試験側で handle -> path を控える */
static char db_path_of[DB_MAX_CONNECTIONS][VFS_MAX_PATH];

static int rop_db_close_want;      /* db_close の失敗判定を close 前に取る */

static int db_close_should_fail(int h)
{
    if (!inj_db_close_fail) return 0;
    if (!inj_db_close_path) return 1;
    if (h < 0 || h >= DB_MAX_CONNECTIONS) return 0;
    return !strcmp(db_path_of[h], inj_db_close_path);
}

static int rop_db_open_existing(const char *p, int w)
{
    int h;
    rop_db_close_want = 0;
    if (inj_db_open_fail && hit(inj_db_open_fail, p)) {
        /* 実在しない名前を引かせて本物の CANTOPEN を作る */
        return kapi_db_open_existing("/no/such/file.db", w);
    }
    h = kapi_db_open_existing(p, w);
    if (h >= 0 && h < DB_MAX_CONNECTIONS) str_cpy(db_path_of[h], p, VFS_MAX_PATH);
    return h;
}
static int rop_db_prepare_only(int h, const char *s)
{
    if (inj_db_prepare_fail && h >= 0 && h < DB_MAX_CONNECTIONS &&
        !strcmp(db_path_of[h], inj_db_prepare_fail)) {
        /* 実在しない表を引かせて本物の SQLite の失敗と診断を作る */
        return kapi_db_prepare_only(h, "SELECT 1 FROM no_such_table_for_tdd");
    }
    return kapi_db_prepare_only(h, s);
}
static int rop_db_step(int h)      { return kapi_db_step(h); }
static int rop_db_finalize(int h)  { return kapi_db_finalize(h); }
static int rop_db_close(int h)
{
    int rc;
    rop_db_close_want = db_close_should_fail(h);
    rc = kapi_db_close(h);           /* 実際には閉じる (資源を漏らさない) */
    return rop_db_close_want ? -1 : rc;
}
static int rop_db_error_code(int h)
{
    if (rop_db_close_want) return SQLITE_IOERR;
    return kapi_db_error_code(h);
}
static unsigned char *rop_shm(void) { return test_shm; }
static int rop_getkey(void)
{
    if (key_script && key_script[key_pos]) return key_script[key_pos++];
    return 0x1B;
}

#include "../../userland/system/install_recover.inc"

static const RecoverOps host_ops = {
    rop_stat, rop_open, rop_read, rop_write, rop_close, rop_unlink,
    rop_rename, rop_mount, rop_is_mounted, rop_sync,
    rop_db_open_existing, rop_db_prepare_only, rop_db_step, rop_db_finalize,
    rop_db_close, rop_db_error_code, rop_shm, rop_kprintf, rop_getkey
};

/* ========================================================================= */
/*  fixture の小物                                                           */
/* ========================================================================= */

static int fx_exists(const char *p)
{ return fixture_find(host_resolve(p), 0) != NULL; }

static u32 fx_size(const char *p)
{
    FixtureFile *f = fixture_find(host_resolve(p), 0);
    return f ? f->size : 0u;
}

static void fx_put(const char *p, const char *data, u32 n)
{ CHECK(fixture_create(NULL, host_resolve(p), data, n) == VFS_OK); }

static void fx_puts(const char *p, const char *text)
{ fx_put(p, text, (u32)strlen(text)); }

static int fx_same(const char *a, const char *b)
{
    FixtureFile *x = fixture_find(host_resolve(a), 0);
    FixtureFile *y = fixture_find(host_resolve(b), 0);
    if (!x || !y) return 0;
    if (x->size != y->size) return 0;
    return memcmp(x->data, y->data, x->size) == 0;
}

/* スナップショット (「1 バイトも変わらない」の確認用) */
typedef struct { int exists; u32 size; unsigned char data[FIXTURE_BYTES]; } Snap;
static void snap_take(const char *p, Snap *s)
{
    FixtureFile *f = fixture_find(host_resolve(p), 0);
    s->exists = f ? 1 : 0;
    s->size = f ? f->size : 0u;
    if (f) memcpy(s->data, f->data, f->size);
}
static int snap_same(const char *p, const Snap *s)
{
    FixtureFile *f = fixture_find(host_resolve(p), 0);
    if (!f) return !s->exists;
    if (!s->exists) return 0;
    if (f->size != s->size) return 0;
    return memcmp(f->data, s->data, f->size) == 0;
}
#define CHECK_SNAP(p, s) do { if (!snap_same((p), &(s))) { \
    fprintf(stderr, "FAIL %s:%d: %s changed\n", __func__, __LINE__, (p)); \
    exit(1); } } while (0)

static Snap snap_db, snap_journal, snap_bak, snap_bakj, snap_master;

static void snap_all(void)
{
    snap_take(RC_MASTER, &snap_master);
    snap_take(rc_path[RC_N_DB], &snap_db);
    snap_take(rc_path[RC_N_JOURNAL], &snap_journal);
    snap_take(rc_path[RC_N_BAK], &snap_bak);
    snap_take(rc_path[RC_N_BAKJ], &snap_bakj);
}

/* 印の中身を読む (試験側の目) */
static void mark_of(RcMark *out)
{
    CHECK(rc_mark_read(&host_ops, out) == 0);
}
static int mark_phase(void)
{
    RcMark m;
    if (!fx_exists(rc_path[RC_N_STATE])) return RC_PH_NONE;
    if (rc_mark_read(&host_ops, &m) != 0) return -1;
    return m.phase;
}

/* ---- 使い捨てのマスタ DB ---------------------------------------------- */
static void make_settings_db(const char *path, int keys, int version)
{
    char sql[256];
    int h, i;
    h = kapi_db_open(path);
    CHECK(h >= 0);
    CHECK(kapi_db_exec(h, "CREATE TABLE meta(schema_version INTEGER)") == 0);
    sprintf(sql, "INSERT INTO meta VALUES(%d)", version);
    CHECK(kapi_db_exec(h, sql) == 0);
    CHECK(kapi_db_exec(h,
        "CREATE TABLE settings(scope TEXT NOT NULL, key TEXT NOT NULL,"
        " type INTEGER NOT NULL, ival INTEGER, tval TEXT, bval BLOB,"
        " PRIMARY KEY(scope,key))") == 0);
    CHECK(kapi_db_exec(h, "BEGIN") == 0);
    for (i = 0; i < keys; i++) {
        sprintf(sql, "INSERT INTO settings VALUES('gshell','k%03d',0,%d,NULL,NULL)",
                i, i);
        CHECK(kapi_db_exec(h, sql) == 0);
    }
    CHECK(kapi_db_exec(h, "COMMIT") == 0);
    CHECK(kapi_db_close(h) == 0);
    CHECK(!fx_exists("/etc/settings.db-journal"));
}

#define MASTER_KEYS 12

static void reset_all(void)
{
    int i;
    memset(test_shm, 0, sizeof(test_shm));
    memset(db_slots, 0, sizeof(db_slots));
    memset(db_open_fail, 0, sizeof(db_open_fail));
    memset(fixture_files, 0, sizeof(fixture_files));
    memset(fd_path, 0, sizeof(fd_path));
    memset(db_path_of, 0, sizeof(db_path_of));
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) open_files[i].in_use = 0;
    cap_buf[0] = '\0';
    cap_len = 0;
    inj_stat_unknown = NULL;
    inj_stat_unknown_n = 0;
    inj_open_fail = NULL;
    inj_read_fail = NULL;
    inj_read_fail_after = 0;
    inj_write_short = NULL;
    inj_write_short_after = 0;
    inj_write_garble = NULL;
    inj_write_garble_after = 0;
    inj_unlink_fail = NULL;
    inj_rename = 0;
    inj_sync_rc = 0;
    inj_db_close_fail = 0;
    inj_db_close_path = NULL;
    inj_db_open_fail = NULL;
    inj_db_prepare_fail = NULL;
    inj_ino_share_a = NULL;
    inj_ino_share_b = NULL;
    inj_no_ino_path = NULL;
    rop_db_close_want = 0;
    host_root_dev = HOST_DEV(1, 0);
    host_hd0_dev = HOST_DEV(0, 0);
    host_hd0_mounted = 1;
    host_mount_calls = 0;
    host_mount_rc = 0;
    key_script = "Y";
    key_pos = 0;
    resolve_owner = current_owner = 2;
    resolve_cwd = "";
    fixture_init();
    make_settings_db(RC_MASTER, MASTER_KEYS, 1);
}

/* 現行の HDD 側 DB を「壊れた 1406B のファイル」として置く (受入 I2 と同じ形) */
static void put_broken_db(void)
{
    static char junk[1406];
    memset(junk, 'x', sizeof(junk));
    fx_put(rc_path[RC_N_DB], junk, (u32)sizeof(junk));
}

static void clear_output(void) { cap_buf[0] = '\0'; cap_len = 0; }

static int run_recover(void)
{
    key_pos = 0;
    return recover_settings_main(&host_ops, "hd0", 0);
}
static int run_revert(void)
{
    key_pos = 0;
    return recover_settings_main(&host_ops, "hd0", 1);
}

/* ========================================================================= */
/*  1a — 起動媒体と対象デバイス                                              */
/* ========================================================================= */

static void case_media(void)
{
    Snap s;

    /* drive は hd0 だけ */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    CHECK(recover_settings_main(&host_ops, "hd1", 0) == 1);
    CHECK_STR("unsupported drive");
    CHECK_SNAP(rc_path[RC_N_DB], s);

    /* HDD ブートからは動かない (受入 I1)。DB は 1 バイトも変わらない。 */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    host_root_dev = HOST_DEV(0, 0);          /* root が hd0 = HDD ブート */
    CHECK(run_recover() == 1);
    CHECK_STR("recover-settings must run from the install floppy");
    CHECK_SNAP(rc_path[RC_N_DB], s);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(!fx_exists(rc_path[RC_N_STATE]));

    /* revert も同じ門で止まる */
    reset_all();
    host_root_dev = HOST_DEV(0, 0);
    CHECK(run_revert() == 1);
    CHECK_STR("must run from the install floppy");

    /* st_dev が 0 (不明) でも拒否 */
    reset_all();
    host_root_dev = 0u;
    CHECK(run_recover() == 1);
    CHECK_STR("must run from the install floppy");

    /* /hd0 に hd1 が刺さっている */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    host_hd0_dev = HOST_DEV(0, 1);
    CHECK(run_recover() == 1);
    CHECK_STR("target /hd0 is not hd0");
    CHECK_SNAP(rc_path[RC_N_DB], s);

    /* /hd0 が FDD に化けている (種別違い) */
    reset_all();
    host_hd0_dev = HOST_DEV(1, 0);
    CHECK(run_recover() == 1);
    CHECK_STR("target /hd0 is not hd0");

    /* 未マウントなら回復専用に mount する */
    reset_all();
    put_broken_db();
    host_hd0_mounted = 0;
    CHECK(run_recover() == 0);
    CHECK(host_mount_calls == 1);
    CHECK_STR("recovered: schema_version 1");

    /* mount に失敗したら何もしない */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    host_hd0_mounted = 0;
    host_mount_rc = OS32_ERR_IO;
    CHECK(run_recover() == 1);
    CHECK_STR("cannot mount /hd0");
    CHECK_SNAP(rc_path[RC_N_DB], s);
}

/* ========================================================================= */
/*  1b — 9 名の三値 / inode / 印の門 / .new 残骸                             */
/* ========================================================================= */

static void case_scan(void)
{
    Snap s;

    /* stat の UNKNOWN は「無い」に丸めない */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    inj_stat_unknown = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("stat failed (settings.db.bak)");
    CHECK_SNAP(rc_path[RC_N_DB], s);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));

    /* 本体の stat が UNKNOWN でも同じ */
    reset_all();
    put_broken_db();
    inj_stat_unknown = rc_path[RC_N_DB];
    inj_stat_unknown_n = 0;
    CHECK(run_recover() == 1);
    CHECK_STR("stat failed (settings.db)");

    /* inode の共有 (rename 事故の残り) は触らずに停止 */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_BAK], "old", 3);
    snap_all();
    inj_ino_share_a = rc_path[RC_N_DB];
    inj_ino_share_b = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("share an inode - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_BAK], snap_bak);

    /* inode を供給しない FS は判定不能で停止 */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    inj_no_ino_path = rc_path[RC_N_DB];
    CHECK(run_recover() == 1);
    CHECK_STR("has no inode - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_DB], s);

    /* `.new` / `.new-journal` の残骸では recover が止まる (他人の生成物) */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_NEW], "stale", 5);
    snap_take(rc_path[RC_N_NEW], &s);
    CHECK(run_recover() == 1);
    CHECK_STR("stale settings.db.new present - inspect and remove manually");
    CHECK_SNAP(rc_path[RC_N_NEW], s);

    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_NEWJ], "stale", 5);
    CHECK(run_recover() == 1);
    CHECK_STR("stale settings.db.new-journal present");
}

/* ========================================================================= */
/*  1b — 印の phase による門 (白リスト: done か印無し)                        */
/* ========================================================================= */

static void case_gate(void)
{
    int p;
    static const int blocked[] = { RC_PH_BACKUP, RC_PH_SWITCHING, RC_PH_SWITCHED,
                                   RC_PH_FAILED, RC_PH_REVERTING };
    Snap s;
    unsigned i;

    for (i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++) {
        reset_all();
        put_broken_db();
        CHECK(rc_mark_write(&host_ops, blocked[i], 1, 0, 1406u) == 0);
        snap_take(rc_path[RC_N_DB], &s);
        clear_output();
        CHECK(run_recover() == 1);
        CHECK_STR("previous recovery incomplete (phase=");
        CHECK_STR("run install --revert-settings hd0 first");
        CHECK_SNAP(rc_path[RC_N_DB], s);
        CHECK(!fx_exists(rc_path[RC_N_BAK]));
    }

    /* phase=done は通る (1 世代置換) */
    reset_all();
    put_broken_db();
    CHECK(rc_mark_write(&host_ops, RC_PH_DONE, 1, 0, 1406u) == 0);
    clear_output();
    CHECK(run_recover() == 0);
    CHECK_STR("recovered: schema_version 1");
    p = mark_phase();
    CHECK(p == RC_PH_DONE);

    /* 印が壊れていたら停止 (消さない) */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_STATE], "garbage", 7);
    snap_take(rc_path[RC_N_STATE], &s);
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state unreadable - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_STATE], s);
    CHECK(run_revert() == 1);
    CHECK_STR("recover-state unreadable - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_STATE], s);

    /* 印の読み書きは往復する (phase / orig / journal / size) */
    reset_all();
    {
        RcMark m;
        CHECK(rc_mark_write(&host_ops, RC_PH_SWITCHING, 0, 1, 4096u) == 0);
        mark_of(&m);
        CHECK(m.present == 1 && m.phase == RC_PH_SWITCHING);
        CHECK(m.orig_present == 0 && m.journal_present == 1 && m.size == 4096u);
        CHECK(rc_mark_write(&host_ops, RC_PH_DONE, 1, 0, 0u) == 0);
        mark_of(&m);
        CHECK(m.phase == RC_PH_DONE && m.orig_present == 1 &&
              m.journal_present == 0 && m.size == 0u);
    }

    /* **B5**: 規定の 1 行 + LF ちょうどでなければ受理しない。
     * `phase=done\ngarbage` を `done` として通すと門が開いてしまう。 */
    reset_all();
    put_broken_db();
    fx_puts(rc_path[RC_N_STATE],
            "phase=done orig=present journal=absent size=1406\ngarbage\n");
    snap_take(rc_path[RC_N_DB], &s);
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state unreadable - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_DB], s);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));

    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_STATE],
           "phase=done orig=present journal=absent size=1406\n\0extra", 51);
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state unreadable - needs manual recovery");

    reset_all();
    put_broken_db();
    fx_puts(rc_path[RC_N_STATE],
            "phase=done orig=present journal=absent size=1406");  /* LF 無し */
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state unreadable - needs manual recovery");

    /* 各 phase 更新 (backup / switching / switched / done) の失敗。
     * 印は O_TRUNC で直接書くので、更新に失敗すると前の phase も残らない
     * ことがある = 次回は「門」か「unreadable」のどちらかで**必ず止まる**。
     * 固定するのはその安全側の性質と「退避対は保護される」こと。 */
    {
        int k;
        for (k = 0; k < 4; k++) {
            reset_all();
            put_broken_db();
            snap_all();
            inj_write_garble = rc_path[RC_N_STATE];
            inj_write_garble_after = k;
            clear_output();
            CHECK(run_recover() == 1);
            CHECK_STR("recover-state write failed");
            CHECK(mark_phase() != RC_PH_DONE);   /* done に化けない */
            if (k == 0) {
                /* backup の印が書けない = まだ破壊段に入っていない */
                CHECK_SNAP(rc_path[RC_N_DB], snap_db);
                CHECK(!fx_exists(rc_path[RC_N_BAK]));
                CHECK(!fx_exists(rc_path[RC_N_NEW]));
                CHECK(!fx_exists(rc_path[RC_N_STATE]));
            } else {
                /* 退避対は残り、次の recover は通常経路に進まない */
                CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));
                inj_write_garble = NULL;
                clear_output();
                CHECK(run_recover() == 1);
                CHECK(strstr(cap_buf, "previous recovery incomplete") ||
                      strstr(cap_buf, "recover-state unreadable"));
                CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));
            }
        }
    }

    /* short write でも同じ (途中まで書けた印を「読めた」ことにしない) */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_short = rc_path[RC_N_STATE];
    inj_write_short_after = 1;           /* backup は通し、switching で切る */
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state write failed");
    CHECK(mark_phase() != RC_PH_DONE);
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));

    /* 印の書き込み / 読み戻しが失敗したら「書けた」ことにしない */
    reset_all();
    inj_open_fail = rc_path[RC_N_STATE];
    CHECK(rc_mark_write(&host_ops, RC_PH_BACKUP, 1, 0, 8u) != 0);
    inj_open_fail = NULL;
    inj_write_short = rc_path[RC_N_STATE];
    CHECK(rc_mark_write(&host_ops, RC_PH_BACKUP, 1, 0, 8u) != 0);
    inj_write_short = NULL;
    inj_write_garble = rc_path[RC_N_STATE];
    CHECK(rc_mark_write(&host_ops, RC_PH_BACKUP, 1, 0, 8u) != 0);
    inj_write_garble = NULL;
}

/* ========================================================================= */
/*  1c — マスタの検査                                                        */
/* ========================================================================= */

static void case_master(void)
{
    Snap s;

    /* マスタが無い */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    CHECK(vfs_rm(RC_MASTER) == VFS_OK);
    CHECK(run_recover() == 1);
    CHECK_STR("master unreadable");
    CHECK_SNAP(rc_path[RC_N_DB], s);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));

    /* マスタが SQLite ではない */
    reset_all();
    put_broken_db();
    fx_put(RC_MASTER, "not a database", 14);
    CHECK(run_recover() == 1);
    CHECK_STR("master unreadable");

    /* meta が無い (schema 検査で落ちる) */
    reset_all();
    put_broken_db();
    CHECK(vfs_rm(RC_MASTER) == VFS_OK);
    {
        int h = kapi_db_open(RC_MASTER);
        CHECK(h >= 0);
        CHECK(kapi_db_exec(h, "CREATE TABLE settings(scope,key,type,ival,tval,bval)") == 0);
        CHECK(kapi_db_close(h) == 0);
    }
    CHECK(run_recover() == 1);
    CHECK_STR("master unreadable");

    /* meta が 2 行 (正常行との混在も CORRUPT) */
    reset_all();
    {
        int h = kapi_db_open_existing(RC_MASTER, 1);
        CHECK(h >= 0);
        CHECK(kapi_db_exec(h, "INSERT INTO meta VALUES(1)") == 0);
        CHECK(kapi_db_close(h) == 0);
    }
    put_broken_db();
    CHECK(run_recover() == 1);
    CHECK_STR("master unreadable");

    /* 検査 close の失敗はマスタでも報告する */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &s);
    inj_db_close_fail = 1;
    inj_db_close_path = RC_MASTER;
    CHECK(run_recover() == 1);
    CHECK_STR("master close failed");
    CHECK_SNAP(rc_path[RC_N_DB], s);

    /* 保証の上限を表示に書く (integrity_check は OMIT) */
    reset_all();
    put_broken_db();
    key_script = "N";
    CHECK(run_recover() == 0);
    CHECK_STR("integrity_check is not available");
}

/* ========================================================================= */
/*  1d — 承認 (受入 I5)                                                      */
/* ========================================================================= */

static void case_approve(void)
{
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "jjjj", 4);
    snap_all();
    key_script = "N";
    CHECK(run_recover() == 0);
    CHECK_STR("Replace /hd0/etc/settings.db with master (schema 1, 12 keys)?");
    CHECK_STR("Current db(+journal) will be copied to"
              " settings.db.bak(+.bak-journal) [Y/N]");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_JOURNAL], snap_journal);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(!fx_exists(rc_path[RC_N_BAKJ]));
    CHECK(!fx_exists(rc_path[RC_N_STATE]));
    CHECK(!fx_exists(rc_path[RC_N_NEW]));

    /* ESC / 他のキーでも同じ */
    reset_all();
    put_broken_db();
    snap_take(rc_path[RC_N_DB], &snap_db);
    key_script = "";
    CHECK(run_recover() == 0);
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
}

/* ========================================================================= */
/*  1e/1f — 正常系 (受入 I2 / I4)                                            */
/* ========================================================================= */

static void case_happy(void)
{
    RcMark m;

    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hotjournal", 10);
    snap_all();

    CHECK(run_recover() == 0);
    CHECK_STR("hot journal present - will be backed up as .bak-journal"
              " and removed before switch");
    CHECK_STR("settings.db: present 1406 B");
    CHECK_STR("recovered: schema_version 1, 12 keys, sync=0, reopen=ok, close=ok");

    /* 退避の対 = 元のバイト列。元の journal は消えている。 */
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));
    CHECK(snap_same(rc_path[RC_N_BAKJ], &snap_journal));
    CHECK(!fx_exists(rc_path[RC_N_JOURNAL]));
    /* 本体はマスタと同一、マスタは不変 */
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));
    CHECK_SNAP(RC_MASTER, snap_master);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    mark_of(&m);
    CHECK(m.phase == RC_PH_DONE && m.orig_present == 1 &&
          m.journal_present == 1 && m.size == 1406u);

    /* 本体が欠損している場合 (受入 I3): .bak は作られない */
    reset_all();
    CHECK(run_recover() == 0);
    CHECK_STR("settings.db: missing");
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));
    mark_of(&m);
    CHECK(m.phase == RC_PH_DONE && m.orig_present == 0 && m.journal_present == 0);

    /* 欠損 + 孤立 journal: journal だけが対の一部として `.bak-journal` へ */
    reset_all();
    fx_put(rc_path[RC_N_JOURNAL], "orphan", 6);
    snap_take(rc_path[RC_N_JOURNAL], &snap_journal);
    CHECK(run_recover() == 0);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(snap_same(rc_path[RC_N_BAKJ], &snap_journal));
    CHECK(!fx_exists(rc_path[RC_N_JOURNAL]));
    mark_of(&m);
    CHECK(m.orig_present == 0 && m.journal_present == 1);

    /* 欠損 + 旧 .bak: 承認済み 1 世代なので旧 .bak は消える */
    reset_all();
    fx_put(rc_path[RC_N_BAK], "previous", 8);
    fx_put(rc_path[RC_N_BAKJ], "previousj", 9);
    CHECK(run_recover() == 0);
    CHECK_STR("settings.db.bak: will replace");
    CHECK(!fx_exists(rc_path[RC_N_BAK]));      /* 本体が無いので作り直されない */
    CHECK(!fx_exists(rc_path[RC_N_BAKJ]));
}

/* ========================================================================= */
/*  1e — 退避の各失敗 (元の対は 1 バイトも変わらない)                         */
/* ========================================================================= */

static void case_backup_fail(void)
{
    /* 旧 .bak が消せない */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_BAK], "old", 3);
    snap_all();
    inj_unlink_fail = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("cannot remove old backup");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_BAK], snap_bak);
    CHECK(!fx_exists(rc_path[RC_N_STATE]));

    /* read が負を返す (現行 copy_file は EOF に丸めるので回復では失敗にする) */
    reset_all();
    put_broken_db();
    snap_all();
    inj_read_fail = rc_path[RC_N_DB];
    CHECK(run_recover() == 1);
    CHECK_STR("backup failed (settings.db.bak)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));      /* 自分が作ったものだけ消える */
    CHECK(!fx_exists(rc_path[RC_N_STATE]));

    /* short write */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_short = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("backup failed (settings.db.bak)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));

    /* 長さは合うが中身が違う (バイト比較だけが気付く) */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_garble = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("backup failed (settings.db.bak)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));

    /* journal の退避に失敗したら、作った .bak も一緒に消す */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_write_garble = rc_path[RC_N_BAKJ];
    CHECK(run_recover() == 1);
    CHECK_STR("backup failed (settings.db.bak-journal)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_JOURNAL], snap_journal);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(!fx_exists(rc_path[RC_N_BAKJ]));
    CHECK(!fx_exists(rc_path[RC_N_STATE]));

    /* 清掃の unlink 自体が失敗しても、元の対には手を出さない */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_garble = rc_path[RC_N_BAK];
    inj_unlink_fail = rc_path[RC_N_BAK];
    CHECK(run_recover() == 1);
    CHECK_STR("backup failed (settings.db.bak)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(fx_exists(rc_path[RC_N_BAK]));      /* 消せなかった残骸は残る */
    CHECK(!fx_exists(rc_path[RC_N_STATE]));
    /* 次回は同一性検査を通ったうえで 1 世代置換に進める (印が無い) */
    inj_write_garble = NULL;
    inj_unlink_fail = NULL;
    clear_output();
    CHECK(run_recover() == 0);
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));

    /* 印が書けない → 作った .bak* と印を消して、元の対は無傷 */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_write_garble = rc_path[RC_N_STATE];
    CHECK(run_recover() == 1);
    CHECK_STR("recover-state write failed");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_JOURNAL], snap_journal);
    CHECK(!fx_exists(rc_path[RC_N_BAK]));
    CHECK(!fx_exists(rc_path[RC_N_BAKJ]));
    CHECK(!fx_exists(rc_path[RC_N_STATE]));
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
}

/* ========================================================================= */
/*  1f — .new の各失敗                                                       */
/* ========================================================================= */

static void case_newfail(void)
{
    RcMark m;

    /* コピーが short write */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_short = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("copy failed (settings.db.new)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    /* .bak と印は残してよい (次回は 1 世代置換ではなく門で止まる) */
    CHECK(fx_exists(rc_path[RC_N_BAK]));
    CHECK(mark_phase() == RC_PH_BACKUP);

    /* 長さは合うが中身が違う */
    reset_all();
    put_broken_db();
    snap_all();
    inj_write_garble = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("verify failed (settings.db.new)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));

    /* .new を DB として開けない */
    reset_all();
    put_broken_db();
    snap_all();
    inj_db_open_fail = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("verify failed (settings.db.new)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));

    /* 検証 close の失敗: .new を消さず、再起動を前提に案内する */
    reset_all();
    put_broken_db();
    snap_all();
    inj_db_close_fail = 1;
    inj_db_close_path = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("verify close failed");
    CHECK_STR("settings.db.new kept; original untouched.");
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0,"
              " rm /hd0/etc/settings.db.new, install --recover-settings hd0");
    CHECK(fx_exists(rc_path[RC_N_NEW]));
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    mark_of(&m);
    CHECK(m.phase == RC_PH_BACKUP);

    /* 次回の recover は門で止まる (phase=backup)。`.new` 残骸より門が先。 */
    inj_db_close_fail = 0;
    inj_db_close_path = NULL;
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("previous recovery incomplete (phase=backup)");
    CHECK(fx_exists(rc_path[RC_N_NEW]));

    /* **実装レビュー往復 1 の B1**: `.new` の DB 検証と close が**同時に**
     * 失敗しても `.new` を消さない (隔離接続が掴む inode)。 */
    reset_all();
    put_broken_db();
    snap_all();
    inj_db_close_fail = 1;
    inj_db_close_path = rc_path[RC_N_NEW];
    inj_db_prepare_fail = rc_path[RC_N_NEW];   /* DB 検査も落とす */
    CHECK(run_recover() == 1);
    CHECK_STR("verify close failed");
    CHECK_STR("settings.db.new kept; original untouched.");
    CHECK_STR("verify failed (settings.db.new) as well");
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0,"
              " rm /hd0/etc/settings.db.new, install --recover-settings hd0");
    CHECK(fx_exists(rc_path[RC_N_NEW]));       /* 検証も失敗しているが消さない */
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(mark_phase() == RC_PH_BACKUP);

    /* 検査だけが失敗し close は通るなら `.new` は消してよい */
    reset_all();
    put_broken_db();
    snap_all();
    inj_db_prepare_fail = rc_path[RC_N_NEW];
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("verify failed (settings.db.new)");
    CHECK_NOSTR("verify close failed");
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);

    /* コピーの書き側が開けない (rc_copy_file の RC_CP_DST) */
    reset_all();
    put_broken_db();
    snap_all();
    inj_open_fail = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("copy failed (settings.db.new)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));

    /* コピーは通るが**比較のための read だけ**が失敗する */
    reset_all();
    put_broken_db();
    snap_all();
    inj_read_fail = RC_MASTER;
    inj_read_fail_after = 2;      /* コピーの read 2 回 (本体 + EOF) は通す */
    CHECK(run_recover() == 1);
    CHECK_STR("verify failed (settings.db.new)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
}

/* ========================================================================= */
/*  1f — hot journal の除去と切替                                            */
/* ========================================================================= */

static void case_switch(void)
{
    RcMark m;

    /* journal が消せない = 破壊段に入れない */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_unlink_fail = rc_path[RC_N_JOURNAL];
    CHECK(run_recover() == 1);
    CHECK_STR("cannot remove hot journal");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK_SNAP(rc_path[RC_N_JOURNAL], snap_journal);
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    CHECK(mark_phase() == RC_PH_BACKUP);
    /* 写しは対で残っている */
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));
    CHECK(snap_same(rc_path[RC_N_BAKJ], &snap_journal));

    /* 両名残存 → 消さずに停止 */
    reset_all();
    put_broken_db();
    snap_all();
    inj_rename = 1;
    CHECK(run_recover() == 1);
    CHECK_STR("needs manual recovery: settings.db and settings.db.new"
              " both present");
    CHECK(fx_exists(rc_path[RC_N_DB]));
    CHECK(fx_exists(rc_path[RC_N_NEW]));
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));    /* 元の内容は .bak に */
    CHECK(mark_phase() == RC_PH_SWITCHING);

    /* 置換先 unlink 後の新名追加失敗 → 写しから本体を戻す */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_rename = 2;
    CHECK(run_recover() == 1);
    CHECK_STR("restored from settings.db.bak");
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(snap_same(rc_path[RC_N_JOURNAL], &snap_journal));
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    mark_of(&m);
    CHECK(m.phase == RC_PH_DONE);

    /* その復元自体が失敗したら phase=failed で止める */
    reset_all();
    put_broken_db();
    snap_all();
    inj_rename = 2;
    inj_write_garble = rc_path[RC_N_DB];
    CHECK(run_recover() == 1);
    CHECK_STR("restore failed - run install --revert-settings hd0 after reboot");
    CHECK(mark_phase() == RC_PH_FAILED);
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));    /* 退避対は保護される */

    /* 両名 ABSENT (rename が置換先も新名も残さなかった) でも写しから対で戻す */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_rename = 3;               /* 置換先 unlink + 旧名も消える */
    CHECK(run_recover() == 1);
    CHECK_STR("restored from settings.db.bak");
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(snap_same(rc_path[RC_N_JOURNAL], &snap_journal));
    CHECK(!fx_exists(rc_path[RC_N_NEW]));
    CHECK(mark_phase() == RC_PH_DONE);

    /* 復元の journal 側だけが失敗しても phase=failed で止める */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_all();
    inj_rename = 2;
    inj_write_garble = rc_path[RC_N_JOURNAL];
    CHECK(run_recover() == 1);
    CHECK_STR("restore failed - run install --revert-settings hd0 after reboot");
    CHECK(mark_phase() == RC_PH_FAILED);
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));
    CHECK(snap_same(rc_path[RC_N_BAKJ], &snap_journal));

    /* rename 後の stat が UNKNOWN なら何も消さない */
    reset_all();
    put_broken_db();
    snap_all();
    inj_rename = 1;
    inj_stat_unknown = rc_path[RC_N_NEW];
    inj_stat_unknown_n = 2;      /* 走査と長さ検証の分は通す */
    CHECK(run_recover() == 1);
    CHECK_STR("needs manual recovery: cannot stat after rename");
    CHECK(fx_exists(rc_path[RC_N_NEW]));
}

/* ========================================================================= */
/*  1f(7) — 記録 (sync / reopen / close)                                     */
/* ========================================================================= */

static void case_record(void)
{
    reset_all();
    put_broken_db();
    inj_sync_rc = -5;
    CHECK(run_recover() == 1);
    CHECK_STR("sync=-5");
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0");
    CHECK(mark_phase() == RC_PH_SWITCHED);
    /* 本体は切り替わっている (破壊段は通過済み) */
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));

    /* reopen が失敗する (切替後なので自動では戻さない) */
    reset_all();
    put_broken_db();
    inj_db_open_fail = rc_path[RC_N_DB];
    CHECK(run_recover() == 1);
    CHECK_STR("reopen=");
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0");
    CHECK(mark_phase() == RC_PH_SWITCHED);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));

    /* reopen の close が失敗する */
    reset_all();
    put_broken_db();
    inj_db_close_fail = 1;
    inj_db_close_path = rc_path[RC_N_DB];
    CHECK(run_recover() == 1);
    CHECK_STR("close=");
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0");
    CHECK(mark_phase() == RC_PH_SWITCHED);
}

/* ========================================================================= */
/*  1g — revert (受入 I7)                                                    */
/* ========================================================================= */

static void case_revert(void)
{
    RcMark m;

    /* 印が無ければ何もしない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_revert() == 1);
    CHECK_STR("nothing to revert (no recover-state)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);

    /* I2 の後: 現在の対が .failed へ、.bak が本体へ戻る */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hotjournal", 10);
    snap_all();
    CHECK(run_recover() == 0);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK_STR("current db(+journal) -> settings.db.failed(+.failed-journal),"
              " then restore: orig=present journal=present");
    CHECK_STR("reverted: orig=present, sync=0");
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));            /* 壊れた元に戻る */
    CHECK(snap_same(rc_path[RC_N_JOURNAL], &snap_journal));
    CHECK(fx_same(rc_path[RC_N_FAILED], RC_MASTER));         /* 直前の本体 */
    CHECK(mark_phase() == RC_PH_DONE);

    /* I3 の後: orig=missing なので本体は消えて欠損に戻る */
    reset_all();
    CHECK(run_recover() == 0);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK_STR("then restore: orig=missing journal=absent");
    CHECK_STR("reverted: orig=missing, sync=0");
    CHECK(!fx_exists(rc_path[RC_N_DB]));
    CHECK(fx_same(rc_path[RC_N_FAILED], RC_MASTER));
    CHECK(!fx_exists(rc_path[RC_N_JOURNAL]));

    /* 切替後に生まれた新世代の journal を旧 DB に付けたまま戻さない
     * (往復 2 の R5)。写しは .failed-journal に残る。 */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);                     /* 退避時は journal=absent */
    mark_of(&m);
    CHECK(m.journal_present == 0);
    fx_put(rc_path[RC_N_JOURNAL], "newgen", 6);    /* 切替後に生まれた journal */
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(!fx_exists(rc_path[RC_N_JOURNAL]));
    CHECK(fx_exists(rc_path[RC_N_FAILEDJ]));
    CHECK(fx_size(rc_path[RC_N_FAILEDJ]) == 6u);

    /* `.new` の残骸があっても revert は進む (別 inode なので触らない) */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    fx_put(rc_path[RC_N_NEW], "stale", 5);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(fx_exists(rc_path[RC_N_NEW]));
    CHECK(fx_size(rc_path[RC_N_NEW]) == 5u);

    /* 承認しなければ何もしない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    snap_take(rc_path[RC_N_DB], &snap_db);
    key_script = "N";
    clear_output();
    CHECK(run_revert() == 0);
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_FAILED]));
    CHECK(mark_phase() == RC_PH_DONE);

    /* 前提検査: `.bak` が無ければ何も触らない */
    reset_all();
    put_broken_db();
    CHECK(run_recover() == 0);
    CHECK(vfs_rm(rc_path[RC_N_BAK]) == VFS_OK);
    snap_take(rc_path[RC_N_DB], &snap_db);
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("backup missing (settings.db.bak) - needs manual recovery");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_FAILED]));
    CHECK(mark_phase() == RC_PH_DONE);

    /* 旧 .failed* は承認済み 1 世代として置き換わる */
    reset_all();
    put_broken_db();
    CHECK(run_recover() == 0);
    fx_put(rc_path[RC_N_FAILED], "older", 5);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK_STR("settings.db.failed: will replace");
    CHECK(fx_same(rc_path[RC_N_FAILED], RC_MASTER));
    mark_of(&m);
    CHECK(m.phase == RC_PH_DONE);
}

/* ========================================================================= */
/*  1g — revert の途中失敗 (phase は reverting のまま)                       */
/* ========================================================================= */

static void case_revert_fail(void)
{
    Snap bak_before;

    /* 復元 (本体の O_TRUNC 後) が失敗 → phase=reverting、.bak は不変 */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    snap_take(rc_path[RC_N_BAK], &bak_before);
    inj_write_garble = rc_path[RC_N_DB];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("restore failed at settings.db - current copy is in"
              " settings.db.failed");
    CHECK_STR("revert failed at settings.db (phase stays reverting)");
    CHECK(mark_phase() == RC_PH_REVERTING);
    CHECK_SNAP(rc_path[RC_N_BAK], bak_before);
    CHECK(fx_exists(rc_path[RC_N_FAILED]));

    /* 次の recover は門で止まり、`.bak` は 1 バイトも変わらない */
    inj_write_garble = NULL;
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("previous recovery incomplete (phase=reverting)");
    CHECK_SNAP(rc_path[RC_N_BAK], bak_before);

    /* 再 revert で復帰する (本体は PRESENT / ABSENT のどちらでも扱える) */
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(mark_phase() == RC_PH_DONE);

    /* .failed への退避が失敗したら、作った .failed* だけ消して止まる */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    snap_take(rc_path[RC_N_DB], &snap_db);
    inj_write_garble = rc_path[RC_N_FAILED];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("save failed (settings.db.failed)");
    CHECK_STR("revert failed at settings.db.failed (phase stays reverting)");
    CHECK_SNAP(rc_path[RC_N_DB], snap_db);
    CHECK(!fx_exists(rc_path[RC_N_FAILED]));
    CHECK(mark_phase() == RC_PH_REVERTING);

    /* sync の失敗も done にしない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    inj_sync_rc = -5;
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("reverted: orig=present, sync=-5");
    CHECK_STR("revert failed at sync (phase stays reverting)");
    CHECK(mark_phase() == RC_PH_REVERTING);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));    /* 復元自体は済んでいる */

    /* 旧 .failed* が消せなければ、印もまだ reverting にしない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    fx_put(rc_path[RC_N_FAILED], "older", 5);
    inj_unlink_fail = rc_path[RC_N_FAILED];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("cannot remove old failed copy");
    CHECK(mark_phase() == RC_PH_DONE);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));

    /* phase=reverting が書けなければ何も触らない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    inj_write_garble = rc_path[RC_N_STATE];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("recover-state write failed");
    CHECK(!fx_exists(rc_path[RC_N_FAILED]));
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));

    /* 現在の journal が消せない */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    fx_put(rc_path[RC_N_JOURNAL], "newgen", 6);
    inj_unlink_fail = rc_path[RC_N_JOURNAL];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("cannot remove current journal");
    CHECK_STR("revert failed at settings.db-journal (phase stays reverting)");
    CHECK(mark_phase() == RC_PH_REVERTING);
    CHECK(fx_exists(rc_path[RC_N_FAILEDJ]));     /* 写しは取れている */

    /* orig=missing の revert で本体が消せない */
    reset_all();
    CHECK(run_recover() == 0);                   /* 本体欠損からの recover */
    inj_unlink_fail = rc_path[RC_N_DB];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("restore failed at settings.db - current copy is in"
              " settings.db.failed");
    CHECK(mark_phase() == RC_PH_REVERTING);
    CHECK(fx_same(rc_path[RC_N_FAILED], RC_MASTER));

    /* 現在の本体が ABSENT + 孤立 journal からの revert (再 revert の形) */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    CHECK(vfs_rm(rc_path[RC_N_DB]) == VFS_OK);   /* 本体だけ消えた状態 */
    fx_put(rc_path[RC_N_JOURNAL], "orphan", 6);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK_STR("settings.db: missing");
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(!fx_exists(rc_path[RC_N_FAILED]));     /* 本体が無いので写しは無い */
    CHECK(fx_exists(rc_path[RC_N_FAILEDJ]));     /* 孤立 journal は写す */
    CHECK(!fx_exists(rc_path[RC_N_JOURNAL]));
    CHECK(mark_phase() == RC_PH_DONE);

    /* journal の復元に失敗したら対を分離したままにしない */
    reset_all();
    put_broken_db();
    fx_put(rc_path[RC_N_JOURNAL], "hotjournal", 10);
    snap_all();
    CHECK(run_recover() == 0);
    inj_write_garble = rc_path[RC_N_JOURNAL];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK_STR("restore failed at settings.db-journal");
    CHECK_STR("settings.db.bak-journal is still there - restore the pair"
              " by hand");
    /* 往復 1 の B2: 復元先を消して「旧 DB だけ・journal 無し」で止めない。
     * 両世代の写しが残っているので手動復旧できる。 */
    CHECK(fx_exists(rc_path[RC_N_JOURNAL]));      /* 書きかけを消さない */
    CHECK(fx_exists(rc_path[RC_N_BAKJ]));         /* 戻す元の写しは残る */
    CHECK(fx_exists(rc_path[RC_N_FAILED]));       /* 直前の本体の写しも残る */
    CHECK(mark_phase() == RC_PH_REVERTING);
}

/* ========================================================================= */
/*  連鎖 (票 §1h)                                                            */
/* ========================================================================= */

static void case_chain(void)
{
    Snap bak_before;

    /* (1) 失敗 → 再実行が門で止まる → revert → recover が通る */
    reset_all();
    put_broken_db();
    snap_all();
    inj_unlink_fail = rc_path[RC_N_JOURNAL];
    fx_put(rc_path[RC_N_JOURNAL], "hot", 3);
    snap_take(rc_path[RC_N_JOURNAL], &snap_journal);
    CHECK(run_recover() == 1);
    CHECK_STR("cannot remove hot journal");
    inj_unlink_fail = NULL;
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("previous recovery incomplete (phase=backup)");
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(snap_same(rc_path[RC_N_JOURNAL], &snap_journal));
    clear_output();
    CHECK(run_recover() == 0);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));

    /* (2) recover → revert → recover */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    clear_output();
    CHECK(run_recover() == 0);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));
    CHECK(snap_same(rc_path[RC_N_BAK], &snap_db));   /* 1 世代前は壊れた元 */
    CHECK(mark_phase() == RC_PH_DONE);

    /* (3) recover 成功 → revert 途中失敗 → recover が門で止まり .bak 不変
     *     → 再 revert で復帰 */
    reset_all();
    put_broken_db();
    snap_all();
    CHECK(run_recover() == 0);
    snap_take(rc_path[RC_N_BAK], &bak_before);
    inj_write_garble = rc_path[RC_N_DB];
    clear_output();
    CHECK(run_revert() == 1);
    CHECK(mark_phase() == RC_PH_REVERTING);
    inj_write_garble = NULL;
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("previous recovery incomplete (phase=reverting)");
    CHECK_SNAP(rc_path[RC_N_BAK], bak_before);
    clear_output();
    CHECK(run_revert() == 0);
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));

    /* (4) `.new` の close 失敗 → 次回停止 → 正式手順 (revert → rm → recover) */
    reset_all();
    put_broken_db();
    snap_all();
    inj_db_close_fail = 1;
    inj_db_close_path = rc_path[RC_N_NEW];
    CHECK(run_recover() == 1);
    CHECK_STR("REBOOT from the floppy, then: install --revert-settings hd0,"
              " rm /hd0/etc/settings.db.new, install --recover-settings hd0");
    CHECK(fx_exists(rc_path[RC_N_NEW]));
    inj_db_close_fail = 0;
    inj_db_close_path = NULL;
    clear_output();
    CHECK(run_recover() == 1);
    CHECK_STR("previous recovery incomplete (phase=backup)");
    clear_output();
    CHECK(run_revert() == 0);                  /* 元は無傷なので写しを戻すだけ */
    CHECK(snap_same(rc_path[RC_N_DB], &snap_db));
    CHECK(mark_phase() == RC_PH_DONE);
    clear_output();
    CHECK(run_recover() == 1);                 /* `.new` の残骸で止まる */
    CHECK_STR("stale settings.db.new present");
    CHECK(vfs_rm(rc_path[RC_N_NEW]) == VFS_OK);
    clear_output();
    CHECK(run_recover() == 0);
    CHECK(fx_same(rc_path[RC_N_DB], RC_MASTER));
}

/* ========================================================================= */
/*  純関数 (印の書式、コピー、比較)                                           */
/* ========================================================================= */

static void case_pure(void)
{
    RcMark m;
    static char big[40000];

    reset_all();

    /* 印の書式は 1 行。読み直しに厳密で、余計な行は無視しない。 */
    fx_puts(rc_path[RC_N_STATE], "phase=backup orig=present journal=absent size=7\n");
    CHECK(rc_mark_read(&host_ops, &m) == 0);
    CHECK(m.phase == RC_PH_BACKUP && m.orig_present == 1 &&
          m.journal_present == 0 && m.size == 7u);
    fx_puts(rc_path[RC_N_STATE], "phase=bogus orig=present journal=absent size=7\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_puts(rc_path[RC_N_STATE], "phase=backup orig=maybe journal=absent size=7\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_puts(rc_path[RC_N_STATE], "phase=backup orig=present journal=absent size=x\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    /* u32 の上限ちょうどは通り、1 つ上は回り込まずに拒否する */
    fx_puts(rc_path[RC_N_STATE],
            "phase=backup orig=present journal=absent size=4294967295\n");
    CHECK(rc_mark_read(&host_ops, &m) == 0 && m.size == 4294967295u);
    fx_puts(rc_path[RC_N_STATE],
            "phase=backup orig=present journal=absent size=4294967296\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_puts(rc_path[RC_N_STATE],
            "phase=backup orig=present journal=absent size=99999999999\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    /* 複数行 / 末尾 LF 無し / 埋込み NUL / 長すぎ (B5) */
    fx_puts(rc_path[RC_N_STATE],
            "phase=done orig=present journal=absent size=7\ngarbage\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_puts(rc_path[RC_N_STATE],
            "phase=done orig=present journal=absent size=7");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_put(rc_path[RC_N_STATE],
           "phase=done orig=present journal=absent size=7\n\0x", 47);
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    {
        static char toolong[RC_MARK_MAX + 8];
        memset(toolong, 'x', sizeof(toolong));
        fx_put(rc_path[RC_N_STATE], toolong, (u32)sizeof(toolong));
        CHECK(rc_mark_read(&host_ops, &m) != 0);
    }
    fx_puts(rc_path[RC_N_STATE], "phase=backup orig=present journal=absent\n");
    CHECK(rc_mark_read(&host_ops, &m) != 0);
    fx_put(rc_path[RC_N_STATE], "", 0);
    CHECK(rc_mark_read(&host_ops, &m) != 0);

    /* コピーは 16KB の境界をまたいでもバイト単位で一致する */
    memset(big, 0, sizeof(big));
    {
        unsigned i;
        for (i = 0; i < sizeof(big); i++) big[i] = (char)(i * 7u + (i >> 3));
    }
    fx_put("/tmp/src", big, (u32)sizeof(big));
    CHECK(rc_copy_file(&host_ops, "/tmp/src", "/tmp/dst") == RC_CP_OK);
    CHECK(rc_cmp_file(&host_ops, "/tmp/src", "/tmp/dst") == 0);
    CHECK(fx_size("/tmp/dst") == (u32)sizeof(big));

    /* 1 バイト違えば比較が気付く */
    {
        FixtureFile *f = fixture_find(host_resolve("/tmp/dst"), 0);
        CHECK(f != NULL);
        f->data[20000] ^= 0xFF;
        CHECK(rc_cmp_file(&host_ops, "/tmp/src", "/tmp/dst") != 0);
    }
    /* 長さ違いも */
    fx_put("/tmp/dst", big, (u32)sizeof(big) - 1u);
    CHECK(rc_cmp_file(&host_ops, "/tmp/src", "/tmp/dst") != 0);
    /* 読めない側 */
    CHECK(rc_cmp_file(&host_ops, "/tmp/src", "/tmp/nope") != 0);
    CHECK(rc_copy_file(&host_ops, "/tmp/nope", "/tmp/dst2") == RC_CP_SRC);

    /* read の負は EOF ではなく失敗 */
    inj_read_fail = "/tmp/src";
    CHECK(rc_copy_file(&host_ops, "/tmp/src", "/tmp/dst3") == RC_CP_READ);
    inj_read_fail = NULL;
    /* short write も失敗 */
    inj_write_short = "/tmp/dst4";
    CHECK(rc_copy_file(&host_ops, "/tmp/src", "/tmp/dst4") == RC_CP_WRITE);
    inj_write_short = NULL;
}

/* ========================================================================= */

int main(int argc, char **argv)
{
    mock_ops.get_file_size = mock_size;
    mock_ops.write_file = mock_write;
    mock_ops.read_stream = mock_read_stream;
    mock_ops.write_stream = mock_write_stream;
    CHECK(os32_sqlite_init() == SQLITE_OK);
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "media")) case_media();
    else if (!strcmp(argv[1], "scan")) case_scan();
    else if (!strcmp(argv[1], "gate")) case_gate();
    else if (!strcmp(argv[1], "master")) case_master();
    else if (!strcmp(argv[1], "approve")) case_approve();
    else if (!strcmp(argv[1], "happy")) case_happy();
    else if (!strcmp(argv[1], "backup_fail")) case_backup_fail();
    else if (!strcmp(argv[1], "newfail")) case_newfail();
    else if (!strcmp(argv[1], "switch")) case_switch();
    else if (!strcmp(argv[1], "record")) case_record();
    else if (!strcmp(argv[1], "revert")) case_revert();
    else if (!strcmp(argv[1], "revert_fail")) case_revert_fail();
    else if (!strcmp(argv[1], "chain")) case_chain();
    else if (!strcmp(argv[1], "pure")) case_pure();
    else return 2;
    printf("PASS %s\n", argv[1]);
    return 0;
}
