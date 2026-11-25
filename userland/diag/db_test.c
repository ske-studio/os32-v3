/* ======================================================================== */
/*  DB_TEST.C — KAPI 経由 SQLite DB テストプログラム                         */
/*                                                                          */
/*  Phase 2 検証: 外部プログラムから KAPI テーブル経由で SQLite を操作し、     */
/*  CREATE TABLE / INSERT / SELECT の一連の動作を確認する。                   */
/*  テスト対象: メモリDB (:memory:) + ファイルDB (/db/test.db)               */
/* ======================================================================== */

#include "os32api.h"
#include "libos32db.h"
#include "rt/testresult.h"
/* crt0_c.c で定義される KernelAPI ポインタ */
extern KernelAPI *kapi;
#define api kapi

static void print_ok(const char *label)
{
    api->kprintf(ATTR_GREEN, "  [OK] %s\n", label);
}

static void print_fail(const char *label, const char *detail)
{
    api->kprintf(ATTR_RED, "  [NG] %s: %s\n", label, detail);
}

static void print_header(const char *title)
{
    api->kprintf(ATTR_CYAN, "\n=== %s ===\n", title);
}

/* ======================================================================== */
/*  テスト1: メモリDB — 基本 CRUD                                           */
/* ======================================================================== */
static int test_memory_db(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;

    print_header("Test 1: Memory DB CRUD");

    /* 1.1 オープン */
    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open(:memory:)", "handle < 0");
        return 0;
    }
    print_ok("db_open(:memory:)");

    /* 1.2 テーブル作成 */
    rc = db_exec(db, "CREATE TABLE memo(id INTEGER PRIMARY KEY, text TEXT)");
    if (rc < 0) {
        print_fail("CREATE TABLE", db_errmsg());
        db_close(db);
        return 0;
    }
    print_ok("CREATE TABLE");

    /* 1.3 データ挿入 */
    rc = db_exec(db, "INSERT INTO memo(text) VALUES('Hello OS32')");
    if (rc < 0) {
        print_fail("INSERT 1", db_errmsg());
        db_close(db);
        return 0;
    }
    print_ok("INSERT 1");

    rc = db_exec(db, "INSERT INTO memo(text) VALUES('SQLite on PC-98')");
    if (rc < 0) {
        print_fail("INSERT 2", db_errmsg());
        db_close(db);
        return 0;
    }
    print_ok("INSERT 2");

    rc = db_exec(db, "INSERT INTO memo(text) VALUES('KAPI IPC works!')");
    if (rc < 0) {
        print_fail("INSERT 3", db_errmsg());
        db_close(db);
        return 0;
    }
    print_ok("INSERT 3");

    /* 1.4 SELECT クエリ */
    rc = db_query(db, "SELECT id, text FROM memo ORDER BY id");
    if (rc < 0) {
        print_fail("SELECT", db_errmsg());
        db_close(db);
        return 0;
    }

    if (rc == DB_STATUS_ROW) {
        int row_count = 0;
        do {
            i32 id = db_column_int(0);
            const char *text = db_column_text(1);
            api->kprintf(ATTR_WHITE, "  row %d: id=%d text=\"%s\"\n",
                         row_count, (int)id, text);
            row_count++;
        } while (db_step(db) == DB_STATUS_ROW);

        if (row_count == 3) {
            print_ok("SELECT (3 rows)");
        } else {
            print_fail("SELECT", "expected 3 rows");
            pass = 0;
        }
    } else if (rc == DB_STATUS_DONE) {
        print_fail("SELECT", "no rows returned");
        pass = 0;
    }

    /* 1.5 クローズ */
    rc = db_close(db);
    if (rc < 0) {
        print_fail("db_close", "failed");
        pass = 0;
    } else {
        print_ok("db_close");
    }

    return pass;
}

/* ======================================================================== */
/*  テスト2: 複数DB接続                                                      */
/* ======================================================================== */
static int test_multiple_connections(void)
{
    db_handle_t db1, db2;
    int rc;
    int pass = 1;

    print_header("Test 2: Multiple Connections");

    db1 = db_open(":memory:");
    db2 = db_open(":memory:");

    if (db1 < 0 || db2 < 0) {
        print_fail("open 2 DBs", "handle < 0");
        if (db1 >= 0) db_close(db1);
        if (db2 >= 0) db_close(db2);
        return 0;
    }
    api->kprintf(ATTR_WHITE, "  db1=%d, db2=%d\n", db1, db2);
    print_ok("open 2 DBs");

    /* db1 にテーブル作成 */
    rc = db_exec(db1, "CREATE TABLE t1(val INTEGER)");
    if (rc < 0) { print_fail("CREATE on db1", db_errmsg()); pass = 0; }
    else print_ok("CREATE on db1");

    /* db2 にテーブル作成 */
    rc = db_exec(db2, "CREATE TABLE t2(val INTEGER)");
    if (rc < 0) { print_fail("CREATE on db2", db_errmsg()); pass = 0; }
    else print_ok("CREATE on db2");

    /* db1 に INSERT */
    rc = db_exec(db1, "INSERT INTO t1 VALUES(100)");
    if (rc < 0) { print_fail("INSERT db1", db_errmsg()); pass = 0; }
    else print_ok("INSERT db1");

    /* db2 に INSERT */
    rc = db_exec(db2, "INSERT INTO t2 VALUES(200)");
    if (rc < 0) { print_fail("INSERT db2", db_errmsg()); pass = 0; }
    else print_ok("INSERT db2");

    /* db1 から SELECT */
    rc = db_query(db1, "SELECT val FROM t1");
    if (rc == DB_STATUS_ROW) {
        i32 val = db_column_int(0);
        if (val == 100) {
            print_ok("SELECT db1 = 100");
        } else {
            print_fail("SELECT db1", "unexpected value");
            pass = 0;
        }
    } else {
        print_fail("SELECT db1", "no rows");
        pass = 0;
    }

    /* db2 から SELECT */
    rc = db_query(db2, "SELECT val FROM t2");
    if (rc == DB_STATUS_ROW) {
        i32 val = db_column_int(0);
        if (val == 200) {
            print_ok("SELECT db2 = 200");
        } else {
            print_fail("SELECT db2", "unexpected value");
            pass = 0;
        }
    } else {
        print_fail("SELECT db2", "no rows");
        pass = 0;
    }

    db_close(db1);
    db_close(db2);
    print_ok("close both DBs");

    return pass;
}

/* ======================================================================== */
/*  テスト3: エラーハンドリング                                               */
/* ======================================================================== */
static int test_error_handling(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;

    print_header("Test 3: Error Handling");

    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open", "failed");
        return 0;
    }

    /* 存在しないテーブルへの INSERT → エラーが返ること */
    rc = db_exec(db, "INSERT INTO nonexistent VALUES(1)");
    if (rc < 0) {
        api->kprintf(ATTR_WHITE, "  errmsg: %s\n", db_errmsg());
        print_ok("error on bad table (expected)");
    } else {
        print_fail("error handling", "should have failed");
        pass = 0;
    }

    /* 不正な SQL → エラーが返ること */
    rc = db_exec(db, "THIS IS NOT SQL");
    if (rc < 0) {
        print_ok("error on bad SQL (expected)");
    } else {
        print_fail("error handling", "bad SQL should fail");
        pass = 0;
    }

    /* 不正なハンドル */
    rc = db_exec(99, "SELECT 1");
    if (rc < 0) {
        print_ok("error on bad handle (expected)");
    } else {
        print_fail("error handling", "bad handle should fail");
        pass = 0;
    }

    /* 票 TASK_DB_ERRSTR の受入 E2 — **戻り値を実際に読む**。
     * 2026-09-17 まで db_last_error() はカーネル番地を返していたので、
     * CPL=3 のここで読むと #PF で死んだ。読めること自体が確認事項。 */
    {
        const char *m = db_last_error(99);
        if (m && m[0] != '\0') {
            api->kprintf(ATTR_WHITE, "  bad-handle errmsg: %s\n", m);
            print_ok("db_last_error(bad handle) is readable from CPL=3");
        } else {
            print_fail("db_last_error(bad handle)", "empty or NULL");
            pass = 0;
        }
    }

    /* 受入 E3 — 結果セットが無い状態の db_column_text()。
     * 以前はカーネルの .rodata の "" を返していたので、m[0] を見るだけで死んだ。 */
    {
        const char *m = db_column_text(0);
        if (m) {
            api->kprintf(ATTR_WHITE, "  no-result coltext: \"%s\"\n", m);
            print_ok("db_column_text(no result) is readable from CPL=3");
        } else {
            print_fail("db_column_text(no result)", "NULL");
            pass = 0;
        }
    }

    db_close(db);
    return pass;
}

/* ======================================================================== */
/*  テスト4: db_finalize — 結果セット途中放棄                                 */
/* ======================================================================== */
static int test_finalize(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;

    print_header("Test 4: db_finalize");

    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open", "failed");
        return 0;
    }

    /* テーブル作成 + 複数行挿入 */
    db_exec(db, "CREATE TABLE items(id INTEGER PRIMARY KEY, val TEXT)");
    db_exec(db, "INSERT INTO items VALUES(1, 'aaa')");
    db_exec(db, "INSERT INTO items VALUES(2, 'bbb')");
    db_exec(db, "INSERT INTO items VALUES(3, 'ccc')");

    /* クエリ開始 → 1行だけ読んで finalize */
    rc = db_query(db, "SELECT id, val FROM items ORDER BY id");
    if (rc != DB_STATUS_ROW) {
        print_fail("db_query", "expected ROW");
        db_close(db);
        return 0;
    }
    print_ok("db_query (first row)");

    rc = db_finalize(db);
    if (rc < 0) {
        print_fail("db_finalize", "returned error");
        pass = 0;
    } else {
        print_ok("db_finalize");
    }

    /* finalize 後に新しいクエリが成功すること */
    rc = db_query(db, "SELECT COUNT(*) FROM items");
    if (rc == DB_STATUS_ROW) {
        i32 cnt = db_column_int(0);
        if (cnt == 3) {
            print_ok("new query after finalize (count=3)");
        } else {
            print_fail("count check", "unexpected value");
            pass = 0;
        }
    } else {
        print_fail("new query after finalize", "no rows");
        pass = 0;
    }

    db_close(db);
    return pass;
}

/* ======================================================================== */
/*  テスト5: db_last_error — エラーメッセージ取得                              */
/* ======================================================================== */
static int test_last_error(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;
    const char *msg;

    print_header("Test 5: db_last_error");

    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open", "failed");
        return 0;
    }

    /* 不正な SQL を実行してエラーを発生させる */
    rc = db_exec(db, "INSERT INTO nonexistent VALUES(1)");
    if (rc >= 0) {
        print_fail("expected error", "should have failed");
        db_close(db);
        return 0;
    }
    print_ok("error triggered (expected)");

    /* db_last_error でメッセージ取得 */
    msg = db_last_error(db);
    if (msg && msg[0] != '\0') {
        api->kprintf(ATTR_WHITE, "  last_error: \"%s\"\n", msg);
        print_ok("db_last_error returned message");
    } else {
        print_fail("db_last_error", "empty or NULL");
        pass = 0;
    }

    /* db_errmsg (共有メモリ版) でも取得できること */
    msg = db_errmsg();
    if (msg && msg[0] != '\0') {
        api->kprintf(ATTR_WHITE, "  db_errmsg: \"%s\"\n", msg);
        print_ok("db_errmsg returned message");
    } else {
        print_fail("db_errmsg", "empty or NULL");
        pass = 0;
    }

    db_close(db);
    return pass;
}

/* ======================================================================== */
/*  テスト6: 接続上限 — DB_MAX_CONNECTIONS 超過                               */
/* ======================================================================== */
static int test_connection_limit(void)
{
    db_handle_t handles[DB_MAX_CONNECTIONS + 1];
    int i;
    int pass = 1;
    db_handle_t extra;

    print_header("Test 6: Connection Limit");

    /* DB_MAX_CONNECTIONS 個オープン */
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        handles[i] = db_open(":memory:");
        if (handles[i] < 0) {
            api->kprintf(ATTR_RED, "  [NG] db_open #%d failed\n", i);
            pass = 0;
            break;
        }
    }
    if (pass) {
        api->kprintf(ATTR_WHITE, "  opened %d connections\n",
                     DB_MAX_CONNECTIONS);
        print_ok("open DB_MAX_CONNECTIONS");
    }

    /* 上限超過 → -1 が返ること */
    extra = db_open(":memory:");
    if (extra < 0) {
        print_ok("overflow rejected (expected)");
    } else {
        print_fail("overflow", "should have returned -1");
        db_close(extra);
        pass = 0;
    }

    /* 全クローズ */
    for (i = 0; i < DB_MAX_CONNECTIONS; i++) {
        if (handles[i] >= 0) db_close(handles[i]);
    }
    print_ok("close all");

    return pass;
}

/* ======================================================================== */
/*  テスト7: 二重 close — クラッシュしないこと                                 */
/* ======================================================================== */
static int test_double_close(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;

    print_header("Test 7: Double Close");

    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open", "failed");
        return 0;
    }
    print_ok("db_open");

    rc = db_close(db);
    if (rc < 0) {
        print_fail("first close", "failed");
        pass = 0;
    } else {
        print_ok("first db_close");
    }

    /* 二重 close → -1 が返ること (クラッシュしないこと) */
    rc = db_close(db);
    if (rc < 0) {
        print_ok("second db_close rejected (expected)");
    } else {
        print_fail("double close", "should have returned -1");
        pass = 0;
    }

    return pass;
}

/* ======================================================================== */
/*  テスト8: ファイル DB 永続化 — ext2 上の /db/test.db                       */
/* ======================================================================== */
static int test_file_db(void)
{
    db_handle_t db;
    int rc;
    int pass = 1;

    print_header("Test 8: File DB Persistence");

    /* 8.1 ファイル DB を作成して書き込み */
    db = db_open("/db/test.db");
    if (db < 0) {
        print_fail("db_open(/db/test.db)", "failed (is /db/ dir present?)");
        return 0;
    }
    print_ok("db_open(/db/test.db)");

    rc = db_exec(db, "CREATE TABLE IF NOT EXISTS items(id INTEGER PRIMARY KEY, name TEXT, val INTEGER)");
    if (rc < 0) {
        print_fail("CREATE TABLE", db_errmsg());
        db_close(db);
        return 0;
    }
    print_ok("CREATE TABLE");

    /* 既存データを消して綺麗な状態にする */
    db_exec(db, "DELETE FROM items");

    rc = db_exec(db, "INSERT INTO items VALUES(1, 'apple', 100)");
    if (rc < 0) { print_fail("INSERT 1", db_errmsg()); db_close(db); return 0; }
    rc = db_exec(db, "INSERT INTO items VALUES(2, 'banana', 200)");
    if (rc < 0) { print_fail("INSERT 2", db_errmsg()); db_close(db); return 0; }
    rc = db_exec(db, "INSERT INTO items VALUES(3, 'cherry', 300)");
    if (rc < 0) { print_fail("INSERT 3", db_errmsg()); db_close(db); return 0; }
    print_ok("INSERT x3");

    /* 8.2 close → 再オープンで永続化確認 */
    db_close(db);
    print_ok("db_close (write session)");

    db = db_open("/db/test.db");
    if (db < 0) {
        print_fail("db_open (reopen)", "failed");
        return 0;
    }
    print_ok("db_open (reopen)");

    /* SELECT で INSERT されたデータが残っているか確認 */
    rc = db_query(db, "SELECT COUNT(*) FROM items");
    if (rc == DB_STATUS_ROW) {
        i32 cnt = db_column_int(0);
        if (cnt == 3) {
            print_ok("SELECT COUNT(*) = 3 (persisted)");
        } else {
            api->kprintf(ATTR_RED, "  [NG] count=%d, expected 3\n", (int)cnt);
            pass = 0;
        }
    } else {
        print_fail("SELECT COUNT", "no rows");
        pass = 0;
    }

    /* 8.3 UPDATE テスト */
    rc = db_exec(db, "UPDATE items SET val=999 WHERE id=2");
    if (rc < 0) {
        print_fail("UPDATE", db_errmsg());
        pass = 0;
    } else {
        /* 更新後の値を確認 */
        rc = db_query(db, "SELECT val FROM items WHERE id=2");
        if (rc == DB_STATUS_ROW) {
            i32 val = db_column_int(0);
            if (val == 999) {
                print_ok("UPDATE val=999 WHERE id=2");
            } else {
                api->kprintf(ATTR_RED, "  [NG] val=%d, expected 999\n", (int)val);
                pass = 0;
            }
        } else {
            print_fail("SELECT after UPDATE", "no rows");
            pass = 0;
        }
    }

    /* 8.4 DELETE テスト */
    rc = db_exec(db, "DELETE FROM items WHERE id=3");
    if (rc < 0) {
        print_fail("DELETE", db_errmsg());
        pass = 0;
    } else {
        rc = db_query(db, "SELECT COUNT(*) FROM items");
        if (rc == DB_STATUS_ROW) {
            i32 cnt = db_column_int(0);
            if (cnt == 2) {
                print_ok("DELETE (count=2 after delete)");
            } else {
                api->kprintf(ATTR_RED, "  [NG] count=%d, expected 2\n", (int)cnt);
                pass = 0;
            }
        } else {
            print_fail("SELECT after DELETE", "no rows");
            pass = 0;
        }
    }

    /* 8.5 クリーンアップ: テスト用テーブルを削除 */
    db_exec(db, "DROP TABLE items");
    db_close(db);
    print_ok("cleanup & close");

    return pass;
}

/* ======================================================================== */
/*  テスト9: MEMSYS5 メモリ使用量モニタリング                                 */
/* ======================================================================== */
static int test_mem_usage(void)
{
    db_handle_t db;
    u32 mem_before;
    u32 mem_after_open;
    u32 mem_after_ops;
    u32 mem_after_close;
    int pass = 1;

    print_header("Test 9: Memory Usage Monitoring");

    mem_before = db_mem_used();
    api->kprintf(ATTR_WHITE, "  MEMSYS5 before:     %u bytes\n", mem_before);

    db = db_open(":memory:");
    if (db < 0) {
        print_fail("db_open", "failed");
        return 0;
    }

    mem_after_open = db_mem_used();
    api->kprintf(ATTR_WHITE, "  MEMSYS5 after open: %u bytes (+%u)\n",
                 mem_after_open, mem_after_open - mem_before);

    if (mem_after_open > mem_before) {
        print_ok("memory increased after open");
    } else {
        print_fail("memory after open", "expected increase");
        pass = 0;
    }

    /* テーブル作成 + INSERT でメモリ使用量がさらに増加 */
    db_exec(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, data TEXT)");
    db_exec(db, "INSERT INTO t VALUES(1, 'test data for memory monitoring')");
    db_exec(db, "INSERT INTO t VALUES(2, 'more test data here')");

    mem_after_ops = db_mem_used();
    api->kprintf(ATTR_WHITE, "  MEMSYS5 after ops:  %u bytes (+%u)\n",
                 mem_after_ops, mem_after_ops - mem_after_open);

    if (mem_after_ops > mem_after_open) {
        print_ok("memory increased after CREATE+INSERT");
    } else {
        /* メモリDB では内部キャッシュの挙動により増加しない場合もある */
        api->kprintf(ATTR_YELLOW, "  [WARN] memory did not increase (may be cached)\n");
    }

    db_close(db);
    mem_after_close = db_mem_used();
    api->kprintf(ATTR_WHITE, "  MEMSYS5 after close: %u bytes\n", mem_after_close);

    if (mem_after_close <= mem_after_ops) {
        print_ok("memory decreased/stable after close");
    } else {
        print_fail("memory after close", "unexpected increase");
        pass = 0;
    }

    return pass;
}

/* ======================================================================== */
/*  エントリポイント                                                         */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *k)
{
    int total = 0;
    int passed = 0;

    (void)argc; (void)argv;
    (void)k;

    api->kprintf(ATTR_CYAN, "db_test: KAPI SQLite integration test\n");
    api->kprintf(ATTR_CYAN, "KAPI version: %d\n", kapi->version);

    /* テスト実行 */
    total++; if (test_memory_db()) passed++;
    total++; if (test_multiple_connections()) passed++;
    total++; if (test_error_handling()) passed++;
    total++; if (test_finalize()) passed++;
    total++; if (test_last_error()) passed++;
    total++; if (test_connection_limit()) passed++;
    total++; if (test_double_close()) passed++;
    total++; if (test_file_db()) passed++;
    total++; if (test_mem_usage()) passed++;

    /* サマリ */
    return os32_test_summary(api, "db_test", passed, total);
}

