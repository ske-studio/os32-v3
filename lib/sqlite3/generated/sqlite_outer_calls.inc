static int stub_close(sqlite3 *db) { (void)db; return OS32_ERR_NOSYS; }
static int stub_prepare_v2(sqlite3 *db, const char *sql, int size, sqlite3_stmt **out, const char **tail) { (void)db; (void)sql; (void)size; (void)out; (void)tail; return OS32_ERR_NOSYS; }
static int stub_exec(sqlite3 *db, const char *sql, SqliteExecCallback callback, void *arg, char **error) { (void)db; (void)sql; (void)callback; (void)arg; (void)error; return OS32_ERR_NOSYS; }
static int stub_step(sqlite3_stmt *stmt) { (void)stmt; return OS32_ERR_NOSYS; }
static int stub_finalize(sqlite3_stmt *stmt) { (void)stmt; return OS32_ERR_NOSYS; }
static int stub_reset(sqlite3_stmt *stmt) { (void)stmt; return OS32_ERR_NOSYS; }
static int stub_bind_int(sqlite3_stmt *stmt, int index, int value) { (void)stmt; (void)index; (void)value; return OS32_ERR_NOSYS; }
static int stub_bind_null(sqlite3_stmt *stmt, int index) { (void)stmt; (void)index; return OS32_ERR_NOSYS; }
static int stub_bind_text(sqlite3_stmt *stmt, int index, const char *value, int length, SqliteDestructor destroy) { (void)stmt; (void)index; (void)value; (void)length; (void)destroy; return OS32_ERR_NOSYS; }
static int stub_bind_blob(sqlite3_stmt *stmt, int index, const void *value, int length, SqliteDestructor destroy) { (void)stmt; (void)index; (void)value; (void)length; (void)destroy; return OS32_ERR_NOSYS; }
static int stub_bind_parameter_count(sqlite3_stmt *stmt) { (void)stmt; return 0; }
static int stub_column_count(sqlite3_stmt *stmt) { (void)stmt; return 0; }
static int stub_column_int(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static double stub_column_double(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static const unsigned char * stub_column_text(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static const void * stub_column_blob(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static int stub_column_bytes(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static int stub_column_type(sqlite3_stmt *stmt, int column) { (void)stmt; (void)column; return 0; }
static int stub_get_autocommit(sqlite3 *db) { (void)db; return 1; }
static int stub_errcode(sqlite3 *db) { (void)db; return OS32_ERR_NOSYS; }
static int stub_extended_errcode(sqlite3 *db) { (void)db; return OS32_ERR_NOSYS; }
static const char * stub_errmsg(sqlite3 *db) { (void)db; return "SQLite engine unavailable"; }
static const char * stub_errstr(int rc) { (void)rc; return "SQLite engine unavailable"; }
static sqlite3_int64 stub_memory_used(void) {  return 0; }
static int stub_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **out) { (void)group; (void)path; (void)flags; (void)out; return OS32_ERR_NOSYS; }
static int stub_db_fd(void *db) { (void)db; return -1; }
static int stub_group_acquire(int kind, int owner, Os32SqliteGroup *out) { (void)kind; (void)owner; (void)out; return OS32_ERR_NOSYS; }
static const char * stub_group_vfs_name(const Os32SqliteGroup *group) { (void)group; return 0; }
static int stub_group_opened(const Os32SqliteGroup *group, void *db) { (void)group; (void)db; return OS32_ERR_NOSYS; }
static int stub_group_begin_close(const Os32SqliteGroup *group) { (void)group; return OS32_ERR_NOSYS; }
static int stub_group_finish(const Os32SqliteGroup *group, int close_rc) { (void)group; (void)close_rc; return OS32_ERR_NOSYS; }
static int stub_group_quarantine(const Os32SqliteGroup *group, int stage, int rc) { (void)group; (void)stage; (void)rc; return OS32_ERR_NOSYS; }
static int stub_group_snapshot(const Os32SqliteGroup *group, Os32SqliteGroupInfo *out) { (void)group; (void)out; return OS32_ERR_NOSYS; }
static int stub_group_for_db(void *db, Os32SqliteGroupInfo *out) { (void)db; (void)out; return OS32_ERR_NOSYS; }
static int stub_canary(void) {  return -1; }
static void stub_dump_vfs(void) {  return; }
static char * stub_vsnprintf(int size, char *out, const char *format, va_list args) { (void)size; (void)out; (void)format; (void)args; return 0; }
static int stub_shutdown(void) {  return OS32_ERR_NOSYS; }
const SqliteModuleApi sqlite_stub_api = {
    .close = stub_close,
    .prepare_v2 = stub_prepare_v2,
    .exec = stub_exec,
    .step = stub_step,
    .finalize = stub_finalize,
    .reset = stub_reset,
    .bind_int = stub_bind_int,
    .bind_null = stub_bind_null,
    .bind_text = stub_bind_text,
    .bind_blob = stub_bind_blob,
    .bind_parameter_count = stub_bind_parameter_count,
    .column_count = stub_column_count,
    .column_int = stub_column_int,
    .column_double = stub_column_double,
    .column_text = stub_column_text,
    .column_blob = stub_column_blob,
    .column_bytes = stub_column_bytes,
    .column_type = stub_column_type,
    .get_autocommit = stub_get_autocommit,
    .errcode = stub_errcode,
    .extended_errcode = stub_extended_errcode,
    .errmsg = stub_errmsg,
    .errstr = stub_errstr,
    .memory_used = stub_memory_used,
    .group_open = stub_group_open,
    .db_fd = stub_db_fd,
    .group_acquire = stub_group_acquire,
    .group_vfs_name = stub_group_vfs_name,
    .group_opened = stub_group_opened,
    .group_begin_close = stub_group_begin_close,
    .group_finish = stub_group_finish,
    .group_quarantine = stub_group_quarantine,
    .group_snapshot = stub_group_snapshot,
    .group_for_db = stub_group_for_db,
    .canary = stub_canary,
    .dump_vfs = stub_dump_vfs,
    .vsnprintf = stub_vsnprintf,
    .shutdown = stub_shutdown,
};
typedef struct { sqlite3 *db; int result; } Call_close;
static void call_close(void *arg) {
    Call_close *f = arg;
    (void)f;
    f->result = sqlite_module_api->close(f->db);
}
int os32_sqlite_close(sqlite3 *db)
{
    Call_close f = {db, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->close(db);
    sqlite_engine_run(call_close, &f, 1);
    return f.result;
}
typedef struct { sqlite3 *db; const char *sql; int size; sqlite3_stmt **out; const char **tail; int result; } Call_prepare_v2;
static void call_prepare_v2(void *arg) {
    Call_prepare_v2 *f = arg;
    (void)f;
    f->result = sqlite_module_api->prepare_v2(f->db, f->sql, f->size, f->out, f->tail);
}
int os32_sqlite_prepare_v2(sqlite3 *db, const char *sql, int size, sqlite3_stmt **out, const char **tail)
{
    Call_prepare_v2 f = {db, sql, size, out, tail, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->prepare_v2(db, sql, size, out, tail);
    sqlite_engine_run(call_prepare_v2, &f, 1);
    return f.result;
}
typedef struct { sqlite3 *db; const char *sql; SqliteExecCallback callback; void *arg; char **error; int result; } Call_exec;
static void call_exec(void *arg) {
    Call_exec *f = arg;
    (void)f;
    f->result = sqlite_module_api->exec(f->db, f->sql, f->callback, f->arg, f->error);
}
int os32_sqlite_exec(sqlite3 *db, const char *sql, SqliteExecCallback callback, void *arg, char **error)
{
    Call_exec f = {db, sql, callback, arg, error, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->exec(db, sql, callback, arg, error);
    sqlite_engine_run(call_exec, &f, 1);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int result; } Call_step;
static void call_step(void *arg) {
    Call_step *f = arg;
    (void)f;
    f->result = sqlite_module_api->step(f->stmt);
}
int os32_sqlite_step(sqlite3_stmt *stmt)
{
    Call_step f = {stmt, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->step(stmt);
    sqlite_engine_run(call_step, &f, 1);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int result; } Call_finalize;
static void call_finalize(void *arg) {
    Call_finalize *f = arg;
    (void)f;
    f->result = sqlite_module_api->finalize(f->stmt);
}
int os32_sqlite_finalize(sqlite3_stmt *stmt)
{
    Call_finalize f = {stmt, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->finalize(stmt);
    sqlite_engine_run(call_finalize, &f, 1);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int result; } Call_reset;
static void call_reset(void *arg) {
    Call_reset *f = arg;
    (void)f;
    f->result = sqlite_module_api->reset(f->stmt);
}
int os32_sqlite_reset(sqlite3_stmt *stmt)
{
    Call_reset f = {stmt, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->reset(stmt);
    sqlite_engine_run(call_reset, &f, 1);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int index; int value; int result; } Call_bind_int;
static void call_bind_int(void *arg) {
    Call_bind_int *f = arg;
    (void)f;
    f->result = sqlite_module_api->bind_int(f->stmt, f->index, f->value);
}
int os32_sqlite_bind_int(sqlite3_stmt *stmt, int index, int value)
{
    Call_bind_int f = {stmt, index, value, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->bind_int(stmt, index, value);
    sqlite_engine_run(call_bind_int, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int index; int result; } Call_bind_null;
static void call_bind_null(void *arg) {
    Call_bind_null *f = arg;
    (void)f;
    f->result = sqlite_module_api->bind_null(f->stmt, f->index);
}
int os32_sqlite_bind_null(sqlite3_stmt *stmt, int index)
{
    Call_bind_null f = {stmt, index, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->bind_null(stmt, index);
    sqlite_engine_run(call_bind_null, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int index; const char *value; int length; SqliteDestructor destroy; int result; } Call_bind_text;
static void call_bind_text(void *arg) {
    Call_bind_text *f = arg;
    (void)f;
    f->result = sqlite_module_api->bind_text(f->stmt, f->index, f->value, f->length, f->destroy);
}
int os32_sqlite_bind_text(sqlite3_stmt *stmt, int index, const char *value, int length, SqliteDestructor destroy)
{
    Call_bind_text f = {stmt, index, value, length, destroy, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->bind_text(stmt, index, value, length, destroy);
    sqlite_engine_run(call_bind_text, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int index; const void *value; int length; SqliteDestructor destroy; int result; } Call_bind_blob;
static void call_bind_blob(void *arg) {
    Call_bind_blob *f = arg;
    (void)f;
    f->result = sqlite_module_api->bind_blob(f->stmt, f->index, f->value, f->length, f->destroy);
}
int os32_sqlite_bind_blob(sqlite3_stmt *stmt, int index, const void *value, int length, SqliteDestructor destroy)
{
    Call_bind_blob f = {stmt, index, value, length, destroy, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->bind_blob(stmt, index, value, length, destroy);
    sqlite_engine_run(call_bind_blob, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int result; } Call_bind_parameter_count;
static void call_bind_parameter_count(void *arg) {
    Call_bind_parameter_count *f = arg;
    (void)f;
    f->result = sqlite_module_api->bind_parameter_count(f->stmt);
}
int os32_sqlite_bind_parameter_count(sqlite3_stmt *stmt)
{
    Call_bind_parameter_count f = {stmt, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->bind_parameter_count(stmt);
    sqlite_engine_run(call_bind_parameter_count, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int result; } Call_column_count;
static void call_column_count(void *arg) {
    Call_column_count *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_count(f->stmt);
}
int os32_sqlite_column_count(sqlite3_stmt *stmt)
{
    Call_column_count f = {stmt, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_count(stmt);
    sqlite_engine_run(call_column_count, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_int;
static void call_column_int(void *arg) {
    Call_column_int *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_int(f->stmt, f->column);
}
int os32_sqlite_column_int(sqlite3_stmt *stmt, int column)
{
    Call_column_int f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_int(stmt, column);
    sqlite_engine_run(call_column_int, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; double result; } Call_column_double;
static void call_column_double(void *arg) {
    Call_column_double *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_double(f->stmt, f->column);
}
double os32_sqlite_column_double(sqlite3_stmt *stmt, int column)
{
    Call_column_double f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_double(stmt, column);
    sqlite_engine_run(call_column_double, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; const unsigned char * result; } Call_column_text;
static void call_column_text(void *arg) {
    Call_column_text *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_text(f->stmt, f->column);
}
const unsigned char * os32_sqlite_column_text(sqlite3_stmt *stmt, int column)
{
    Call_column_text f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_text(stmt, column);
    sqlite_engine_run(call_column_text, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; const void * result; } Call_column_blob;
static void call_column_blob(void *arg) {
    Call_column_blob *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_blob(f->stmt, f->column);
}
const void * os32_sqlite_column_blob(sqlite3_stmt *stmt, int column)
{
    Call_column_blob f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_blob(stmt, column);
    sqlite_engine_run(call_column_blob, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_bytes;
static void call_column_bytes(void *arg) {
    Call_column_bytes *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_bytes(f->stmt, f->column);
}
int os32_sqlite_column_bytes(sqlite3_stmt *stmt, int column)
{
    Call_column_bytes f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_bytes(stmt, column);
    sqlite_engine_run(call_column_bytes, &f, 0);
    return f.result;
}
typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_type;
static void call_column_type(void *arg) {
    Call_column_type *f = arg;
    (void)f;
    f->result = sqlite_module_api->column_type(f->stmt, f->column);
}
int os32_sqlite_column_type(sqlite3_stmt *stmt, int column)
{
    Call_column_type f = {stmt, column, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->column_type(stmt, column);
    sqlite_engine_run(call_column_type, &f, 0);
    return f.result;
}
typedef struct { sqlite3 *db; int result; } Call_get_autocommit;
static void call_get_autocommit(void *arg) {
    Call_get_autocommit *f = arg;
    (void)f;
    f->result = sqlite_module_api->get_autocommit(f->db);
}
int os32_sqlite_get_autocommit(sqlite3 *db)
{
    Call_get_autocommit f = {db, 1};
    if (!sqlite_module_ready()) return sqlite_module_api->get_autocommit(db);
    sqlite_engine_run(call_get_autocommit, &f, 0);
    return f.result;
}
typedef struct { sqlite3 *db; int result; } Call_errcode;
static void call_errcode(void *arg) {
    Call_errcode *f = arg;
    (void)f;
    f->result = sqlite_module_api->errcode(f->db);
}
int os32_sqlite_errcode(sqlite3 *db)
{
    Call_errcode f = {db, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->errcode(db);
    sqlite_engine_run(call_errcode, &f, 0);
    return f.result;
}
typedef struct { sqlite3 *db; int result; } Call_extended_errcode;
static void call_extended_errcode(void *arg) {
    Call_extended_errcode *f = arg;
    (void)f;
    f->result = sqlite_module_api->extended_errcode(f->db);
}
int os32_sqlite_extended_errcode(sqlite3 *db)
{
    Call_extended_errcode f = {db, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->extended_errcode(db);
    sqlite_engine_run(call_extended_errcode, &f, 0);
    return f.result;
}
typedef struct { sqlite3 *db; const char * result; } Call_errmsg;
static void call_errmsg(void *arg) {
    Call_errmsg *f = arg;
    (void)f;
    f->result = sqlite_module_api->errmsg(f->db);
}
const char * os32_sqlite_errmsg(sqlite3 *db)
{
    Call_errmsg f = {db, "SQLite engine unavailable"};
    if (!sqlite_module_ready()) return sqlite_module_api->errmsg(db);
    sqlite_engine_run(call_errmsg, &f, 0);
    return f.result;
}
typedef struct { int rc; const char * result; } Call_errstr;
static void call_errstr(void *arg) {
    Call_errstr *f = arg;
    (void)f;
    f->result = sqlite_module_api->errstr(f->rc);
}
const char * os32_sqlite_errstr(int rc)
{
    Call_errstr f = {rc, "SQLite engine unavailable"};
    if (!sqlite_module_ready()) return sqlite_module_api->errstr(rc);
    sqlite_engine_run(call_errstr, &f, 0);
    return f.result;
}
typedef struct { sqlite3_int64 result; } Call_memory_used;
static void call_memory_used(void *arg) {
    Call_memory_used *f = arg;
    (void)f;
    f->result = sqlite_module_api->memory_used();
}
sqlite3_int64 os32_sqlite_memory_used(void)
{
    Call_memory_used f = {0};
    if (!sqlite_module_ready()) return sqlite_module_api->memory_used();
    sqlite_engine_run(call_memory_used, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; const char *path; int flags; sqlite3 **out; int result; } Call_group_open;
static void call_group_open(void *arg) {
    Call_group_open *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_open(f->group, f->path, f->flags, f->out);
}
int os32_sqlite_outer_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **out)
{
    Call_group_open f = {group, path, flags, out, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_open(group, path, flags, out);
    sqlite_engine_run(call_group_open, &f, 1);
    return f.result;
}
typedef struct { void *db; int result; } Call_db_fd;
static void call_db_fd(void *arg) {
    Call_db_fd *f = arg;
    (void)f;
    f->result = sqlite_module_api->db_fd(f->db);
}
int os32_sqlite_outer_db_fd(void *db)
{
    Call_db_fd f = {db, -1};
    if (!sqlite_module_ready()) return sqlite_module_api->db_fd(db);
    sqlite_engine_run(call_db_fd, &f, 0);
    return f.result;
}
typedef struct { int kind; int owner; Os32SqliteGroup *out; int result; } Call_group_acquire;
static void call_group_acquire(void *arg) {
    Call_group_acquire *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_acquire(f->kind, f->owner, f->out);
}
int os32_sqlite_outer_group_acquire(int kind, int owner, Os32SqliteGroup *out)
{
    Call_group_acquire f = {kind, owner, out, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_acquire(kind, owner, out);
    sqlite_engine_run(call_group_acquire, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; const char * result; } Call_group_vfs_name;
static void call_group_vfs_name(void *arg) {
    Call_group_vfs_name *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_vfs_name(f->group);
}
const char * os32_sqlite_outer_group_vfs_name(const Os32SqliteGroup *group)
{
    Call_group_vfs_name f = {group, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->group_vfs_name(group);
    sqlite_engine_run(call_group_vfs_name, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; void *db; int result; } Call_group_opened;
static void call_group_opened(void *arg) {
    Call_group_opened *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_opened(f->group, f->db);
}
int os32_sqlite_outer_group_opened(const Os32SqliteGroup *group, void *db)
{
    Call_group_opened f = {group, db, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_opened(group, db);
    sqlite_engine_run(call_group_opened, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; int result; } Call_group_begin_close;
static void call_group_begin_close(void *arg) {
    Call_group_begin_close *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_begin_close(f->group);
}
int os32_sqlite_outer_group_begin_close(const Os32SqliteGroup *group)
{
    Call_group_begin_close f = {group, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_begin_close(group);
    sqlite_engine_run(call_group_begin_close, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; int close_rc; int result; } Call_group_finish;
static void call_group_finish(void *arg) {
    Call_group_finish *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_finish(f->group, f->close_rc);
}
int os32_sqlite_outer_group_finish(const Os32SqliteGroup *group, int close_rc)
{
    Call_group_finish f = {group, close_rc, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_finish(group, close_rc);
    sqlite_engine_run(call_group_finish, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; int stage; int rc; int result; } Call_group_quarantine;
static void call_group_quarantine(void *arg) {
    Call_group_quarantine *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_quarantine(f->group, f->stage, f->rc);
}
int os32_sqlite_outer_group_quarantine(const Os32SqliteGroup *group, int stage, int rc)
{
    Call_group_quarantine f = {group, stage, rc, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_quarantine(group, stage, rc);
    sqlite_engine_run(call_group_quarantine, &f, 0);
    return f.result;
}
typedef struct { const Os32SqliteGroup *group; Os32SqliteGroupInfo *out; int result; } Call_group_snapshot;
static void call_group_snapshot(void *arg) {
    Call_group_snapshot *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_snapshot(f->group, f->out);
}
int os32_sqlite_outer_group_snapshot(const Os32SqliteGroup *group, Os32SqliteGroupInfo *out)
{
    Call_group_snapshot f = {group, out, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_snapshot(group, out);
    sqlite_engine_run(call_group_snapshot, &f, 0);
    return f.result;
}
typedef struct { void *db; Os32SqliteGroupInfo *out; int result; } Call_group_for_db;
static void call_group_for_db(void *arg) {
    Call_group_for_db *f = arg;
    (void)f;
    f->result = sqlite_module_api->group_for_db(f->db, f->out);
}
int os32_sqlite_outer_group_for_db(void *db, Os32SqliteGroupInfo *out)
{
    Call_group_for_db f = {db, out, SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->group_for_db(db, out);
    sqlite_engine_run(call_group_for_db, &f, 0);
    return f.result;
}
typedef struct { int result; } Call_canary;
static void call_canary(void *arg) {
    Call_canary *f = arg;
    (void)f;
    f->result = sqlite_module_api->canary();
}
int os32_sqlite_outer_canary(void)
{
    Call_canary f = {-1};
    if (!sqlite_module_ready()) return sqlite_module_api->canary();
    sqlite_engine_run(call_canary, &f, 0);
    return f.result;
}
typedef struct { int unused; } Call_dump_vfs;
static void call_dump_vfs(void *arg) {
    Call_dump_vfs *f = arg;
    (void)f;
    sqlite_module_api->dump_vfs();
}
void os32_sqlite_outer_dump_vfs(void)
{
    Call_dump_vfs f = {0};
    if (!sqlite_module_ready()) { sqlite_module_api->dump_vfs(); return; }
    sqlite_engine_run(call_dump_vfs, &f, 0);
    return;
}
typedef struct { int size; char *out; const char *format; va_list args; char * result; } Call_vsnprintf;
static void call_vsnprintf(void *arg) {
    Call_vsnprintf *f = arg;
    (void)f;
    f->result = sqlite_module_api->vsnprintf(f->size, f->out, f->format, f->args);
}
char * os32_sqlite_outer_vsnprintf(int size, char *out, const char *format, va_list args)
{
    Call_vsnprintf f = {size, out, format, args, 0};
    if (!sqlite_module_ready()) return sqlite_module_api->vsnprintf(size, out, format, args);
    sqlite_engine_run(call_vsnprintf, &f, 0);
    return f.result;
}
typedef struct { int result; } Call_shutdown;
static void call_shutdown(void *arg) {
    Call_shutdown *f = arg;
    (void)f;
    f->result = sqlite_module_api->shutdown();
}
int os32_sqlite_outer_shutdown(void)
{
    Call_shutdown f = {SQLITE_MISUSE};
    if (!sqlite_module_ready()) return sqlite_module_api->shutdown();
    sqlite_engine_run(call_shutdown, &f, 0);
    return f.result;
}
