#define OS32_SQLITE_OUTER_IMPL
#include "os32_sqlite_outer.h"
#include "sqlite_engine.h"

typedef struct { sqlite3 *db; int result; } Call_close;
static void call_close(void *arg)
{
    Call_close *f = arg;
    f->result = sqlite3_close(f->db);
}
int os32_sqlite_close(sqlite3 *db)
{
    Call_close f = {db, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_close, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3 *db; const char *sql; int size; sqlite3_stmt **out; const char **tail; int result; } Call_prepare_v2;
static void call_prepare_v2(void *arg)
{
    Call_prepare_v2 *f = arg;
    f->result = sqlite3_prepare_v2(f->db, f->sql, f->size, f->out, f->tail);
}
int os32_sqlite_prepare_v2(sqlite3 *db, const char *sql, int size, sqlite3_stmt **out, const char **tail)
{
    Call_prepare_v2 f = {db, sql, size, out, tail, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_prepare_v2, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3 *db; const char *sql; SqliteExecCallback callback; void *arg; char **error; int result; } Call_exec;
static void call_exec(void *arg)
{
    Call_exec *f = arg;
    f->result = sqlite3_exec(f->db, f->sql, f->callback, f->arg, f->error);
}
int os32_sqlite_exec(sqlite3 *db, const char *sql, SqliteExecCallback callback, void *arg, char **error)
{
    Call_exec f = {db, sql, callback, arg, error, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_exec, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int result; } Call_step;
static void call_step(void *arg)
{
    Call_step *f = arg;
    f->result = sqlite3_step(f->stmt);
}
int os32_sqlite_step(sqlite3_stmt *stmt)
{
    Call_step f = {stmt, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_step, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int result; } Call_finalize;
static void call_finalize(void *arg)
{
    Call_finalize *f = arg;
    f->result = sqlite3_finalize(f->stmt);
}
int os32_sqlite_finalize(sqlite3_stmt *stmt)
{
    Call_finalize f = {stmt, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_finalize, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int result; } Call_reset;
static void call_reset(void *arg)
{
    Call_reset *f = arg;
    f->result = sqlite3_reset(f->stmt);
}
int os32_sqlite_reset(sqlite3_stmt *stmt)
{
    Call_reset f = {stmt, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_reset, &f, 1)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int index; int value; int result; } Call_bind_int;
static void call_bind_int(void *arg)
{
    Call_bind_int *f = arg;
    f->result = sqlite3_bind_int(f->stmt, f->index, f->value);
}
int os32_sqlite_bind_int(sqlite3_stmt *stmt, int index, int value)
{
    Call_bind_int f = {stmt, index, value, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_bind_int, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int index; int result; } Call_bind_null;
static void call_bind_null(void *arg)
{
    Call_bind_null *f = arg;
    f->result = sqlite3_bind_null(f->stmt, f->index);
}
int os32_sqlite_bind_null(sqlite3_stmt *stmt, int index)
{
    Call_bind_null f = {stmt, index, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_bind_null, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int index; const char *value; int length; SqliteDestructor destroy; int result; } Call_bind_text;
static void call_bind_text(void *arg)
{
    Call_bind_text *f = arg;
    f->result = sqlite3_bind_text(f->stmt, f->index, f->value, f->length, f->destroy);
}
int os32_sqlite_bind_text(sqlite3_stmt *stmt, int index, const char *value, int length, SqliteDestructor destroy)
{
    Call_bind_text f = {stmt, index, value, length, destroy, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_bind_text, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int index; const void *value; int length; SqliteDestructor destroy; int result; } Call_bind_blob;
static void call_bind_blob(void *arg)
{
    Call_bind_blob *f = arg;
    f->result = sqlite3_bind_blob(f->stmt, f->index, f->value, f->length, f->destroy);
}
int os32_sqlite_bind_blob(sqlite3_stmt *stmt, int index, const void *value, int length, SqliteDestructor destroy)
{
    Call_bind_blob f = {stmt, index, value, length, destroy, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_bind_blob, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int result; } Call_bind_parameter_count;
static void call_bind_parameter_count(void *arg)
{
    Call_bind_parameter_count *f = arg;
    f->result = sqlite3_bind_parameter_count(f->stmt);
}
int os32_sqlite_bind_parameter_count(sqlite3_stmt *stmt)
{
    Call_bind_parameter_count f = {stmt, 0};
    if (!sqlite_engine_run(call_bind_parameter_count, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int result; } Call_column_count;
static void call_column_count(void *arg)
{
    Call_column_count *f = arg;
    f->result = sqlite3_column_count(f->stmt);
}
int os32_sqlite_column_count(sqlite3_stmt *stmt)
{
    Call_column_count f = {stmt, 0};
    if (!sqlite_engine_run(call_column_count, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_int;
static void call_column_int(void *arg)
{
    Call_column_int *f = arg;
    f->result = sqlite3_column_int(f->stmt, f->column);
}
int os32_sqlite_column_int(sqlite3_stmt *stmt, int column)
{
    Call_column_int f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_int, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; double result; } Call_column_double;
static void call_column_double(void *arg)
{
    Call_column_double *f = arg;
    f->result = sqlite3_column_double(f->stmt, f->column);
}
double os32_sqlite_column_double(sqlite3_stmt *stmt, int column)
{
    Call_column_double f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_double, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; const unsigned char * result; } Call_column_text;
static void call_column_text(void *arg)
{
    Call_column_text *f = arg;
    f->result = sqlite3_column_text(f->stmt, f->column);
}
const unsigned char * os32_sqlite_column_text(sqlite3_stmt *stmt, int column)
{
    Call_column_text f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_text, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; const void * result; } Call_column_blob;
static void call_column_blob(void *arg)
{
    Call_column_blob *f = arg;
    f->result = sqlite3_column_blob(f->stmt, f->column);
}
const void * os32_sqlite_column_blob(sqlite3_stmt *stmt, int column)
{
    Call_column_blob f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_blob, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_bytes;
static void call_column_bytes(void *arg)
{
    Call_column_bytes *f = arg;
    f->result = sqlite3_column_bytes(f->stmt, f->column);
}
int os32_sqlite_column_bytes(sqlite3_stmt *stmt, int column)
{
    Call_column_bytes f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_bytes, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3_stmt *stmt; int column; int result; } Call_column_type;
static void call_column_type(void *arg)
{
    Call_column_type *f = arg;
    f->result = sqlite3_column_type(f->stmt, f->column);
}
int os32_sqlite_column_type(sqlite3_stmt *stmt, int column)
{
    Call_column_type f = {stmt, column, 0};
    if (!sqlite_engine_run(call_column_type, &f, 0)) return 0;
    return f.result;
}

typedef struct { sqlite3 *db; int result; } Call_get_autocommit;
static void call_get_autocommit(void *arg)
{
    Call_get_autocommit *f = arg;
    f->result = sqlite3_get_autocommit(f->db);
}
int os32_sqlite_get_autocommit(sqlite3 *db)
{
    Call_get_autocommit f = {db, 1};
    if (!sqlite_engine_run(call_get_autocommit, &f, 0)) return 1;
    return f.result;
}

typedef struct { sqlite3 *db; int result; } Call_errcode;
static void call_errcode(void *arg)
{
    Call_errcode *f = arg;
    f->result = sqlite3_errcode(f->db);
}
int os32_sqlite_errcode(sqlite3 *db)
{
    Call_errcode f = {db, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_errcode, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3 *db; int result; } Call_extended_errcode;
static void call_extended_errcode(void *arg)
{
    Call_extended_errcode *f = arg;
    f->result = sqlite3_extended_errcode(f->db);
}
int os32_sqlite_extended_errcode(sqlite3 *db)
{
    Call_extended_errcode f = {db, SQLITE_MISUSE};
    if (!sqlite_engine_run(call_extended_errcode, &f, 0)) return SQLITE_MISUSE;
    return f.result;
}

typedef struct { sqlite3 *db; const char * result; } Call_errmsg;
static void call_errmsg(void *arg)
{
    Call_errmsg *f = arg;
    f->result = sqlite3_errmsg(f->db);
}
const char * os32_sqlite_errmsg(sqlite3 *db)
{
    Call_errmsg f = {db, "SQLite engine unavailable"};
    if (!sqlite_engine_run(call_errmsg, &f, 0)) return "SQLite engine unavailable";
    return f.result;
}

typedef struct { int rc; const char * result; } Call_errstr;
static void call_errstr(void *arg)
{
    Call_errstr *f = arg;
    f->result = sqlite3_errstr(f->rc);
}
const char * os32_sqlite_errstr(int rc)
{
    Call_errstr f = {rc, "SQLite engine unavailable"};
    if (!sqlite_engine_run(call_errstr, &f, 0)) return "SQLite engine unavailable";
    return f.result;
}

typedef struct {  sqlite3_int64 result; } Call_memory_used;
static void call_memory_used(void *arg)
{
    Call_memory_used *f = arg;
    f->result = sqlite3_memory_used();
}
sqlite3_int64 os32_sqlite_memory_used(void)
{
    Call_memory_used f = {0};
    if (!sqlite_engine_run(call_memory_used, &f, 0)) return 0;
    return f.result;
}

#include "os32_sqlite_vfs.h"
#include <stdarg.h>
static void call_init(void *arg) { *(int *)arg = os32_sqlite_init(); }
int os32_sqlite_start(void)
{
    int rc = SQLITE_MISUSE;
    sqlite_engine_run(call_init, &rc, 0);
    return rc;
}
typedef struct { void *db; int result; } Call_fd;
static void call_fd(void *arg)
{
    Call_fd *f = arg;
    f->result = os32_sqlite_db_fd(f->db);
}
int os32_sqlite_outer_db_fd(void *db)
{
    Call_fd f = {db, -1};
    sqlite_engine_run(call_fd, &f, 0);
    return f.result;
}
typedef struct { int size; char *out; const char *format; va_list args; } Call_format;
static void call_format(void *arg)
{
    Call_format *f = arg;
    sqlite3_vsnprintf(f->size, f->out, f->format, f->args);
}
char *os32_sqlite_snprintf(int size, char *out, const char *format, ...)
{
    Call_format f;
    int ok;
    f.size = size; f.out = out; f.format = format;
    va_start(f.args, format);
    ok = sqlite_engine_run(call_format, &f, 0);
    va_end(f.args);
    return ok ? out : 0;
}

typedef struct {
    const Os32SqliteGroup *group;
    const char *path;
    int flags;
    sqlite3 **out;
    int result;
} Call_group_open;
static void call_group_open(void *arg)
{
    Call_group_open *f = arg;
    f->result = os32_sqlite_group_open(f->group, f->path, f->flags, f->out);
}
int os32_sqlite_outer_group_open(const Os32SqliteGroup *group, const char *path,
                                int flags, sqlite3 **out)
{
    Call_group_open f = {group, path, flags, out, SQLITE_MISUSE};
    sqlite_engine_run(call_group_open, &f, 1);
    return f.result;
}
