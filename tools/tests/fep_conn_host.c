/* Compile the product lifetime code with typed outer/VFS fault injection. */
#include "kernel/ime/ime_conn.c"
static int order, bad, begin_rc, close_rc, finish_rc, stale, fd = 7, available = 1;
static void expect(int step) { if (++order != step) bad = 1; }
int sqlite_engine_available(int io) { return available && io; }
int os32_sqlite_outer_db_fd(void *db) { (void)db; return fd; }
int vfs_fd_is_stale(int value) { if (value != fd) bad = 1; return stale; }
int os32_sqlite_outer_group_begin_close(const Os32SqliteGroup *g)
{ (void)g; expect(1); return begin_rc; }
int os32_sqlite_finalize(sqlite3_stmt *stmt)
{ expect((int)(unsigned long)stmt); return SQLITE_IOERR; }
int os32_sqlite_close(sqlite3 *db)
{ if (db != (void *)1) bad = 1; expect(5); return close_rc; }
static FepConn *active;
int os32_sqlite_outer_group_finish(const Os32SqliteGroup *g, int rc)
{
    (void)g; expect(6);
    if (rc != close_rc || (rc == SQLITE_OK && active->db)) bad = 1;
    return finish_rc;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void setup(FepConn *c)
{
    *c = (FepConn){0}; c->db = (void *)1;
    c->exact = (void *)2; c->prefix = (void *)3; c->learn = (void *)4;
    c->group.generation = 1; c->path[0] = '/';
    active = c; order = bad = begin_rc = close_rc = finish_rc = 0;
}
static int test(void)
{
    static FepConn c;
    CHECK(fep_conn_close(0) == OS32_ERR_INVAL);
    CHECK(fep_conn_ready(0) == OS32_ERR_INVAL);
    CHECK(fep_conn_close(&c) == 0);
    CHECK(fep_conn_ready(&c) == OS32_ERR_AGAIN);
    setup(&c);
    CHECK(fep_conn_ready(&c) == 0 && order == 0);
    stale = 1; CHECK(fep_conn_ready(&c) == OS32_ERR_STALE && order == 0);
    stale = 0; fd = -1; CHECK(fep_conn_ready(&c) == OS32_ERR_IO);
    fd = 7; available = 0; CHECK(fep_conn_ready(&c) == OS32_ERR_IO);
    available = 1;
    CHECK(fep_conn_close(&c) == 0 && order == 6 && !bad);
    CHECK(!c.db && !c.exact && !c.prefix && !c.learn && !c.group.generation);
    CHECK(c.path[0] == '/' && fep_conn_close(&c) == 0 && order == 6);
    setup(&c); begin_rc = SQLITE_BUSY;
    CHECK(fep_conn_close(&c) == OS32_ERR_BUSY && c.isolated && order == 1);
    CHECK(c.db && c.exact && c.group.generation);
    CHECK(fep_conn_ready(&c) == OS32_ERR_BUSY);
    CHECK(fep_conn_close(&c) == OS32_ERR_BUSY && order == 1);
    setup(&c); close_rc = finish_rc = SQLITE_BUSY;
    CHECK(fep_conn_close(&c) == OS32_ERR_BUSY && c.isolated && c.db && !bad);
    CHECK(c.group.generation && !c.exact && !c.prefix && !c.learn);
    setup(&c); finish_rc = SQLITE_BUSY;
    CHECK(fep_conn_close(&c) == OS32_ERR_BUSY && c.isolated && !c.db && !bad);
    CHECK(c.group.generation);
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
