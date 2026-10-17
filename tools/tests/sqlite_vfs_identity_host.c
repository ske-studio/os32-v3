/* T4b-4: real FD table, real SQLite VFS and bundled engine; filesystem I/O
 * only is a RAM boundary. IDs model ext2 hardlinks, not path equality. */
#define GROUPS_TEST_MAIN groups_baseline_main
#include "sqlite_groups_host.c"
#undef GROUPS_TEST_MAIN

static sqlite3_vfs *acquire(int kind, int owner, Os32SqliteGroup *g)
{
    CHECK(os32_sqlite_group_acquire(kind, owner, g) == SQLITE_OK);
    return sqlite3_vfs_find(os32_sqlite_group_vfs_name(g));
}
static void finish_file(Os32SqliteGroup *g, Os32File *f)
{
    CHECK(os32_sqlite_group_begin_close(g) == SQLITE_OK);
    CHECK(os32Close(&f->base) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(g, SQLITE_OK) == SQLITE_OK);
}
static int alias_lookup(void *ctx, const char *path, u32 *ino)
{
    return fixture_lookup_ino(ctx, !strcmp(path, "/alias") ? "/db" : path, ino);
}
static VfsInoOps alias_ops;
static int main_open(sqlite3_vfs *v, const char *path, Os32File *f, int flags, int *out)
{
    memset(f, 0, sizeof(*f));
    return v->xOpen(v, path, &f->base, flags | SQLITE_OPEN_MAIN_DB, out);
}
static void duplicate(const char *mode)
{
    Os32SqliteGroup a, b;
    Os32File first, second;
    sqlite3_vfs *va, *vb;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    const char *path = !strcmp(mode, "alias") ? "/alias" :
                       !strcmp(mode, "relative") ? "./db" : "/db";
    int before;
    int parent = !strcmp(mode, "parent_child") || !strcmp(mode, "same_owner") ||
                 !strcmp(mode, "parked");
    int owner = parent ? 1 : 0;
    CHECK(fixture_create(NULL, "/db", "contents", 8) == VFS_OK);
    CHECK(fixture_create(NULL, "/alias", "contents", 8) == VFS_OK);
    alias_ops = fixture_ino_ops; alias_ops.lookup = alias_lookup;
    mock_ops.ino = &alias_ops;
    va = acquire(parent ? OS32_SQLITE_EXEC : OS32_SQLITE_RESIDENT, owner, &a);
    vb = acquire(OS32_SQLITE_EXEC, !strcmp(mode, "same_owner") ? owner : 2, &b);
    CHECK(main_open(va, "/db", &first, flags, NULL) == SQLITE_OK);
    if (!strcmp(mode, "parked")) {
        vfs_close_owned(owner); /* A parked parent's generic cleanup cannot release it. */
        CHECK(vfs_count_sqlite(&a) == 1);
    }
    if (!strcmp(mode, "quarantine"))
        CHECK(os32_sqlite_group_quarantine(&a, OS32_SQLITE_STAGE_CLOSE, SQLITE_BUSY) == SQLITE_OK);
    if (!strcmp(mode, "close_gap")) CHECK(os32Close(&first.base) == SQLITE_OK);
    before = vfs_count_sqlite(&a);
    CHECK(main_open(vb, path, &second, flags, NULL) == SQLITE_CANTOPEN);
    CHECK(second.base.pMethods == NULL && vfs_count_sqlite(&b) == 0);
    CHECK(vfs_count_sqlite(&a) == before);
    CHECK(fixture_find("/db", 0)->size == 8);
    CHECK(!memcmp(fixture_find("/db", 0)->data, "contents", 8));
    if (!strcmp(mode, "quarantine")) return;
    if (!strcmp(mode, "close_gap")) {
        CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
        CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    } else finish_file(&a, &first);
    CHECK(main_open(vb, path, &second, flags, NULL) == SQLITE_OK);
    finish_file(&b, &second);
}
static void separate(void)
{
    Os32SqliteGroup a, b, c, d;
    sqlite3 *da = NULL, *db = NULL, *dc = NULL, *dd = NULL;
    acquire(OS32_SQLITE_EXEC, 1, &a); acquire(OS32_SQLITE_EXEC, 1, &b);
    acquire(OS32_SQLITE_EXEC, 2, &c); acquire(OS32_SQLITE_EXEC, 2, &d);
    CHECK(os32_sqlite_group_open(&a, "/one", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &da) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(&b, "/two", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(da, "CREATE TABLE t(x); INSERT INTO t VALUES(1)", 0, 0, 0) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE t(x); INSERT INTO t VALUES(2)", 0, 0, 0) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(&c, ":memory:", SQLITE_OPEN_READWRITE, &dc) == SQLITE_OK);
    CHECK(os32_sqlite_group_open(&d, ":memory:", SQLITE_OPEN_READWRITE, &dd) == SQLITE_OK);
    CHECK(sqlite3_exec(dc, "CREATE TABLE only_here(x)", 0, 0, 0) == SQLITE_OK);
    CHECK(sqlite3_exec(dd, "SELECT * FROM only_here", 0, 0, 0) == SQLITE_ERROR);
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(sqlite3_close(da) == SQLITE_OK); CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_begin_close(&b) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK); CHECK(os32_sqlite_group_finish(&b, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_begin_close(&c) == SQLITE_OK);
    CHECK(sqlite3_close(dc) == SQLITE_OK); CHECK(os32_sqlite_group_finish(&c, SQLITE_OK) == SQLITE_OK);
    CHECK(os32_sqlite_group_begin_close(&d) == SQLITE_OK);
    CHECK(sqlite3_close(dd) == SQLITE_OK); CHECK(os32_sqlite_group_finish(&d, SQLITE_OK) == SQLITE_OK);
}
static int fail_resize(void *ctx, u32 ino, u32 size) { return VFS_ERR_IO; }
static void errors(void)
{
    Os32SqliteGroup a;
    Os32File f;
    sqlite3_vfs *v = acquire(OS32_SQLITE_EXEC, 1, &a);
    VfsSqliteOps bad = fixture_sqlite_ops;
    int out;
    CHECK(fixture_create(NULL, "/db", "content", 7) == VFS_OK);
    CHECK(main_open(v, "/db", &f, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(os32Truncate(&f.base, 3) == SQLITE_OK);
    CHECK(fixture_find("/db", 0)->size == 3 && vfs_get_size(f.fd) == 3);
    bad.resize = fail_resize; mock_ops.sqlite = &bad;
    CHECK(os32Truncate(&f.base, 0) == SQLITE_IOERR_TRUNCATE);
    CHECK(fixture_find("/db", 0)->size == 3);
    fixture_sync_rc = VFS_ERR_IO;
    CHECK(os32Sync(&f.base, 0) == SQLITE_IOERR_FSYNC);
    fixture_rm_rc = VFS_ERR_IO;
    CHECK(v->xDelete(v, "/other", 0) == SQLITE_IOERR_DELETE);
    fixture_rm_rc = 0;
    CHECK(v->xDelete(v, "/absent", 0) == SQLITE_IOERR_DELETE_NOENT);
    CHECK(fixture_create(NULL, "/other", "", 0) == VFS_OK);
    CHECK(v->xDelete(v, "/other", 1) == SQLITE_IOERR_DIR_FSYNC);
    fixture_sync_rc = 0;
    fixture_stat_rc = VFS_ERR_IO; out = 71;
    CHECK(v->xAccess(v, "/db", SQLITE_ACCESS_EXISTS, &out) == SQLITE_IOERR_ACCESS && out == 71);
    fixture_stat_rc = 0;
    CHECK(v->xAccess(v, "/absent", SQLITE_ACCESS_EXISTS, &out) == SQLITE_OK && out == 0);
    CHECK(v->xAccess(v, "/db", SQLITE_ACCESS_READ, &out) == SQLITE_OK && out == 1);
    CHECK(v->xAccess(v, "/db", SQLITE_ACCESS_READWRITE, &out) == SQLITE_OK && out == 1);
    fixture_readonly = 1;
    CHECK(v->xAccess(v, "/db", SQLITE_ACCESS_READWRITE, &out) == SQLITE_OK && out == 0);
    finish_file(&a, &f);
}
static void readonly(void)
{
    Os32SqliteGroup a;
    Os32File f;
    sqlite3_vfs *v = acquire(OS32_SQLITE_EXEC, 1, &a);
    int flags = 0;
    CHECK(fixture_create(NULL, "/db", "content", 7) == VFS_OK);
    fixture_readonly = 1;
    CHECK(main_open(v, "/db", &f, SQLITE_OPEN_READWRITE, &flags) == SQLITE_OK);
    CHECK((flags & SQLITE_OPEN_READONLY) && !(flags & (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)));
    CHECK(os32Write(&f.base, "X", 1, 0) == SQLITE_READONLY);
    CHECK(os32Truncate(&f.base, 0) == SQLITE_READONLY);
    CHECK(fixture_find("/db", 0)->size == 7 && fixture_find("/db", 0)->data[0] == 'c');
    finish_file(&a, &f);
}
static char created_path[VFS_MAX_PATH];
static int trace_create(void *ctx, const char *path, const void *buf, u32 size)
{
    str_cpy(created_path, path, sizeof(created_path));
    return fixture_create(ctx, path, buf, size);
}
static void fullpath(void)
{
    Os32SqliteGroup a;
    sqlite3_vfs *v = acquire(OS32_SQLITE_EXEC, 1, &a);
    sqlite3 *db = NULL;
    char path[VFS_MAX_PATH];
    resolve_cwd = "/old";
    CHECK(v->xFullPathname(v, "./db", sizeof(path), path) == SQLITE_OK);
    CHECK(!strcmp(path, "/old/db"));
    CHECK(v->xFullPathname(v, "./db", 5, path) == SQLITE_CANTOPEN);
    mock_ops.write_file = trace_create;
    CHECK(os32_sqlite_group_open(&a, "db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db) == SQLITE_OK);
    resolve_cwd = "/else";
    CHECK(sqlite3_exec(db, "CREATE TABLE t(x); INSERT INTO t VALUES(1)", 0, 0, 0) == SQLITE_OK);
    CHECK(!strcmp(created_path, "/old/db-journal"));
    CHECK(!fixture_find("/else/db-journal", 0));
    CHECK(os32_sqlite_group_begin_close(&a) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(os32_sqlite_group_finish(&a, SQLITE_OK) == SQLITE_OK);
}
static void unsupported(void)
{
    const char *names[] = {"fat", "hostdrv", "host"};
    Os32SqliteGroup a;
    Os32File f;
    sqlite3_vfs *v = acquire(OS32_SQLITE_EXEC, 1, &a);
    int i;
    mock_ops.sqlite = NULL;
    for (i = 0; i < 3; i++) {
        mock_ops.name = names[i];
        CHECK(main_open(v, "/new", &f, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_CANTOPEN);
        CHECK(!fixture_find("/new", 0) && vfs_count_sqlite(&a) == 0);
    }
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    CHECK(os32_sqlite_init() == SQLITE_OK);
    fixture_init();
    if (!strcmp(argv[1], "separate")) separate();
    else if (!strcmp(argv[1], "errors")) errors();
    else if (!strcmp(argv[1], "readonly")) readonly();
    else if (!strcmp(argv[1], "unsupported")) unsupported();
    else if (!strcmp(argv[1], "fullpath")) fullpath();
    else duplicate(argv[1]);
    printf("PASS %s\n", argv[1]);
    return 0;
}
