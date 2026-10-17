/* SQLite-entry mocks only. Real group/FD lifetime is tested in db-v50 and IME. */
int os32_sqlite_group_acquire(int kind, int owner, Os32SqliteGroup *out)
{
    static u32 generation;
    (void)kind; (void)owner;
    out->group_index = 0; out->generation = ++generation;
    return SQLITE_OK;
}
int os32_sqlite_group_begin_close(const Os32SqliteGroup *g) { (void)g; return SQLITE_OK; }
int os32_sqlite_group_finish(const Os32SqliteGroup *g, int rc) { (void)g; return rc; }
int os32_sqlite_group_quarantine(const Os32SqliteGroup *g, int stage, int rc)
{ (void)g; (void)stage; (void)rc; return SQLITE_OK; }
int os32_sqlite_group_open(const Os32SqliteGroup *g, const char *path, int flags, sqlite3 **out)
{
    (void)g;
    return flags & SQLITE_OPEN_CREATE ? sqlite3_open(path, out) :
        sqlite3_open_v2(path, out, flags, 0);
}
