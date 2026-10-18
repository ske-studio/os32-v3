/* X-11: real shared services and keyboard queues, ILP32. Device rendering,
 * dictionary storage and CPU transitions are fixture boundaries. No per-owner
 * IME state: switching owners alone must never manufacture a private copy. */
#include "types.h"
#include "io.h"
#include "appslot.h"
#include "ime.h"
#include "gui.h"
#include "con_sink.h"
#include "kbd_inject.h"
#include "kstring.h"

int services_public = 1;
volatile u32 services_boot_fault;
static int owner = 1, user, wm_depth, wait_depth, calls, seen_owner;
static int opens, reopens, searches, learns, management, storage_rc;
static int scroll_rows, candidate_draws, mode_draws;
static u32 ime_cells[25][80];
static u32 gui_ime_cells[25][80];
static AppSlot slots[APP_SLOT_COUNT];
volatile u32 tick_count;
volatile int exec_nest_level, ring3_wm_depth;
int rshell_active;

static void say(const char *s)
{
    u32 n = kstrlen(s);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, msg) do { if (!(c)) { say("FAIL " msg "\n"); return 1; } } while (0)
int res_owner_get(void) { return owner; }
void res_owner_set(int id) { owner = id; }
int appslot_cur(void) { return owner; }
AppSlot *appslot_get(int id) { return &slots[id]; }
int appslot_gfx_owner(void) { return 0; }
i32 launch_child(i32 id) { (void)id; return 0; }
int ring3_call_from_user(void) { return user && !wm_depth; }
void ring3_abort_check(void) {}
void ring3_abort_request(void) {}
int ring3_wait_pending(void) { return 1; }
int exec_park_kbd(void) { return 0; }
int exec_park_poll(u32 t) { (void)t; return 0; }
void appslot_poll_yield_reset(void) {}
int v86_is_active(void) { return 0; }
int v86_kbd_push(u8 code) { (void)code; return 0; }
void v86_request_exit(void) {}
int serial_trygetchar(void) { return -1; }
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) { (void)s; }
void ring3_wm_enter(void) { wm_depth++; ring3_wm_depth++; }
void ring3_wm_leave(void) { wm_depth--; ring3_wm_depth--; }
void appslot_gui_op_enter(int wait) { wait_depth = wait ? 1 : 2; }
void appslot_gui_op_leave(void) { wait_depth = 0; }
i32 exec_trim_done(u32 epoch) { (void)epoch; return OS32_ERR_INVAL; }

void __cdecl kprintf(u8 color, const char *fmt, ...) { (void)color; (void)fmt; }
void tvram_set_scroll_reserve(int rows) { scroll_rows = rows; }
void tvram_readchar_at(int x, int y, u16 *ch, u8 *attr)
{ (void)x; (void)y; *ch = ' '; *attr = 7; }
void tvram_putchar_at(int x, int y, char ch, u8 attr)
{ (void)x; (void)y; (void)ch; (void)attr; }
void tvram_writechar_at(int x, int y, u16 ch, u8 attr)
{ (void)x; (void)y; (void)ch; (void)attr; }
static void draw_char(int x, int y, char ch, u8 attr)
{ (void)attr; ime_cells[y][x] = (u8)ch; if (ch == '[') mode_draws++; }
static int draw_wide(int x, int y, u32 ch, u8 attr)
{ (void)attr; ime_cells[y][x] = ch; if (y < 24) candidate_draws++; return 2; }
static void clear_row(int y, u8 attr) { (void)attr; kmemset(ime_cells[y], 0, sizeof(ime_cells[y])); }
const IME_Render g_ime_render_tvram = {draw_char, draw_wide, clear_row, 0, 0};
static void gui_draw_char(int x, int y, char ch, u8 attr)
{ (void)attr; gui_ime_cells[y][x] = (u8)ch; }
static int gui_draw_wide(int x, int y, u32 ch, u8 attr)
{ (void)attr; gui_ime_cells[y][x] = ch; return 2; }
static void gui_clear_row(int y, u8 attr)
{ (void)attr; kmemset(gui_ime_cells[y], 0, sizeof(gui_ime_cells[y])); }
void *kmalloc(u32 n) { (void)n; return 0; }

/* The same dictionary instance is shared through all nine IME entries.
 * SQLite/VFS error propagation is covered separately by ime_dict_host.c. */
static IME_Dict *shared_dict;
int ime_dict_open(IME_Dict *d, const char *p)
{ opens++; shared_dict = d; kstrncpy(d->path, p, sizeof(d->path)); d->db = d; return 0; }
int ime_dict_reopen(IME_Dict *d, const char *p)
{ reopens++; return ime_dict_open(d, p); }
int ime_dict_search(IME_Dict *d, const char *y, IME_Result *r, int max)
{
    (void)y; (void)max;
    if (d != shared_dict) return 0;
    searches++;
    kstrncpy(r[0].kanji, "亜", sizeof(r[0].kanji));
    kstrncpy(r[1].kanji, "阿", sizeof(r[1].kanji));
    return 2;
}
void ime_dict_learn(IME_Dict *d, const char *y, const char *k)
{ (void)y; (void)k; if (d == shared_dict) learns++; }
int ime_user_list(IME_Dict *d, const char *prefix, IME_UserEntry *out, int max)
{ (void)d; (void)prefix; (void)out; (void)max; management++; return storage_rc; }
int ime_user_delete(IME_Dict *d, const char *y, const char *k)
{ (void)y; (void)k; management++; return d == shared_dict ? storage_rc : -99; }
int ime_user_export(IME_Dict *d, const char *p)
{ (void)p; management++; return d == shared_dict ? storage_rc : -99; }
int ime_user_clear(IME_Dict *d)
{ management++; return d == shared_dict ? storage_rc : -99; }

#include "kbd_source.inc"
#include "ime_source.inc"
#include "gui_source.inc"
#include "sink_source.inc"
#include "inject_source.inc"
#include "exec_result.inc"

static void reset(void)
{
    owner = 1; user = 0;
    con_sink_owner_exit(con_sink_reader_get());
    con_sink_disable();
    kbd_set_gui_mode(0);
    kbd_head = kbd_tail = kbd_count = 0;
    kbd_raw_head = kbd_raw_tail = kbd_raw_count = 0;
    kbd_shift_state = 0;
    kmemset(&g_ime, 0, sizeof(g_ime));
    g_ime.per_page = 9;
    g_ime.render = &g_ime_render_tvram;
    g_ime_reader_owner = opens = reopens = searches = learns = management = 0;
    g_gui_handler = 0;
    g_ime_cui_redraw = mode_draws = 0;
    kmemset(ime_cells, 0, sizeof(ime_cells));
    kmemset(gui_ime_cells, 0, sizeof(gui_ime_cells));
    shared_dict = 0;
}
static void key(u8 code) { kbd_deliver(code); kbd_deliver(code | 0x80); }
static int equal_state(const IME_State *a) { return !memcmp(a, &g_ime, sizeof(*a)); }

static int modes(void)
{
    reset();
    const int invalid[] = {-1, 3, 0x7fffffff};
    for (int active = 0; active < 2; active++) {
        if (active) { ime_toggle(); key(0x1d); (void)ime_trygetkey(); }
        IME_State saved = g_ime;
        int opened = opens, scroll = scroll_rows;
        for (u32 i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
            ime_set_mode(invalid[i]);
            CHECK(equal_state(&saved) && opens == opened && scroll_rows == scroll,
                  "invalid mode preserves complete state and does not open");
            CHECK(ime_switch_dict(invalid[i] == 3 ? 99 : invalid[i]) == -1 &&
                  !reopens && equal_state(&saved), "invalid variant preserves state");
        }
    }
    ime_set_mode(IME_MODE_KATAKANA);
    CHECK(ime_get_mode() == IME_MODE_KATAKANA && opens == 1, "valid katakana reuses dictionary");
    ime_set_mode(IME_MODE_OFF);
    CHECK(!ime_is_active(), "valid OFF");
    say("PASS X11 modes\n"); return 0;
}

static int delivery(void)
{
    reset(); owner = 2; user = 1;
    key(0x1d); /* a */
    CHECK(ime_trygetchar() == 'a' && ime_trygetkey() == -1 && kbd_trygetrawkey() == -1,
          "CUI one cooked consumption");
    ime_toggle();
    key(0x1d); CHECK(ime_trygetkey() == -1, "CUI kana consumed");
    key(KEY_SPACE); CHECK(ime_trygetkey() == -1 && g_ime.result_count == 2, "CUI candidates");
    key(KEY_RETURN);
    CHECK(ime_trygetchar() == 0xe4 && ime_trygetkey() == 0xba && ime_trygetchar() == 0x9c &&
          ime_trygetkey() == -1 && learns == 1 && searches == 1, "CUI UTF8 exactly once");

    key(0x1d); CHECK(ime_trygetkey() == -1, "CUI kana before toggle OFF");
    ime_toggle();
    CHECK(!ime_is_active() && ime_trygetchar() == 0xe3 && ime_trygetkey() == 0x81 &&
          ime_trygetchar() == 0x82 && ime_trygetkey() == -1,
          "toggle OFF confirms kana exactly once");
    ime_toggle();

    /* GUI raw -> WM feed -> Text / terminal injection. The real Rust WM
     * dispatcher is tested by its existing host suite; here we cross the
     * actual C KAPI/keyboard boundary, including reads between UTF8 bytes. */
    con_sink_enable(); kbd_set_gui_mode(1);
    key(0x1d);
    int raw = kbd_trygetrawkey();
    CHECK((raw & 0x1ff) == (0x100 | 0x1d) && !kbd_count, "WM raw only");
    CHECK(ime_feed_key((0x1d << 8) | 'a') == -1, "WM kana consumed");
    CHECK((kbd_trygetrawkey() & 0x1ff) == 0x1d && kbd_trygetrawkey() == -1,
          "WM make and break each once");
    CHECK(ime_feed_key((KEY_SPACE << 8) | ' ') == -1, "WM candidates");
    CHECK(ime_feed_key((KEY_RETURN << 8) | 13) == 0xe4, "WM commit first byte");
    IME_State saved = g_ime;
    ime_owner_exit(2);
    CHECK(equal_state(&saved), "GUI app exit preserves WM output and dictionary");
    CHECK(ime_trygetchar() == -1 && ime_trygetkey() == -1 &&
          ime_getchar_kapi() == -1 && ime_getkey_kapi() == -1 && equal_state(&saved),
          "GUI cooked readers cannot steal WM commit tail");
    CHECK(ime_feed_key(-1) == 0xba && ime_feed_key(-1) == 0x9c && ime_feed_key(-1) == -1,
          "WM drains UTF8 exactly once");
    u8 buf[CON_SINK_REC_MAX];
    CHECK(con_sink_read(buf, sizeof(buf)) == 0, "terminal becomes reader");
    CHECK(kbd_inject((const u8 *)"ka", 2) == 2, "terminal inject already converted text");
    saved = g_ime;
    CHECK(ime_trygetkey() == 'k' && ime_trygetchar() == 'a' && equal_state(&saved),
          "GUI injected ASCII is not converted twice");
    CHECK(kbd_inject((const u8 *)"ki", 2) == 2 && ime_getkey_kapi() == 'k' &&
          ime_getchar_kapi() == 'i' && equal_state(&saved), "GUI blocking readers bypass FEP");
    say("PASS X11 CUI/WM delivery\n"); return 0;
}

/* IMEIND-1: real CUI keyboard/FEP path; renderer cells model the visible row.
 * No dictionary lookup is needed for discarding an incomplete consonant. */
static int cui_return_indicator(void)
{
    for (int mode = IME_MODE_HIRAGANA; mode <= IME_MODE_KATAKANA; mode++) {
        for (int read_kind = 0; read_kind < 2; read_kind++) {
            reset(); ime_set_mode(mode);
            int (*read_key)(void) = read_kind ? ime_trygetkey : ime_trygetchar;
            u32 indicator = mode == IME_MODE_HIRAGANA ? 0x3042 : 0x30a2;
            key(0x24); /* k */
            CHECK(read_key() == -1 && !g_ime.kana_len &&
                  !kstrcmp(g_ime.rk.preedit, "k") && ime_cells[24][4] == 'k',
                  "IMEIND CUI incomplete consonant displayed");
            key(KEY_RETURN);
            CHECK(read_key() == -1 && g_ime.commit_pos == g_ime.commit_len,
                  "IMEIND discarded RETURN produces no output");
            CHECK(ime_cells[24][0] == '[' && ime_cells[24][1] == indicator &&
                  ime_cells[24][3] == ']',
                  "IMEIND discarded RETURN keeps visible ON indicator");
            for (int x = 4; x < TVRAM_COLS; x++) {
                CHECK(!ime_cells[24][x], "IMEIND discarded reading is no longer visible");
            }
            CHECK(ime_get_mode() == mode && scroll_rows == 1 &&
                  !g_ime.rk.preedit[0] && !g_ime.rk.n_wait,
                  "IMEIND discarded RETURN clears romaji and keeps mode");
            key(KEY_RETURN);
            CHECK((read_key() & 255) == 13, "IMEIND empty RETURN passes through");
            key(0x1d); /* a must not combine with the discarded k */
            CHECK(read_key() == -1 && !kstrcmp(g_ime.kana_buf, "あ"),
                  "IMEIND next typing starts a fresh reading");
            key(KEY_RETURN);
            CHECK(read_key() == 0xe3 && read_key() == (mode == IME_MODE_HIRAGANA ? 0x81 : 0x82) &&
                  read_key() == (mode == IME_MODE_HIRAGANA ? 0x82 : 0xa2) && read_key() == -1 &&
                  ime_cells[24][0] == '[' && ime_cells[24][1] == indicator,
                  "IMEIND kana RETURN commits once and keeps ON indicator");
            key(0x2e); /* n still flushes to kana before the romaji reset. */
            CHECK(read_key() == -1, "IMEIND pending n consumed");
            key(KEY_RETURN);
            CHECK(read_key() == 0xe3 && read_key() == (mode == IME_MODE_HIRAGANA ? 0x82 : 0x83) &&
                  read_key() == (mode == IME_MODE_HIRAGANA ? 0x93 : 0xb3) && read_key() == -1,
                  "IMEIND RETURN still flushes n exactly once");
        }
    }
    say("PASS IMEIND CUI RETURN indicator\n"); return 0;
}

static int lifetime(void)
{
    for (int read_kind = 0; read_kind < 2; read_kind++) {
        reset(); owner = 2; user = 1; ime_toggle();
        key(0x1d);
        CHECK((read_kind ? ime_trygetkey() : ime_trygetchar()) == -1, "parent input consumed");
        key(KEY_SPACE);
        CHECK((read_kind ? ime_trygetkey() : ime_trygetchar()) == -1, "parent candidates ready");
        IME_State saved = g_ime;
        owner = 3; /* child with no input; an empty poll must not acquire custody */
        CHECK(ime_trygetkey() == -1 && ime_trygetchar() == -1, "child empty polls");
        ime_owner_exit(3);
        CHECK(equal_state(&saved), "nonreader child exit preserves parent candidate/dictionary");
        CHECK(ime_switch_dict(2) == 0 && !kstrcmp(g_ime.dict.path, "/db/fep_l.db"),
              "dictionary switch globally visible");
        CHECK(g_ime.result_count == saved.result_count && g_ime.converting == saved.converting &&
              !memcmp(g_ime.results, saved.results, sizeof(saved.results)),
              "dictionary switch preserves copied candidates");
        owner = 3; storage_rc = -4;
        CHECK(ime_user_delete_facade("あ", 0) == -4 &&
              ime_user_export_facade("/tmp/learn.csv") == -4 &&
              ime_user_clear_facade() == -4 && management == 3, "shared management forwards failures");
        storage_rc = 0;
        CHECK(ime_user_delete_facade("あ", "亜") == 0 && ime_user_clear_facade() == 0 &&
              ime_user_export_facade("/tmp/learn.csv") == 0, "shared management forwards success");
        owner = 2;
        key(KEY_RETURN);
        int first = read_kind ? ime_trygetkey() : ime_trygetchar();
        CHECK(first == 0xe4, "parent commit");
        ime_owner_exit(2);
        CHECK(!g_ime.commit_len && !g_ime.kana_len && !g_ime.result_count && !g_ime.converting &&
              g_ime.mode == IME_MODE_HIRAGANA && g_ime.dict.db &&
              !kstrcmp(g_ime.dict.path, "/db/fep_l.db"), "nonblocking reader exit cancels only input");
        /* A child that really reads advances the one shared composition;
         * its parent is then no longer the reader whose exit cancels it. */
        owner = 2; key(0x1d); (void)ime_trygetkey();
        owner = 3; key(KEY_SPACE);
        CHECK((read_kind ? ime_trygetkey() : ime_trygetchar()) == -1 && g_ime.result_count == 2,
              "child reads parent's shared kana into candidates");
        saved = g_ime; ime_owner_exit(2);
        CHECK(equal_state(&saved), "old reader exit preserves current child's candidate");
        ime_owner_exit(3);
        CHECK(!g_ime.result_count && !g_ime.kana_len && g_ime.dict.db,
              "child reader exit cancels shared input and keeps resident dictionary");
    }
    say("PASS X11 parent child output lifetime\n"); return 0;
}

/* IMECAND-1: actual FEP, keyboard queues and GUI teardown boundary. The Rust
 * suite checks window/owner dispatch to STOP; here STOP is not mocked. */
static int gui_input_lifetime(void)
{
    static const IME_Render gfx_render = {gui_draw_char, gui_draw_wide, gui_clear_row, 0, 0};
    for (int boundary = 0; boundary < 2; boundary++) {
        for (int phase = 0; phase < 4; phase++) {
            reset();
            ime_toggle();
            ime_set_render((void *)&gfx_render);
            CHECK(gui_ime_cells[24][0] == '[' && gui_ime_cells[24][1] == 0x3042,
                  "IMECAND GUI entry ON indicator drawn synchronously");
            (void)ime_feed_key((0x1d << 8) | 'a');
            if (phase >= 1) (void)ime_feed_key((KEY_SPACE << 8) | ' ');
            if (phase == 2) (void)ime_feed_key(KEY_DOWN << 8);
            if (phase == 3) (void)ime_feed_key((KEY_RETURN << 8) | 13);
            IME_State saved = g_ime;
            ime_set_render((void *)&gfx_render);
            CHECK(equal_state(&saved), "IMECAND same renderer preserves current input");
            int learned = learns;
            if (!boundary) {
                /* WM cancel_input clears its GFX grid before sending STOP. */
                kmemset(gui_ime_cells, 0, sizeof(gui_ime_cells));
                (void)ime_feed_key(KEY_STOP << 8);
            } else {
                /* Normal gshell detach, then defensive owner-exit reset. */
                ime_set_render(0);
                gui_owner_exit(GUI_SHELL_OWNER, 0);
            }
            CHECK(!g_ime.kana_len && !g_ime.rk.preedit[0] && !g_ime.rk.n_wait &&
                  !g_ime.result_count && !g_ime.converting && !g_ime.convert_len &&
                  g_ime.state == IME_ST_INPUT && !g_ime.commit_len && !g_ime.commit_pos,
                  "IMECAND input lifetime cancels all transient state");
            CHECK(learns == learned && g_ime.dict.db && ime_is_active() && scroll_rows == 1,
                  "IMECAND cancellation keeps mode dictionary without learning");
            u32 (*cells)[80] = boundary ? ime_cells : gui_ime_cells;
            CHECK(cells[24][0] == '[' && cells[24][1] == 0x3042,
                  "IMECAND cancellation keeps visible ON indicator");
            CHECK(ime_feed_key(-1) == -1 && ime_feed_key((KEY_RETURN << 8) | 13) ==
                  ((KEY_RETURN << 8) | 13), "IMECAND no candidate or swallowed RETURN");
            if (boundary) {
                mode_draws = 0; /* model the shell reload loop's TVRAM clear */
                kmemset(ime_cells, 0, sizeof(ime_cells));
                CHECK(ime_trygetkey() == -1 && mode_draws == 1,
                      "IMECAND CUI mode visible after reload clear");
            }
            /* Cancellation keeps IME ON: new typing belongs to FEP, with a
             * visible mode indicator in both GUI and CUI. */
            if (boundary) {
                key(0x1d);
                CHECK(ime_trygetkey() == -1 && g_ime.kana_len > 0,
                      "IMECAND CUI ON typing enters FEP after cancellation");
            } else {
                CHECK(ime_feed_key((0x1d << 8) | 'a') == -1 && g_ime.kana_len > 0,
                      "IMECAND GUI ON typing enters FEP after cancellation");
            }
            CHECK(cells[24][0] == '[' && cells[24][1] == 0x3042,
                  "IMECAND ON typing keeps visible mode indicator");
            (void)ime_feed_key(KEY_STOP << 8);
            /* Shared mode remains ON. OFF now has nothing to confirm. */
            ime_toggle();
            CHECK(ime_feed_key(-1) == -1, "IMECAND OFF cannot confirm old kana");
            if (!boundary) {
                CHECK(ime_feed_key((0x2b << 8) | 'c') == ((0x2b << 8) | 'c') &&
                      ime_feed_key((0x25 << 8) | 'l') == ((0x25 << 8) | 'l') &&
                      ime_feed_key((0x1e << 8) | 's') == ((0x1e << 8) | 's') &&
                      ime_feed_key((KEY_RETURN << 8) | 13) == ((KEY_RETURN << 8) | 13),
                      "IMECAND GUI subsequent cls RETURN intact");
            } else {
                key(0x2b); key(0x25); key(0x1e); key(KEY_RETURN);
                CHECK((ime_getkey() & 255) == 'c' && (ime_getkey() & 255) == 'l' &&
                      (ime_getkey() & 255) == 's' && (ime_getkey() & 255) == 13,
                      "IMECAND CUI subsequent cls RETURN intact");
            }
        }
    }
    /* Emergency gshell exit without a normal detach also cancels. */
    reset(); ime_set_render((void *)&gfx_render); ime_toggle();
    (void)ime_feed_key((0x1d << 8) | 'a');
    (void)ime_feed_key((KEY_SPACE << 8) | ' ');
    gui_owner_exit(GUI_SHELL_OWNER, 0);
    CHECK(!g_ime.result_count && !g_ime.kana_len && ime_feed_key(-1) == -1,
          "IMECAND emergency GUI exit cancels candidate");
    say("PASS IMECAND real STOP and GUI CUI lifetime\n"); return 0;
}

#include "x11_park_host.c"

static i32 handler(u32 op, u32 arg, int id)
{ (void)op; calls++; seen_owner = id; return wm_depth == 1 && wait_depth ? (i32)arg : -99; }
static int gui_dispatch(void)
{
    reset();
    CHECK(gui_call(GUI_OP_WAIT, 7) == OS32_ERR_NOSYS, "WM absent");
    CHECK(gui_register((void *)handler, 0) == 0, "normal WM register");
    calls = 0; user = 1;
    for (owner = 2; owner <= 3; owner++) {
        CHECK(gui_call(GUI_OP_WAIT, 27) == 27 && seen_owner == owner &&
              !wm_depth && !wait_depth, "WM dispatch real owner and balanced context");
        CHECK(gui_call(GUI_OP_OWNER_EXIT, 0) == OS32_ERR_INVAL && calls == owner - 1,
              "internal owner exit rejected before WM");
    }
    CHECK(gui_call(GUI_OP_TRIM_DONE, 0) == OS32_ERR_INVAL && calls == 2,
          "trim completion stays core handled");
    say("PASS X11 GUI owner dispatch\n"); return 0;
}

static int sink_readers(void)
{
    reset(); u8 buf[CON_SINK_REC_MAX];
    kmemset(buf, 0xa5, sizeof(buf));
    owner = 2; con_sink_enable();
    CHECK(con_sink_read(0, sizeof(buf)) == OS32_ERR_INVAL &&
          con_sink_read(buf, sizeof(buf)-1) == OS32_ERR_INVAL &&
          con_sink_reader_get() == CON_SINK_NO_READER, "invalid sink input no custody");
    CHECK(con_sink_read(buf, sizeof(buf)) == 0 && con_sink_reader_get() == 2,
          "empty first read claims sink");
    owner = 3; con_sink_owner_exit(3); /* child exit and mode changes must not release parent */
    con_sink_disable(); con_sink_enable();
    CHECK(con_sink_read(buf, sizeof(buf)) == OS32_ERR_EXIST && buf[0] == 0xa5,
          "reader retained across child exit park and mode changes");
    con_sink_push_print("AB", 2, 7);
    CHECK(con_sink_read(buf, sizeof(buf)) == OS32_ERR_EXIST, "other reader cannot consume output");
    owner = 2;
    CHECK(con_sink_read(buf, sizeof(buf)) == 5 && buf[3] == 'A' && buf[4] == 'B',
          "parent output copied once");
    owner = 3;
    con_sink_owner_exit(2);
    CHECK(con_sink_read(buf, sizeof(buf)) == 0 && con_sink_reader_get() == 3 && buf[3] == 'A',
          "reader exit returns custody copied output remains caller owned");
    say("PASS X11 sink reader lifetime\n"); return 0;
}

static int results(void)
{
    reset(); int kind = -9, code = -9;
    g_last_kind = EXEC_KIND_NONE; g_last_code = 99;
    CHECK(exec_last_result(&kind, &code) == OS32_ERR_INVAL && kind == EXEC_KIND_NONE && !code,
          "no exec result clears output");
    const int kinds[] = {EXEC_KIND_EXITED, EXEC_KIND_FAULT, EXEC_KIND_ABORTED};
    for (u32 i = 0; i < sizeof(kinds)/sizeof(kinds[0]); i++) {
        /* Production exec_run + exec_finish are exercised by test_exec_r1;
         * this observation uses its actual getter, no private owner cache. */
        g_last_kind = kinds[i]; g_last_code = 17;
        owner = 3;
        CHECK(exec_last_result(&kind, &code) == 0 && kind == kinds[i] && code == 17,
              "child observes global last result");
        g_last_kind = EXEC_KIND_EXITED; g_last_code = 23; owner = 2;
        CHECK(exec_last_result(&kind, &code) == 0 && kind == EXEC_KIND_EXITED && code == 23,
              "parent observes newest global result");
        owner = 1; owner = 2;
        CHECK(exec_last_result(0, &code) == 0 && code == 23 && exec_last_result(&kind, 0) == 0,
              "park and optional outputs do not consume result");
    }
    say("PASS X11 global result\n"); return 0;
}
static int unpublished(void)
{
    reset();
    services_public = 0;
    ime_toggle(); ime_set_mode(IME_MODE_HIRAGANA);
    CHECK(!ime_is_active() && ime_get_mode() == IME_MODE_OFF && !opens, "unpublished IME stays disabled");
    CHECK(ime_switch_dict(1) == OS32_ERR_NOSYS &&
          ime_user_list_facade(0, 0, 0) == OS32_ERR_NOSYS &&
          ime_user_delete_facade(0, 0) == OS32_ERR_NOSYS &&
          ime_user_export_facade(0) == OS32_ERR_NOSYS &&
          ime_user_clear_facade() == OS32_ERR_NOSYS && !management,
          "unpublished management stubs");
    CHECK(ime_feed_key(0x1e61) == 0x1e61 && ime_feed_key(-1) == -1,
          "MINIMAL GUI raw key and empty input");
    CHECK(ime_trygetkey() == -1 && ime_trygetchar() == -1, "MINIMAL empty CUI polls");
    key(0x1d);
    CHECK(ime_trygetchar() == 'a', "MINIMAL CUI raw char");
    /* PC-98 arrows carry control bytes; F1/F2 are actual ASCII-zero keys. */
    key(KEY_F1);
    CHECK(ime_trygetchar() == -1, "MINIMAL scan-only poll returns no character");
    key(KEY_F1); key(0x1d);
    CHECK(ime_getchar() == 'a', "MINIMAL wait skips scan-only key");
    key(KEY_F2); key(0x1d);
    CHECK(ime_getchar_kapi() == 'a', "MINIMAL interruptible wait skips scan-only key");
    services_public = 1;
    say("PASS unpublished DB-independent IME stubs and MINIMAL input\n");
    return 0;
}
void _start(void)
{
    int rc = unpublished() || modes() || delivery() || cui_return_indicator() || lifetime() || gui_input_lifetime() || park_lifetime() ||
             gui_dispatch() || sink_readers() || results();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
