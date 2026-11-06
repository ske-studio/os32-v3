/* Shared FEP connection lifetime; reopen policy belongs to each store caller. */
#include "ime_store.h"
#include "../../lib/sqlite3/os32_sqlite_outer.h"
#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#endif

int fep_conn_close(FepConn *conn)
{
    int rc = SQLITE_OK;
    if (!conn) return OS32_ERR_INVAL;
    if (conn->isolated) return OS32_ERR_BUSY;
    if (!conn->group.generation) return 0;
    if (os32_sqlite_group_begin_close(&conn->group) != SQLITE_OK) {
        conn->isolated = 1;
        return OS32_ERR_BUSY;
    }
    void **statements[] = { &conn->exact, &conn->prefix, &conn->learn };
    for (u32 i = 0; i < sizeof(statements) / sizeof(statements[0]); i++) {
        if (*statements[i]) {
            /* finalize rc describes the last evaluation, not destruction. */
            (void)sqlite3_finalize((sqlite3_stmt *)*statements[i]);
            *statements[i] = 0;
        }
    }
    if (conn->db) rc = sqlite3_close((sqlite3 *)conn->db);
    if (rc == SQLITE_OK) conn->db = 0;
    if (os32_sqlite_group_finish(&conn->group, rc) != SQLITE_OK) {
        conn->isolated = 1;
        return OS32_ERR_BUSY;
    }
    conn->group.generation = 0;
    return 0;
}

int fep_conn_ready(FepConn *conn)
{
    int fd;
    if (!conn) return OS32_ERR_INVAL;
    if (conn->isolated) return OS32_ERR_BUSY;
    if (!conn->db) return OS32_ERR_AGAIN;
    if (!SQLITE_OUTER_READY(1)) return OS32_ERR_IO;
    fd = os32_sqlite_db_fd(conn->db);
    if (fd < 0) return OS32_ERR_IO;
    if (vfs_fd_is_stale(fd)) return OS32_ERR_STALE;
    return 0;
}
