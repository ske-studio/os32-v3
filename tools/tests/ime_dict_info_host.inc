/* Extends B2's production SQLite/VFS/FD + caller boundary fixture.
 * bad_byte models a RO page or the first inaccessible byte across a page. */
static const void *info_blob(sqlite3_stmt *s, int i)
{
    enter(); const void *p = sqlite3_column_blob(s, i); engine = 0; return p;
}
static int info_bytes(sqlite3_stmt *s, int i)
{
    enter(); int n = sqlite3_column_bytes(s, i); engine = 0; return n;
}
static void *info_memcpy(void *dst, const void *src, u32 n)
{
    require(n <= IME_DICT_INFO_CAP, "metadata copy bound");
    return kmemcpy(dst, src, n);
}
#define kmemcpy info_memcpy
#define sqlite3_prepare_v2 prepare
#define sqlite3_bind_text bind_text
#define sqlite3_step step
#define sqlite3_finalize finalize
#define sqlite3_column_blob info_blob
#define sqlite3_column_bytes info_bytes
#include "ime_dict_info.inc"
#undef kmemcpy
#undef sqlite3_prepare_v2
#undef sqlite3_bind_text
#undef sqlite3_step
#undef sqlite3_finalize
#undef sqlite3_column_blob
#undef sqlite3_column_bytes

#include "ime_info_stub.inc"

static unsigned char info_out[IME_DICT_INFO_CAP + 2];
static const char *const info_names[] = {
    "status", "format_version", "min_fep_version", "dict_id", "kind", "language",
    "charset", "variant", "source", "source_version", "license", "attribution",
    "build_tool", "build_date", "entry_count", "pos_table_version", "cost_scale",
    "mem_reserve_kb", "cache_pages", "checksum"
};
static const char *const info_values[] = {
    "meta", "1", "1", "test-dict", "system", "ja", "UTF-8", "test-variant",
    "source", "2.7.0", "license", "attribution", "builder", "2026-11-05T00:00:00Z",
    "123", "1", "scale", "192", "16", "sha256:test"
};
static int info_call(int key, int off, int cap)
{
    memset(info_out, 0xa5, sizeof(info_out));
    int copied = copies;
    int n = ime_dict_info_facade(key, off, (char *)info_out + 1, cap);
    require(n < 0 || copies == copied + 1, "metadata one copy on success");
    require(copies <= copied + 1, "metadata no repeated copy");
    return n;
}
static void info_error(int key, int off, int cap, int expected)
{
    int actual = info_call(key, off, cap);
    if (actual != expected) fprintf(stderr, "metadata key=%d off=%d cap=%d expected=%d actual=%d\n", key, off, cap, expected, actual);
    require(actual == expected, "metadata error code");
    for (u32 i = 0; i < sizeof(info_out); i++)
        require(info_out[i] == 0xa5, "metadata error unchanged");
}
static void info_value(int key, int off, int cap, const char *expected, int size)
{
    int n = info_call(key, off, cap);
    require(n == size && !memcmp(info_out + 1, expected, size), "metadata exact bytes");
    require(info_out[n + 1] == 0, "metadata NUL");
    require(info_out[0] == 0xa5, "metadata leading sentinel");
    for (u32 i = (u32)n + 2; i < sizeof(info_out); i++)
        require(info_out[i] == 0xa5, "metadata trailing sentinel");
}
static void info_sql(const char *sql)
{ require(sqlite3_exec(g_ime.dict.db, sql, NULL, NULL, NULL) == SQLITE_OK, "metadata setup SQL"); }
static void info_put(const char *key, const char *value)
{
    sqlite3_stmt *s = NULL;
    require(sqlite3_prepare_v2(g_ime.dict.db, "INSERT OR REPLACE INTO meta VALUES(?,?)", -1, &s, NULL) == SQLITE_OK, "metadata insert prepare");
    require(sqlite3_bind_text(s, 1, key, -1, SQLITE_STATIC) == SQLITE_OK &&
            sqlite3_bind_text(s, 2, value, -1, SQLITE_STATIC) == SQLITE_OK &&
            sqlite3_step(s) == SQLITE_DONE && sqlite3_finalize(s) == SQLITE_OK, "metadata insert");
}
static void info_fault(const char *bad)
{
    int entered = entries, copied = copies;
    memset(info_out, 0xa5, sizeof(info_out));
    bad_byte = bad; expect_kill = 1;
    if (!__builtin_setjmp(kill_escape)) {
        (void)ime_dict_info_facade(IME_DICT_INFO_STATUS, 0, (char *)info_out + 1, IME_DICT_INFO_CAP);
        require(0, "metadata invalid output must kill");
    }
    expect_kill = 0; bad_byte = NULL;
    require(entries == entered && copies == copied, "metadata fault before SQL");
    for (u32 i = 0; i < sizeof(info_out); i++) require(info_out[i] == 0xa5, "metadata fault unchanged");
}
static void metadata(void)
{
    watched = NULL; info_test = 1;
    require(stub_dict_info(IME_DICT_INFO_STATUS, 0, (char *)info_out, 256) == OS32_ERR_NOSYS &&
            stub_dict_info(0, -1, NULL, 0) == OS32_ERR_NOSYS, "metadata absent module stub");
    for (origin = CALLER_TRUSTED; origin <= CALLER_USER; origin++) {
        services_public = 0;
        for (int key = IME_DICT_INFO_STATUS; key <= IME_DICT_INFO_CHECKSUM; key++)
            info_error(key, 0, 256, OS32_ERR_NOSYS);
        info_error(0, -1, 0, OS32_ERR_NOSYS);
        services_public = 1;
        info_error(0, 0, 256, OS32_ERR_INVAL);
        info_error(IME_DICT_INFO_CHECKSUM + 1, 0, 256, OS32_ERR_INVAL);
        info_error(IME_DICT_INFO_STATUS, -1, 256, OS32_ERR_INVAL);
        info_error(IME_DICT_INFO_STATUS, 0, 0, OS32_ERR_INVAL);
        info_error(IME_DICT_INFO_STATUS, 0, 1, OS32_ERR_INVAL);
        require(ime_dict_info_facade(IME_DICT_INFO_STATUS, 0, NULL, 256) == OS32_ERR_INVAL, "metadata NULL");
        identity = 0; info_error(IME_DICT_INFO_STATUS, 0, 256, OS32_ERR_INVAL); identity = 1;
        info_fault((char *)info_out + 1);
        info_fault((char *)info_out + IME_DICT_INFO_CAP);
        /* Clamping must precede the caller range walk. */
        bad_byte = (char *)info_out + IME_DICT_INFO_CAP + 1;
        info_value(IME_DICT_INFO_STATUS, 0, 257, "metadata-unavailable", 20);
        bad_byte = NULL;
        for (int key = IME_DICT_INFO_STATUS; key <= IME_DICT_INFO_CHECKSUM; key++) {
            if (key == IME_DICT_INFO_STATUS) info_value(key, 0, 256, "metadata-unavailable", 20);
            else if (key == IME_DICT_INFO_VARIANT) info_value(key, 0, 256, "M", 1);
            else info_error(key, 0, 256, OS32_ERR_NOTFOUND);
        }
        int fd = dict_fd(&g_ime.dict), entered = entries;
        open_files[fd].stale = 1;
        for (int key = IME_DICT_INFO_STATUS; key <= IME_DICT_INFO_CHECKSUM; key++)
            info_error(key, 0, 256, OS32_ERR_STALE);
        require(entries == entered, "metadata stale never SQL");
        open_files[fd].stale = 0;
        require(ime_dict_close(&g_ime.dict) == 0, "metadata close");
        info_value(IME_DICT_INFO_STATUS, 0, 256, "unloaded", 8);
        for (int key = IME_DICT_INFO_FORMAT_VERSION; key <= IME_DICT_INFO_CHECKSUM; key++)
            info_error(key, 0, 256, OS32_ERR_AGAIN);
        require(ime_dict_open(&g_ime.dict, "/db/fep.db") == 0, "metadata reopen");
        const char *paths[] = {"/db/fep_s.db", "/db/fep_l.db", "/db/custom.db"};
        for (int i = 0; i < 3; i++) {
            if (origin == CALLER_TRUSTED) make_dict(paths[i], "試");
            require(ime_dict_reopen(&g_ime.dict, paths[i]) == 0, "metadata switch");
            if (i < 2) info_value(IME_DICT_INFO_VARIANT, 0, 256, i ? "L" : "S", 1);
            else info_error(IME_DICT_INFO_VARIANT, 0, 256, OS32_ERR_NOTFOUND);
        }
        require(ime_dict_reopen(&g_ime.dict, "/db/missing.db") != 0, "metadata failed switch");
        info_value(IME_DICT_INFO_STATUS, 0, 256, "unloaded", 8);
        info_error(IME_DICT_INFO_VARIANT, 0, 256, OS32_ERR_AGAIN);
        require(ime_dict_reopen(&g_ime.dict, "/db/fep.db") == 0, "metadata switch back");
        info_sql("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
        for (int i = 1; i < (int)(sizeof(info_names) / sizeof(info_names[0])); i++) info_put(info_names[i], info_values[i]);
        for (int key = IME_DICT_INFO_STATUS; key <= IME_DICT_INFO_CHECKSUM; key++)
            info_value(key, 0, 256, info_values[key - 1], strlen(info_values[key - 1]));
        /* UTF-8 cut in the middle of every character, beyond 256B in total. */
        char original[IME_DICT_INFO_CAP * 3 + 1], joined[sizeof(original)];
        for (u32 i = 0; i < sizeof(original) - 1; i += 3) memcpy(original + i, "あ", 3);
        original[sizeof(original) - 1] = 0;
        info_put("license", original);
        int caps[] = {2, 256, 257};
        for (u32 c = 0; c < sizeof(caps) / sizeof(caps[0]); c++) {
            int off = 0, n;
            while ((n = info_call(IME_DICT_INFO_LICENSE, off, caps[c])) > 0) {
                require(info_out[n + 1] == 0 && info_out[0] == 0xa5 && info_out[n + 2] == 0xa5, "metadata chunk bounds");
                require(n <= caps[c] - 1 && n <= IME_DICT_INFO_CAP - 1, "metadata chunk cap");
                memcpy(joined + off, info_out + 1, n); off += n;
                require(off <= (int)sizeof(original) - 1, "metadata total bound");
            }
            require(n == 0 && off == sizeof(original) - 1 && !memcmp(joined, original, off), "metadata full byte reassembly");
        }
        info_value(IME_DICT_INFO_LICENSE, sizeof(original) - 2, 256, original + sizeof(original) - 2, 1);
        info_value(IME_DICT_INFO_LICENSE, sizeof(original) - 1, 256, "", 0);
        info_value(IME_DICT_INFO_LICENSE, 0x7fffffff, 256, "", 0);
        info_put("attribution", ""); info_value(IME_DICT_INFO_ATTRIBUTION, 0, 256, "", 0);
        info_sql("DELETE FROM meta WHERE key='attribution'");
        info_error(IME_DICT_INFO_ATTRIBUTION, 0, 256, OS32_ERR_NOTFOUND);
        for (int i = 0; i < 4; i++) {
            fail_prepare = i == 0; fail_bind = i == 1; fail_step = i == 2; fail_final = i == 3;
            info_error(IME_DICT_INFO_LICENSE, 0, 256, OS32_ERR_IO);
            require(live == 3, "metadata SQL stmt cleanup");
            fail_prepare = fail_bind = fail_step = fail_final = 0;
        }
        fail_final = finals + 2;
        info_error(IME_DICT_INFO_LICENSE, 0, 256, OS32_ERR_IO);
        require(live == 3, "metadata read finalize cleanup");
        fail_final = 0;
        late = 1; info_error(IME_DICT_INFO_LICENSE, 0, 256, OS32_ERR_IO); late = 0;
        g_ime.dict.isolated = 1; info_error(IME_DICT_INFO_STATUS, 0, 256, OS32_ERR_IO); g_ime.dict.isolated = 0;
        info_sql("DROP TABLE meta; CREATE TABLE meta(wrong TEXT)");
        info_error(IME_DICT_INFO_LICENSE, 0, 256, OS32_ERR_IO);
        info_sql("DROP TABLE meta");
    }
    info_test = 0;
    puts("PASS metadata all keys byte chunks states errors stale caller bounds");
}
