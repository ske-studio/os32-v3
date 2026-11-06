/* Real suspension implementation; only state, dictionary I/O and rendering are fake. */
#include "ime.h"
#include "kprintf.h"
#include "fep_boundary_runtime.h"
static IME_State state;
static int closes, opens, close_rc, open_rc, warnings, close_isolates;
static char closed_path[sizeof(state.store.system.path)];
static int reopened_same_path;
IME_State *ime_suspend_state(void) { return &state; }
void ime_set_mode(int mode)
{
    if (mode < IME_MODE_OFF || mode > IME_MODE_KATAKANA) return;
    if (mode != IME_MODE_OFF && !state.dict_loaded) {
        if (fep_store_open(&state.store, IME_DICT_PATH, "/etc/custom.db")) {
            warnings++;
            return;
        }
        state.dict_loaded = 1;
    }
    state.mode = mode;
}
int fep_store_close(FepStore *store)
{
    FepConn *dict = &store->system;
    closes++;
    if (dict->isolated) return -1;
    kstrncpy(closed_path, dict->path, sizeof(closed_path));
    if (close_rc) {
        if (close_isolates) dict->isolated = 1;
        return close_rc;
    }
    dict->db = 0;
    return 0;
}
int fep_store_open(FepStore *store, const char *path, const char *user_path)
{
    FepConn *dict = &store->system;
    if (store->user.path[0] && kstrcmp(store->user.path, user_path)) return -1;
    kstrncpy(store->user.path, user_path, sizeof(store->user.path));
    opens++;
    if (dict->isolated || dict->db) return -1;
    reopened_same_path = !kstrcmp(path, closed_path);
    kmemset(dict, 0, sizeof(*dict));
    kstrncpy(dict->path, path, sizeof(dict->path));
    if (open_rc) return open_rc;
    dict->db = dict; /* Any non-NULL connection represents a live DB. */
    return 0;
}
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{ (void)attr; (void)fmt; warnings++; }
#include "ime_suspend_source.inc"
static void require(int cond, const char *name)
{ __asm__ volatile("" : "+r"(cond)); if (!cond) { printf("FAIL %s\n", name); exit(1); } }
int main(void)
{
    state.store.boot_reserve_kb = FEP_DICT_RESERVE_KB;
    require(!ime_dict_suspend(0) && !opens, "reopen without suspend");
    require(!ime_dict_suspend(1) && !closes, "unloaded no-op");
    require(!fep_store_open(&state.store, "/db/fep_s.db", "/etc/custom.db"), "initial open");
    opens = 0;
    state.dict_loaded = 1; state.mode = IME_MODE_KATAKANA;
    require(!ime_dict_suspend(1) && !state.dict_loaded && state.mode == IME_MODE_OFF && closes == 1, "close and OFF");
    require(!ime_dict_suspend(1) && closes == 1, "double close");
    require(!ime_dict_suspend(0) && state.dict_loaded && state.mode == IME_MODE_KATAKANA && opens == 1, "restore mode");
    require(reopened_same_path && !kstrcmp(state.store.system.path, "/db/fep_s.db"), "dictionary path preserved");
    require(!ime_dict_suspend(0) && opens == 1, "double reopen");
    close_rc = -3;
    require(ime_dict_suspend(1) == -3 && state.dict_loaded && state.mode == IME_MODE_KATAKANA, "close failure restores mode");
    require(!ime_dict_suspend(0) && opens == 1, "failed close not suspended");
    close_rc = 0; state.mode = IME_MODE_HIRAGANA;
    require(!ime_dict_suspend(1), "second close");
    open_rc = -4;
    require(ime_dict_suspend(0) == -4 && !state.dict_loaded && state.mode == IME_MODE_OFF && warnings == 1, "reopen failure disabled");
    open_rc = 0;
    require(!ime_dict_suspend(0) && state.dict_loaded && state.mode == IME_MODE_HIRAGANA, "reopen retry");
    state.mode = IME_MODE_OFF;
    require(!ime_dict_suspend(1) && !ime_dict_suspend(0) && state.mode == IME_MODE_OFF, "loaded OFF restored");
    /* A failed reopen may be followed by ime on (the production lazy open). */
    ime_set_mode(IME_MODE_HIRAGANA);
    require(!ime_dict_suspend(1), "close before external reopen");
    open_rc = -4;
    require(ime_dict_suspend(0) == -4 && !state.dict_loaded, "external reopen setup failure");
    open_rc = 0;
    ime_set_mode(IME_MODE_KATAKANA);
    require(state.dict_loaded && state.store.system.db && state.mode == IME_MODE_KATAKANA, "external lazy open");
    int before_closes = closes;
    require(!ime_dict_suspend(1) && !state.dict_loaded && !state.store.system.db && closes == before_closes + 1, "external reopen can suspend again");
    require(!ime_dict_suspend(0) && state.dict_loaded && state.mode == IME_MODE_KATAKANA && reopened_same_path, "external reopen restored");
    require(!fep_store_close(&state.store), "switch dictionary close");
    state.dict_loaded = 0;
    require(!fep_store_open(&state.store, "/db/fep_l.db", "/etc/custom.db"), "switch dictionary open");
    state.dict_loaded = 1;
    require(!ime_dict_suspend(1) && !ime_dict_suspend(0) && reopened_same_path &&
            !kstrcmp(state.store.system.path, "/db/fep_l.db"), "large dictionary path preserved");
    close_rc = -3; close_isolates = 1;
    require(ime_dict_suspend(1) == -3 && state.store.system.isolated && state.mode == IME_MODE_OFF, "isolated close stays OFF");
    puts("GREEN ime suspend");
    return 0;
}
