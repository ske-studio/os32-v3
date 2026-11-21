#include "in2_int.h"
#include "keymap.h"
#include "input_context.h"
int fep_services_public(void) { return 0; }
int input_capture_ime(const KmKey *k, u8 old, const KmEvalResult *r)
{ (void)k; (void)old; (void)r; return 0; }
void input_ime_action_flush(void) {}
u32 legacy_cooked_popped(void) { return 0; }
int input_ime_action_due(u32 popped) { (void)popped; return 0; }
int input_ime_cancel_pending(void) { return 0; }
int input_context_snapshot(KbdPolicySnapshot *dst) { (void)dst; return -1; }
int input_ime_action_take(const KbdPolicySnapshot *dst, u32 n, u32 *action)
{ (void)dst; (void)n; (void)action; return 0; }
int ime_apply_absolute(u32 action) { (void)action; return -1; }
static u32 now, command_start;
static int capture, frozen, pending, serial_reads, steps, ready_at, value, toggle_at;
volatile int kbd_gui_mode;
static int rshell_active, serial_value = -1, flip_gui;
static unsigned trace[2][512], lengths[2], tracing, lane;
static void mark(unsigned tag) {
    if (!tracing) return;
    if (lengths[lane] >= 512) __builtin_trap();
    trace[lane][lengths[lane]++] = tag;
}
static u32 ticks(void) { return now; }
static u32 timeout(void) { return 3000; }
static int cap(void) { return capture ? (frozen ? 2 : 1) : 0; }
static int stop(void) { return pending; }
static int local(void) { mark(1); return now >= (u32)ready_at ? value : -1; }
static int serial(void) { mark(2); serial_reads++; return serial_value; }
static int enabled(void) { return rshell_active && !cap(); }
static int gui(void) { return kbd_gui_mode; }
static int inject(void) { mark(7); return local() < 0 ? -1 : value & 0xff; }
static void step(void) {
    now += 100; steps++;
    if (flip_gui && steps == 1) kbd_gui_mode = !kbd_gui_mode;
    if (toggle_at && steps == toggle_at) capture = !capture;
    if (steps > 100) __builtin_trap();
}
static const struct InputLegacyProvider provider = {
    .try_key=local, .terminal_try=serial, .terminal_enabled=enabled,
    .gui_active=gui, .inject_try=inject,
    .wait_ticks=ticks, .wait_timeout_ticks=timeout, .capture_active=cap,
    .wait_pending=stop, .wait_step=step,
};
/* Fixed oracle and actual x86 provider share these fake devices. */
#define tick_count now
#define kbd_count (local() >= 0)
#define KBD_RSHELL_TIMEOUT_SEC 30
#define PIT_HZ 100
#define KBD_BUF_SIZE 1
#define RING_DEQUEUE(entry, buf, head, count, size) do { (entry)=value; } while (0)
static int con_sink_capture_active(void) { return cap(); }
static void kbd_foreground_poll(void) { if (!kbd_gui_mode) mark(3); }
static int serial_trygetchar(void) { return serial(); }
static int ring3_wait_pending(void) { mark(4); return pending; }
static void idle_work_run(void) { mark(5); }
static void _halt(void) { mark(6); step(); }
/* kbd waits through the common idle_wait_step (rx-recent); same order as before. */
static void idle_wait_step(void) { idle_work_run(); _halt(); }
static int kbd_gui_trygetbyte(void) { return inject(); }
static int exec_park_kbd(void) { mark(8); return 0; }
static int old_gui(int interruptible);
#define kbd_gui_getbyte old_gui
#include "old_wait.inc"
#undef kbd_gui_getbyte
int legacy_cooked_take(void) { return local(); }
int legacy_cooked_peek(void) { return local(); }
static int serial_peekchar(void) { return serial_value; }
static int kbd_inject_peek(u8 *b) { *b=(u8)value; return local()>=0; }
static int exec_park_poll(u32 tick) { (void)tick; return 0; }
#include "current_wait.inc"
static int old_wait(int mode, int interruptible) {
    return mode == IL_KEY ? old_key(interruptible) : old_char(interruptible);
}
static int run(int old, int mode, int interruptible) {
    if (old == 2) {
        input_legacy_register(&kbd_legacy_provider);
        return mode == IL_CHAR ? kbd_getchar_wait(interruptible) : kbd_getkey_wait(interruptible);
    }
    return old ? old_wait(mode,interruptible) : input_legacy_wait(mode,interruptible);
}
static int probe(int old) {
    input_legacy_register(&provider);
    capture=1; frozen=0; pending=0; toggle_at=0; rshell_active=0; serial_reads=0; ready_at=100000; steps=0;
    command_start=100; now=command_start+1700;
    if (run(old,IL_CHAR,1)!=13 || now!=4800 || !capture) return 1;
    steps=0;
    if (run(old,IL_KEY,1)!=27 || now!=7800 || serial_reads || !capture) return 2;
    frozen=1; steps=0;
    if (run(old,IL_CHAR,1)!=13 || cap()!=2 || now!=10800) return 3;
    pending=1; steps=0;
    if (run(old,IL_KEY,1)!=-1 || steps) return 4;
    pending=0; capture=0; rshell_active=1; ready_at=11000; value=0x1d61; steps=0;
    if (run(old,IL_KEY,1)!=0x1d61 || serial_reads) return 5;
    rshell_active=1; capture=1; now=0; ready_at=100000; steps=0; toggle_at=10;
    if (run(old,IL_CHAR,1)!=' ' || now!=3000) return 6;
    capture=0; now=0; steps=0; toggle_at=10;
    if (run(old,IL_CHAR,1)!='\r' || now!=3000) return 7;
    toggle_at=0;
    rshell_active=0; capture=0; now=0; ready_at=3100; steps=0; value='z';
    if (run(old,IL_CHAR,1)!='z' || now!=3100) return 8;
    {
        kbd_gui_mode=1; rshell_active=1; capture=1; frozen=1;
        now=0; ready_at=3100; steps=0;
        if (run(old,IL_CHAR,1)!='z' || now!=3100 || cap()!=2) return 9;
        kbd_gui_mode=0;
    }
    return 0;
}
/* Same fixture on both routes: ready before/at/after the deadline, serial,
 * GUI (including mode changes after entry), capture/FREEZE, serial priority,
 * both interruptibility choices and pending STOP. Compare every call in order. */
static int compare(void) {
    static const int delays[] = {0, 100, 2900, 3000, 3100};
    for (int flags=0; flags<192; flags++) for (int d=0; d<5; d++) {
        int result[2], reads[2], elapsed[2];
        tracing=1; lengths[0]=lengths[1]=0;
        for (int old=0; old<2; old++) {
            lane=(unsigned)old; flip_gui=flags & 32;
            kbd_gui_mode=flags & 1; rshell_active=(flags >> 1) & 1;
            capture=(flags >> 6) != 0; frozen=(flags >> 6) == 2;
            pending=(flags >> 2) & 1; serial_value=flags & 8 ? 's' : -1;
            steps=serial_reads=0; toggle_at=0; now=1800;
            ready_at=1800+delays[d]; value=0x1d7a;
            result[old]=old ? old_char((flags >> 4) & 1) : run(2,IL_CHAR,(flags >> 4) & 1);
            reads[old]=serial_reads; elapsed[old]=(int)now;
            if (cap() != (flags >> 6)) return 10;
        }
        if (result[0]!=result[1] || reads[0]!=reads[1] || elapsed[0]!=elapsed[1] || lengths[0]!=lengths[1]) return 11;
        for (unsigned i=0; i<lengths[0]; i++) if (trace[0][i]!=trace[1][i]) return 11;
    }
    tracing=0;
    for (int flags=0; flags<192; flags++) for (int d=0; d<5; d++) for (int toggle=0; toggle<2; toggle++) {
        int result[2], reads[2], elapsed[2];
        for (int old=0; old<2; old++) {
            flip_gui=flags & 32; kbd_gui_mode=flags & 1;
            rshell_active=(flags >> 1) & 1; capture=(flags >> 6) != 0;
            frozen=(flags >> 6) == 2; pending=(flags >> 2) & 1;
            serial_value=flags & 8 ? 's' : -1; steps=serial_reads=0;
            toggle_at=toggle ? 10 : 0; now=1800; ready_at=1800+delays[d]; value=0x1d7a;
            result[old]=old ? old_key((flags >> 4) & 1) : run(2,IL_KEY,(flags >> 4) & 1);
            reads[old]=serial_reads; elapsed[old]=(int)now;
        }
        if (result[0]!=result[1] || reads[0]!=reads[1] || elapsed[0]!=elapsed[1]) return 13;
    }
    serial_value=-1; tracing=flip_gui=toggle_at=0;
    return 0;
}
#ifndef PPC_COMPILE
static enum InputReadStep recheck(int cancel, int *key)
{
    (void)cancel; (void)key;
    return IR_RECHECK;
}
static int freeze_rechecks(void)
{
    struct InputLegacyProvider retry = provider;
    retry.read_cooked = recheck;
    input_legacy_register(&retry);
    capture = frozen = 1;
    kbd_gui_mode = 0;
    now = 123;
    u32 start;
    int key;
    if (fep_read_wait(1, &key, &start, 0) != IR_RECHECK) return 14;
    now += 2900;
    if (fep_read_wait(1, &key, &start, 1) != IR_RECHECK) return 15;
    now += 100;
    return fep_read_wait(1, &key, &start, 1) == IR_KEY && key == 27 ? 0 : 16;
}
void _start(void) {
    int rc=probe(1);
    if (!rc) rc=probe(0);
    if (!rc) rc=probe(2);
    if (!rc) rc=compare();
    kbd_gui_mode=1; ready_at=0;
    if (!rc && kbd_getkey_wait(1)!=(value & 0xff)) rc=12;
    if (!rc) rc=freeze_rechecks();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#else
int fixture(void) { int rc=probe(0); if (!rc) rc=compare(); kbd_gui_mode=1; ready_at=0; return rc ? rc : kbd_getkey_wait(1)!=(value & 0xff); }
#endif
