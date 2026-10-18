/* T4b-7 / E11-A2b: use the raw KernelAPI entries, without input/sound wrappers.
 * PM supplies input or CTRL+STOP after READY. main is the first function.
 * parent is CUI only: nested exec_run is intentionally unavailable in GUI. */
#include "os32api.h"
#include <string.h>

#define RAW_USER_BYTES (96u * 1024u)
#define RAW_MML_NOTES 128
#define RAW_CHILD "/usr/bin/raw_wait_test.bin "
#define RAW_COMMAND_BYTES 128
#define RAW_DB_CREATE "CREATE TABLE probe(x)"
#define RAW_DB_INSERT "INSERT INTO probe VALUES(73)"
#define RAW_IME_WARM_LIMIT 4
#define RAW_IME_YOMI "か"
#define RAW_IME_ROMAJI "ka"
#define RAW_DICT_VARIANT 1
/* Four rows in each of twelve factors: 4^12 VM iterations, without CTEs
 * (SQLITE_OMIT_CTE) or more than SQLITE_MAX_COMPOUND_SELECT=5 terms.
 * Comma joins keep the SQL within DB_SQL_MAX_BYTES, including NUL. */
#define RAW_SQL_FOUR \
    "(SELECT 1 AS x UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4)"
#define RAW_SLOW_SQL \
    "SELECT sum(a.x+b.x+c.x+d.x+e.x+f.x+g.x+h.x+i.x+j.x+k.x+l.x) FROM " \
    RAW_SQL_FOUR " a, " RAW_SQL_FOUR " b, " \
    RAW_SQL_FOUR " c, " RAW_SQL_FOUR " d, " \
    RAW_SQL_FOUR " e, " RAW_SQL_FOUR " f, " \
    RAW_SQL_FOUR " g, " RAW_SQL_FOUR " h, " \
    RAW_SQL_FOUR " i, " RAW_SQL_FOUR " j, " \
    RAW_SQL_FOUR " k, " RAW_SQL_FOUR " l"

static int parent_run(KernelAPI *api, const char *entry, const char *end);
static int probe(KernelAPI *api, const char *entry, const char *end);

int main(int argc, char **argv, KernelAPI *api)
{
    if (argc == 4 && !strcmp(argv[1], "parent"))
        return parent_run(api, argv[2], argv[3]);
    if (argc == 3) return probe(api, argv[1], argv[2]);
    api->kprintf(ATTR_WHITE, "%s",
        "usage: raw_wait_test [parent] kbdchar|kbdkey|imechar|imekey|serial|fm|db|log done|stop\n"
        "       raw_wait_test parent fault done\n"
        "done: send Q to keyboard/IME or serial after READY; FM/DB/log finish themselves\n"
        "stop: CTRL+STOP after READY; GUI: select and kill this app in WM\n");
    return 1;
}

static int probe(KernelAPI *api, const char *entry, const char *end)
{
    int stop = !strcmp(end, "stop"), value = 'Q', rc = 0;
    int kbdchar = !strcmp(entry, "kbdchar"), kbdkey = !strcmp(entry, "kbdkey");
    int imechar = !strcmp(entry, "imechar"), imekey = !strcmp(entry, "imekey");
    int serial = !strcmp(entry, "serial"), fm = !strcmp(entry, "fm");
    int dbmode = !strcmp(entry, "db"), logmode = !strcmp(entry, "log");
    int fault = !strcmp(entry, "fault");
    if ((!stop && strcmp(end, "done")) ||
        !(kbdchar || kbdkey || imechar || imekey || serial || fm || dbmode || logmode || fault))
        return 1;
    /* Allocate USER memory and a live SQLite group before every wait. Leave
     * both owned by this app: normal exit and STOP must reclaim them alike.
     * Warm engine caches before taking the parent's accounting baseline. */
    u8 *bytes = api->mem_alloc(RAW_USER_BYTES);
    if (!bytes) return 1;
    bytes[0] = 0x5a; bytes[RAW_USER_BYTES - 1] = 0xa5;
    int db = api->db_open(":memory:");
    if (db < 0 || api->db_exec(db, RAW_DB_CREATE) < 0 ||
        api->db_exec(db, RAW_DB_INSERT) < 0) return 1;
    MemStat self;
    if (api->mem_stat(-1, &self, sizeof(self)) != sizeof(self)) return 1;
    api->kprintf(ATTR_WHITE,
        "raw_wait READY %s %s app=%d USER_pages=%u USER_heap=%u SQLite_bytes=%u\n",
        entry, end, self.app_id, self.owner_pages, self.exec_heap_used, api->db_mem_used());
    if (kbdchar) value = api->kbd_getchar();
    else if (kbdkey) value = api->kbd_getkey();
    else if (imechar) value = api->ime_getchar();
    else if (imekey) value = api->ime_getkey();
    else if (serial) value = api->serial_getchar();
    else if (fm) {
        static char mml[RAW_MML_NOTES + 1];
        int count = stop ? RAW_MML_NOTES : 1;
        for (int i = 0; i < count; i++) mml[i] = 'C';
        mml[count] = 0;
        api->fm_play_mml(mml);
    } else if (dbmode) rc = api->db_exec(db, RAW_SLOW_SQL);
    else if (logmode) {
        /* The success diagnostic uses internal serial_puts, inside a USER
         * syscall. PM injects STOP at that diagnostic, not at this banner. */
        rc = api->ime_switch_dict(RAW_DICT_VARIANT);
    } else if (fault) __asm__ volatile("ud2" : : : "memory");
    if (stop || rc < 0 || (value & 0xff) != 'Q' ||
        bytes[0] != 0x5a || bytes[RAW_USER_BYTES - 1] != 0xa5) {
        api->kprintf(ATTR_RED, "raw_wait FAIL returned %s value=%d rc=%d\n", entry, value, rc);
        return 1;
    }
    /* Next KAPI with an output pointer also checks caller restoration after
     * a GUI keyboard park/resume. Keep the group open for owner-exit cleanup. */
    if (api->mem_stat(-1, &self, sizeof(self)) != sizeof(self) ||
        api->db_exec(db, "INSERT INTO probe VALUES(91)") < 0) return 1;
    api->kprintf(ATTR_GREEN, "raw_wait PASS normal %s next-KAPI app=%d\n", entry, self.app_id);
    return 0;
}

static int parent_run(KernelAPI *api, const char *entry, const char *end)
{
    char command[RAW_COMMAND_BYTES];
    MemStat before, after;
    int kind, code;
    /* Warm the same SQLite workload once so one-time engine caches are not
     * mistaken for a child leak. This group stays alive across child exit. */
    u8 *sentinel = api->mem_alloc(RAW_USER_BYTES);
    int db = api->db_open(":memory:");
    if (!sentinel || db < 0 || api->db_exec(db, RAW_DB_CREATE) < 0 ||
        api->db_exec(db, RAW_DB_INSERT) < 0) return 1;
    sentinel[0] = 0x39; sentinel[RAW_USER_BYTES - 1] = 0x93;
    if (strlen(RAW_CHILD) + strlen(entry) + strlen(end) + 2 > sizeof(command)) return 1;
    strcpy(command, RAW_CHILD); strcat(command, entry); strcat(command, " "); strcat(command, end);
    /* The first dictionary reopen retains resident SQLite state. Perform the
     * same successful switch as the child before taking either baseline. */
    if (!strcmp(entry, "log") && api->ime_switch_dict(RAW_DICT_VARIANT) < 0)
        return 1;
    /* db_mem_used includes the resident FEP's page cache and statements.
     * PM uses only yomi "ka" (か), SPACE, ESC twice, FEP OFF, Q here and
     * the same yomi for the child's STOP. Require two settled observations
     * before accounting; never absorb growth observed after child exit. */
    if ((!strcmp(entry, "imechar") || !strcmp(entry, "imekey")) && !strcmp(end, "stop")) {
        u32 previous = api->db_mem_used();
        int settled = 0;
        for (int i = 0; i < RAW_IME_WARM_LIMIT; i++) {
            api->kprintf(ATTR_WHITE,
                "raw_wait WARMUP IME: ON hiragana, " RAW_IME_ROMAJI " (" RAW_IME_YOMI
                "), SPACE, ESC twice, OFF, Q\n");
            int key = !strcmp(entry, "imechar") ? api->ime_getchar() : api->ime_getkey();
            if ((key & 0xff) != 'Q') return 1;
            u32 current = api->db_mem_used();
            if (i > 0 && current == previous) { settled = 1; break; }
            previous = current;
        }
        if (!settled) return 1;
    }
    u32 sqlite_before = api->db_mem_used();
    if (api->mem_stat(-1, &before, sizeof(before)) != sizeof(before)) return 1;
    (void)api->exec_run(command);
    /* Read the global last-result before launching any other child. */
    if (api->exec_last_result(&kind, &code) < 0 ||
        api->mem_stat(-1, &after, sizeof(after)) != sizeof(after)) return 1;
    u32 sqlite_after = api->db_mem_used();
    int want = !strcmp(end, "stop") ? EXEC_KIND_ABORTED :
               !strcmp(entry, "fault") ? EXEC_KIND_FAULT : EXEC_KIND_EXITED;
    int ok = kind == want && code == (want == EXEC_KIND_EXITED ? 0 : EXEC_ERR_FAULT) &&
        after.app_id == before.app_id && after.owner_pages == before.owner_pages &&
        after.exec_heap_used == before.exec_heap_used &&
        after.phys_free_pages == before.phys_free_pages &&
        after.reclaim_count == before.reclaim_count + 1 &&
        after.ledger_bad_free == before.ledger_bad_free &&
        after.leftover_pages == before.leftover_pages && sqlite_after == sqlite_before &&
        sentinel[0] == 0x39 && sentinel[RAW_USER_BYTES - 1] == 0x93;
    /* A separate USER copy and SQLite call after return exercise the restored
     * caller; exact TSS.ESP0/caller values are observed by PM and the R1 host test. */
    if (api->db_exec(db, "INSERT INTO probe VALUES(91)") < 0) ok = 0;
    api->kprintf(ok ? ATTR_GREEN : ATTR_RED,
        "raw_wait %s parent %s kind=%d code=%d reclaim=%u free=%u/%u USER=%u/%u SQLite=%u/%u\n",
        ok ? "PASS" : "FAIL", entry, kind, code, after.reclaim_count - before.reclaim_count,
        before.phys_free_pages, after.phys_free_pages, before.owner_pages, after.owner_pages,
        sqlite_before, sqlite_after);
    api->db_close(db); api->mem_free(sentinel);
    return ok ? 0 : 1;
}
