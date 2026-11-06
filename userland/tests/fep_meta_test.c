/* DICT_META P: run on a disposable writable guest image with S/M/L deployed.
 * Before boot: cfg set system fep_mem_reserve_kb int 224
 *              cfg set system fep_user_dict_path text /etc/fep_meta_user.db
 * Use an empty test user DB. Reboot after setting these; restore settings and
 * reboot afterwards. L/user metadata is temporarily edited and restored below.
 * Interrupted runs require restoring the fixture image. No reboot is faked.
 * Deployment/hsync across reboot and ROFS remain PM acceptance steps.
 */
#include "os32api.h"
#include "rt/testresult.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSTEM_S "/db/fep_s.db"
#define SYSTEM_L "/db/fep_l.db"
#define SETTINGS "/etc/settings.db"
#define USER_DB "/etc/fep_meta_user.db"
#define TEST_RESERVE 224
#define SC_SPACE 0x34
#define SC_RETURN 0x1c
static KernelAPI *api;
static int passed, total;
static const char *keys[] = {
    "", "status", "format_version", "min_fep_version", "dict_id", "kind",
    "language", "charset", "variant", "source", "source_version", "license",
    "attribution", "build_tool", "build_date", "entry_count",
    "pos_table_version", "cost_scale", "mem_reserve_kb", "cache_pages", "checksum"
};

static int check(const char *name, int ok)
{
    total++;
    passed += !!ok;
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    return ok;
}

static int scalar(int db, const char *sql, char *out, int cap)
{
    int ok = api->db_prepare(db, sql) == DB_STATUS_ROW;
    if (ok) {
        const char *s = api->db_column_text(db, 0);
        ok = s && strlen(s) < (unsigned)cap;
        if (ok) strcpy(out, s);
    }
    return api->db_finalize(db) == 0 && ok;
}

static int read_value(const char *path, const char *sql, char *out, int cap)
{
    int db = api->db_open_existing(path, 0);
    if (db < 0) return 0;
    int ok = scalar(db, sql, out, cap);
    return api->db_close(db) == 0 && ok;
}

static int patch(const char *path, const char *key, const char *value)
{
    int db = api->db_open_existing(path, 1);
    if (db < 0) return 0;
    int ok = api->db_prepare_only(db, "UPDATE meta SET value=?1 WHERE key=?2") == 0;
    if (ok) ok = api->db_bind_text(db, 1, value, (int)strlen(value)) == 0 &&
                 api->db_bind_text(db, 2, key, (int)strlen(key)) == 0 && api->db_step(db) == DB_STATUS_DONE;
    if (api->db_finalize(db) != 0) ok = 0;
    return api->db_close(db) == 0 && ok;
}

static int info_is(int key, const char *value)
{
    char buf[IME_DICT_INFO_CAP];
    int n = api->ime_dict_info(key, 0, buf, sizeof(buf));
    return n == (int)strlen(value) && !strcmp(buf, value);
}

static int metadata(void)
{
    /* Same-DB exclusion also covers RO opens. Snapshot KAPI text first,
     * then close FEP before independently reading the SQLite fixture. */
    static char text[65536];
    int start[IME_DICT_INFO_KEY_MAX + 1], length[IME_DICT_INFO_KEY_MAX + 1];
    int used = 0, ok = 1;
    for (int key = 1; key <= IME_DICT_INFO_KEY_MAX; key++) {
        char chunk[IME_DICT_INFO_CAP];
        int n, offset = 0;
        start[key] = used;
        do {
            memset(chunk, 0xa5, sizeof(chunk));
            n = api->ime_dict_info(key, offset, chunk, sizeof(chunk));
            if (n < 0 || n >= (int)sizeof(chunk) || chunk[n] != 0 ||
                used + n + 1 > (int)sizeof(text)) return 0;
            memcpy(text + used, chunk, n);
            offset += n;
            used += n;
        } while (n);
        length[key] = offset;
        text[used++] = 0;
    }
    if (api->ime_dict_suspend(1) != 0) return 0;
    int db = api->db_open_existing(SYSTEM_S, 0);
    if (db < 0) ok = 0;
    for (int key = 1; db >= 0 && key <= IME_DICT_INFO_KEY_MAX; key++) {
        int offset = 0, size, same = length[key] > 0;
        char expected[IME_DICT_INFO_CAP], sql[256];
        do {
            if (key == IME_DICT_INFO_STATUS)
                snprintf(sql, sizeof(sql), "SELECT substr('meta',%d,255)", offset + 1);
            else
                snprintf(sql, sizeof(sql), "SELECT CAST(substr(CAST(value AS BLOB),%d,255) AS TEXT) FROM meta WHERE key='%s'", offset + 1, keys[key]);
            if (!scalar(db, sql, expected, sizeof(expected))) { same = 0; break; }
            size = (int)strlen(expected);
            if (offset + size > length[key] ||
                memcmp(text + start[key] + offset, expected, size)) { same = 0; break; }
            offset += size;
        } while (size);
        same = same && offset == length[key];
        check(keys[key], same);
        if (!same) ok = 0;
    }
    char count[32];
    if (db >= 0) {
        if (!scalar(db, "SELECT count(*) FROM dict", count, sizeof(count)) ||
            strcmp(text + start[IME_DICT_INFO_ENTRY_COUNT], count)) ok = 0;
        if (api->db_close(db) != 0) ok = 0;
    }
    if (api->ime_dict_suspend(0) != 0) ok = 0;
    return ok;
}

static int convert(void)
{
    char got[32];
    const char *roman = "neko";
    api->ime_set_mode(IME_MODE_OFF);
    api->ime_set_mode(IME_MODE_HIRAGANA);
    for (int i = 0; i < 32 && api->ime_feed_key(-1) >= 0; i++) {}
    api->ime_feed_key(27); /* Clear pre-existing kana before the test reading. */
    for (int i = 0; roman[i]; i++) api->ime_feed_key(roman[i]);
    api->ime_feed_key((SC_SPACE << 8) | ' ');
    int c = api->ime_feed_key((SC_RETURN << 8) | '\r'), n = 0;
    while (c > 0 && c < 256 && n < (int)sizeof(got) - 1) {
        got[n++] = (char)c;
        c = api->ime_feed_key(-1);
    }
    got[n] = 0;
    return c < 0 && !strcmp(got, "猫");
}

static int frequency(void)
{
    IME_UserEntry rows[64];
    int n = api->ime_user_list("ねこ", rows, 64);
    if (n < 0) return -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(rows[i].yomi, "ねこ") && !strcmp(rows[i].kanji, "猫")) return rows[i].freq;
    return 0;
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    char setting[64], id[128], pos[32], reserve[32], original[8];
    int mode, saved = 0;
    (void)argc; (void)argv;
    api = kapi;
    mode = api->ime_get_mode();
    int n = api->ime_dict_info(IME_DICT_INFO_VARIANT, 0, original, sizeof(original));
    if (!check("initial dictionary", n == 1 && original[0] && strchr("SML", original[0]))) goto done;
    if (!check("boot dictionary fixture", info_is(IME_DICT_INFO_MEM_RESERVE_KB, "192"))) goto done;
    if (!check("settings fixture (reboot required)",
        read_value(SETTINGS, "SELECT CAST(ival AS TEXT) FROM settings WHERE scope='system' AND key='fep_mem_reserve_kb' AND type=0", setting, sizeof(setting)) && atoi(setting) == TEST_RESERVE &&
        read_value(SETTINGS, "SELECT tval FROM settings WHERE scope='system' AND key='fep_user_dict_path' AND type=1", setting, sizeof(setting)) && !strcmp(setting, USER_DB))) goto done;
    if (!check("select S", api->ime_switch_dict(0) == 0)) goto done;
    if (!check("metadata bytes and entry_count", metadata())) goto done;
    int fixtures = api->ime_dict_suspend(1) == 0 &&
        read_value(USER_DB, "SELECT value FROM meta WHERE key='dict_id'", id, sizeof(id)) &&
        read_value(USER_DB, "SELECT value FROM meta WHERE key='pos_table_version'", pos, sizeof(pos)) &&
        read_value(SYSTEM_L, "SELECT value FROM meta WHERE key='mem_reserve_kb'", reserve, sizeof(reserve));
    int resumed = api->ime_dict_suspend(0) == 0;
    if (!check("save fixtures", fixtures && resumed)) goto done;
    saved = 1;
    int before = frequency();
    if (!check("learn", before >= 0 && convert() && frequency() == before + 1)) goto restore;
    int learned = frequency();
    check("S-L-S retains frequency", api->ime_switch_dict(2) == 0 && frequency() == learned &&
          api->ime_switch_dict(0) == 0 && frequency() == learned);
    if (!check("return to S after retention", api->ime_switch_dict(0) == 0)) goto restore;
    /* The boundary proves the boot settings override (S metadata is 192 KiB). */
    check("settings reservation adopted", patch(SYSTEM_L, "mem_reserve_kb", "224") &&
          api->ime_switch_dict(2) == 0 && info_is(IME_DICT_INFO_VARIANT, "L"));
    if (!check("return to S after budget", api->ime_switch_dict(0) == 0)) goto restore;
    check("over-budget keeps S", patch(SYSTEM_L, "mem_reserve_kb", "225") &&
          api->ime_switch_dict(2) == OS32_ERR_AGAIN && info_is(IME_DICT_INFO_VARIANT, "S") && convert());
    if (!check("restore L reservation", patch(SYSTEM_L, "mem_reserve_kb", reserve))) goto restore;
    for (int i = 0; i < 2; i++) {
        const char *key = i ? "pos_table_version" : "dict_id";
        int freq = frequency();
        /* System POS versions are validated before open: put the mismatch
         * in the learning DB, leaving a valid system dictionary available. */
        int off = api->ime_dict_suspend(1) == 0;
        int changed = off && patch(USER_DB, key, i ? "2" : "fep-meta-test-mismatch");
        int on = api->ime_dict_suspend(0) == 0;
        int ok = freq > 0 && changed && on && frequency() < 0 && convert();
        off = api->ime_dict_suspend(1) == 0;
        int fixed = off && patch(USER_DB, key, i ? pos : id);
        on = api->ime_dict_suspend(0) == 0;
        check(i ? "POS mismatch: system only, no learning" : "ID mismatch: system only, no learning",
              ok && fixed && on && frequency() == freq);
    }
restore:
    if (saved) {
        int ok = api->ime_switch_dict(0) == 0;
        if (!patch(SYSTEM_L, "mem_reserve_kb", reserve)) ok = 0;
        int off = api->ime_dict_suspend(1) == 0;
        if (!off || !patch(USER_DB, "dict_id", id)) ok = 0;
        if (!off || !patch(USER_DB, "pos_table_version", pos)) ok = 0;
        if (api->ime_dict_suspend(0) != 0) ok = 0;
        check("restore fixture metadata", ok);
    }
done:
    if (n == 1 && original[0] && strchr("SML", original[0]))
        check("restore selection", api->ime_switch_dict(original[0] == 'S' ? 0 : original[0] == 'M' ? 1 : 2) == 0);
    api->ime_set_mode(mode);
    return os32_test_summary(api, "fep_meta_test", passed, total);
}
