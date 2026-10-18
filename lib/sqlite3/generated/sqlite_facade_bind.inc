static int module_close(sqlite3 *db) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_close(db);
}
static int module_prepare_v2(sqlite3 *db, const char *sql, int size, sqlite3_stmt **out, const char **tail) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_prepare_v2(db, sql, size, out, tail);
}
static int module_exec(sqlite3 *db, const char *sql, SqliteExecCallback callback, void *arg, char **error) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_exec(db, sql, callback, arg, error);
}
static int module_step(sqlite3_stmt *stmt) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_step(stmt);
}
static int module_finalize(sqlite3_stmt *stmt) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_finalize(stmt);
}
static int module_reset(sqlite3_stmt *stmt) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_reset(stmt);
}
static int module_bind_int(sqlite3_stmt *stmt, int index, int value) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_bind_int(stmt, index, value);
}
static int module_bind_null(sqlite3_stmt *stmt, int index) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_bind_null(stmt, index);
}
static int module_bind_text(sqlite3_stmt *stmt, int index, const char *value, int length, SqliteDestructor destroy) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_bind_text(stmt, index, value, length, destroy);
}
static int module_bind_blob(sqlite3_stmt *stmt, int index, const void *value, int length, SqliteDestructor destroy) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_bind_blob(stmt, index, value, length, destroy);
}
static int module_bind_parameter_count(sqlite3_stmt *stmt) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_bind_parameter_count(stmt);
}
static int module_column_count(sqlite3_stmt *stmt) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_count(stmt);
}
static int module_column_int(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_int(stmt, column);
}
static double module_column_double(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_double(stmt, column);
}
static const unsigned char * module_column_text(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_text(stmt, column);
}
static const void * module_column_blob(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_blob(stmt, column);
}
static int module_column_bytes(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_bytes(stmt, column);
}
static int module_column_type(sqlite3_stmt *stmt, int column) {
    os32_sqlite_allocation_context((stmt ? sqlite3_db_handle(stmt) : 0), 0);
    return sqlite3_column_type(stmt, column);
}
static int module_get_autocommit(sqlite3 *db) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_get_autocommit(db);
}
static int module_errcode(sqlite3 *db) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_errcode(db);
}
static int module_extended_errcode(sqlite3 *db) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_extended_errcode(db);
}
static const char * module_errmsg(sqlite3 *db) {
    os32_sqlite_allocation_context(db, 0);
    return sqlite3_errmsg(db);
}
static const char * module_errstr(int rc) {
    os32_sqlite_allocation_context(0, 0);
    return sqlite3_errstr(rc);
}
static sqlite3_int64 module_memory_used(void) {
    os32_sqlite_allocation_context(0, 0);
    return sqlite3_memory_used();
}
static int module_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **out) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_open(group, path, flags, out);
}
static int module_db_fd(void *db) {
    os32_sqlite_allocation_context(db, 0);
    return os32_sqlite_db_fd(db);
}
static int module_group_acquire(int kind, int owner, Os32SqliteGroup *out) {
    os32_sqlite_allocation_context(0, 0);
    return os32_sqlite_group_acquire(kind, owner, out);
}
static const char * module_group_vfs_name(const Os32SqliteGroup *group) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_vfs_name(group);
}
static int module_group_opened(const Os32SqliteGroup *group, void *db) {
    os32_sqlite_allocation_context(db, group);
    return os32_sqlite_group_opened(group, db);
}
static int module_group_begin_close(const Os32SqliteGroup *group) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_begin_close(group);
}
static int module_group_finish(const Os32SqliteGroup *group, int close_rc) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_finish(group, close_rc);
}
static int module_group_quarantine(const Os32SqliteGroup *group, int stage, int rc) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_quarantine(group, stage, rc);
}
static int module_group_snapshot(const Os32SqliteGroup *group, Os32SqliteGroupInfo *out) {
    os32_sqlite_allocation_context(0, group);
    return os32_sqlite_group_snapshot(group, out);
}
static int module_group_for_db(void *db, Os32SqliteGroupInfo *out) {
    os32_sqlite_allocation_context(db, 0);
    return os32_sqlite_group_for_db(db, out);
}
static int module_canary(void) {
    os32_sqlite_allocation_context(0, 0);
    return memsys5_check_canary();
}
static void module_dump_vfs(void) {
    os32_sqlite_allocation_context(0, 0);
    os32_sqlite_dump_vfs();
}
static char * module_vsnprintf(int size, char *out, const char *format, va_list args) {
    os32_sqlite_allocation_context(0, 0);
    return sqlite3_vsnprintf(size, out, format, args);
}
static int module_shutdown(void) {
    os32_sqlite_allocation_context(0, 0);
    return os32_sqlite_shutdown();
}
static int module_boot_check(int live, int reserve) {
    os32_sqlite_allocation_context(0, 0);
    return os32_sqlite_boot_check(live, reserve);
}
static const SqliteModuleApi api = {
    .close = module_close,
    .prepare_v2 = module_prepare_v2,
    .exec = module_exec,
    .step = module_step,
    .finalize = module_finalize,
    .reset = module_reset,
    .bind_int = module_bind_int,
    .bind_null = module_bind_null,
    .bind_text = module_bind_text,
    .bind_blob = module_bind_blob,
    .bind_parameter_count = module_bind_parameter_count,
    .column_count = module_column_count,
    .column_int = module_column_int,
    .column_double = module_column_double,
    .column_text = module_column_text,
    .column_blob = module_column_blob,
    .column_bytes = module_column_bytes,
    .column_type = module_column_type,
    .get_autocommit = module_get_autocommit,
    .errcode = module_errcode,
    .extended_errcode = module_extended_errcode,
    .errmsg = module_errmsg,
    .errstr = module_errstr,
    .memory_used = module_memory_used,
    .group_open = module_group_open,
    .db_fd = module_db_fd,
    .group_acquire = module_group_acquire,
    .group_vfs_name = module_group_vfs_name,
    .group_opened = module_group_opened,
    .group_begin_close = module_group_begin_close,
    .group_finish = module_group_finish,
    .group_quarantine = module_group_quarantine,
    .group_snapshot = module_group_snapshot,
    .group_for_db = module_group_for_db,
    .canary = module_canary,
    .dump_vfs = module_dump_vfs,
    .vsnprintf = module_vsnprintf,
    .shutdown = module_shutdown,
    .boot_check = module_boot_check,
};
