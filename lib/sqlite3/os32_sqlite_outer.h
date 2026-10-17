/* Typed outer calls. VFS callbacks keep the active engine and stack. */
#ifndef OS32_SQLITE_OUTER_H
#define OS32_SQLITE_OUTER_H
#include "sqlite3.h"
#include "os32_sqlite_vfs.h"
typedef int (*SqliteExecCallback)(void *, int, char **, char **);
typedef void (*SqliteDestructor)(void *);
int os32_sqlite_close(sqlite3 *db);
int os32_sqlite_prepare_v2(sqlite3 *db, const char *sql, int size, sqlite3_stmt **out, const char **tail);
int os32_sqlite_exec(sqlite3 *db, const char *sql, SqliteExecCallback callback, void *arg, char **error);
int os32_sqlite_step(sqlite3_stmt *stmt);
int os32_sqlite_finalize(sqlite3_stmt *stmt);
int os32_sqlite_reset(sqlite3_stmt *stmt);
int os32_sqlite_bind_int(sqlite3_stmt *stmt, int index, int value);
int os32_sqlite_bind_null(sqlite3_stmt *stmt, int index);
int os32_sqlite_bind_text(sqlite3_stmt *stmt, int index, const char *value, int length, SqliteDestructor destroy);
int os32_sqlite_bind_blob(sqlite3_stmt *stmt, int index, const void *value, int length, SqliteDestructor destroy);
int os32_sqlite_bind_parameter_count(sqlite3_stmt *stmt);
int os32_sqlite_column_count(sqlite3_stmt *stmt);
int os32_sqlite_column_int(sqlite3_stmt *stmt, int column);
double os32_sqlite_column_double(sqlite3_stmt *stmt, int column);
const unsigned char * os32_sqlite_column_text(sqlite3_stmt *stmt, int column);
const void * os32_sqlite_column_blob(sqlite3_stmt *stmt, int column);
int os32_sqlite_column_bytes(sqlite3_stmt *stmt, int column);
int os32_sqlite_column_type(sqlite3_stmt *stmt, int column);
int os32_sqlite_get_autocommit(sqlite3 *db);
int os32_sqlite_errcode(sqlite3 *db);
int os32_sqlite_extended_errcode(sqlite3 *db);
const char * os32_sqlite_errmsg(sqlite3 *db);
const char * os32_sqlite_errstr(int rc);
sqlite3_int64 os32_sqlite_memory_used(void);
int os32_sqlite_outer_group_open(const Os32SqliteGroup *group, const char *path, int flags, sqlite3 **out);
int os32_sqlite_start(void);
int os32_sqlite_outer_db_fd(void *db);
char *os32_sqlite_snprintf(int size, char *out, const char *format, ...);
int sqlite_engine_available(int io);
#ifdef OS32_SQLITE_DIRECT_TEST
#define SQLITE_OUTER_READY(io) 1
#else
#define SQLITE_OUTER_READY(io) sqlite_engine_available(io)
#endif
/* Legacy host fixtures model only the caller/VFS boundary. The engine
 * fixture links these actual thunks and the actual i386 trampoline. */
#if !defined(OS32_SQLITE_DIRECT_TEST) && !defined(OS32_SQLITE_OUTER_IMPL)
#define os32_sqlite_group_open os32_sqlite_outer_group_open
#define sqlite3_snprintf os32_sqlite_snprintf
#define os32_sqlite_db_fd os32_sqlite_outer_db_fd
#define sqlite3_close os32_sqlite_close
#define sqlite3_prepare_v2 os32_sqlite_prepare_v2
#define sqlite3_exec os32_sqlite_exec
#define sqlite3_step os32_sqlite_step
#define sqlite3_finalize os32_sqlite_finalize
#define sqlite3_reset os32_sqlite_reset
#define sqlite3_bind_int os32_sqlite_bind_int
#define sqlite3_bind_null os32_sqlite_bind_null
#define sqlite3_bind_text os32_sqlite_bind_text
#define sqlite3_bind_blob os32_sqlite_bind_blob
#define sqlite3_bind_parameter_count os32_sqlite_bind_parameter_count
#define sqlite3_column_count os32_sqlite_column_count
#define sqlite3_column_int os32_sqlite_column_int
#define sqlite3_column_double os32_sqlite_column_double
#define sqlite3_column_text os32_sqlite_column_text
#define sqlite3_column_blob os32_sqlite_column_blob
#define sqlite3_column_bytes os32_sqlite_column_bytes
#define sqlite3_column_type os32_sqlite_column_type
#define sqlite3_get_autocommit os32_sqlite_get_autocommit
#define sqlite3_errcode os32_sqlite_errcode
#define sqlite3_extended_errcode os32_sqlite_extended_errcode
#define sqlite3_errmsg os32_sqlite_errmsg
#define sqlite3_errstr os32_sqlite_errstr
#define sqlite3_memory_used os32_sqlite_memory_used
#endif
#endif
