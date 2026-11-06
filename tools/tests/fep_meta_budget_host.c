/* O2: actual prefix allocator; observer only records live FEP charge. */
static void budget_sql(sqlite3 *db, const char *text)
{ CHECK(sqlite3_exec(db, text, 0, 0, 0) == SQLITE_OK); }
static void prepare_load(int count)
{
    sqlite3 *db = 0;
    sqlite3_stmt *stmt = 0;
    CHECK(setup_open(FEP_DICT_PATH_M, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) == 0);
    budget_sql(db, "PRAGMA page_size=1024; CREATE TABLE dict(yomi TEXT,kanji TEXT,pos_id INT,cost INT); CREATE INDEX idx_dict_yomi ON dict(yomi); BEGIN");
    CHECK(sqlite3_prepare_v2(db, "INSERT INTO dict VALUES(?1,?2,1,?3)", -1, &stmt, 0) == 0);
    for (int i = 0; i < count; i++) {
        char word[32]; snprintf(word, sizeof(word), "亜亜亜亜亜亜亜%09da", i);
        CHECK(sqlite3_bind_text(stmt, 1, i & 1 ? "あい" : "あいうえええええええa", -1, SQLITE_STATIC) == 0);
        CHECK(sqlite3_bind_text(stmt, 2, word, -1, SQLITE_STATIC) == 0);
        CHECK(sqlite3_bind_int(stmt, 3, i % 2048) == 0);
        CHECK(sqlite3_step(stmt) == SQLITE_DONE); CHECK(sqlite3_reset(stmt) == 0);
    }
    CHECK(sqlite3_finalize(stmt) == 0);
    budget_sql(db, "COMMIT");
    budget_sql(db, "PRAGMA user_version=1; CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
        "INSERT INTO meta VALUES('format_version','1'),('min_fep_version','1'),"
        "('dict_id','ipadic-2.7.0'),('kind','system'),('language','ja'),('charset','UTF-8'),"
        "('variant','M'),('source','IPADIC'),('source_version','2.7.0'),"
        "('build_tool','fep_to_sqlite.py/1'),('build_date','1970-01-01T00:00:00Z'),"
        "('entry_count','65536'),('pos_table_version','1'),('cost_scale','os32-log2047-boost-v1'),"
        "('mem_reserve_kb','192'),('cache_pages','16'),"
        "('checksum','sha256:0000000000000000000000000000000000000000000000000000000000000000')");
    /* Original IPADIC COPYING is 3795 bytes; use a larger 4096-byte bound. */
    static char original[4097]; memset(original, 'L', sizeof(original) - 1);
    CHECK(sqlite3_prepare_v2(db, "INSERT INTO meta VALUES('license',?1),('attribution',?1)", -1, &stmt, 0) == 0);
    CHECK(sqlite3_bind_text(stmt, 1, original, -1, SQLITE_STATIC) == 0);
    CHECK(sqlite3_step(stmt) == SQLITE_DONE && sqlite3_finalize(stmt) == 0);
    CHECK(setup_close(db) == 0);
}
int main(void)
{
    const int counts[] = {4096, 16384, 65536};
    unsigned maximum = 0;
    CHECK(os32_sqlite_init() == 0); fixture_init();
    CHECK(OS32_SQLITE_RESIDENT_GROUPS == 3 && VFS_FONT_FD_RESERVED == 7);
    for (int v = 0; v < 3; v++) {
        prepare_load(counts[v]);
        FepStore s = {0}; FepMeta meta;
        os32_sqlite_allocation_context(0, 0);
        budget->context = BUDGET_FEP; observed_peak = 0;
        CHECK(fep_store_open(&s, FEP_DICT_PATH_M, FEP_USER_PATH) == 0);
        CHECK(s.user_enabled && fep_store_live_connections(&s) == 2);
        unsigned open_peak = observed_peak;
        /* Tall learning B-tree and legal 31-byte spellings. */
        budget_sql(s.user.db, "BEGIN");
        for (int i = 0; i < 8192; i++) {
            char word[32], sql[256];
            snprintf(word, sizeof(word), "亜亜亜亜亜亜亜%09da", i);
            snprintf(sql, sizeof(sql), "INSERT INTO dict_user VALUES('あいう%08d','%s',1,0)", i, word);
            budget_sql(s.user.db, sql);
        }
        budget_sql(s.user.db, "COMMIT");
        /* Fill both cursor caches, then probe while both remain open. */
        IME_Result out[IME_MAX_RESULTS];
        CHECK(fep_store_search(&s, "あい", out, IME_MAX_RESULTS) > 0);
        CHECK(fep_store_search(&s, "あいう", out, IME_MAX_RESULTS) == IME_MAX_RESULTS);
        unsigned workload_peak = observed_peak;
        unsigned steady = budget->live[BUDGET_FEP];
        observed_peak = steady;
        CHECK(os32_sqlite_boot_check(2, FEP_DICT_RESERVE_KB * 1024u) == 0);
        void *pressure[512]; int held = 0;
        budget->context = BUDGET_APP;
        while (held < 512 && (pressure[held] = budget_malloc(1024))) held++;
        CHECK(held > 0 && held < 512);
        unsigned app_charge = budget->live[BUDGET_APP];
        budget->context = BUDGET_FEP;
        FixtureFile *file = fixture_find(FEP_DICT_PATH_M, 0);
        CHECK(fixture_create(0, "/db/probe.db", file->data, file->size) == 0);
        CHECK(fep_meta_probe("/db/probe.db", &meta) == 0);
        unsigned probe_peak = observed_peak;
        while (held) budget_free(pressure[--held]);
        if (probe_peak < steady) return 2;
        /* Third group and its reserved main FD must coexist with system/user.
         * Probe closes before returning, so retain the same RO open explicitly. */
        Os32SqliteGroup group; sqlite3 *probe = 0;
        CHECK(os32_sqlite_group_acquire(OS32_SQLITE_RESIDENT, 0, &group) == 0);
        /* Same inode is forbidden; use another fixture identity. */
        CHECK(os32_sqlite_group_open(&group, "/db/probe.db", SQLITE_OPEN_READONLY, &probe) == 0);
        int a = os32_sqlite_db_fd(s.system.db), b = os32_sqlite_db_fd(s.user.db), c = os32_sqlite_db_fd(probe);
        CHECK(a != b && a != c && b != c && c < VFS_FONT_FD_RESERVED);
        CHECK(os32_sqlite_group_begin_close(&group) == 0);
        CHECK(os32_sqlite_group_finish(&group, sqlite3_close(probe)) == 0);
        CHECK(fixture_rm("/db/probe.db") == 0);
        budget->context = BUDGET_FEP;
        fep_store_learn(&s, "ああああああああああa", "亜亜亜亜亜亜亜亜亜亜a");
        unsigned peak = observed_peak > workload_peak ? observed_peak : workload_peak;
        if (peak > maximum) maximum = peak;
        printf("META_BUDGET variant=%s rows=%d learn_rows=8192 open_peak=%u steady=%u probe_peak=%u peak=%u fds=%d,%d,%d font_fd=%d reserve=%u app_pressure=%u fits=%d\n",
            (v == 0 ? "S" : v == 1 ? "M" : "L"), counts[v], open_peak, steady, probe_peak, peak,
            a, b, c, VFS_FONT_FD_RESERVED, FEP_DICT_RESERVE_KB * 1024u, app_charge, peak <= FEP_DICT_RESERVE_KB * 1024u);
        CHECK(fep_store_close(&s) == 0);
        CHECK(!budget->live[BUDGET_FEP]);
        CHECK(fixture_rm(FEP_DICT_PATH_M) == 0 && fixture_rm(FEP_USER_PATH) == 0);
    }
    printf("META_BUDGET_MAX bytes=%u reserve=%u fits=%d group_state_bytes=%u third_group_bytes=%u\n",
        maximum, FEP_DICT_RESERVE_KB * 1024u, maximum <= FEP_DICT_RESERVE_KB * 1024u,
        (unsigned)sizeof(groups), (unsigned)sizeof(groups[0]));
    return 0;
}
