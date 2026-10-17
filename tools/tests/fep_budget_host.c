/* T4a-4 measurement prototype, never linked into the product.
 * Real IME + bundled SQLite + real VFS/FD on disposable RAM files.
 * Host libc/file backing is outside MEMSYS5, never reported as USER malloc.
 * The independent prefix fixture deducts fixed state as in T4a-3.
 */
#define IME_DICT_HOST_MAIN ime_regression_main
#include "ime_dict_host.c"

#define BUDGET_BYTES (512u * 1024u)
#define ATOM_BYTES 64u
#define HELD_STMTS 256
#define APP_ROWS 128
#define LEARN_ROWS 8192
#ifndef OMIT_APP_GATE
#define OMIT_APP_GATE 0
#endif
#ifndef OMIT_SWITCH_GATE
#define OMIT_SWITCH_GATE 0
#endif

enum { SYSTEM, FEP, APP, CLASSES };
typedef struct { unsigned owner, charge; } BudgetTag;
_Static_assert(sizeof(BudgetTag) == 8, "ILP32 prefix layout");
typedef struct {
    sqlite3_mem_methods backing;
    unsigned context, live[CLASSES], peak, fep_peak, max_request;
    unsigned reserve, denied, physical_fail, app_peak;
} BudgetState;
static BudgetState state;
#define STATE_BYTES ((sizeof(BudgetState) + 7u) & ~7u)
#define HEAP_BYTES (BUDGET_BYTES - STATE_BYTES)
#define CAPACITY ((HEAP_BYTES / (ATOM_BYTES + 1u)) * ATOM_BYTES)
static union { double align; unsigned char bytes[HEAP_BYTES]; } budget;
_Static_assert(sizeof(state) + sizeof(budget) <= BUDGET_BYTES, "fixed state inside budget");
static const char *paths[] = {"/db/fep_s.db", "/db/fep.db", "/db/fep_l.db"};
static const char *labels[] = {"S", "M", "L"};
static char keys[3][2][32];
static sqlite3_stmt *held[HELD_STMTS];
static unsigned held_count, learns[3];
static sqlite3 *app;

static unsigned total(void) { return state.live[SYSTEM] + state.live[FEP] + state.live[APP]; }
static void *budget_malloc(int bytes)
{
    unsigned charge = (unsigned)state.backing.xRoundup(bytes + sizeof(BudgetTag));
    BudgetTag *tag;
    if (charge > state.max_request) state.max_request = charge;
    if (!OMIT_APP_GATE && state.context == APP &&
        state.live[APP] + state.live[SYSTEM] + charge > CAPACITY - state.reserve) {
        state.denied++;
        return NULL;
    }
    tag = state.backing.xMalloc((int)charge);
    if (!tag) { state.physical_fail++; return NULL; }
    tag->owner = state.context;
    tag->charge = (unsigned)state.backing.xSize(tag);
    state.live[state.context] += tag->charge;
    if (total() > state.peak) state.peak = total();
    if (state.live[FEP] > state.fep_peak) state.fep_peak = state.live[FEP];
    if (state.live[APP] > state.app_peak) state.app_peak = state.live[APP];
    return tag + 1;
}
static void budget_free(void *p)
{
    BudgetTag *tag;
    if (!p) return;
    tag = (BudgetTag *)p - 1;
    CHECK(tag->owner < CLASSES && state.live[tag->owner] >= tag->charge);
    state.live[tag->owner] -= tag->charge;
    state.backing.xFree(tag);
}
static int budget_size(void *p)
{ return state.backing.xSize((BudgetTag *)p - 1) - (int)sizeof(BudgetTag); }
static int budget_roundup(int bytes)
{ return state.backing.xRoundup(bytes + sizeof(BudgetTag)) - (int)sizeof(BudgetTag); }
static void *budget_realloc(void *p, int bytes)
{
    void *next;
    unsigned saved = state.context, size;
    if (!p) return budget_malloc(bytes);
    size = (unsigned)budget_size(p);
    if ((unsigned)bytes <= size) return p;
    state.context = ((BudgetTag *)p - 1)->owner;
    next = budget_malloc(bytes);
    state.context = saved;
    if (next) { memcpy(next, p, size); budget_free(p); }
    return next;
}
static int budget_init(void *p) { (void)p; return state.backing.xInit(state.backing.pAppData); }
static void budget_end(void *p) { (void)p; state.backing.xShutdown(state.backing.pAppData); }
static void start_budget(void)
{
    sqlite3_mem_methods methods = {budget_malloc, budget_free, budget_realloc,
        budget_size, budget_roundup, budget_init, budget_end, NULL};
    CHECK(sqlite3_config(SQLITE_CONFIG_HEAP, budget.bytes, HEAP_BYTES, ATOM_BYTES) == SQLITE_OK);
    CHECK(sqlite3_config(SQLITE_CONFIG_GETMALLOC, &state.backing) == SQLITE_OK);
    CHECK(sqlite3_config(SQLITE_CONFIG_MALLOC, &methods) == SQLITE_OK);
    state.context = SYSTEM;
    CHECK(sqlite3_initialize() == SQLITE_OK);
}
/* Probe the actual buddy free lists through their API. No debug build or
 * synthetic total-free estimate; successful probes are immediately returned. */
static unsigned largest_free(void)
{
    unsigned n;
    for (n = 1u << 19; n >= ATOM_BYTES; n >>= 1) {
        void *p = state.backing.xMalloc((int)n);
        if (p) { state.backing.xFree(p); return n; }
    }
    return 0;
}
static void load_file(const char *host, const char *guest)
{
    FILE *file = fopen(host, "rb");
    FixtureFile *f = fixture_find(guest, 1);
    long size;
    CHECK(file && f);
    CHECK(!fseek(file, 0, SEEK_END)); size = ftell(file);
    CHECK(size > 0 && (unsigned long)size <= FIXTURE_BYTES);
    rewind(file);
    CHECK(fread(f->data, 1, (size_t)size, file) == (size_t)size);
    f->size = (u32)size;
    CHECK(!fclose(file));
}
static void word(IME_Dict *dict, int variant)
{
    IME_Result results[IME_MAX_RESULTS];
    sqlite3_stmt *s = NULL;
    int n;
    state.context = FEP;
    /* Max fanout exact (<=2 chars) and prefix (3 chars) selected by builder. */
    for (int k = 0; k < 2; k++) {
        n = ime_dict_search(dict, keys[variant][k], results, IME_MAX_RESULTS);
        CHECK(n > 0);
    }
    /* Max legal byte-length pair, new row then update, with a large learning
     * B-tree already present. Verify persisted freq, not the void API result. */
    n_kprintf = 0;
    ime_dict_learn(dict, "ああああああああああa", "亜亜亜亜亜亜亜亜亜亜a");
    CHECK(!n_kprintf);
    CHECK(sqlite3_prepare_v2(dict->db,
        "SELECT freq FROM dict_user WHERE yomi='ああああああああああa' AND kanji='亜亜亜亜亜亜亜亜亜亜a'",
        -1, &s, NULL) == SQLITE_OK);
    CHECK(sqlite3_step(s) == SQLITE_ROW && sqlite3_column_int(s, 0) == (int)++learns[variant]);
    CHECK(sqlite3_finalize(s) == SQLITE_OK);
}
static void drop_pressure(void)
{
    state.context = APP;
    for (unsigned i = 0; i < held_count; i++) {
        sqlite3_finalize(held[i]); held[i] = NULL;
    }
    held_count = 0;
    if (app) CHECK(sqlite3_close(app) == SQLITE_OK);
    app = NULL;
    CHECK(!state.live[APP]);
}
static void pressure(int mode)
{
    sqlite3_stmt *sort = NULL;
    int rc;
    unsigned before = state.denied;
    state.context = APP;
    CHECK(sqlite3_open_v2(":memory:", &app, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK);
    CHECK(sqlite3_exec(app, "CREATE TABLE a(x TEXT)", NULL, NULL, NULL) == SQLITE_OK);
    for (int i = 0; i < APP_ROWS; i++)
        CHECK(sqlite3_exec(app, "INSERT INTO a VALUES(hex(zeroblob(100)))", NULL, NULL, NULL) == SQLITE_OK);
    if (mode != 0) {
        /* Keep successful sort VM/rows alive alongside retained prepares. */
        CHECK(sqlite3_prepare_v2(app, "SELECT x||x||x FROM a ORDER BY x||x||x DESC", -1, &sort, NULL) == SQLITE_OK);
        rc = sqlite3_step(sort);
        CHECK(rc == SQLITE_ROW || rc == SQLITE_NOMEM);
        held[held_count++] = sort;
    }
    for (unsigned pass = 0; pass < 2; pass++) {
        for (; held_count < HELD_STMTS; held_count++) {
            sqlite3_stmt *s = NULL;
            rc = sqlite3_prepare_v2(app,
                held_count & 1 ? "SELECT x,x||x,hex(x) FROM a ORDER BY x DESC" :
                                 "SELECT x,substr(x,1,12) FROM a WHERE x > ?1 ORDER BY x",
                -1, &s, NULL);
            if (rc == SQLITE_NOMEM) { CHECK(!s); break; }
            CHECK(rc == SQLITE_OK && s);
            held[held_count] = s;
        }
        CHECK(held_count < HELD_STMTS);
        if (!pass && mode == 2) {
            /* Different lifetimes/sizes leave real MEMSYS5 buddy holes. */
            unsigned count = 0;
            for (unsigned i = 0; i < held_count; i++) {
                if (i & 1) held[count++] = held[i];
                else sqlite3_finalize(held[i]);
            }
            held_count = count;
        } else break;
    }
    CHECK(state.denied > before); /* removing APP admission must break this control */
    CHECK(state.live[APP] + state.live[SYSTEM] <= CAPACITY - state.reserve);
}
static void switch_dict(IME_Dict *d, int target, unsigned need, int *pending)
{
    state.context = FEP;
    if (!OMIT_SWITCH_GATE && need > state.reserve) { *pending = target; return; }
    CHECK(ime_dict_reopen(d, paths[target]) == 0);
}
int main(int argc, char **argv)
{
    IME_Dict d = {0};
    unsigned demands[3], common = 0;
    CHECK(argc == 11 && sizeof(void *) == 4);
    state.reserve = (unsigned)strtoul(argv[1], NULL, 10);
    CHECK(state.reserve > 0 && state.reserve < CAPACITY);
    fixture_init();
    for (int i = 0; i < 3; i++) {
        load_file(argv[2 + i * 3], paths[i]);
        str_cpy(keys[i][0], argv[3 + i * 3], sizeof(keys[i][0]));
        str_cpy(keys[i][1], argv[4 + i * 3], sizeof(keys[i][1]));
    }
    start_budget();
    for (int i = 0; i < 3; i++) {
        state.context = FEP; state.fep_peak = 0; state.max_request = 0;
        CHECK(ime_dict_open(&d, paths[i]) == 0);
        word(&d, i);
        demands[i] = state.fep_peak;
        if (demands[i] > common) common = demands[i];
        printf("PROFILE variant=%s steady=%u peak=%u request=%u file=%u learn_rows=%u\n",
            labels[i], state.live[FEP], state.fep_peak, state.max_request, (unsigned)fixture_find(paths[i], 0)->size, LEARN_ROWS);
        ime_dict_close(&d);
        CHECK(!state.live[FEP]);
    }
    CHECK(common <= state.reserve);
    for (int mode = 0; mode < 3; mode++) for (int from = 0; from < 3; from++) {
        state.context = FEP;
        CHECK(ime_dict_open(&d, paths[from]) == 0);
        pressure(mode);
        state.fep_peak = state.live[FEP]; state.peak = total(); state.max_request = 0;
        word(&d, from);
        for (int to = 0; to < 3; to++) {
            unsigned before;
            int pending = -1;
            switch_dict(&d, from, demands[from], &pending);
            CHECK(pending == -1);
            word(&d, from);
            before = total();
            state.peak = total(); state.fep_peak = state.live[FEP]; state.max_request = 0;
            switch_dict(&d, to, demands[to], &pending);
            CHECK(pending == -1 && !strcmp(d.path, paths[to]));
            word(&d, to);
            CHECK(state.fep_peak <= state.reserve);
            if (state.fep_peak > common) common = state.fep_peak;
            printf("SWITCH mode=%d from=%s to=%s before=%u peak=%u extra=%u fep_peak=%u request=%u max_free=%u app=%u system=%u\n",
                mode, labels[from], labels[to], before, state.peak, state.peak - before,
                state.fep_peak, state.max_request, largest_free(), state.live[APP], state.live[SYSTEM]);
        }
        {
            int pending = -1;
            void *old_db = d.db;
            unsigned old_live = total();
            char old_path[OS32_MAX_PATH];
            str_cpy(old_path, d.path, sizeof(old_path));
            switch_dict(&d, 0, state.reserve + ATOM_BYTES, &pending);
            CHECK(pending == 0 && d.db == old_db && total() == old_live && !strcmp(d.path, old_path));
            word(&d, 2);
            printf("RESTART mode=%d from=%s old=L requested=S unchanged=1\n", mode, labels[from]);
        }
        state.context = FEP; ime_dict_close(&d);
        drop_pressure();
        CHECK(!state.live[FEP]);
    }
    state.context = SYSTEM;
    CHECK(sqlite3_shutdown() == SQLITE_OK);
    CHECK(!total());
    printf("BUDGET bytes=%u heap=%u state=%u capacity=%u prefix=%u reserve=%u app_limit=%u app_peak=%u denied=%u physical_fail=%u common_measured=%u result=PASS\n",
        BUDGET_BYTES, (unsigned)HEAP_BYTES, (unsigned)STATE_BYTES,
        (unsigned)CAPACITY, (unsigned)sizeof(BudgetTag), state.reserve,
        (unsigned)CAPACITY - state.reserve, state.app_peak, state.denied, state.physical_fail, common);
    return 0;
}
