/* F2b: actual VFS + F2a FD table + bundled SQLite, host boundaries only. */
#define main f2a_main
#include "vfs_fd_sqlite_host.c"
#undef main
/* Host size_t differs from kernel u32; retain libc memory functions. */
#define __KSTRING_H
char *kstrncpy(char *dst, const char *src, u32 size)
{ str_cpy(dst, src, (int)size); return dst; }
void *kmemset(void *dst, int val, u32 size) { return memset(dst, val, size); }
int kstrncmp(const char *a, const char *b, u32 n) { return strncmp(a, b, n); }
#include "../../lib/sqlite3/sqlite3.h"
static int register_fail, register_success[10];
static int managed_opens;
static int traced_open(const char *path, int mode, int owner,
                       const VfsSqliteCookie *cookie, int flags, VfsSqliteLease *out);
static int host_register(sqlite3_vfs *vfs, int make_default);
#define sqlite3_vfs_register host_register
#define vfs_open_sqlite traced_open
#include "../../lib/sqlite3/os32_sqlite_vfs.c"
#undef vfs_open_sqlite
#undef sqlite3_vfs_register
static int traced_open(const char *path, int mode, int owner,
                       const VfsSqliteCookie *cookie, int flags, VfsSqliteLease *out)
{
    int rc = vfs_open_sqlite(path, mode, owner, cookie, flags, out);
    if (rc == VFS_OK) managed_opens++;
    return rc;
}
static int host_register(sqlite3_vfs *vfs, int make_default)
{
    int i, rc;
    if (register_fail && !strcmp(vfs->zName, "os32-g2")) {
        register_fail = 0; return SQLITE_BUSY;
    }
    rc = sqlite3_vfs_register(vfs, make_default);
    if (rc == SQLITE_OK) {
        i = vfs == &os32_vfs ? 9 : (int)(vfs - group_vfs);
        CHECK(i >= 0 && i < 10); register_success[i]++;
    }
    return rc;
}
#include "sqlite_groups_backend.h"
volatile u32 tick_count;
u32 sys_time(void) { return 0; }
void kprintf(u8 color, const char *fmt, ...) { }
int vfs_sync(void) { probes++; return 0; }
int vfs_rm(const char *path)
{ probes++; return fixture_active ? fixture_rm(path) : 0; }
int vfs_stat(const char *path, OS32_Stat *st)
{ probes++; return fixture_active && fixture_find(path, 0) ? 0 : -1; }


static void capacity(void)
{
    Os32SqliteGroup g[9], sentinel, extra;
    int i, before;
    memset(&sentinel, 0x5a, sizeof(sentinel)); extra = sentinel;
    before = probes;
    for (i = 0; i < 8; i++) {
        CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &g[i]) == SQLITE_OK);
        CHECK(g[i].group_index == i && g[i].generation != 0);
    }
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &extra) == SQLITE_FULL);
    CHECK(!memcmp(&extra, &sentinel, sizeof(extra)));
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 2, &g[8]) == SQLITE_OK);
    CHECK(g[8].group_index == 8);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, &extra) == SQLITE_FULL);
    CHECK(os32_sqlite_group_acquire(99, 0, &extra) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, -1, &extra) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 0, NULL) == SQLITE_MISUSE);
    CHECK(probes == before);
}

static void owned_open(void)
{
    Os32SqliteGroup a, b;
    sqlite3_vfs *va, *vb;
    Os32File main_file, journal, temp;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 2, &b) == SQLITE_OK);
    va = sqlite3_vfs_find("os32-g0"); vb = sqlite3_vfs_find("os32-g1");
    CHECK(va->xOpen(va, "/main", &main_file.base, SQLITE_OPEN_READWRITE | SQLITE_OPEN_MAIN_DB, NULL) == SQLITE_OK);
    CHECK(open_files[main_file.fd].lifetime == VFS_FD_SQLITE);
    CHECK(open_files[main_file.fd].owner == 1);
    CHECK(va->xOpen(va, "/not-a-suffix", &journal.base, SQLITE_OPEN_READWRITE | SQLITE_OPEN_MAIN_JOURNAL, NULL) == SQLITE_OK);
    CHECK(vb->xOpen(vb, "/main-journal", &temp.base, SQLITE_OPEN_READWRITE | SQLITE_OPEN_TEMP_DB, NULL) == SQLITE_OK);
    CHECK(vfs_count_sqlite(&a) == 2 && vfs_count_sqlite(&b) == 1);
    CHECK(open_files[journal.fd].owner == 1 && open_files[temp.fd].owner == 2);
    CHECK(current_owner == 2 && owner_sets == 0);
    CHECK(os32Close(&main_file.base) == SQLITE_OK);
    CHECK(os32Close(&temp.base) == SQLITE_OK);
    CHECK(os32Close(&journal.base) == SQLITE_OK);
    CHECK(vfs_count_sqlite(&a) == 0 && vfs_count_sqlite(&b) == 0);
}


static void lifecycle(void)
{
    Os32SqliteGroup a, old, b;
    sqlite3_vfs *v;
    Os32File f;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    old = a;
    CHECK(os32_sqlite_group_vfs_name(&a) != NULL);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    CHECK(os32_sqlite_group_opened(&a, (void *)1) == SQLITE_OK);
    CHECK(os32_sqlite_group_opened(&a, (void *)1) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(v->xOpen(v, "/rollback-journal", &f.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(os32Close(&f.base) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &b) == SQLITE_OK);
    CHECK(b.group_index == old.group_index && b.generation > old.generation);
    CHECK(os32_sqlite_group_vfs_name(&old) == NULL);
    CHECK(os32_sqlite_group_begin_close(&old) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_finish(&old, SQLITE_OK) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_begin_close(&b) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&b, SQLITE_OK) == SQLITE_OK);
    groups[b.group_index].cookie.generation = OS32_SQLITE_GROUP_GENERATION_MAX;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(a.group_index != b.group_index);
}

static void failed_finish(const char *mode)
{
    Os32SqliteGroup a, b;
    Os32File main_file, journal, denied;
    sqlite3_vfs *v;
    VfsFile generic;
    int fd, before, rc;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    CHECK(v->xOpen(v, "/main", &main_file.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(v->xOpen(v, "/journal", &journal.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(os32_sqlite_group_opened(&a, (void *)1) == SQLITE_OK);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(os32Close(&main_file.base) == SQLITE_OK);
    if (!strcmp(mode, "busy_zero")) CHECK(os32Close(&journal.base) == SQLITE_OK);
    fd = vfs_open("/unrelated", O_RDWR); generic = open_files[fd];
    rc = !strcmp(mode, "orphan") ? SQLITE_OK : SQLITE_BUSY;
    CHECK(os32_sqlite_group_finish(&a, rc) != SQLITE_OK);
    CHECK(groups[a.group_index].state == OS32_SQLITE_QUARANTINED);
    CHECK(groups[a.group_index].db == (rc == SQLITE_OK ? NULL : (void *)1));
    before = probes;
    CHECK(v->xOpen(v, "/future", &denied.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_CANTOPEN);
    CHECK(probes == before && denied.base.pMethods == NULL);
    CHECK(os32_sqlite_group_vfs_name(&a) == NULL);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) != SQLITE_OK);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &b) == SQLITE_OK);
    CHECK(a.group_index != b.group_index);
    CHECK(!memcmp(&generic, &open_files[fd], sizeof(generic)));
    if (strcmp(mode, "busy_zero")) {
        CHECK(vfs_count_sqlite(&a) == 1);
        CHECK(open_files[journal.fd].quarantined);
    } else CHECK(vfs_count_sqlite(&a) == 0);
}

static void sticky_close(void)
{
    Os32SqliteGroup a, b;
    Os32File *f;
    sqlite3_vfs *v;
    int fd, before;
    VfsFile saved;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    f = malloc(sizeof(*f)); CHECK(f != NULL);
    CHECK(v->xOpen(v, "/old", &f->base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(vfs_close_sqlite(&f->lease) == VFS_OK); /* Inject stale lease. */
    fd = vfs_open("/replacement", O_RDWR); saved = open_files[fd]; before = probes;
    CHECK(os32Close(&f->base) == SQLITE_IOERR_CLOSE);
    memset(f, 0xa5, sizeof(*f)); free(f); /* SQLite owns callback storage. */
    CHECK(groups[a.group_index].first_rc == SQLITE_IOERR_CLOSE);
    CHECK(groups[a.group_index].first_stage == OS32_SQLITE_STAGE_XCLOSE);
    CHECK(vfs_count_sqlite(&a) == 0);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_IOERR_CLOSE);
    CHECK(groups[a.group_index].state == OS32_SQLITE_QUARANTINED);
    CHECK(groups[a.group_index].db == NULL);
    CHECK(probes == before && !memcmp(&saved, &open_files[fd], sizeof(saved)));
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &b) == SQLITE_OK);
    CHECK(a.group_index != b.group_index);
}

static void callback_lease(const char *method)
{
    Os32SqliteGroup a;
    Os32File f;
    sqlite3_vfs *v;
    VfsFile saved;
    int fd, before, out = 99;
    sqlite3_int64 size = 99;
    char buf[8];
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    CHECK(v->xOpen(v, "/stale", &f.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(vfs_close_sqlite(&f.lease) == VFS_OK);
    fd = vfs_open("/replacement", O_RDWR); saved = open_files[fd]; before = probes;
    if (!strcmp(method, "read")) os32Read(&f.base, buf, sizeof(buf), 0);
    else if (!strcmp(method, "write")) os32Write(&f.base, buf, sizeof(buf), 0);
    else if (!strcmp(method, "truncate")) os32Truncate(&f.base, 0);
    else if (!strcmp(method, "sync")) os32Sync(&f.base, 0);
    else if (!strcmp(method, "size")) os32FileSize(&f.base, &size);
    else if (!strcmp(method, "lock")) os32Lock(&f.base, 0);
    else if (!strcmp(method, "unlock")) os32Unlock(&f.base, 0);
    else if (!strcmp(method, "reserved")) os32CheckReservedLock(&f.base, &out);
    else if (!strcmp(method, "control")) os32FileControl(&f.base, 0, &out);
    else if (!strcmp(method, "sector")) os32SectorSize(&f.base);
    else if (!strcmp(method, "device")) os32DeviceCharacteristics(&f.base);
    else CHECK(0);
    CHECK(groups[a.group_index].first_rc != SQLITE_OK);
    CHECK(probes == before && !memcmp(&saved, &open_files[fd], sizeof(saved)));
    CHECK(out == 99 && size == 99);
}


static void scoped_open(void)
{
    Os32SqliteGroup a;
    sqlite3 *db = NULL;
    int before, rc;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    before = probes;
    CHECK(os32_sqlite_group_open(&a, ":memory:", SQLITE_OPEN_READWRITE, &db) == SQLITE_OK);
    CHECK(db != NULL && probes == before);
    CHECK(groups[a.group_index].db == db);
    CHECK(os32_sqlite_group_open(&a, "/second", SQLITE_OPEN_READWRITE, &db) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    rc = sqlite3_close(db); if (rc == SQLITE_OK) db = NULL;
    CHECK(os32_sqlite_group_finish(&a, rc) == SQLITE_OK);
    CHECK(db == NULL);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(&a, "file:/evil?vfs=os32", SQLITE_OPEN_READWRITE, &db) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_open(&a, "/uri", SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, &db) == SQLITE_MISUSE);
    CHECK(probes == before && db == NULL);
    size_rc = VFS_ERR_NOTFOUND;
    CHECK(os32_sqlite_group_open(&a, "/missing", SQLITE_OPEN_READONLY, &db) == SQLITE_CANTOPEN);
    CHECK(db != NULL && groups[a.group_index].db == db);
    CHECK(groups[a.group_index].state == OS32_SQLITE_OPENING);
    CHECK(groups[a.group_index].first_stage == OS32_SQLITE_STAGE_OPEN);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    rc = sqlite3_close(db); if (rc == SQLITE_OK) db = NULL;
    CHECK(os32_sqlite_group_finish(&a, rc) == SQLITE_OK);
    CHECK(groups[a.group_index].first_rc == SQLITE_CANTOPEN);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(groups[a.group_index].first_rc == SQLITE_OK);
}


static void diagnostics(void)
{
    Os32SqliteGroup a, stale;
    Os32SqliteGroupInfo info = {0};
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(os32_sqlite_group_snapshot(&a, &info) == SQLITE_OK);
    CHECK(info.state == OS32_SQLITE_OPENING && info.fd_count == 0 && !info.has_db);
    CHECK(os32_sqlite_group_quarantine(&a, OS32_SQLITE_STAGE_OPEN, SQLITE_CANTOPEN) == SQLITE_OK);
    CHECK(os32_sqlite_group_quarantine(&a, OS32_SQLITE_STAGE_CLOSE, SQLITE_BUSY) == SQLITE_OK);
    CHECK(os32_sqlite_group_snapshot(&a, &info) == SQLITE_OK);
    CHECK(info.state == OS32_SQLITE_QUARANTINED && info.fd_count == 0);
    CHECK(info.first_stage == OS32_SQLITE_STAGE_OPEN && info.first_rc == SQLITE_CANTOPEN);
    CHECK(info.sticky_rc != 0);
    stale = a; stale.generation++;
    CHECK(os32_sqlite_group_snapshot(&stale, &info) == SQLITE_MISUSE);
    CHECK(os32_sqlite_group_snapshot(&a, NULL) == SQLITE_MISUSE);
}

static void path_quarantine(const char *method)
{
    Os32SqliteGroup a;
    sqlite3_vfs *v;
    int before, out = 99, rc = SQLITE_OK;
    char buf[32];
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    CHECK(os32_sqlite_group_quarantine(&a, OS32_SQLITE_STAGE_CLOSE, SQLITE_BUSY) == SQLITE_OK);
    before = probes;
    if (!strcmp(method, "delete")) rc = v->xDelete(v, "/same", 0);
    else if (!strcmp(method, "access")) rc = v->xAccess(v, "/same", 0, &out);
    else if (!strcmp(method, "fullpath")) rc = v->xFullPathname(v, "/same", sizeof(buf), buf);
    else CHECK(0);
    CHECK(rc != SQLITE_OK);
    CHECK(probes == before && out == 99);
}

static void null_open(void)
{
    Os32SqliteGroup a;
    Os32SqliteGroupInfo info;
    sqlite3 *db = NULL;
    void *blocks[8192];
    int n = 0, i, before;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    while (n < 8192 && (blocks[n] = sqlite3_malloc(64)) != NULL) n++;
    CHECK(n < 8192);
    before = probes;
    CHECK(os32_sqlite_group_open(&a, ":memory:", SQLITE_OPEN_READWRITE, &db) == SQLITE_NOMEM);
    CHECK(db == NULL && probes == before);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_snapshot(&a, &info) == SQLITE_OK);
    CHECK(info.state == OS32_SQLITE_FREE && info.first_rc == SQLITE_NOMEM);
    CHECK(!info.has_db && info.fd_count == 0 && !info.sticky_rc);
    for (i = 0; i < n; i++) sqlite3_free(blocks[i]);
}

static void registration(void)
{
    sqlite3_vfs *v, *next;
    int i;
    v = sqlite3_vfs_find("os32-g0");
    CHECK(v != NULL);
    next = v->pNext;
    for (i = 0; i < 20; i++) CHECK(sqlite3_os_init() == SQLITE_OK);
    CHECK(sqlite3_vfs_find("os32-g0") == v && v->pNext == next);
    CHECK(!strcmp(sqlite3_vfs_find(NULL)->zName, "os32"));
}

static void uri_callback(void)
{
    Os32SqliteGroup a;
    Os32File f;
    sqlite3_vfs *v;
    int before;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a)); before = probes;
    CHECK(v->xOpen(v, "/uri", &f.base, SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, NULL) == SQLITE_CANTOPEN);
    CHECK(probes == before && f.base.pMethods == NULL);
}

static void edge_opens(void)
{
    Os32SqliteGroup a;
    Os32File f, main_file;
    sqlite3_vfs *v = sqlite3_vfs_find("os32-g0");
    int i, before, out = 99;
    before = probes;
    CHECK(v->xOpen(v, "/free", &f.base, SQLITE_OPEN_READWRITE, &out) == SQLITE_CANTOPEN);
    CHECK(probes == before && out == 99 && f.base.pMethods == NULL);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(v->xOpen(v, NULL, &f.base, SQLITE_OPEN_READWRITE, &out) == SQLITE_CANTOPEN);
    CHECK(probes == before && f.base.pMethods == NULL);
    CHECK(v->xOpen(v, "/main", &main_file.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    size_rc = VFS_ERR_NOTFOUND;
    CHECK(v->xOpen(v, "/journal-fail", &f.base, SQLITE_OPEN_READWRITE, &out) == SQLITE_CANTOPEN);
    CHECK(f.base.pMethods == NULL && f.fd == -1 && out == 99 && vfs_count_sqlite(&a) == 1);
    size_rc = 0;
    for (i = 4; i < VFS_MAX_OPEN_FILES; i++) CHECK(vfs_open("/generic", O_RDWR) == i);
    before = probes;
    CHECK(v->xOpen(v, "/full", &f.base, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &out) == SQLITE_CANTOPEN);
    CHECK(probes == before && f.base.pMethods == NULL && out == 99);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(os32Close(&main_file.base) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    v = sqlite3_vfs_find("os32");
    CHECK(v->xOpen(v, "/legacy", &f.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(!f.managed && open_files[f.fd].lifetime == VFS_FD_GENERIC);
    CHECK(os32Close(&f.base) == SQLITE_OK);
}

static void stale_group(void)
{
    Os32SqliteGroup a, b;
    Os32File f, saved_file, fresh;
    sqlite3_vfs *v;
    VfsFile saved_fd;
    Os32SqliteGroupInfo info;
    int before;
    char buf[8];
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    v = sqlite3_vfs_find(os32_sqlite_group_vfs_name(&a));
    CHECK(v->xOpen(v, "/old", &f.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    saved_file = f;
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(os32Close(&f.base) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &b) == SQLITE_OK);
    CHECK(v->xOpen(v, "/new", &fresh.base, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    saved_fd = open_files[fresh.fd]; before = probes;
    CHECK(os32Close(&saved_file.base) == SQLITE_IOERR_CLOSE);
    CHECK(os32Read(&saved_file.base, buf, sizeof(buf), 0) == SQLITE_IOERR_READ);
    CHECK(os32_sqlite_group_quarantine(&a, OS32_SQLITE_STAGE_CLOSE, SQLITE_BUSY) == SQLITE_MISUSE);
    CHECK(!memcmp(&saved_fd, &open_files[fresh.fd], sizeof(saved_fd)) && probes == before);
    CHECK(os32_sqlite_group_snapshot(&b, &info) == SQLITE_OK);
    CHECK(info.sticky_rc == 0 && info.state == OS32_SQLITE_OPENING);
    CHECK(os32_sqlite_group_quarantine(&b, OS32_SQLITE_STAGE_CLOSE, SQLITE_BUSY) == SQLITE_OK);
    CHECK(os32Read(&fresh.base, buf, sizeof(buf), 0) == SQLITE_IOERR_READ);
    CHECK(os32Close(&fresh.base) == SQLITE_IOERR_CLOSE);
    CHECK(probes == before && vfs_count_sqlite(&b) == 1);
}

static void repeated_lifetimes(void)
{
    Os32SqliteGroup a;
    /* Bundled SQLite also registers its own memdb VFS. */
    sqlite3_vfs *v, *links[OS32_SQLITE_GROUPS + 2];
    sqlite3 *db = NULL;
    int i, j, rc;
    v = sqlite3_vfs_find(NULL);
    for (i = 0; i < OS32_SQLITE_GROUPS + 2; i++) {
        CHECK(v != NULL); links[i] = v; v = v->pNext;
    }
    CHECK(v == NULL);
    CHECK(sqlite3_vfs_find("memdb") != NULL);
    for (j = 0; j < 100; j++) {
        CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
        CHECK(os32_sqlite_group_open(&a, ":memory:", SQLITE_OPEN_READWRITE, &db) == SQLITE_OK);
        CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
        rc = sqlite3_close(db); if (rc == SQLITE_OK) db = NULL;
        CHECK(os32_sqlite_group_finish(&a, rc) == SQLITE_OK);
        CHECK(sqlite3_os_init() == SQLITE_OK);
        v = sqlite3_vfs_find(NULL);
        for (i = 0; i < OS32_SQLITE_GROUPS + 2; i++) {
            CHECK(v == links[i]); v = v->pNext;
        }
        for (i = 0; i < 10; i++) CHECK(register_success[i] == 1);
        CHECK(v == NULL);
    }
    for (i = 0; i < OS32_SQLITE_EXEC_GROUPS; i++)
        groups[i].cookie.generation = OS32_SQLITE_GROUP_GENERATION_MAX;
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_FULL);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 2, &a) == SQLITE_OK);
}

static void real_sqlite(const char *mode)
{
    Os32SqliteGroup a;
    Os32SqliteGroupInfo info;
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    sqlite3_file *file = NULL;
    int i, rc, journal = 0, before, opens_before_temp;
    fixture_init();
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 1, &a) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(&a, "/real.db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db) == SQLITE_OK);
    CHECK(vfs_count_sqlite(&a) == 1);
    CHECK(sqlite3_exec(db, "CREATE TABLE t(x); BEGIN;", NULL, NULL, NULL) == SQLITE_OK);
    for (i = 0; i < 80; i++)
        CHECK(sqlite3_exec(db, "INSERT INTO t VALUES(zeroblob(512));", NULL, NULL, NULL) == SQLITE_OK);
    for (i = 3; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) continue;
        CHECK(open_files[i].lifetime == VFS_FD_SQLITE);
        CHECK(open_files[i].owner == 1);
        CHECK(open_files[i].cookie.group_index == a.group_index);
        CHECK(open_files[i].cookie.generation == a.generation);
        if (open_files[i].sqlite_flags & SQLITE_OPEN_MAIN_JOURNAL) {
            journal++;
            printf("REAL journal=%s group=%d/%lu owner=%d current=%d fd=%d/%lu flags=%x\n",
                open_files[i].path, a.group_index, (unsigned long)a.generation,
                open_files[i].owner, current_owner, i,
                (unsigned long)open_files[i].generation, open_files[i].sqlite_flags);
        }
    }
    CHECK(journal == 1 && vfs_count_sqlite(&a) == 2);
    CHECK(current_owner == 2 && owner_sets == 0);
    vfs_close_owned(2);
    CHECK(vfs_count_sqlite(&a) == 2);
    CHECK(sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK);
    opens_before_temp = managed_opens;
    CHECK(sqlite3_exec(db, "CREATE TEMP TABLE tmp AS SELECT x FROM t;", NULL, NULL, NULL) == SQLITE_OK);
    CHECK(vfs_count_sqlite(&a) == 1);
    CHECK(sqlite3_prepare_v2(db, "SELECT count(*) FROM tmp", -1, &stmt, NULL) == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 80);
    CHECK(sqlite3_finalize(stmt) == SQLITE_OK); stmt = NULL;
    CHECK(managed_opens == opens_before_temp); /* Includes transient opens. */
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    if (!strcmp(mode, "stale")) {
        CHECK(sqlite3_file_control(db, "main", SQLITE_FCNTL_FILE_POINTER, &file) == SQLITE_OK);
        CHECK(vfs_close_sqlite(&((Os32File *)file)->lease) == VFS_OK);
    }
    before = probes;
    rc = sqlite3_close(db); if (rc == SQLITE_OK) db = NULL;
    CHECK(rc == SQLITE_OK && db == NULL);
    rc = os32_sqlite_group_finish(&a, rc);
    CHECK(os32_sqlite_group_snapshot(&a, &info) == SQLITE_OK);
    CHECK(info.fd_count == 0 && !info.has_db);
    if (!strcmp(mode, "stale")) {
        CHECK(rc != SQLITE_OK && info.state == OS32_SQLITE_QUARANTINED && info.sticky_rc);
        CHECK(probes == before);
    } else CHECK(rc == SQLITE_OK && info.state == OS32_SQLITE_FREE);
    CHECK(memsys5_check_canary() == 0);
    printf("REAL close SQLite=OK finish=%d temp_rows=80 temp_FD=0\n", rc);
}
int main(int argc, char **argv)
{
    int i;
    CHECK(argc == 2);
    if (!strcmp(argv[1], "register_retry")) {
        register_fail = 1;
        CHECK(sqlite3_os_init() == SQLITE_BUSY);
        CHECK(sqlite3_os_init() == SQLITE_OK);
        for (i = 0; i < 10; i++) CHECK(register_success[i] == 1);
        return 0;
    }
    CHECK(os32_sqlite_init() == SQLITE_OK);
    mock_ops.get_file_size = mock_size;
    mock_ops.write_file = mock_write;
    mock_ops.read_stream = mock_read_stream;
    mock_ops.write_stream = mock_write_stream;
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registration")) registration();
    else if (!strcmp(argv[1], "capacity")) capacity();
    else if (!strcmp(argv[1], "owned_open")) owned_open();
    else if (!strcmp(argv[1], "lifecycle")) lifecycle();
    else if (!strcmp(argv[1], "busy_partial") || !strcmp(argv[1], "busy_zero") ||
             !strcmp(argv[1], "orphan")) failed_finish(argv[1]);
    else if (!strcmp(argv[1], "sticky_close")) sticky_close();
    else if (!strcmp(argv[1], "scoped_open")) scoped_open();
    else if (!strcmp(argv[1], "null_open")) null_open();
    else if (!strcmp(argv[1], "diagnostics")) diagnostics();
    else if (!strcmp(argv[1], "edge_opens")) edge_opens();
    else if (!strcmp(argv[1], "uri_callback")) uri_callback();
    else if (!strcmp(argv[1], "stale_group")) stale_group();
    else if (!strcmp(argv[1], "repeated_lifetimes")) repeated_lifetimes();
    else if (!strncmp(argv[1], "real_", 5)) real_sqlite(argv[1] + 5);
    else if (!strncmp(argv[1], "path_", 5)) path_quarantine(argv[1] + 5);
    else if (!strncmp(argv[1], "callback_", 9)) callback_lease(argv[1] + 9);
    else CHECK(0);
    return 0;
}
