/* B2: production facade + dictionary + product SQLite/VFS/FD fixture.
 * B1 mapping/IF walks have their own real-PD tests. Here copy/CPU boundaries
 * model an inaccessible/read-only byte, a stale output at copyout and IF explicitly;
 * SQL wrappers model the existing outer run/leave and inject ordinary errors.
 * This does not model #PF/IDT/IRQ hardware. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "io.h"
#include "ime.h"
#include "redir_access.h"
#include "sqlite3.h"
static int engine, entries, live, finals, columns, writes, copies;
static int fail_prepare, fail_bind, fail_int, fail_step, fail_final, late, reenter;
static int fail_after, steps;
static int origin, identity = 1;
static const char *bad_byte, *caller_y, *caller_k;
static IME_UserEntry *watched;
static void *kill_escape[5];
static int expect_kill;
static struct { sqlite3_stmt *stmt; const char *borrow[2]; } bindings[16];
static void require(int ok, const char *msg);
static int binding_slot(sqlite3_stmt *s)
{
    for (int i = 0; i < 16; i++) if (bindings[i].stmt == s) return i;
    for (int i = 0; i < 16; i++) if (!bindings[i].stmt) { bindings[i].stmt = s; return i; }
    require(0, "binding observer capacity"); return 0;
}
static unsigned char before[64 * sizeof(IME_UserEntry)];
static void require(int ok, const char *msg)
{ if (!ok) { fprintf(stderr, "FAIL B2 %s\n", msg); exit(1); } }
/* Model the nonlocal kill before SQL/staging acquisition, not #PF hardware. */
void ring3_fault_kill(void)
{
    require(expect_kill, "unexpected output fault kill");
    __builtin_longjmp(kill_escape, 1);
}
static void enter(void)
{
    require(!engine && _irq_enabled(), "engine IF/reentry");
    if (watched) require(!memcmp(watched, before, sizeof(before)), "output unchanged until copyout");
    engine = 1; entries++;
}
static int prepare(sqlite3 *db, const char *sql, int n, sqlite3_stmt **out, const char **tail)
{
    enter();
    int rc = fail_prepare ? SQLITE_ERROR : sqlite3_prepare_v2(db, sql, n, out, tail);
    if (fail_prepare) *out = 0;
    if (*out) live++;
    engine = 0; return rc;
}
static int bind_text(sqlite3_stmt *s, int i, const char *p, int n, sqlite3_destructor_type d)
{
    enter();
    require(p != caller_y && p != caller_k, "only copied inputs reach SQL");
    int rc = fail_bind ? SQLITE_NOMEM : sqlite3_bind_text(s, i, p, n, d);
    if (rc == SQLITE_OK && i <= 2) bindings[binding_slot(s)].borrow[i - 1] = p;
    engine = 0; return rc;
}
static int bind_null(sqlite3_stmt *s, int i)
{
    enter();
    int rc = sqlite3_bind_null(s, i);
    if (rc == SQLITE_OK && i <= 2) bindings[binding_slot(s)].borrow[i - 1] = NULL;
    engine = 0; return rc;
}
static int bind_int(sqlite3_stmt *s, int i, int value)
{
    enter();
    int rc = fail_int ? SQLITE_NOMEM : sqlite3_bind_int(s, i, value);
    engine = 0; return rc;
}
static int step(sqlite3_stmt *s)
{
    enter();
    int rc = (fail_step || (fail_after && ++steps == fail_after)) ? SQLITE_ERROR : sqlite3_step(s);
    engine = 0; return rc;
}
static int finalize(sqlite3_stmt *s)
{
    enter();
    int rc = sqlite3_finalize(s);
    if (s) { live--; memset(&bindings[binding_slot(s)], 0, sizeof(bindings[0])); }
    finals++;
    engine = 0; return fail_final ? SQLITE_ERROR : rc;
}
static const unsigned char *column_text(sqlite3_stmt *s, int i)
{
    enter(); columns++;
    const unsigned char *p = sqlite3_column_text(s, i);
    engine = 0; return p;
}
#define sqlite3_prepare_v2 prepare
#define sqlite3_bind_text bind_text
#define sqlite3_bind_null bind_null
#define sqlite3_bind_int bind_int
#define sqlite3_step step
#define sqlite3_finalize finalize
#define sqlite3_column_text column_text
#define IME_DICT_HOST_MAIN baseline_main
#include "ime_dict_host.c"
#undef sqlite3_prepare_v2
#undef sqlite3_bind_text
#undef sqlite3_bind_null
#undef sqlite3_bind_int
#undef sqlite3_step
#undef sqlite3_finalize
#undef sqlite3_column_text

int services_public = 1;
int sqlite_engine_available(int io) { return !engine && (!io || _irq_enabled()); }
int caller_access_get(struct caller_access *c)
{ memset(c, 0, sizeof(*c)); c->origin = origin; return identity; }
int copy_caller_cstr(const struct caller_access *c, const char *s, char *d, u32 cap)
{
    (void)c;
    if (!s) return 0;
    for (u32 i = 0; i < cap; i++) {
        if (s + i == bad_byte) return 0;
        d[i] = s[i];
        if (!d[i]) return 1;
    }
    return 0;
}
int check_caller_write_range(const struct caller_access *c, void *p, u32 n)
{ (void)c; return p && !(bad_byte && (uptr)bad_byte >= (uptr)p && (uptr)bad_byte - (uptr)p < n); }
int copy_to_caller(const struct caller_access *c, void *p, const void *s, u32 n)
{
    require(!engine && live == 3 && finals > 0, "finalize then leave then copyout");
    copies++;
    if (reenter) {
        reenter = 0;
        int entered = entries;
        require(ime_user_list_facade(NULL, p, 64) == -1 && entries == entered, "busy rejects reentry");
    }
    if (late || !check_caller_write_range(c, p, n)) return 0;
    if (n) memcpy(p, s, n);
    writes += n;
    return 1;
}
#include "ime_source.inc"

static IME_UserEntry out[64];
static void watch(void)
{
    memset(out, 0xa5, sizeof(out));
    memcpy(before, out, sizeof(out)); watched = out;
    finals = entries = columns = copies = writes = 0;
}
static void untouched(int rc)
{ require(rc < 0 && entries == 0 && writes == 0 && !memcmp(out, before, sizeof(out)), "invalid input engine0/output unchanged"); }
static void bad_output(int max)
{
    expect_kill = 1;
    int enabled = _irq_enabled();
    if (!__builtin_setjmp(kill_escape)) {
        (void)ime_user_list_facade(NULL, out, max);
        require(0, "invalid output must kill");
    }
    expect_kill = 0;
    require(_irq_enabled() == enabled && !engine && !entries && !copies && !writes &&
            !ime_user_staging_busy && !memcmp(out, before, sizeof(out)),
            "output fault before engine/staging/copy");
}
static void no_borrow(sqlite3_stmt *s)
{
    require(!sqlite3_stmt_busy(s), "reset before input lifetime ends");
    int i = binding_slot(s);
    require(!bindings[i].borrow[0] && !bindings[i].borrow[1], "binding lifetime ended");
}
static void inputs(void)
{
    char key[257]; memset(key, 'x', sizeof(key)); key[256] = 0;
    watch(); untouched(ime_user_list_facade(NULL, out, 0));
    int invalid[] = {-1, 65, 0x7fffffff};
    for (u32 i = 0; i < 3; i++) untouched(ime_user_list_facade(NULL, out, invalid[i]));
    untouched(ime_user_list_facade(NULL, NULL, 1));
    untouched(ime_user_list_facade(key, out, 1));
    untouched(ime_user_delete_facade(NULL, NULL));
    untouched(ime_user_delete_facade("", NULL));
    untouched(ime_user_delete_facade("か", key));
    untouched(ime_user_delete_facade(key, NULL));
    untouched(ime_user_export_facade(key));
    untouched(ime_user_export_facade(NULL));
    untouched(ime_user_export_facade(""));
    bad_byte = key + 1;
    untouched(ime_user_list_facade(key, out, 64));
    untouched(ime_user_delete_facade("か", key));
    untouched(ime_user_export_facade(key));
    key[0] = 0; /* A NUL before inaccessible byte must stop the copy. */
    require(ime_user_list_facade(key, out, 1) == 0, "page end NUL accepted");
    bad_byte = NULL;
    watch(); bad_byte = (const char *)out + sizeof(out) - 1;
    bad_output(64);
    /* A read-only USER output with max=1 used to escape as an ordinary -1. */
    bad_byte = (const char *)out;
    bad_output(1); bad_byte = NULL;
    require(ime_user_list_facade(NULL, out, 1) == 0, "output fault leaves staging reusable");
    watch();
    _disable(); untouched(ime_user_list_facade(NULL, out, 1));
    untouched(ime_user_delete_facade("か", NULL));
    untouched(ime_user_export_facade("/db/out.csv"));
    require(!_irq_enabled(), "IF0 preserved"); _enable();
    identity = 0; untouched(ime_user_list_facade(NULL, out, 1)); identity = 1;
    engine = 1; untouched(ime_user_list_facade(NULL, out, 1)); engine = 0;
    watched = NULL;
    /* A 255B key is accepted in full; >32B is not a new input limit. */
    memset(key, 'x', 255); key[255] = 0;
    ime_dict_learn(&g_ime.dict, key, "long");
    watch(); caller_y = key;
    require(ime_user_list_facade(key, out, 64) == 1, "255B prefix accepted");
    watched = NULL;
    require(ime_user_delete_facade(key, "long") == 0, "255B yomi accepted");
    caller_y = NULL;
    char kanji[256]; memset(kanji, 'k', 255); kanji[255] = 0;
    ime_dict_learn(&g_ime.dict, "long-kanji", kanji);
    caller_k = kanji;
    require(ime_user_delete_facade("long-kanji", kanji) == 0, "255B kanji accepted");
    caller_k = NULL;
}
static void deletes(void)
{
    for (int mode = 0; mode < 3; mode++) {
        watched = NULL;
        ime_dict_learn(&g_ime.dict, "か", "蚊"); ime_dict_learn(&g_ime.dict, "か", "課");
        char bad[256]; memset(bad, 'x', sizeof(bad));
        if (!mode) {
            int entered = entries;
            require(ime_user_delete_facade("か", bad) == -1 && entries == entered, "failed kanji must not become NULL");
            watch(); require(ime_user_list_facade("か", out, 64) == 2, "failed kanji preserves all candidates"); watched = NULL;
        }
        require(ime_user_delete_facade("か", mode == 0 ? "蚊" : mode == 1 ? NULL : "") == 0, "delete NULL empty exact contract");
        watch(); require(ime_user_list_facade("か", out, 64) == (mode == 0 ? 1 : 0), "delete scope"); watched = NULL;
        require(ime_user_delete_facade("か", NULL) == 0, "delete cleanup");
    }
}
static void listing(void)
{
    watched = NULL;
    for (int i = 0; i < 64; i++) { char k[16]; snprintf(k, sizeof(k), "k%02d", i); ime_dict_learn(&g_ime.dict, "row", k); }
    watch(); require(ime_user_list_facade(NULL, out, 64) == 64 && writes == sizeof(out), "64 rows exact copy");
    watch(); require(ime_user_list_facade("", out, 1) == 1 && writes == sizeof(out[0]) && !memcmp((char *)out + sizeof(out[0]), before + sizeof(out[0]), sizeof(out) - sizeof(out[0])), "copy returned rows only");
    /* Prefix/output alias: snapshot must survive output writes. */
    watch(); strcpy((char *)out, "row"); memcpy(before, out, sizeof(out));
    caller_y = (const char *)out;
    require(ime_user_list_facade((char *)out, out, 64) == 64, "prefix output overlap"); caller_y = NULL;
    watch(); late = 1;
    require(ime_user_list_facade(NULL, out, 64) == -1 && !writes && !memcmp(out, before, sizeof(out)), "late refusal unchanged");
    late = 0; reenter = 1;
    require(ime_user_list_facade(NULL, out, 64) == 64, "busy released after late refusal");
    for (int i = 0; i < 6; i++) {
        watch(); fail_prepare = i == 0; fail_bind = i == 1; fail_step = i == 2; fail_final = i == 3; fail_int = i == 4; fail_after = i == 5 ? 3 : 0; steps = 0;
        require(ime_user_list_facade("row", out, 64) < 0 && !writes && !memcmp(out, before, sizeof(out)) && live == 3, "SQL failure no partial output or live stmt");
        fail_prepare = fail_bind = fail_step = fail_final = fail_int = fail_after = 0;
        require(ime_user_list_facade(NULL, out, 64) == 64, "busy released after SQL failure");
    }
    watched = NULL;
    ime_dict_learn(&g_ime.dict, "borrow-key", "borrow-kanji");
    no_borrow(g_ime.dict.learn_stmt);
    IME_Result r[2]; ime_dict_search(&g_ime.dict, "borrow-key", r, 1);
    no_borrow(g_ime.dict.prefix_stmt);
    ime_dict_search(&g_ime.dict, "か", r, 1); no_borrow(g_ime.dict.exact_stmt);
    for (int i = 0; i < 2; i++) {
        fail_bind = i == 0; fail_step = i == 1;
        ime_dict_search(&g_ime.dict, "borrow-key", r, 1); no_borrow(g_ime.dict.prefix_stmt);
        ime_dict_learn(&g_ime.dict, "borrow-key", "borrow-kanji"); no_borrow(g_ime.dict.learn_stmt);
        fail_bind = fail_step = 0;
    }
    /* UTF8 display truncation, NUL and unused bytes. */
    ime_dict_learn(&g_ime.dict, "utf", "あああああああああああ");
    watch(); require(ime_user_list_facade("utf", out, 1) == 1 && strlen(out[0].kanji) == 30 && !out[0].kanji[31], "UTF8 display boundary and zero tail");
    watched = NULL;
}
static int generic_fds(void)
{
    int n = 0;
    for (int i = 3; i < VFS_MAX_OPEN_FILES; i++) if (open_files[i].in_use) n++;
    return n;
}
static int export_write_error(void *ctx, const char *path, const void *buf, u32 size, u32 offset)
{ (void)ctx; (void)path; (void)buf; (void)size; (void)offset; return VFS_ERR_IO; }
static void exports(void)
{
    const char *path = "/db/export.csv";
    require(fixture_create(NULL, path, "sentinel", 8) == VFS_OK, "export sentinel");
    bad_byte = path + 5;
    int entered = entries, io = probes;
    require(ime_user_export_facade(path) == -1 && entries == entered && probes == io, "bad path never enters engine");
    FixtureFile *f = fixture_find(path, 0);
    require(f && f->size == 8 && !memcmp(f->data, "sentinel", 8), "bad path no truncate");
    require(!fixture_find("/db/ex", 0), "bad path no create"); bad_byte = NULL;
    require(ime_user_export_facade(path) == 0 && f->size > 8, "valid export");
    char long_path[256]; memset(long_path, 'p', 255); long_path[0] = '/'; long_path[255] = 0;
    require(ime_user_export_facade(long_path) == 0, "255B export path accepted");
    int baseline = generic_fds();
    for (int i = 0; i < 3; i++) {
        fail_prepare = i == 0; fail_step = i == 1; fail_final = i == 2;
        require(ime_user_export_facade(path) < 0 && live == 3 && generic_fds() == baseline, "export SQL cleanup");
        fail_prepare = fail_step = fail_final = 0;
    }
    mock_ops.write_stream = export_write_error;
    require(ime_user_export_facade(path) == -5 && live == 3 && generic_fds() == baseline, "export write failure cleanup");
    mock_ops.write_stream = fixture_write;
    int held[VFS_MAX_OPEN_FILES], n = 0, fd;
    while ((fd = vfs_open(path, KAPI_O_RDONLY)) >= 0) held[n++] = fd;
    require(ime_user_export_facade(path) == -2 && live == 3 && generic_fds() == baseline + n, "export open failure cleanup");
    while (n) vfs_close(held[--n]);
}
int main(void)
{
    CHECK(os32_sqlite_init() == SQLITE_OK); fixture_init(); make_dict("/db/fep.db", "蚊");
    CHECK(ime_dict_open(&g_ime.dict, "/db/fep.db") == 0);
    for (origin = CALLER_TRUSTED; origin <= CALLER_USER; origin++) {
        require(ime_user_clear_facade() == 0, "clear fixture");
        inputs(); deletes(); listing(); exports();
    }
    require(ime_dict_close(&g_ime.dict) == 0 && live == 0, "all statements finalized");
    puts("PASS B2 bounds NULL staged output lifetime SQL errors busy export"); return 0;
}
