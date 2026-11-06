/* Real hsync registration and warning branch, with SQLite-backed KAPI seams. */
#include <stdarg.h>
#include <string.h>
#include "sqlite3.h"
#include "os32/os32_kapi_shared.h"
#include "hsync_protect.inc"
static sqlite3 *connection;
static sqlite3_stmt *statement;
static int warnings;
static char g_root[OS32_MAX_PATH];
static int open_existing(const char *path, int mode)
{ (void)path; (void)mode; return 0; }
static int prepare_only(int db, const char *sql)
{ (void)db; return sqlite3_prepare_v2(connection, sql, -1, &statement, 0) == SQLITE_OK ? 0 : -1; }
static int step(int db)
{
    int rc;
    (void)db;
    rc = sqlite3_step(statement);
    return rc == SQLITE_DONE ? DB_STATUS_DONE : rc == SQLITE_ROW ? DB_STATUS_ROW : DB_STATUS_ERROR;
}
static int col_int(int db, int col)
{ (void)db; return sqlite3_column_int(statement, col); }
static const char *col_text(int db, int col)
{ (void)db; return (const char *)sqlite3_column_text(statement, col); }
static int finalize(int db)
{ int rc; (void)db; rc = sqlite3_finalize(statement); statement = 0; return rc == SQLITE_OK ? 0 : -1; }
static int close_db(int db) { (void)db; return 0; }
static int stat_path(const char *path, OS32_Stat *st)
{ (void)path; (void)st; return 0; }
static void warning(int attr, const char *fmt, ...)
{ (void)attr; (void)fmt; warnings++; }
static struct {
    int version;
    int (*db_open_existing)(const char *, int);
    int (*db_prepare_only)(int, const char *);
    int (*db_step)(int);
    int (*db_column_int)(int, int);
    const char *(*db_column_text)(int, int);
    int (*db_finalize)(int);
    int (*db_close)(int);
    int (*sys_stat)(const char *, OS32_Stat *);
    void (*kprintf)(int, const char *, ...);
} boundary = {81, open_existing, prepare_only, step, col_int, col_text,
              finalize, close_db, stat_path, warning}, *api = &boundary;
#include "hsync_sql_source.inc"
int check_hsync_default(sqlite3 *db)
{
    connection = db;
    warnings = 0;
    /* Deliberately dirty prior-invocation state; product must reset it. */
    hsp_fep_all_db = 1;
    strcpy(hsp_fep_user_path, "/etc/old.db");
#include "hsync_sql_warning.inc"
    return !warnings && !hsp_fep_all_db &&
        !strcmp(hsp_fep_user_path, HSP_FEP_USER_PATH);
}
