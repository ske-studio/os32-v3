/* Compile real console/FM/serial/kbd/IME bodies and the rshell wrapper.
 * Replace device I/O, scheduler landings and dictionary storage. */
#include "types.h"
#include "io.h"
#include "fm.h"
#include "os32_kapi_shared.h"
static unsigned ports, waits, abort_checks, request_wait=3, opn_reg, last_key;
static volatile int user, escaped;
#include "appslot.h"
static AppSlot wait_slot;
#define abort_requested wait_slot.abort_req
int appslot_cur(void) { return 2; }
int res_owner_get(void) { return appslot_cur(); }
AppSlot *appslot_get(int id) { (void)id; return &wait_slot; }
static int sink, sink_x, sink_y;
static unsigned empty_mml_ports;
static volatile int stop_requested, stop_on_wait, park_succeeds;
int appslot_stop_pending(void) { return stop_requested; }
int ring3_call_from_user(void);
#include "wait_source.inc"
static void *jump[5];
static void (*wait_hook)(void);
volatile u32 tick_count;
static void host_wait(void)
{
    tick_count++;
    if (wait_hook) wait_hook();
    if (++waits == request_wait) {
        if (stop_on_wait) stop_requested = 1;
        else abort_requested = 1;
    }
    if (waits > 400) { escaped = 2; __builtin_longjmp(jump, 1); }
}
int ring3_call_from_user(void) { return user; }
void ring3_abort_check(void)
{
    abort_checks++;
    if (abort_requested) {
        escaped = 1;
        __builtin_longjmp(jump, 1);
    }
}
static void host_out(unsigned p, unsigned v)
{
    ports++;
    if (p == OPN_ADDR) opn_reg=v;
    if (p == OPN_DATA && opn_reg == OPN_REG_KEY_ONOFF) last_key=v;
}
static int tx_ready;
static unsigned host_in(unsigned p) { (void)p; return tx_ready ? 255 : 0; }
void cpu_delay_us(u32 us) { (void)us; host_wait(); }
#define outp(p,v) host_out(p,v)
#define inp(p) host_in(p)
#define io_wait() host_wait()
#define io_wait_n(n) ((void)(n))
#define _halt() host_wait()
#include "console_source.inc"
#include "fm_source.inc"
#include "serial_source.inc"
#define KAPI_HIT(n) ((void)(n))
#include "kbd_source.inc"
#include "ime_source.inc"
#include "rshell_source.inc"
#include "kstring.c"
#include "kstring_c.c"
#include "utf8.c"
#include "ime_romkana.c"
/* Dictionary storage is outside the input wait contract. */
int ime_dict_open(IME_Dict *d, const char *p)
{ (void)d; (void)p; serial_puts("IME: Dict loaded"); return 0; }
int ime_dict_search(IME_Dict *d, const char *y, IME_Result *r, int n)
{ (void)d; (void)y; (void)r; (void)n; return 0; }
void ime_dict_learn(IME_Dict *d, const char *y, const char *k)
{ (void)d; (void)y; (void)k; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

int exec_park_kbd(void) {
    if (park_succeeds) { escaped=3; __builtin_longjmp(jump, 1); }
    return 0;
}
int exec_park_poll(u32 tick) { (void)tick; return 0; }
int kbd_inject_take(u8 *b) { (void)b; return 0; }
int v86_is_active(void) { return 0; }
int con_sink_is_enabled(void) { return sink; }
void con_sink_push_print(const char *s, u32 n, u8 c) { (void)s; (void)n; (void)c; }
void bootlog_push(const char *s, u32 n) { (void)s; (void)n; }
void con_sink_push_cursor(int x, int y) { sink_x=x; sink_y=y; }
static void serial_wake(void)
{
    if (waits == 2) { ser_buf[0]='Z'; ser_head=0; ser_count=1; }
}
static void say(const char *s, unsigned n)
{
    unsigned call=4;
    __asm__ volatile("int $0x80" : "+a"(call) : "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(c, msg) do { if (!(c)) { say("FAIL " msg "\n", sizeof("FAIL " msg "\n")-1); return 1; } } while (0)
static int input_kind, input_wake;
static u32 preedit_cells[TVRAM_COLS];
static unsigned preedit_writes, preedit_clears;
static void audit_ime_putc(int x, int y, char c, u8 color)
{
    (void)color;
    if (y == IME_PREEDIT_ROW) {
        preedit_writes++;
        preedit_cells[x] = (u8)c;
    }
}
static int audit_ime_putw(int x, int y, u32 c, u8 color)
{
    (void)color;
    if (y == IME_PREEDIT_ROW) {
        preedit_writes++;
        preedit_cells[x] = c;
        preedit_cells[x + 1] = 0;
    }
    return 2;
}
static void audit_ime_clear_row(int y, u8 color)
{
    (void)color;
    if (y == IME_PREEDIT_ROW) {
        preedit_clears++;
        for (int x = 0; x < TVRAM_COLS; x++) preedit_cells[x] = ' ';
    }
}
static const IME_Render audit_ime_render = {
    .putc = audit_ime_putc, .putw = audit_ime_putw,
    .clear_row = audit_ime_clear_row
};
static int test_ime_exit_display(void)
{
    const int modes[] = {IME_MODE_OFF, IME_MODE_HIRAGANA, IME_MODE_KATAKANA};
    g_ime.render = &audit_ime_render;
    user=1; sink=kbd_gui_mode=abort_requested=stop_requested=0;
    for (unsigned i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        g_ime.mode=modes[i];
        /* A successful read still records the owner for normal app exit. */
        g_ime.commit_buf[0]='B'; g_ime.commit_len=1; g_ime.commit_pos=0;
        CHECK(wrap_ime_getkey()=='B', "IME normal reader before exit");
        g_ime.state=IME_ST_CONVERT; g_ime.converting=1;
        g_ime.kana_len=g_ime.result_count=1;
        g_ime.kana_buf[0]='K'; g_ime.kana_buf[1]=0;
        g_ime.rk.preedit[0]='k'; g_ime.rk.preedit[1]=0;
        g_ime.commit_len=2;
        for (int x = 0; x < TVRAM_COLS; x++) preedit_cells[x]='X';
        preedit_writes=preedit_clears=0;
        ime_owner_exit(res_owner_get());
        CHECK(!g_ime.kana_len && !g_ime.kana_buf[0] && !g_ime.result_count &&
              !g_ime.commit_len && !g_ime.commit_pos && !g_ime.rk.preedit[0] &&
              !g_ime.converting && g_ime.state==IME_ST_INPUT && g_ime.mode==modes[i],
              "IME exit resets buffers and preserves mode");
        if (modes[i] == IME_MODE_OFF) {
            CHECK(!preedit_clears && !preedit_writes, "IME OFF exit never touches last row");
            for (int x = 0; x < TVRAM_COLS; x++)
                CHECK(preedit_cells[x]=='X', "IME OFF exit preserves app output");
        } else {
            CHECK(preedit_clears==1 && preedit_writes==3 &&
                  preedit_cells[0]=='[' && preedit_cells[3]==']' &&
                  preedit_cells[1]==(modes[i]==IME_MODE_HIRAGANA ? 0x3042 : 0x30a2),
                  "IME ON exit redraws mode indicator");
            for (int x = 4; x < TVRAM_COLS; x++)
                CHECK(preedit_cells[x]==' ', "IME ON exit clears old composition before redraw");
        }
    }
    g_ime.render=0;
    return 0;
}
static void input_wait(void)
{
    if (input_wake && waits == 1) {
        kbd_head=0; kbd_count=1; kbd_buf[0]=('A' | (KEY_RETURN << 8));
    }
}
static int input_call(void)
{
    switch (input_kind) {
    case 0: return wrap_kbd_getchar();
    case 1: return wrap_kbd_getkey();
    case 2: return wrap_ime_getchar();
    default: return wrap_ime_getkey();
    }
}
static int test_input(void)
{
    for (input_kind=0; input_kind<4; input_kind++) {
        int value;
        user=1; kbd_gui_mode=0; rshell_active=0; kbd_count=0;
        g_ime.mode=IME_MODE_OFF; g_ime.commit_len=g_ime.commit_pos=0;
        waits=abort_checks=0; escaped=abort_requested=stop_requested=stop_on_wait=0;
        if (!__builtin_setjmp(jump)) (void)input_call();
        CHECK(escaped==1 && waits==3, "input CUI abort");
        escaped=abort_requested=0; waits=0; stop_on_wait=1;
        value=-2;
        if (!__builtin_setjmp(jump)) value=input_call();
        CHECK(!escaped && value==-1 && waits==3, "input STOP return");
        /* Force park failure for a GUI slot; gui=0 is checked separately. */
        kbd_gui_mode=1; stop_requested=0; waits=0; escaped=0; value=-2;
        if (!__builtin_setjmp(jump)) value=input_call();
        CHECK(!escaped && value==-1 && waits==3, "input GUI fallback STOP");
        /* Normal GUI park still transfers control without examining STOP. */
        park_succeeds=1; escaped=0; abort_checks=waits=0;
        if (!__builtin_setjmp(jump)) (void)input_call();
        CHECK(escaped==3 && !abort_checks && !waits, "normal GUI park unchanged");
        park_succeeds=0;
        kbd_gui_mode=0; user=0; abort_requested=stop_requested=1;
        waits=abort_checks=0; escaped=0; input_wake=1; wait_hook=input_wait;
        value=-2;
        if (!__builtin_setjmp(jump)) value=input_call();
        CHECK(!escaped, "trusted input escape");
        CHECK(!abort_checks, "trusted input abort guard");
        CHECK(waits==2, "trusted input wait count");
        CHECK(value==(input_kind==0 || input_kind==2 ? 'A' : ('A' | (KEY_RETURN << 8))),
              "trusted input value");
        input_wake=0; wait_hook=0; stop_on_wait=abort_requested=stop_requested=0;
        user=1; kbd_head=0; kbd_count=1; kbd_buf[0]='B'; waits=0;
        CHECK(input_call()=='B' && !waits, "input ready");
    }
    for (input_kind=0; input_kind<4; input_kind++) {
        int value=-2;
        user=1; abort_requested=stop_requested=1; escaped=0;
        input_wake=1; wait_hook=input_wait; waits=abort_checks=0; kbd_count=0;
        g_ime.mode=IME_MODE_OFF; g_ime.commit_len=g_ime.commit_pos=0;
        if (!__builtin_setjmp(jump)) {
            switch (input_kind) {
            case 0: value=kbd_getchar(); break;
            case 1: value=kbd_getkey(); break;
            case 2: value=ime_getchar(); break;
            default: value=ime_getkey(); break;
            }
        }
        CHECK(value>=0 && !escaped && !abort_checks && waits==2,
              "internal keyboard and IME do not abort USER syscall");
    }
    input_wake=0; wait_hook=0; abort_requested=stop_requested=0;
    user=0; rshell_active=1; kbd_count=ser_count=0; waits=abort_checks=0;
    CHECK(wrap_kbd_getchar()==' ' && waits==KBD_TIMEOUT_TICKS && !abort_checks,
          "trusted rshell timeout");
    ser_head=0; ser_count=1; ser_buf[0]='S'; waits=0;
    CHECK(wrap_kbd_getchar()=='S' && !waits, "trusted rshell serial input");
    rshell_active=0;
    /* An interrupted wait must not confirm a pending candidate. */
    g_ime.mode=IME_MODE_HIRAGANA; g_ime.state=IME_ST_CONVERT;
    g_ime.result_count=1; g_ime.candidate_idx=0;
    g_ime.results[0].kanji[0]='K'; g_ime.results[0].kanji[1]=0;
    user=1; kbd_gui_mode=0; kbd_count=0; stop_requested=1; abort_requested=0;
    sink=1; kbd_gui_mode=1;
    CHECK(wrap_ime_getkey()==-1 && g_ime.state==IME_ST_CONVERT && g_ime.result_count==1,
          "GUI STOP preserves WM composition");
    sink=0; kbd_gui_mode=0;
    g_ime.kana_len=1; g_ime.kana_buf[0]='K';
    CHECK(wrap_ime_getkey()==-1 && g_ime.state==IME_ST_INPUT && !g_ime.kana_len && !g_ime.result_count,
          "IME key STOP cancels composition");
    g_ime.state=IME_ST_CONVERT; g_ime.result_count=1; g_ime.kana_len=1;
    CHECK(wrap_ime_getchar()==-1 && g_ime.state==IME_ST_INPUT && !g_ime.kana_len && !g_ime.result_count,
          "IME char STOP cancels composition");
    for (input_kind=2; input_kind<4; input_kind++) {
        g_ime.state=IME_ST_CONVERT; g_ime.result_count=1; g_ime.kana_len=1;
        abort_requested=1; escaped=0;
        if (!__builtin_setjmp(jump)) (void)input_call();
        CHECK(escaped==1 && g_ime.state==IME_ST_INPUT && !g_ime.kana_len && !g_ime.result_count,
              "CUI IME abort cancels composition");
    }
    abort_requested=0;
    /* gui=0 nested child ignores WM STOP and continues to real input. */
    wait_slot.gui=0; input_wake=1; wait_hook=input_wait; waits=0; kbd_count=0;
    CHECK(wrap_kbd_getchar()=='A' && waits==2, "gui=0 ignores pending WM STOP");
    wait_slot.gui=1; input_wake=0; wait_hook=0;
    g_ime.mode=IME_MODE_OFF; g_ime.state=IME_ST_INPUT; stop_requested=0;
    g_ime.commit_buf[0]=(char)0xe3; g_ime.commit_len=1; g_ime.commit_pos=0;
    CHECK(wrap_ime_getchar()==0xe3, "IME committed UTF8");
    g_ime.commit_pos=0;
    CHECK(wrap_ime_getkey()==0xe3, "IME committed key");
    g_ime.kana_len=1; g_ime.result_count=1; g_ime.state=IME_ST_CONVERT;
    g_ime.rk.preedit[0]='k'; g_ime.rk.preedit[1]=0;
    g_ime.commit_len=2;
    ime_owner_exit(3);
    CHECK(g_ime.kana_len==1 && g_ime.result_count==1, "other owner preserves CUI input");
    ime_owner_exit(2);
    CHECK(!g_ime.kana_len && !g_ime.result_count && !g_ime.commit_len &&
          !g_ime.rk.preedit[0] && g_ime.state==IME_ST_INPUT,
          "owner exit cancels ready input and preedit");
    return 0;
}
static int test(void)
{
    wait_slot.gui=wait_slot.cpl3=1;
    const int extremes[] = {-1, 3, (-0x7fffffff-1), 0x7fffffff};
    unsigned i;
    if (AUDIT_CASE == 0 || AUDIT_CASE == 1) {
        console_set_cursor((-0x7fffffff-1),(-0x7fffffff-1));
        CHECK(console_get_cursor_x()==0, "cursor x low");
        CHECK(console_get_cursor_y()==0, "cursor y low");
        console_set_cursor(0x7fffffff,0x7fffffff);
        CHECK(console_get_cursor_x()==TVRAM_COLS-1, "cursor x high");
        CHECK(console_get_cursor_y()==TVRAM_ROWS-1, "cursor y high");
        console_set_cursor(7,8);
        CHECK(console_get_cursor_x()==7 && console_get_cursor_y()==8, "cursor valid");
        sink=1; ports=0;
        console_set_cursor(0x7fffffff,-1);
        CHECK(sink_x==TVRAM_COLS-1 && sink_y==0, "sink cursor clamped");
        CHECK(console_get_cursor_x()==7 && console_get_cursor_y()==8 && !ports, "sink cursor frozen");
        sink=0;
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 2) {
        for (i=0;i<sizeof(extremes)/sizeof(extremes[0]);i++) {
            int ch=extremes[i];
            ports=0; fm_set_tone(ch,tone_piano); CHECK(!ports,"fm tone channel");
            ports=0; fm_set_tone_num(ch,0); CHECK(!ports,"fm numbered channel");
            ports=0; fm_note_on(ch,48); CHECK(!ports,"fm on channel");
            ports=0; fm_note_off(ch); CHECK(!ports,"fm off channel");
            ports=0; ssg_tone(ch,100); CHECK(!ports,"ssg tone channel");
            ports=0; ssg_volume(ch,15); CHECK(!ports,"ssg volume channel");
        }
        ports=0; fm_set_tone_num(0,-1); CHECK(!ports,"tone number low");
        fm_set_tone_num(0,NUM_TONES); fm_set_tone_num(0,0x7fffffff);
        CHECK(!ports,"tone number high");
        for (i=0;i<3;i++) {
            ports=0; fm_set_tone_num(i,NUM_TONES-1); CHECK(ports,"fm tone valid");
            ports=0; fm_note_on(i,48); CHECK(ports,"fm on valid");
            ports=0; fm_note_off(i); CHECK(ports,"fm off valid");
            ports=0; ssg_tone(i,100); CHECK(ports,"ssg tone valid");
            ports=0; ssg_volume(i,15); CHECK(ports,"ssg volume valid");
        }
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 4) {
        rshell_active=0; user=1; wrap_rshell_set_active(1);
        CHECK(!rshell_active,"rshell user enable");
        rshell_active=1; wrap_rshell_set_active(0);
        CHECK(rshell_active==1,"rshell user disable");
        user=0; wrap_rshell_set_active(0); CHECK(!rshell_active,"rshell trusted disable");
        wrap_rshell_set_active(1); CHECK(rshell_active==1,"rshell trusted enable");
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 3) {
        /* STOP arrives during a sustained note/rest, or while scanning non-notes.
         * A bounded fake device wait detects a removed check without timeout. */
        user=1; waits=abort_checks=0; escaped=abort_requested=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("C");
        CHECK(escaped==1 && waits==3,"mml wait abort");
        waits=abort_checks=0; escaped=0; abort_requested=1;
        ports=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("");
        empty_mml_ports=ports; ports=0; escaped=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("xxxxxxxxC");
        CHECK(ports==empty_mml_ports, "MML scan stops before next note");
        CHECK(escaped==1 && !waits,"mml scan abort");
        waits=abort_checks=0; escaped=abort_requested=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("R");
        CHECK(escaped==1 && waits==3,"mml rest abort");
        user=1; stop_on_wait=1; stop_requested=abort_requested=0; waits=0;
        wrap_fm_play_mml("CDEFG");
        CHECK(stop_requested && waits==3,"mml wait stop");
        stop_requested=1; waits=abort_checks=0; wrap_fm_play_mml("xxxxxxx");
        CHECK(!waits && abort_checks==1,"mml scan stop");
        stop_requested=0; waits=0; wrap_fm_play_mml("R");
        CHECK(stop_requested && waits==3,"mml rest stop");
        stop_requested=0; waits=0; request_wait=10;
        wrap_fm_play_mml("CDEFG");
        CHECK(stop_requested && waits==10 && last_key==0,"mml sustained stop key off");
        stop_requested=stop_on_wait=abort_requested=escaped=0; waits=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("CDEFG");
        CHECK(escaped==1 && waits==10 && last_key==0,"CUI sustained abort key off");
        request_wait=3; stop_requested=stop_on_wait=0;
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 5) {
        user=1; waits=abort_checks=0; escaped=abort_requested=0;
        s_gate=0; ser_count=0;
        if (!__builtin_setjmp(jump)) (void)wrap_serial_getchar();
        CHECK(escaped==1 && waits==3,"serial abort");
        abort_requested=stop_requested=0; stop_on_wait=1; waits=0;
        CHECK(wrap_serial_getchar()==-1 && waits==3,"serial stop");
        stop_requested=stop_on_wait=0;
        abort_requested=0; waits=0; ser_head=0; ser_count=1; ser_buf[0]='Q';
        CHECK(wrap_serial_getchar()=='Q' && !waits && !ser_count,"serial ready");
        s_gate=1; CHECK(wrap_serial_getchar()==-1,"serial gate");
        s_gate=0; user=0; abort_requested=1; escaped=0;
        waits=abort_checks=0; ser_count=0; wait_hook=serial_wake;
        if (!__builtin_setjmp(jump)) {
            CHECK(wrap_serial_getchar()=='Z',"trusted serial value");
        }
        CHECK(!escaped && !abort_checks && waits==3,"trusted serial abort guard");
        user=1; waits=abort_checks=0; escaped=0; ser_count=0;
        if (!__builtin_setjmp(jump)) CHECK(serial_getchar()=='Z', "internal receive value");
        CHECK(!escaped && !abort_checks && waits==3, "internal receive does not abort USER syscall");
        wait_hook=0;
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 3) {
        /* Trusted callers remain usable even with an outstanding request. */
        user=0; stop_requested=abort_requested=1; escaped=0; waits=abort_checks=0;
        if (!__builtin_setjmp(jump)) wrap_fm_play_mml("R");
        CHECK(!escaped && !abort_checks && waits==20,"trusted mml");
        user=1; waits=abort_checks=0; escaped=0;
        if (!__builtin_setjmp(jump)) fm_play_mml("R");
        CHECK(!escaped && !abort_checks && waits==20, "internal MML does not abort USER syscall");
        stop_requested=0;
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 7) {
        const char *long_text="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        user=1; tx_ready=1; s_gate=1; waits=abort_checks=0;
        escaped=abort_requested=stop_requested=stop_on_wait=0;
        if (!__builtin_setjmp(jump)) wrap_serial_puts(long_text);
        CHECK(escaped==1 && waits==4, "serial puts scan abort");
        s_gate=0;
        escaped=abort_requested=0; stop_on_wait=1; waits=0; ports=0;
        wrap_serial_puts(long_text);
        CHECK(stop_requested && waits==4 && ports==32, "serial puts scan STOP");
        tx_ready=1; abort_requested=1; stop_requested=stop_on_wait=0; waits=0; escaped=0;
        if (!__builtin_setjmp(jump)) (void)wrap_serial_putchar('X');
        CHECK(escaped==1 && !waits, "serial TX boundary abort");
        escaped=abort_requested=0; stop_requested=1; waits=ports=0;
        CHECK(wrap_serial_putchar('X')==SER_TX_DROPPED && !ports,
              "serial TX boundary STOP");
        user=0; tx_ready=1; abort_requested=stop_requested=1; waits=abort_checks=ports=0;
        escaped=0;
        if (!__builtin_setjmp(jump)) wrap_serial_puts("abcdef");
        CHECK(!escaped && !abort_checks && ports==6, "trusted serial puts");
        /* Kernel diagnostics within a syscall may run with IF=0. */
        user=1; abort_checks=ports=0; escaped=0; _disable();
        if (!__builtin_setjmp(jump)) serial_puts("abcdef");
        _enable();
        CHECK(!escaped && !abort_checks && ports==6, "IF=0 serial logging");
        user=1; abort_requested=stop_requested=1; abort_checks=ports=0; escaped=0;
        if (!__builtin_setjmp(jump)) {
            CHECK(ime_dict_open(&g_ime.dict, "dict")==0, "dictionary open finishes");
            CHECK(serial_gate_put((const u8 *)"frame",5)==SER_TX_OK, "internal frame TX");
            sink=1; rshell_active=1;
            shell_print("L", ATTR_WHITE);
            sink=0; rshell_active=0;
        }
        CHECK(!escaped && !abort_checks && ports==22, "USER internal logs and frames never abort");
        tx_ready=0; user=stop_on_wait=abort_requested=stop_requested=0;
    }
    if (AUDIT_CASE == 0 || AUDIT_CASE == 6) CHECK(test_input()==0, "input tests");
    if (AUDIT_CASE == 0 || AUDIT_CASE == 6)
        CHECK(test_ime_exit_display()==0, "IME exit display tests");
    say("PASS kapi audit\n",sizeof("PASS kapi audit\n")-1);
    return 0;
}
void _start(void)
{
    int rc=test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
