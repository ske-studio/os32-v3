/* ======================================================================== */
/*  SQLITE_STANDALONE.C — SQLite ユーザー空間スタンドアロンテスト             */
/*                                                                          */
/*  SQLite エンジンをカーネル拡張域を経由せず、外部プログラムとして           */
/*  ユーザー空間 (0x400000+) にリンクして直接実行する。                       */
/*  CREATE TABLE の Page Fault がカーネル配置問題か SQLite ポート自体の       */
/*  バグかを切り分ける。                                                     */
/* ======================================================================== */

#define OS32_DBG_SERIAL
#include "os32api.h"
#include "rt/dbgserial.h"
#include "os32_sqlite_config.h"
#include "sqlite3.h"

extern KernelAPI *kapi;
#define api kapi

/* sqlite_user_vfs.c で定義 */
extern int user_sqlite_init(void);
extern int user_memsys5_check_canary(void);

static int test_count = 0;
static int pass_count = 0;

static void ok(const char *label)
{
    test_count++;
    pass_count++;
    api->kprintf(ATTR_GREEN, "  [OK] %s\n", label);
    DBGF("  [OK] %s", label);
}

static void ng(const char *label, const char *detail)
{
    test_count++;
    api->kprintf(ATTR_RED, "  [NG] %s: %s\n", label, detail);
    DBGF("  [NG] %s: %s", label, detail);
}

static void header(const char *title)
{
    api->kprintf(ATTR_CYAN, "\n=== %s ===\n", title);
    DBGF("\n=== %s ===", title);
}

/* ======================================================================== */
/*  テスト1: MEMSYS5 初期化                                                  */
/* ======================================================================== */
static int test_init(void)
{
    int rc;
    void *p;

    header("Test 1: SQLite Init + MEMSYS5");

    rc = user_sqlite_init();
    if (rc != 0) {
        ng("sqlite_init", "failed");
        return 0;
    }
    ok("sqlite_init");

    /* malloc/free テスト */
    p = sqlite3_malloc(256);
    if (!p) {
        ng("sqlite3_malloc(256)", "returned NULL");
        return 0;
    }
    api->kprintf(ATTR_WHITE, "  malloc ptr=%x\n", (u32)p);
    ok("sqlite3_malloc(256)");

    sqlite3_free(p);
    ok("sqlite3_free");

    /* canary チェック */
    rc = user_memsys5_check_canary();
    if (rc != 0) {
        ng("memsys5 canary", "corrupted");
        return 0;
    }
    ok("memsys5 canary intact");

    return 1;
}

/* ======================================================================== */
/*  テスト2: SELECT 1                                                        */
/* ======================================================================== */
static int test_select1(void)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    header("Test 2: SELECT 1 (memory DB)");

    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        ng("sqlite3_open", "failed");
        return 0;
    }
    ok("sqlite3_open(:memory:)");

    rc = sqlite3_prepare_v2(db, "SELECT 1", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        ng("prepare SELECT 1", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("prepare SELECT 1");

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int val = sqlite3_column_int(stmt, 0);
        api->kprintf(ATTR_WHITE, "  result = %d\n", val);
        if (val == 1) {
            ok("step SELECT 1 = 1");
        } else {
            ng("step SELECT 1", "unexpected value");
        }
    } else {
        ng("step SELECT 1", "no row");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    ok("close");

    /* canary チェック */
    rc = user_memsys5_check_canary();
    if (rc != 0) {
        ng("canary after SELECT", "corrupted");
        return 0;
    }
    ok("canary intact after SELECT");

    return 1;
}

/* ======================================================================== */
/*  テスト3: CREATE TABLE (★ 核心テスト)                                     */
/* ======================================================================== */
static int test_create_table(void)
{
    sqlite3 *db;
    int rc;
    u32 esp_val;

    header("Test 3: CREATE TABLE (memory DB)");

    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        ng("sqlite3_open", "failed");
        return 0;
    }
    ok("sqlite3_open(:memory:)");

    /* ESP をログ */
    __asm__ volatile("mov %%esp, %0" : "=r"(esp_val));
    api->kprintf(ATTR_WHITE, "  pre-CREATE ESP=%x canary=%d\n",
                 esp_val, user_memsys5_check_canary());

    /* ★ ここで Page Fault が発生するか？ */
    api->kprintf(ATTR_YELLOW, "  >>> CREATE TABLE executing...\n");
    DBG("  >>> CREATE TABLE executing...");
    rc = sqlite3_exec(db,
        "CREATE TABLE test(id INTEGER PRIMARY KEY, val TEXT)",
        0, 0, 0);
    DBGF("  <<< CREATE TABLE returned rc=%d", rc);

    /* ここに到達すれば成功 */
    __asm__ volatile("mov %%esp, %0" : "=r"(esp_val));
    api->kprintf(ATTR_WHITE, "  post-CREATE ESP=%x canary=%d rc=%d\n",
                 esp_val, user_memsys5_check_canary(), rc);

    if (rc != SQLITE_OK) {
        ng("CREATE TABLE", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("CREATE TABLE");

    /* canary チェック */
    rc = user_memsys5_check_canary();
    if (rc != 0) {
        ng("canary after CREATE", "corrupted");
        sqlite3_close(db);
        return 0;
    }
    ok("canary intact after CREATE TABLE");

    sqlite3_close(db);
    ok("close");
    return 1;
}

/* ======================================================================== */
/*  テスト4: INSERT + SELECT (CREATE TABLE 成功時のみ)                       */
/* ======================================================================== */
static int test_insert_select(void)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    header("Test 4: INSERT + SELECT");

    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        ng("sqlite3_open", "failed");
        return 0;
    }

    rc = sqlite3_exec(db,
        "CREATE TABLE test(id INTEGER PRIMARY KEY, val TEXT)",
        0, 0, 0);
    if (rc != SQLITE_OK) {
        ng("CREATE TABLE", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("CREATE TABLE");

    /* INSERT */
    rc = sqlite3_exec(db,
        "INSERT INTO test VALUES(1, 'Hello OS32')", 0, 0, 0);
    if (rc != SQLITE_OK) {
        ng("INSERT 1", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("INSERT 1");

    rc = sqlite3_exec(db,
        "INSERT INTO test VALUES(2, 'SQLite Standalone')", 0, 0, 0);
    if (rc != SQLITE_OK) {
        ng("INSERT 2", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("INSERT 2");

    /* SELECT */
    rc = sqlite3_prepare_v2(db,
        "SELECT id, val FROM test ORDER BY id", -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        ng("prepare SELECT", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }
    ok("prepare SELECT");

    {
        int row_count = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char *val = (const char *)sqlite3_column_text(stmt, 1);
            api->kprintf(ATTR_WHITE, "  row %d: id=%d val=\"%s\"\n",
                         row_count, id, val ? val : "(null)");
            row_count++;
        }
        if (row_count == 2) {
            ok("SELECT (2 rows)");
        } else {
            ng("SELECT", "expected 2 rows");
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    /* 最終 canary チェック */
    rc = user_memsys5_check_canary();
    if (rc != 0) {
        ng("final canary", "corrupted");
        return 0;
    }
    ok("final canary intact");

    return 1;
}

/* ======================================================================== */
/*  エントリポイント                                                         */
/* ======================================================================== */
int main(int argc, char **argv, KernelAPI *k)
{
    u32 esp_start;
    int level = 4; /* デフォルト: 全テスト */

    (void)k;

    dbg_init(kapi);

    /* 引数でテストレベルを指定: 1=init, 2=select, 3=create, 4=insert */
    if (argc >= 2 && argv[1][0] >= '1' && argv[1][0] <= '4') {
        level = argv[1][0] - '0';
    }

    __asm__ volatile("mov %%esp, %0" : "=r"(esp_start));

    api->kprintf(ATTR_CYAN, "sqlite_standalone: SQLite user-space test\n");
    api->kprintf(ATTR_WHITE, "  KAPI ver=%d  initial ESP=%x  level=%d\n",
                 kapi->version, esp_start, level);
    DBG("=== sqlite_standalone START ===");
    DBGF("  ESP=%x  level=%d", esp_start, level);

    /* テスト実行 */
    if (level >= 1) {
        api->kprintf(ATTR_WHITE, ">> Starting Test 1 (init)...\n");
        test_init();
        api->kprintf(ATTR_WHITE, "<< Test 1 done\n");
    }
    if (level >= 2) {
        api->kprintf(ATTR_WHITE, ">> Starting Test 2 (SELECT 1)...\n");
        test_select1();
        api->kprintf(ATTR_WHITE, "<< Test 2 done\n");
    }
    if (level >= 3) {
        api->kprintf(ATTR_WHITE, ">> Starting Test 3 (CREATE TABLE)...\n");
        test_create_table();
        api->kprintf(ATTR_WHITE, "<< Test 3 done\n");
    }
    if (level >= 4) {
        api->kprintf(ATTR_WHITE, ">> Starting Test 4 (INSERT+SELECT)...\n");
        test_insert_select();
        api->kprintf(ATTR_WHITE, "<< Test 4 done\n");
    }

    /* サマリ */
    api->kprintf(ATTR_CYAN, "\n=== Result: %d/%d passed ===\n",
                 pass_count, test_count);
    if (pass_count == test_count) {
        api->kprintf(ATTR_GREEN, "All tests passed!\n");
    } else {
        api->kprintf(ATTR_RED, "Some tests FAILED.\n");
    }

    return 0;
}

