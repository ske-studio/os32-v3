/* T4c-3: real IME, bundled product-config SQLite/MEMSYS5, VFS/FD,
 * ISO9660 and ATAPI. Only ATA ports, owner/TTY and export destination are
 * boundaries. Build clean media before mounting; never change a live image.
 * The service/alt-stack transaction itself remains in the T4c-2 suite.
 */
#define OS32_SQLITE_DIRECT_TEST 1
#define SQLITE_CD_HOST_MAIN cd_contract_main
#include "sqlite_cd_host.c"

char *kstrncat(char *dst, const char *src, u32 size)
{
    u32 n = kstrlen(dst);
    if (n < size) kstrncpy(dst + n, src, size - n);
    return dst;
}
void serial_puts(const char *s) { }
int utf8_strlen(const u8 *s)
{
    int n = 0;
    while (*s) { if ((*s & 0xc0) != 0x80) n++; s++; }
    return n;
}

/* Observe attempts, including CREATE IF NOT EXISTS on an existing table and
 * a learning prepare which SQLite would accept on a read-only connection.
 * VFS write0 alone cannot detect either regression. */
static unsigned create_attempts, learn_prepares;
static int observed_exec(sqlite3 *db, const char *sql,
                         int (*cb)(void *, int, char **, char **), void *arg, char **err)
{
    if (strstr(sql, "CREATE ")) create_attempts++;
    return sqlite3_exec(db, sql, cb, arg, err);
}
static int observed_prepare(sqlite3 *db, const char *sql, int n,
                            sqlite3_stmt **out, const char **tail)
{
    if (strstr(sql, "INSERT INTO dict_user")) learn_prepares++;
    return sqlite3_prepare_v2(db, sql, n, out, tail);
}
#define sqlite3_exec observed_exec
#define sqlite3_prepare_v2 observed_prepare
#include "../../kernel/ime_dict.c"
#undef sqlite3_exec
#undef sqlite3_prepare_v2

/* A small writable auxiliary mount receives CSV output. The source DB and
 * every journal stay on the real CD path. This is not a second SQLite heap. */
static char csv[1024];
static u32 csv_size;
static int csv_exists;
static void *out_mount(int id) { return csv; }
static int out_stat(void *ctx, const char *path, OS32_Stat *st)
{
    if (!strcmp(path, "/")) {
        memset(st, 0, sizeof(*st)); st->st_mode = OS_S_IFDIR; return VFS_OK;
    }
    if (!csv_exists) return VFS_ERR_NOTFOUND;
    memset(st, 0, sizeof(*st));
    st->st_mode = OS_S_IFREG | OS_S_IRUSR | OS_S_IWUSR; st->st_size = csv_size;
    return VFS_OK;
}
static int out_size(void *ctx, const char *path, u32 *size)
{
    if (!csv_exists) return VFS_ERR_NOTFOUND;
    *size = csv_size; return VFS_OK;
}
static int out_create(void *ctx, const char *path, const void *data, u32 size)
{
    CHECK(size < sizeof(csv));
    memcpy(csv, data, size); csv[size] = 0;
    csv_exists = 1; csv_size = size; return VFS_OK;
}
static int out_write(void *ctx, const char *path, const void *data, u32 size, u32 off)
{
    CHECK(off + size < sizeof(csv));
    memcpy(csv + off, data, size); csv[off + size] = 0;
    if (csv_size < off + size) csv_size = off + size;
    return (int)size;
}
static VfsOps out_ops = {
    .name = "fep-csv", .mount = out_mount, .stat = out_stat,
    .get_file_size = out_size, .write_file = out_create, .write_stream = out_write
};

static void fep_media(int user)
{
    sqlite3 *db = NULL;
    sqlite3_int64 bytes;
    unsigned char *data;
    u8 *root = g_media_a.img + ROOT_LBA * SEC;
    u32 off = 0;
    CHECK(sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(sqlite3_exec(db,
        "PRAGMA page_size=1024;"
        "CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INT,cost INT);"
        "CREATE INDEX idx_dict_yomi ON dict(yomi);"
        "INSERT INTO dict VALUES('か','科',1,10),('にほんご','日本語',2,20);",
        NULL, NULL, NULL) == SQLITE_OK);
    if (user) CHECK(sqlite3_exec(db,
        "CREATE TABLE dict_user(yomi TEXT NOT NULL,kanji TEXT NOT NULL,"
        "freq INTEGER DEFAULT 1,last_ts INTEGER DEFAULT 0,PRIMARY KEY(yomi,kanji));"
        "CREATE INDEX idx_user_yomi ON dict_user(yomi);"
        "INSERT INTO dict_user VALUES('か','蚊',7,0);", NULL, NULL, NULL) == SQLITE_OK);
    data = sqlite3_serialize(db, "main", &bytes, 0);
    CHECK(data && bytes > 0 && bytes <= 32 * SEC);
    memcpy(g_media_a.img + BIG_LBA_A * SEC, data, (size_t)bytes);
    sqlite3_free(data); CHECK(sqlite3_close(db) == SQLITE_OK);
    memset(root, 0, SEC);
    off += put_rec(root + off, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\0", 1);
    off += put_rec(root + off, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\1", 1);
    put_rec(root + off, SUB_LBA, SEC, ISO_FLAG_DIRECTORY, "DB", 2);
    root = g_media_a.img + SUB_LBA * SEC;
    memset(root, 0, SEC);
    off = put_rec(root, SUB_LBA, SEC, ISO_FLAG_DIRECTORY, "\0", 1);
    off += put_rec(root + off, ROOT_LBA, SEC, ISO_FLAG_DIRECTORY, "\1", 1);
    put_rec(root + off, BIG_LBA_A, (u32)bytes, 0, "FEP.DB;1", 8);
}
static void conversion(IME_Dict *dict, int user)
{
    IME_Result result[IME_MAX_RESULTS];
    os32_sqlite_allocation_context(dict->db, &dict->group);
    CHECK(ime_dict_search(dict, "か", result, IME_MAX_RESULTS) == (user ? 2 : 1));
    CHECK(!strcmp(result[0].kanji, user ? "蚊" : "科"));
    CHECK(ime_dict_search(dict, "にほん", result, IME_MAX_RESULTS) == 1);
    CHECK(!strcmp(result[0].kanji, "日本語"));
}

/* Real app SQL runs until the admission gate returns NOMEM. Smaller real
 * sqlite3_malloc requests then fill the rounded remainder exactly to the
 * ILP32 product APP+SYSTEM cap. All allocations remain live during FEP work.
 * Context selection is the same API used by generated module adapters. */
static void pressure(IME_Dict *dict, int user)
{
    Os32SqliteGroup group;
    sqlite3 *app = NULL;
    static void *held[8192];
    unsigned count = 0, rows = 0;
    const unsigned limit = 319488u;
    CHECK(BUDGET_CAPACITY - budget->reserve == limit);
    CHECK(os32_sqlite_group_acquire(OS32_SQLITE_EXEC, 2, &group) == SQLITE_OK);
    os32_sqlite_allocation_context(NULL, &group);
    CHECK(os32_sqlite_group_open(&group, ":memory:", SQLITE_OPEN_READWRITE, &app) == SQLITE_OK);
    CHECK(sqlite3_exec(app, "CREATE TABLE pressure(x)", NULL, NULL, NULL) == SQLITE_OK);
    int rc;
    while ((rc = sqlite3_exec(app, "INSERT INTO pressure VALUES(zeroblob(4096))",
                             NULL, NULL, NULL)) == SQLITE_OK) CHECK(++rows < 200);
    CHECK(rc == SQLITE_NOMEM && rows > 0);
    while (budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] < limit) {
        CHECK(count < sizeof(held) / sizeof(held[0]));
        held[count] = sqlite3_malloc(1);
        CHECK(held[count]); count++;
    }
    CHECK(budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] == limit);
    CHECK(sqlite3_malloc(1) == NULL);
    conversion(dict, user);
    ime_dict_learn(dict, "か", "科");
    CHECK(!dict->learn_stmt && memsys5_check_canary() == 0);
    CHECK(budget->live[BUDGET_APP] + budget->live[BUDGET_SYSTEM] == limit);
    printf("PASS pressure user=%d app=%u system=%u cap=%u fep=%u rows=%u\n",
           user, budget->live[BUDGET_APP], budget->live[BUDGET_SYSTEM], limit,
           budget->live[BUDGET_FEP], rows);
    os32_sqlite_allocation_context(app, &group);
    while (count) sqlite3_free(held[--count]);
    close_db(&group, app);
    os32_sqlite_allocation_context(dict->db, &dict->group);
    CHECK(budget->live[BUDGET_APP] == 0);
}
int main(int argc, char **argv)
{
    IME_Dict dict = {0};
    IME_UserEntry entries[4];
    Os32SqliteGroup group;
    sqlite3 *app = NULL;
    sqlite3_file *f;
    unsigned char *before;
    int user;
    CHECK(argc == 2 && sizeof(void *) == 4);
    user = !strcmp(argv[1], "user");
    CHECK(os32_sqlite_init() == SQLITE_OK);
    g_big_size = 32 * SEC; /* Keep the host-only media inside the newlib heap. */
    setup_np2();
    fep_media(user);
    before = malloc(g_media_a.secs * SEC); CHECK(before);
    memcpy(before, g_media_a.img, g_media_a.secs * SEC);
    vfs_register_fs(&iso9660_ops);
    CHECK(vfs_mount("/", "cd0", "iso9660") == VFS_OK);
    /* Simulate the coordinator decision. Production CD wiring is T5b-5. */
    CHECK(vfs_sqlite_allow_immutable("/") == VFS_OK);
    iso9660_ops.write_file = count_write;
    iso9660_ops.write_stream = count_stream;
    iso9660_ops.unlink = count_unlink;
    vfs_register_fs(&out_ops);
    CHECK(vfs_mount("/out", "hd1", "fep-csv") == VFS_OK);
    /* Boot has no FEP db yet; its group acquisition is followed by FEP
     * adapter context selection in the product. Select it here for open. */
    budget->context = BUDGET_FEP;
    CHECK(ime_dict_open(&dict, "/db/fep.db") == 0);
    CHECK(create_attempts == 0);
    CHECK(learn_prepares == 0);
    CHECK(dict.readonly && dict.no_user_table == !user && !dict.learn_stmt);
    CHECK(os32_sqlite_boot_check(1, OS32_SQLITE_FEP_RESERVE) == SQLITE_OK);
    conversion(&dict, user);
    ime_dict_learn(&dict, "か", "科");
    CHECK(ime_user_delete(&dict, "か", "蚊") == OS32_ERR_ROFS);
    CHECK(ime_user_delete(&dict, "か", NULL) == OS32_ERR_ROFS);
    CHECK(ime_user_clear(&dict) == OS32_ERR_ROFS);
    CHECK(ime_user_list(&dict, NULL, entries, 4) == user);
    if (user) CHECK(!strcmp(entries[0].kanji, "蚊") && entries[0].freq == 7);
    CHECK(ime_user_list(&dict, "か", entries, 4) == user);
    CHECK(ime_user_list(&dict, "なし", entries, 4) == 0);
    CHECK(ime_user_export(&dict, "/out/user.csv") == 0);
    CHECK(csv_exists && !strcmp(csv, user ? "か,蚊,7\n" : ""));
    CHECK(writes == 0);
    CHECK(ime_user_export(&dict, "/user.csv") == -2); /* Destination is RO. */
    int denied_writes = writes; /* The explicit RO destination open above. */
    f = db_file(dict.db);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_SHARED) == SQLITE_OK);
    CHECK(f->pMethods->xUnlock(f, SQLITE_LOCK_NONE) == SQLITE_OK);
    CHECK(f->pMethods->xLock(f, SQLITE_LOCK_RESERVED) == SQLITE_READONLY);
    cd_group(OS32_SQLITE_EXEC, 2, &group);
    os32_sqlite_allocation_context(NULL, &group);
    CHECK(os32_sqlite_group_open(&group, "/db/fep.db", SQLITE_OPEN_READONLY, &app) == SQLITE_OK);
    CHECK(sqlite3_exec(app, "SELECT * FROM dict", NULL, NULL, NULL) == SQLITE_OK);
    close_db(&group, app);
    pressure(&dict, user);
    CHECK(writes == denied_writes);
    OS32_Stat st;
    CHECK(vfs_stat("/db/fep.db-journal", &st) == VFS_ERR_NOTFOUND);
    CHECK(vfs_stat("/db/fep.db-wal", &st) == VFS_ERR_NOTFOUND);
    /* Reopen must keep the RO mode and absence/presence decision. */
    CHECK(ime_dict_reopen(&dict, "/db/fep.db") == 0);
    conversion(&dict, user);
    CHECK(create_attempts == 0 && learn_prepares == 0 && !dict.learn_stmt);
    CHECK(ime_dict_close(&dict) == 0);
    CHECK(!memcmp(before, g_media_a.img, g_media_a.secs * SEC));
    CHECK(memsys5_check_canary() == 0);
    printf("PASS CD user=%d exact/prefix list/export ROFS journal0 CREATE0 learn_prepare0 image_unchanged\n", user);
    return 0;
}
